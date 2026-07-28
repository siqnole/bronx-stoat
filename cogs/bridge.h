#pragma once
#include "stoatpp/bot_module.h"
#include "../database.h"
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include "command_config.h"

namespace bronx::cogs {

class BridgeModule : public stoatpp::bot_module {
public:
    BridgeModule() : running_(true), socket_fd_(-1) {
        loop_thread_ = std::thread([this]() { run_socket_loop(); });
    }

    ~BridgeModule() {
        running_ = false;
        {
            std::lock_guard<std::mutex> lock(socket_mutex_);
            if (socket_fd_ != -1) {
                close(socket_fd_);
                socket_fd_ = -1;
            }
        }
        if (loop_thread_.joinable()) {
            loop_thread_.join();
        }
    }

    void register_handlers(stoatpp::cluster& bot) override {
        bot_ptr_ = &bot;

        // Message relay hook
        bot.on_message([this, &bot](const stoatpp::events::Message& msg) {
            if (msg.author.id == bot.current_user().id) return; // ignore self
            if (msg.author.bot) return; // ignore other bots
            if (msg.raw.contains("createdByWebhookId") && !msg.raw["createdByWebhookId"].is_null()) return;
            if (msg.raw.contains("webhookId") && !msg.raw["webhookId"].is_null()) return;
            if (msg.raw.contains("webhook_id") && !msg.raw["webhook_id"].is_null()) return;

            // Ignore commands
            std::vector<std::string> prefixes;
            if (bot.config().prefix_resolver) {
                prefixes = bot.config().prefix_resolver(bot, msg);
            } else {
                prefixes = {bot.config().command_prefix};
            }
            bool is_command = false;
            for (const auto& p : prefixes) {
                if (!p.empty() && msg.content.rfind(p, 0) == 0) {
                    is_command = true;
                    break;
                }
            }
            if (is_command) return;
            
            bool is_bridged = false;
            {
                std::lock_guard<std::mutex> lock(channels_mutex_);
                if (bridged_channels_.count(msg.channel_id)) {
                    is_bridged = true;
                }
            }

            if (is_bridged) {
                auto user_opt = bot.get_user(msg.author.id);
                std::string author_name = user_opt ? user_opt->username : msg.author.username;
                std::string avatar = user_opt ? user_opt->avatar_url(bot.config().cdn_url) : msg.author.avatar_url(bot.config().cdn_url);

                // If the sender has a custom masquerade set, prefer that name/avatar.
                // stoatpp::events::Message doesn't expose masquerade directly; read from raw JSON.
                if (msg.raw.contains("masquerade") && msg.raw["masquerade"].is_object()) {
                    const auto& masq = msg.raw["masquerade"];
                    if (masq.contains("name") && masq["name"].is_string())
                        author_name = masq["name"].get<std::string>();
                    if (masq.contains("avatar") && masq["avatar"].is_string())
                        avatar = masq["avatar"].get<std::string>();
                }

                std::string channel_name = "";
                auto channel_opt = bot.get_channel(msg.channel_id);
                if (channel_opt && channel_opt->name) {
                    channel_name = *channel_opt->name;
                }
                
                std::string server_name = "";
                auto server_opt = bot.get_server(msg.server_id);
                if (server_opt) {
                    server_name = server_opt->name;
                }
                
                nlohmann::json relay_payload = {
                    {"type", "message"},
                    {"channel_id", msg.channel_id},
                    {"content", msg.content},
                    {"author_name", author_name},
                    {"author_avatar", avatar},
                    {"author_id", msg.author.id},
                    {"channel_name", channel_name},
                    {"server_id", msg.server_id},
                    {"server_name", server_name},
                    {"message_id", msg.id}
                };
                if (!msg.replies.empty()) {
                    relay_payload["reply_to_message_id"] = msg.replies[0];
                }
                send_json(relay_payload);
            }
        });

        // Reaction relay hooks
        bot.on_message_react([this, &bot](const stoatpp::events::MessageReact& e) {
            if (e.user_id == bot.current_user().id) return;
            
            bool is_bridged = false;
            {
                std::lock_guard<std::mutex> lock(channels_mutex_);
                if (bridged_channels_.count(e.channel_id)) {
                    is_bridged = true;
                }
            }
            if (is_bridged) {
                nlohmann::json req = {
                    {"type", "reaction_add"},
                    {"channel_id", e.channel_id},
                    {"message_id", e.id},
                    {"emoji", e.emoji_id},
                    {"user_id", e.user_id}
                };
                send_json(req);
            }
        });

        bot.on_message_unreact([this, &bot](const stoatpp::events::MessageUnreact& e) {
            if (e.user_id == bot.current_user().id) return;
            
            bool is_bridged = false;
            {
                std::lock_guard<std::mutex> lock(channels_mutex_);
                if (bridged_channels_.count(e.channel_id)) {
                    is_bridged = true;
                }
            }
            if (is_bridged) {
                nlohmann::json req = {
                    {"type", "reaction_remove"},
                    {"channel_id", e.channel_id},
                    {"message_id", e.id},
                    {"emoji", e.emoji_id},
                    {"user_id", e.user_id}
                };
                send_json(req);
            }
        });

        // Moderation action hooks
        bot.on_moderation_action = [this](const std::string& user_id, const std::string& action, const std::string& reason) {
            nlohmann::json mod_payload = {
                {"type", "mod_action"},
                {"target_user_id", user_id},
                {"action", action},
                {"reason", reason}
            };
            send_json(mod_payload);
        };

        // Database balance update hook
        if (bronx::g_db) {
            bronx::g_db->on_balance_update = [this](const std::string& user_id, int64_t wallet_delta, int64_t bank_delta) {
                bool skip = false;
                {
                    std::lock_guard<std::mutex> lock(syncs_mutex_);
                    if (users_syncing_.count(user_id)) {
                        skip = true;
                    }
                }
                if (!skip) {
                    nlohmann::json upd = {
                        {"type", "balance_update"},
                        {"user_id", user_id},
                        {"wallet_delta", wallet_delta},
                        {"bank_delta", bank_delta}
                    };
                    send_json(upd);
                }
            };

            bronx::g_db->on_afk_save = [this](const bronx::Database::UserAFK& afk) {
                bool skip = false;
                {
                    std::lock_guard<std::mutex> lock(syncs_mutex_);
                    if (users_syncing_.count(afk.user_id)) {
                        skip = true;
                    }
                }
                if (!skip) {
                    nlohmann::json upd = {
                        {"type", "afk_update"},
                        {"user_id", afk.user_id},
                        {"is_afk", afk.is_afk},
                        {"reason", afk.reason}
                    };
                    send_json(upd);
                }
            };
        }

        // `.link` commands
        bot.register_command({
            "link",
            {},
            "link accounts between Discord, Revolt (Stoat), and Guilded (Fluxer).",
            "link [generate|redeem] [code]",
            {},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!bronx::g_db) {
                    bot.send_message(msg.channel_id, "⚠️ Database is unavailable.");
                    return;
                }

                if (args.empty()) {
                    auto link_opt = bronx::g_db->get_account_link("stoat", msg.author.id);
                    if (link_opt) {
                        std::string out = "🔗 **Account Link Status**\n";
                        out += "• **Stoat ID**: " + (link_opt->stoat_id.empty() ? "*None*" : "`" + link_opt->stoat_id + "`") + "\n";
                        out += "• **Fluxer ID**: " + (link_opt->fluxer_id.empty() ? "*None*" : "`" + link_opt->fluxer_id + "`") + "\n";
                        out += "• **Discord ID**: " + (link_opt->discord_id.empty() ? "*None*" : "`" + link_opt->discord_id + "`") + "\n";
                        bot.send_message(msg.channel_id, out);
                    } else {
                        std::string out = "❌ **You do not have any linked accounts.**\n\n"
                                          "To link with another platform:\n"
                                          "1. Type `.link generate` to get a 6-digit link code.\n"
                                          "2. Go to Fluxer or Discord and type `.link redeem <code>`.";
                        bot.send_message(msg.channel_id, out);
                    }
                } else if (args[0] == "generate") {
                    std::string user_id = msg.author.id;
                    std::string channel_id = msg.channel_id;
                    {
                        std::lock_guard<std::mutex> lock(pending_gens_mutex_);
                        pending_gens_[user_id] = [&bot, channel_id](const std::string& code) {
                            bot.send_message(channel_id, "🔑 **Link Code Generated!**\n"
                                                         "Your code is: `" + code + "`\n"
                                                         "Redeem it on Fluxer or Discord using `.link redeem " + code + "`.\n"
                                                         "This code will expire in 5 minutes.");
                        };
                    }
                    nlohmann::json req = {
                        {"type", "link_generate"},
                        {"user_id", user_id}
                    };
                    send_json(req);
                } else if (args[0] == "redeem") {
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "❌ Please specify the code: `.link redeem <code>`.");
                        return;
                    }
                    std::string code = args[1];
                    std::string user_id = msg.author.id;
                    std::string channel_id = msg.channel_id;
                    {
                        std::lock_guard<std::mutex> lock(pending_redeems_mutex_);
                        pending_redeems_[user_id] = [&bot, channel_id](const std::string& err) {
                            if (err.empty()) {
                                bot.send_message(channel_id, "🎉 **Success!** Your accounts have been linked.");
                            } else {
                                bot.send_message(channel_id, "❌ **Link Error:** " + err);
                            }
                        };
                    }
                    nlohmann::json req = {
                        {"type", "link_redeem"},
                        {"user_id", user_id},
                        {"code", code}
                    };
                    send_json(req);
                }
            },
            "Utility"
        });

        // `.bridge` command
        bot.register_command({
            "bridge",
            {},
            "bridge and manage channel links across Discord, Revolt, and Guilded.",
            "bridge [add|remove|check|info|emojis]",
            {},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (args.empty()) {
                    bot.send_message(msg.channel_id, "🔗 **Bridge Commands**\n"
                                                     "• `.bridge add`: Starts a bridge connection or redeems a code.\n"
                                                     "• `.bridge remove`: Tears down the current channel's bridge.\n"
                                                     "• `.bridge check`: Shows active bridges for this channel.\n"
                                                     "• `.bridge info`: Shows origin metadata for a replied message.\n"
                                                     "• `.bridge emojis`: Shows custom emojis sync status across platforms.");
                    return;
                }
                
                std::string subcommand = args[0];
                if (subcommand == "add") {
                    bot.fetch_member(msg.server_id, msg.author.id, [this, &bot, msg, args](stoatpp::models::Member member, bool success) {
                        if (!success) {
                            bot.send_message(msg.channel_id, "⚠️ Failed to verify your permissions.");
                            return;
                        }
                        if (!is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, member)) {
                            bot.send_message(msg.channel_id, "⚠️ Permission error: you must be an administrator or trusted user to manage bridges.");
                            return;
                        }

                        if (args.size() < 2) {
                            std::string user_id = msg.author.id;
                            {
                                std::lock_guard<std::mutex> lock(pending_bridge_gens_mutex_);
                                pending_bridge_gens_[user_id] = [&bot, msg](const std::string& code, const std::string& /*channel_id*/) {
                                    bot.send_message(msg.channel_id, "🔑 **Bridge Verification Code**\n"
                                                                     "Run `.bridge add " + code + "` in the target channel on another platform to bridge them.\n"
                                                                     "⏳ This code will expire in 5 minutes.");
                                };
                            }
                            nlohmann::json req = {
                                {"type", "bridge_add_generate"},
                                {"user_id", user_id},
                                {"channel_id", msg.channel_id}
                            };
                            send_json(req);
                        } else {
                            std::string code = args[1];
                            std::string user_id = msg.author.id;
                            std::string channel_id = msg.channel_id;
                            {
                                std::lock_guard<std::mutex> lock(pending_bridge_redeems_mutex_);
                                pending_bridge_redeems_[user_id] = [&bot, channel_id](const std::string& err, const std::string& other_channel_id, const std::string& other_platform) {
                                    if (err.empty()) {
                                        bot.send_message(channel_id, "🎉 **Success!** Channel has been successfully bridged to `" + other_channel_id + "` on **" + other_platform + "**!");
                                    } else {
                                        bot.send_message(channel_id, "❌ **Bridge Error:** " + err);
                                    }
                                };
                            }
                            nlohmann::json req = {
                                {"type", "bridge_add_redeem"},
                                {"user_id", user_id},
                                {"channel_id", channel_id},
                                {"code", code}
                            };
                            send_json(req);
                        }
                    });
                }
                else if (subcommand == "remove") {
                    bot.fetch_member(msg.server_id, msg.author.id, [this, &bot, msg](stoatpp::models::Member member, bool success) {
                        if (!success) {
                            bot.send_message(msg.channel_id, "⚠️ Failed to verify your permissions.");
                            return;
                        }
                        if (!is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, member)) {
                            bot.send_message(msg.channel_id, "⚠️ Permission error: you must be an administrator or trusted user to manage bridges.");
                            return;
                        }

                        std::string user_id = msg.author.id;
                        std::string channel_id = msg.channel_id;
                        {
                            std::lock_guard<std::mutex> lock(pending_bridge_removes_mutex_);
                            pending_bridge_removes_[user_id] = [&bot, channel_id](const std::string& err) {
                                if (err.empty()) {
                                    bot.send_message(channel_id, "🎉 **Success!** This channel has been unbridged.");
                                } else {
                                    bot.send_message(channel_id, "❌ **Bridge Error:** " + err);
                                }
                            };
                        }
                        nlohmann::json req = {
                            {"type", "bridge_remove"},
                            {"user_id", user_id},
                            {"channel_id", channel_id}
                        };
                        send_json(req);
                    });
                }
                else if (subcommand == "check") {
                    std::string user_id = msg.author.id;
                    std::string channel_id = msg.channel_id;
                    {
                        std::lock_guard<std::mutex> lock(pending_bridge_checks_mutex_);
                        pending_bridge_checks_[user_id] = [&bot, channel_id](bool found, const std::string& stoat_ch, const std::string& fluxer_ch, const std::string& discord_ch) {
                            if (found) {
                                std::string out = "🔗 **Active Bridge Status**\n";
                                out += "• **Stoat Channel**: " + (stoat_ch.empty() ? "*None*" : "`" + stoat_ch + "`") + "\n";
                                out += "• **Fluxer Channel**: " + (fluxer_ch.empty() ? "*None*" : "`" + fluxer_ch + "`") + "\n";
                                out += "• **Discord Channel**: " + (discord_ch.empty() ? "*None*" : "`" + discord_ch + "`") + "\n";
                                bot.send_message(channel_id, out);
                            } else {
                                bot.send_message(channel_id, "ℹ️ This channel is not bridged.");
                            }
                        };
                    }
                    nlohmann::json req = {
                        {"type", "bridge_check"},
                        {"user_id", user_id},
                        {"channel_id", channel_id}
                    };
                    send_json(req);
                }
                else if (subcommand == "info") {
                    if (msg.replies.empty()) {
                        bot.send_message(msg.channel_id, "❌ Please reply to a message to check its bridge info.");
                        return;
                    }
                    std::string replied_msg_id = msg.replies[0];
                    std::string user_id = msg.author.id;
                    std::string channel_id = msg.channel_id;

                    {
                        std::lock_guard<std::mutex> lock(pending_bridge_infos_mutex_);
                        pending_bridge_infos_[user_id] = [this, &bot, channel_id, replied_msg_id, msg](bool found, const std::string& src_platform, const std::string& src_channel_id, const std::string& src_channel_name, const std::string& src_server_id, const std::string& src_server_name, const std::string& src_user_id, const std::string& src_username) {
                            if (found) {
                                std::string out = "ℹ️ **Bridged Message Information**\n";
                                out += "• **Origin Platform**: " + src_platform + "\n";
                                out += "• **Sender**: " + src_username + " (`" + src_user_id + "`)\n";
                                out += "• **Origin Channel**: #" + (src_channel_name.empty() ? src_channel_id : src_channel_name) + " (`" + src_channel_id + "`)\n";
                                if (!src_server_id.empty()) {
                                    out += "• **Origin Server**: " + (src_server_name.empty() ? src_server_id : src_server_name) + " (`" + src_server_id + "`)\n";
                                }
                                bot.send_message(channel_id, out);
                            } else {
                                std::thread([this, &bot, channel_id, replied_msg_id]() {
                                    auto local_res = bot.rest().get_message(channel_id, replied_msg_id);
                                    if (local_res.success() && local_res.body.is_object()) {
                                        auto local_msg = stoatpp::models::Message::from_json(local_res.body);
                                        auto user_opt = bot.get_user(local_msg.author);
                                        std::string author_name = user_opt ? user_opt->username : local_msg.author;
                                        std::string out = "ℹ️ **Local Message Information**\n";
                                        out += "• **Origin Platform**: stoat\n";
                                        out += "• **Sender**: " + author_name + " (`" + local_msg.author + "`)\n";
                                        out += "• **Origin Channel**: <#" + local_msg.channel + "> (`" + local_msg.channel + "`)\n";
                                        
                                        std::string server_id = "";
                                        auto ch_opt = bot.get_channel(local_msg.channel);
                                        if (ch_opt && ch_opt->server) {
                                            server_id = *ch_opt->server;
                                        }
                                        if (!server_id.empty()) {
                                            out += "• **Origin Server**: `" + server_id + "`\n";
                                        }
                                        bot.send_message(channel_id, out);
                                    } else {
                                        bot.send_message(channel_id, "❌ Message not found.");
                                    }
                                }).detach();
                            }
                        };
                    }
                    nlohmann::json req = {
                        {"type", "bridge_info_query"},
                        {"user_id", user_id},
                        {"channel_id", channel_id},
                        {"message_id", replied_msg_id}
                    };
                    send_json(req);
                }
                else if (subcommand == "emojis") {
                    std::string user_id = msg.author.id;
                    std::string channel_id = msg.channel_id;
                    {
                        std::lock_guard<std::mutex> lock(pending_bridge_emojis_mutex_);
                        pending_bridge_emojis_[user_id] = [&bot, channel_id](bool found, const nlohmann::json& emojis) {
                            if (found) {
                                std::string out = "🔗 **Bridged Emojis Sync Status**\n\n";
                                std::string matches = "";
                                std::string unmatched_discord = "";
                                std::string unmatched_stoat = "";
                                std::string unmatched_fluxer = "";
                                int match_count = 0;

                                for (const auto& item : emojis) {
                                    std::string name = item["name"].get<std::string>();
                                    bool has_disc = item.contains("discord") && !item["discord"].is_null();
                                    bool has_stoat = item.contains("stoat") && !item["stoat"].is_null();
                                    bool has_fluxer = item.contains("fluxer") && !item["fluxer"].is_null();

                                    std::vector<std::string> platforms;
                                    if (has_disc) platforms.push_back("Discord");
                                    if (has_stoat) platforms.push_back("Revolt");
                                    if (has_fluxer) platforms.push_back("Guilded");

                                    if (platforms.size() >= 2) {
                                        matches += "• **:" + name + ":** (";
                                        for (size_t i = 0; i < platforms.size(); ++i) {
                                            matches += platforms[i] + (i == platforms.size() - 1 ? "" : ", ");
                                        }
                                        matches += ")\n";
                                        match_count++;
                                    } else {
                                        if (has_disc) unmatched_discord += "• **:" + name + ":**\n";
                                        if (has_stoat) unmatched_stoat += "• **:" + name + ":**\n";
                                        if (has_fluxer) unmatched_fluxer += "• **:" + name + ":**\n";
                                    }
                                }

                                if (match_count > 0) {
                                    out += "**Matched Emojis (" + std::to_string(match_count) + ")**:\n" + matches + "\n";
                                } else {
                                    out += "**Matched Emojis**: *none*\n\n";
                                }

                                out += "**Unmatched Emojis**:\n";
                                out += "• **Discord**: " + (unmatched_discord.empty() ? "*none*" : "\n" + unmatched_discord) + "\n";
                                out += "• **Revolt (Stoat)**: " + (unmatched_stoat.empty() ? "*none*" : "\n" + unmatched_stoat) + "\n";
                                out += "• **Guilded (Fluxer)**: " + (unmatched_fluxer.empty() ? "*none*" : "\n" + unmatched_fluxer) + "\n";

                                bot.send_message(channel_id, out);
                            } else {
                                bot.send_message(channel_id, "❌ **Bridge Error:** This channel is not bridged.");
                            }
                        };
                    }

                    nlohmann::json req = {
                        {"type", "bridge_emojis_query"},
                        {"user_id", user_id},
                        {"channel_id", channel_id}
                    };
                    send_json(req);
                }
            },
            "Utility"
        });
    }

private:
    void send_json(const nlohmann::json& j) {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        if (socket_fd_ != -1) {
            std::string data = j.dump() + "\n";
            send(socket_fd_, data.c_str(), data.size(), 0);
        }
    }

    void run_socket_loop() {
        while (running_) {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                continue;
            }

            sockaddr_in serv_addr;
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(9000);
            inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

            if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
                close(sock);
                std::this_thread::sleep_for(std::chrono::seconds(5));
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(socket_mutex_);
                socket_fd_ = sock;
            }

            // Register platform
            nlohmann::json reg = {
                {"type", "register"},
                {"platform", "stoat"}
            };
            send_json(reg);

            std::string buffer;
            char char_buf[1024];
            while (running_) {
                ssize_t bytes = recv(sock, char_buf, sizeof(char_buf) - 1, 0);
                if (bytes <= 0) {
                    break;
                }
                char_buf[bytes] = '\0';
                buffer += char_buf;

                size_t pos;
                while ((pos = buffer.find('\n')) != std::string::npos) {
                    std::string line = buffer.substr(0, pos);
                    buffer.erase(0, pos + 1);

                    try {
                        auto j = nlohmann::json::parse(line);
                        handle_event(j);
                    } catch (const std::exception& e) {
                        std::cerr << "[bridge stoat] JSON parse error: " << e.what() << "\n";
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(socket_mutex_);
                close(socket_fd_);
                socket_fd_ = -1;
            }

            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    void handle_event(const nlohmann::json& j) {
        if (!bot_ptr_) return;
        auto& bot = *bot_ptr_;

        std::string type = j.contains("type") ? j["type"].get<std::string>() : "";

        if (type == "init") {
            std::lock_guard<std::mutex> lock(channels_mutex_);
            bridged_channels_.clear();
            if (j.contains("bridged_channels") && j["bridged_channels"].is_array()) {
                for (const auto& c : j["bridged_channels"]) {
                    bridged_channels_.insert(c.get<std::string>());
                }
            }
            send_emoji_registry();
        } 
        else if (type == "relay") {
            std::string channel_id = j["channel_id"].get<std::string>();
            std::string author_name = j["author_name"].get<std::string>();
            std::string author_avatar = j["author_avatar"].get<std::string>();
            std::string content = j["content"].get<std::string>();

            std::string src_platform = j.contains("src_platform") ? j["src_platform"].get<std::string>() : "";
            std::string src_channel_id = j.contains("src_channel_id") ? j["src_channel_id"].get<std::string>() : "";
            std::string src_channel_name = j.contains("src_channel_name") ? j["src_channel_name"].get<std::string>() : "";
            std::string src_server_id = j.contains("src_server_id") ? j["src_server_id"].get<std::string>() : "";
            std::string src_server_name = j.contains("src_server_name") ? j["src_server_name"].get<std::string>() : "";
            std::string src_user_id = j.contains("src_user_id") ? j["src_user_id"].get<std::string>() : "";
            std::string src_username = j.contains("src_username") ? j["src_username"].get<std::string>() : "";
            std::string src_message_id = j.contains("src_message_id") ? j["src_message_id"].get<std::string>() : "";
            std::string reply_to_message_id = j.contains("reply_to_message_id") ? j["reply_to_message_id"].get<std::string>() : "";

            // Relaying to Stoat (Revolt): Use masquerade with the bot
            stoatpp::models::MessagePayload payload;
            payload.content = content;
            payload.masquerade = stoatpp::models::MessagePayload::Masquerade{};
            payload.masquerade->name = author_name;
            if (!author_avatar.empty()) {
                payload.masquerade->avatar = author_avatar;
            }
            if (!reply_to_message_id.empty()) {
                payload.add_reply(reply_to_message_id);
            }
            bot.send_message(channel_id, payload, [this, src_platform, src_channel_id, src_channel_name, src_server_id, src_server_name, src_user_id, src_username, src_message_id](stoatpp::models::Message result, bool success) {
                if (success) {
                    nlohmann::json track = {
                        {"type", "track_relayed_message"},
                        {"dest_message_id", result.id},
                        {"src_message_id", src_message_id},
                        {"src_platform", src_platform},
                        {"src_channel_id", src_channel_id},
                        {"src_channel_name", src_channel_name},
                        {"src_server_id", src_server_id},
                        {"src_server_name", src_server_name},
                        {"src_user_id", src_user_id},
                        {"src_username", src_username}
                    };
                    send_json(track);
                }
            });
        } 
        else if (type == "reaction_add") {
            std::string channel_id = j["channel_id"].get<std::string>();
            std::string message_id = j["message_id"].get<std::string>();
            std::string emoji = j["emoji"].get<std::string>();
            bot.react_to_message(channel_id, message_id, emoji);
        }
        else if (type == "reaction_remove") {
            std::string channel_id = j["channel_id"].get<std::string>();
            std::string message_id = j["message_id"].get<std::string>();
            std::string emoji = j["emoji"].get<std::string>();
            bot.unreact_from_message(channel_id, message_id, emoji, bot.current_user().id);
        } 
        else if (type == "link_generated") {
            std::string user_id = j["user_id"].get<std::string>();
            std::string code = j["code"].get<std::string>();
            std::function<void(const std::string&)> cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(pending_gens_mutex_);
                auto it = pending_gens_.find(user_id);
                if (it != pending_gens_.end()) {
                    cb = it->second;
                    pending_gens_.erase(it);
                }
            }
            if (cb) cb(code);
        } 
        else if (type == "link_created") {
            std::string stoat_id = j.contains("stoat_id") && !j["stoat_id"].is_null() ? j["stoat_id"].get<std::string>() : "";
            std::string fluxer_id = j.contains("fluxer_id") && !j["fluxer_id"].is_null() ? j["fluxer_id"].get<std::string>() : "";
            std::string discord_id = j.contains("discord_id") && !j["discord_id"].is_null() ? j["discord_id"].get<std::string>() : "";

            if (bronx::g_db) {
                bronx::g_db->save_account_link(stoat_id, fluxer_id, discord_id);
            }

            // Route successful redeem if this user redeemed it
            std::function<void(const std::string&)> cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(pending_redeems_mutex_);
                auto it = pending_redeems_.find(stoat_id);
                if (it != pending_redeems_.end()) {
                    cb = it->second;
                    pending_redeems_.erase(it);
                }
            }
            if (cb) cb("");
        } 
        else if (type == "link_error") {
            std::string user_id = j["user_id"].get<std::string>();
            std::string message = j["message"].get<std::string>();
            std::function<void(const std::string&)> cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(pending_redeems_mutex_);
                auto it = pending_redeems_.find(user_id);
                if (it != pending_redeems_.end()) {
                    cb = it->second;
                    pending_redeems_.erase(it);
                }
            }
            if (cb) cb(message);
        } 
        else if (type == "merge_balances_request") {
            std::string stoat_id = j.contains("stoat_id") && !j["stoat_id"].is_null() ? j["stoat_id"].get<std::string>() : "";
            std::string fluxer_id = j.contains("fluxer_id") && !j["fluxer_id"].is_null() ? j["fluxer_id"].get<std::string>() : "";
            std::string discord_id = j.contains("discord_id") && !j["discord_id"].is_null() ? j["discord_id"].get<std::string>() : "";

            if (!stoat_id.empty() && bronx::g_db) {
                auto eco_opt = bronx::g_db->get_economy_user(stoat_id);
                if (eco_opt) {
                    nlohmann::json rep = {
                        {"type", "report_balance"},
                        {"stoat_id", stoat_id},
                        {"fluxer_id", fluxer_id},
                        {"discord_id", discord_id},
                        {"wallet", eco_opt->wallet},
                        {"bank", eco_opt->bank}
                    };
                    send_json(rep);
                }
            }
        } 
        else if (type == "balance_sync") {
            std::string stoat_id = j.contains("stoat_id") && !j["stoat_id"].is_null() ? j["stoat_id"].get<std::string>() : "";
            int64_t target_wallet = j["wallet"].get<int64_t>();
            int64_t target_bank = j["bank"].get<int64_t>();

            if (!stoat_id.empty() && bronx::g_db) {
                auto eco_opt = bronx::g_db->get_economy_user(stoat_id);
                if (eco_opt) {
                    int64_t w_delta = target_wallet - eco_opt->wallet;
                    int64_t b_delta = target_bank - eco_opt->bank;
                    if (w_delta != 0 || b_delta != 0) {
                        {
                            std::lock_guard<std::mutex> lock(syncs_mutex_);
                            users_syncing_.insert(stoat_id);
                        }
                        bronx::g_db->update_balances(stoat_id, w_delta, b_delta);
                        {
                            std::lock_guard<std::mutex> lock(syncs_mutex_);
                            users_syncing_.erase(stoat_id);
                        }
                    }
                }
            }
        } 
        else if (type == "balance_sync_delta") {
            std::string stoat_id = j.contains("stoat_id") && !j["stoat_id"].is_null() ? j["stoat_id"].get<std::string>() : "";
            int64_t w_delta = j["wallet_delta"].get<int64_t>();
            int64_t b_delta = j["bank_delta"].get<int64_t>();

            if (!stoat_id.empty() && bronx::g_db && (w_delta != 0 || b_delta != 0)) {
                {
                    std::lock_guard<std::mutex> lock(syncs_mutex_);
                    users_syncing_.insert(stoat_id);
                }
                bronx::g_db->update_balances(stoat_id, w_delta, b_delta);
                {
                    std::lock_guard<std::mutex> lock(syncs_mutex_);
                    users_syncing_.erase(stoat_id);
                }
            }
        } 
        else if (type == "afk_sync") {
            std::string stoat_id = j.contains("stoat_id") && !j["stoat_id"].is_null() ? j["stoat_id"].get<std::string>() : "";
            bool is_afk = j["is_afk"].get<bool>();
            std::string reason = j["reason"].get<std::string>();

            if (!stoat_id.empty() && bronx::g_db) {
                {
                    std::lock_guard<std::mutex> lock(syncs_mutex_);
                    users_syncing_.insert(stoat_id);
                }
                bronx::Database::UserAFK afk;
                afk.user_id = stoat_id;
                afk.is_afk = is_afk;
                afk.reason = reason;
                afk.since = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                bronx::g_db->save_user_afk(afk);
                {
                    std::lock_guard<std::mutex> lock(syncs_mutex_);
                    users_syncing_.erase(stoat_id);
                }
            }
        } 
        else if (type == "mod_sync") {
            std::string stoat_id = j.contains("stoat_id") && !j["stoat_id"].is_null() ? j["stoat_id"].get<std::string>() : "";
            std::string action = j["action"].get<std::string>();
            std::string reason = j["reason"].get<std::string>();

            if (!stoat_id.empty()) {
                // Perform moderation action on all servers the bot is in where this member is present
                std::thread([&bot, stoat_id, action, reason]() {
                    auto servers = bot.get_servers();
                    for (const auto& srv : servers) {
                        auto member_opt = bot.get_member(srv.id, stoat_id);
                        if (member_opt) {
                            if (action == "ban") {
                                bot.ban_user(srv.id, stoat_id, reason);
                            } else if (action == "kick") {
                                bot.kick_member(srv.id, stoat_id);
                            } else if (action == "timeout") {
                                bot.timeout_member(srv.id, stoat_id, reason); // duration in ISO
                            } else if (action == "untimeout") {
                                bot.remove_timeout(srv.id, stoat_id);
                            }
                        }
                    }
                }).detach();
            }
        }
        else if (type == "bridge_add_generated") {
            std::string user_id = j["user_id"].get<std::string>();
            std::string code = j["code"].get<std::string>();
            std::string channel_id = j["channel_id"].get<std::string>();
            std::function<void(const std::string&, const std::string&)> cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(pending_bridge_gens_mutex_);
                auto it = pending_bridge_gens_.find(user_id);
                if (it != pending_bridge_gens_.end()) {
                    cb = it->second;
                    pending_bridge_gens_.erase(it);
                }
            }
            if (cb) cb(code, channel_id);
        }
        else if (type == "bridge_add_success") {
            std::string user_id = j["user_id"].get<std::string>();
            std::string channel_id = j["channel_id"].get<std::string>();
            std::string other_channel_id = j["other_channel_id"].get<std::string>();
            std::string other_platform = j["other_platform"].get<std::string>();
            std::function<void(const std::string&, const std::string&, const std::string&)> cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(pending_bridge_redeems_mutex_);
                auto it = pending_bridge_redeems_.find(user_id);
                if (it != pending_bridge_redeems_.end()) {
                    cb = it->second;
                    pending_bridge_redeems_.erase(it);
                }
            }
            if (cb) cb("", other_channel_id, other_platform);
        }
        else if (type == "bridge_add_error") {
            std::string user_id = j["user_id"].get<std::string>();
            std::string message = j["message"].get<std::string>();
            std::function<void(const std::string&, const std::string&, const std::string&)> cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(pending_bridge_redeems_mutex_);
                auto it = pending_bridge_redeems_.find(user_id);
                if (it != pending_bridge_redeems_.end()) {
                    cb = it->second;
                    pending_bridge_redeems_.erase(it);
                }
            }
            if (cb) cb(message, "", "");
        }
        else if (type == "bridge_remove_success") {
            std::string user_id = j["user_id"].get<std::string>();
            std::function<void(const std::string&)> cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(pending_bridge_removes_mutex_);
                auto it = pending_bridge_removes_.find(user_id);
                if (it != pending_bridge_removes_.end()) {
                    cb = it->second;
                    pending_bridge_removes_.erase(it);
                }
            }
            if (cb) cb("");
        }
        else if (type == "bridge_remove_error") {
            std::string user_id = j["user_id"].get<std::string>();
            std::string message = j["message"].get<std::string>();
            std::function<void(const std::string&)> cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(pending_bridge_removes_mutex_);
                auto it = pending_bridge_removes_.find(user_id);
                if (it != pending_bridge_removes_.end()) {
                    cb = it->second;
                    pending_bridge_removes_.erase(it);
                }
            }
            if (cb) cb(message);
        }
        else if (type == "bridge_check_response") {
            std::string user_id = j["user_id"].get<std::string>();
            bool found = j["found"].get<bool>();
            std::string stoat_ch = found && j.contains("stoat_channel") && !j["stoat_channel"].is_null() ? j["stoat_channel"].get<std::string>() : "";
            std::string fluxer_ch = found && j.contains("fluxer_channel") && !j["fluxer_channel"].is_null() ? j["fluxer_channel"].get<std::string>() : "";
            std::string discord_ch = found && j.contains("discord_channel") && !j["discord_channel"].is_null() ? j["discord_channel"].get<std::string>() : "";
            std::function<void(bool, const std::string&, const std::string&, const std::string&)> cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(pending_bridge_checks_mutex_);
                auto it = pending_bridge_checks_.find(user_id);
                if (it != pending_bridge_checks_.end()) {
                    cb = it->second;
                    pending_bridge_checks_.erase(it);
                }
            }
            if (cb) cb(found, stoat_ch, fluxer_ch, discord_ch);
        }
        else if (type == "bridge_info_response") {
            std::string user_id = j["user_id"].get<std::string>();
            bool found = j["found"].get<bool>();
            std::string src_platform = found ? j["src_platform"].get<std::string>() : "";
            std::string src_channel_id = found ? j["src_channel_id"].get<std::string>() : "";
            std::string src_channel_name = found ? j["src_channel_name"].get<std::string>() : "";
            std::string src_server_id = found ? j["src_server_id"].get<std::string>() : "";
            std::string src_server_name = found ? j["src_server_name"].get<std::string>() : "";
            std::string src_user_id = found ? j["src_user_id"].get<std::string>() : "";
            std::string src_username = found ? j["src_username"].get<std::string>() : "";
            std::function<void(bool, const std::string&, const std::string&, const std::string&, const std::string&, const std::string&, const std::string&, const std::string&)> cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(pending_bridge_infos_mutex_);
                auto it = pending_bridge_infos_.find(user_id);
                if (it != pending_bridge_infos_.end()) {
                    cb = it->second;
                    pending_bridge_infos_.erase(it);
                }
            }
            if (cb) cb(found, src_platform, src_channel_id, src_channel_name, src_server_id, src_server_name, src_user_id, src_username);
        }
        else if (type == "bridge_emojis_response") {
            std::string user_id = j["user_id"].get<std::string>();
            bool found = j["found"].get<bool>();
            nlohmann::json emojis = (found && j.contains("emojis")) ? j["emojis"] : nlohmann::json::array();
            std::function<void(bool, const nlohmann::json&)> cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(pending_bridge_emojis_mutex_);
                auto it = pending_bridge_emojis_.find(user_id);
                if (it != pending_bridge_emojis_.end()) {
                    cb = it->second;
                    pending_bridge_emojis_.erase(it);
                }
            }
            if (cb) cb(found, emojis);
        }
        else if (type == "bridge_established_notification") {
            std::string channel_id = j["channel_id"].get<std::string>();
            std::string other_channel_id = j["other_channel_id"].get<std::string>();
            std::string other_platform = j["other_platform"].get<std::string>();
            bot.send_message(channel_id, "🔗 Bridge successfully established with channel `" + other_channel_id + "` on **" + other_platform + "**!");
        }
        else if (type == "bridge_removed_notification") {
            std::string channel_id = j["channel_id"].get<std::string>();
            bot.send_message(channel_id, "⚠️ This channel has been unbridged.");
        }
    }

    void send_emoji_registry() {
        if (!bot_ptr_) return;
        auto& bot = *bot_ptr_;
        
        std::thread([this, &bot]() {
            nlohmann::json reg = {{"type", "emoji_registry"}};
            nlohmann::json emojis_array = nlohmann::json::array();
            std::unordered_set<std::string> processed_servers;
            
            std::unordered_set<std::string> channels_copy;
            {
                std::lock_guard<std::mutex> lock(channels_mutex_);
                channels_copy = bridged_channels_;
            }

            for (const auto& channel_id : channels_copy) {
                auto ch_opt = bot.get_channel(channel_id);
                if (ch_opt && ch_opt->server) {
                    std::string server_id = *ch_opt->server;
                    if (processed_servers.count(server_id) == 0) {
                        processed_servers.insert(server_id);
                        auto res = bot.rest().get_server_emojis(server_id);
                        if (res.success() && res.body.is_array()) {
                            for (const auto& item : res.body) {
                                auto emoji = stoatpp::models::Emoji::from_json(item);
                                emojis_array.push_back({
                                    {"name", emoji.name},
                                    {"id", emoji.id},
                                    {"channel_id", channel_id}
                                });
                            }
                        }
                    }
                }
            }
            reg["emojis"] = emojis_array;
            send_json(reg);
        }).detach();
    }

    std::atomic<bool> running_;
    int socket_fd_;
    std::mutex socket_mutex_;
    std::thread loop_thread_;

    stoatpp::cluster* bot_ptr_ = nullptr;

    std::mutex channels_mutex_;
    std::unordered_set<std::string> bridged_channels_;

    std::mutex pending_gens_mutex_;
    std::unordered_map<std::string, std::function<void(const std::string&)>> pending_gens_;

    std::mutex pending_redeems_mutex_;
    std::unordered_map<std::string, std::function<void(const std::string&)>> pending_redeems_;

    std::mutex syncs_mutex_;
    std::unordered_set<std::string> users_syncing_;

    std::mutex pending_bridge_gens_mutex_;
    std::unordered_map<std::string, std::function<void(const std::string&, const std::string&)>> pending_bridge_gens_;

    std::mutex pending_bridge_redeems_mutex_;
    std::unordered_map<std::string, std::function<void(const std::string&, const std::string&, const std::string&)>> pending_bridge_redeems_;

    std::mutex pending_bridge_removes_mutex_;
    std::unordered_map<std::string, std::function<void(const std::string&)>> pending_bridge_removes_;

    std::mutex pending_bridge_checks_mutex_;
    std::unordered_map<std::string, std::function<void(bool, const std::string&, const std::string&, const std::string&)>> pending_bridge_checks_;

    std::mutex pending_bridge_infos_mutex_;
    std::unordered_map<std::string, std::function<void(bool, const std::string&, const std::string&, const std::string&, const std::string&, const std::string&, const std::string&, const std::string&)>> pending_bridge_infos_;

    std::mutex pending_bridge_emojis_mutex_;
    std::unordered_map<std::string, std::function<void(bool, const nlohmann::json&)>> pending_bridge_emojis_;
};

} // namespace bronx::cogs
