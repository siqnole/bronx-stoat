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
#include <httplib.h>
#include <iostream>
#include "command_config.h"
#include "logging.h"

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

            // Check if this user is in an active server-sync wizard
            {
                std::lock_guard<std::mutex> lock(sync_wizard_mutex_);
                auto it = sync_wizards_.find(msg.author.id);
                if (it != sync_wizards_.end()) {
                    handle_sync_wizard_input(bot, msg, it->second);
                    return;
                }
            }

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
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Database is unavailable.");
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
                        std::string out = "" + bronx::emj_no + " **You do not have any linked accounts.**\n\n"
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
                        bot.send_message(msg.channel_id, "" + bronx::emj_no + " Please specify the code: `.link redeem <code>`.");
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
                                bot.send_message(channel_id, "" + bronx::emj_no + " **Link Error:** " + err);
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
                                                     "• `.bridge emojis`: Shows custom emojis sync status across platforms.\n"
                                                     "• `.bridge emojis sync`: Copies missing custom emojis from Discord/Fluxer to Stoat.");
                    return;
                }
                
                std::string subcommand = args[0];
                if (subcommand == "add") {
                    bot.fetch_member(msg.server_id, msg.author.id, [this, &bot, msg, args](stoatpp::models::Member member, bool success) {
                        if (!success) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to verify your permissions.");
                            return;
                        }
                        if (!is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, member)) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Permission error: you must be an administrator or trusted user to manage bridges.");
                            return;
                        }

                        if (args.size() < 2) {
                            std::string user_id = msg.author.id;
                            std::string channel_id = msg.channel_id;
                            {
                                std::lock_guard<std::mutex> lock(pending_bridge_gens_mutex_);
                                pending_bridge_gens_[user_id] = [&bot, user_id, channel_id](const std::string& code, const std::string&) {
                                    auto dm_res = bot.rest().open_dm(user_id);
                                    std::string dm_channel_id;
                                    if (dm_res.body.contains("_id") && dm_res.body["_id"].is_string())
                                        dm_channel_id = dm_res.body["_id"].get<std::string>();
                                    else if (dm_res.body.contains("id") && dm_res.body["id"].is_string())
                                        dm_channel_id = dm_res.body["id"].get<std::string>();
                                    if (!dm_channel_id.empty()) {
                                        bot.send_message(dm_channel_id, "🔑 **Bridge Verification Code**\n"
                                                                         "Run `.bridge add " + code + "` in the target channel on another platform to bridge them.\n"
                                                                         "⏳ This code will expire in 5 minutes.");
                                        bot.send_message(channel_id, "📩 **Bridge Code Sent!** Check your DMs (<@" + user_id + ">) for the verification code.");
                                    } else {
                                        bot.send_message(channel_id, "" + bronx::emj_warning + " Could not open a DM. Please enable DMs from server members.");
                                    }
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
                                        bot.send_message(channel_id, "" + bronx::emj_no + " **Bridge Error:** " + err);
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
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to verify your permissions.");
                            return;
                        }
                        if (!is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, member)) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Permission error: you must be an administrator or trusted user to manage bridges.");
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
                                    bot.send_message(channel_id, "" + bronx::emj_no + " **Bridge Error:** " + err);
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
                        bot.send_message(msg.channel_id, "" + bronx::emj_no + " Please reply to a message to check its bridge info.");
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
                                        bot.send_message(channel_id, "" + bronx::emj_no + " Message not found.");
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
                    bool is_sync = (args.size() > 1 && (args[1] == "sync" || args[1] == "copy"));

                    if (is_sync) {
                        bot.fetch_member(msg.server_id, msg.author.id, [this, &bot, msg](stoatpp::models::Member member, bool success) {
                            if (!success) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to verify your permissions.");
                                return;
                            }
                            if (!is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, member)) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Permission error: you must be an administrator or trusted user to sync bridge emojis.");
                                return;
                            }

                            std::string user_id = msg.author.id;
                            std::string channel_id = msg.channel_id;
                            std::string server_id = msg.server_id;

                            bot.send_message(channel_id, "🎨 **Syncing Bridged Emojis...**\nFetching missing custom emojis from bridged platforms...");

                            {
                                std::lock_guard<std::mutex> lock(pending_bridge_emojis_mutex_);
                                pending_bridge_emojis_[user_id] = [this, &bot, channel_id, server_id](bool found, const nlohmann::json& emojis) {
                                    if (!found) {
                                        bot.send_message(channel_id, "" + bronx::emj_no + " **Bridge Error:** This channel is not bridged or no emojis were found.");
                                        return;
                                    }

                                    std::thread([this, &bot, channel_id, server_id, emojis]() {
                                        std::unordered_set<std::string> existing_names;
                                        auto existing_res = bot.rest().get_server_emojis(server_id);
                                        if (existing_res.success() && existing_res.body.is_array()) {
                                            for (const auto& item : existing_res.body) {
                                                if (item.contains("name") && item["name"].is_string()) {
                                                    std::string n = item["name"].get<std::string>();
                                                    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                                                    existing_names.insert(n);
                                                }
                                            }
                                        }

                                        int added_count = 0;
                                        int skipped_count = 0;
                                        int failed_count = 0;
                                        std::vector<std::string> added_names;

                                        for (const auto& item : emojis) {
                                            if (!item.contains("name") || !item["name"].is_string()) continue;
                                            std::string raw_name = item["name"].get<std::string>();

                                            std::string clean_name = "";
                                            for (char c : raw_name) {
                                                if (std::isalnum((unsigned char)c) || c == '_') clean_name += c;
                                            }
                                            if (clean_name.empty()) clean_name = "emoji";
                                            if (clean_name.length() > 32) clean_name = clean_name.substr(0, 32);

                                            std::string name_lower = clean_name;
                                            std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);

                                            bool has_stoat = item.contains("stoat") && !item["stoat"].is_null() && !item["stoat"].get<std::string>().empty();
                                            if (has_stoat || existing_names.count(name_lower)) {
                                                skipped_count++;
                                                continue;
                                            }

                                            std::string img_url = "";
                                            if (item.contains("url") && item["url"].is_string() && !item["url"].get<std::string>().empty()) {
                                                img_url = item["url"].get<std::string>();
                                            } else if (item.contains("discord") && item["discord"].is_string() && item["discord"].get<std::string>().rfind("http", 0) == 0) {
                                                img_url = item["discord"].get<std::string>();
                                            } else if (item.contains("fluxer") && item["fluxer"].is_string() && item["fluxer"].get<std::string>().rfind("http", 0) == 0) {
                                                img_url = item["fluxer"].get<std::string>();
                                            } else if (item.contains("discord_url") && item["discord_url"].is_string()) {
                                                img_url = item["discord_url"].get<std::string>();
                                            } else if (item.contains("fluxer_url") && item["fluxer_url"].is_string()) {
                                                img_url = item["fluxer_url"].get<std::string>();
                                            } else if (item.contains("image_url") && item["image_url"].is_string()) {
                                                img_url = item["image_url"].get<std::string>();
                                            }

                                            if (img_url.empty()) {
                                                failed_count++;
                                                continue;
                                            }

                                            bool is_animated = (img_url.find(".gif") != std::string::npos);
                                            bool uploaded_ok = download_and_create_emoji(bot, server_id, clean_name, img_url, is_animated);
                                            if (uploaded_ok) {
                                                added_count++;
                                                added_names.push_back("`:" + clean_name + ":`");
                                                existing_names.insert(name_lower);
                                            } else {
                                                failed_count++;
                                            }
                                        }

                                        std::string out = "🎉 **Bridged Emojis Sync Complete!**\n\n";
                                        out += "• **Added to Stoat**: " + std::to_string(added_count) + "\n";
                                        out += "• **Already Synced/Present**: " + std::to_string(skipped_count) + "\n";
                                        if (failed_count > 0) {
                                            out += "• **Failed**: " + std::to_string(failed_count) + "\n";
                                        }
                                        if (!added_names.empty()) {
                                            out += "\n**Newly Added Emojis:** ";
                                            for (size_t i = 0; i < added_names.size(); ++i) {
                                                out += added_names[i] + (i + 1 < added_names.size() ? ", " : "");
                                            }
                                        }

                                        bot.send_message(channel_id, out);
                                        send_emoji_registry();
                                    }).detach();
                                };
                            }

                            nlohmann::json req = {
                                {"type", "bridge_emojis_sync_request"},
                                {"user_id", user_id},
                                {"channel_id", channel_id},
                                {"server_id", server_id}
                            };
                            send_json(req);
                        });
                    } else {
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
                                    out += "• **Guilded (Fluxer)**: " + (unmatched_fluxer.empty() ? "*none*" : "\n" + unmatched_fluxer) + "\n\n";
                                    out += "*Run `.bridge emojis sync` to copy missing custom emojis from Discord/Fluxer to Stoat.*";

                                    bot.send_message(channel_id, out);
                                } else {
                                    bot.send_message(channel_id, "" + bronx::emj_no + " **Bridge Error:** This channel is not bridged.");
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
                }
            },
            "Utility"
        });

        // `.serversync` command
        bot.register_command({
            "serversync",
            {},
            "sync entire server structures between platforms, auto-bridging matched channels.",
            "serversync [generate|<code>|yes|edit|remove|status]",
            {},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::string user_id   = msg.author.id;
                std::string server_id = msg.server_id;
                std::string channel_id = msg.channel_id;

                if (args.empty()) {
                    // Show status for this server
                    {
                        std::lock_guard<std::mutex> lock(pending_sync_status_mutex_);
                        pending_sync_status_[user_id] = [&bot, channel_id](bool found, const nlohmann::json& pairs) {
                            if (!found || pairs.empty()) {
                                bot.send_message(channel_id,
                                    "ℹ️ **Server Sync Status**\nThis server has no active sync links.\n\n"
                                    "• `.serversync generate` — generate a pairing code\n"
                                    "• `.serversync <code>` — redeem a code from another platform\n"
                                    "• `.serversync remove [platform]` — remove all pairs with a platform");
                                return;
                            }
                            std::string out = "🔗 **Server Sync Status**\n\n";
                            for (const auto& p : pairs) {
                                out += "• **" + p.value("local_channel_name", p.value("local_channel_id", "?")) + "**";
                                out += " ↔ `" + p.value("remote_channel_id", "?") + "`";
                                out += " on **" + p.value("remote_platform", "?") + "**\n";
                            }
                            bot.send_message(channel_id, out);
                        };
                    }
                    nlohmann::json req = {
                        {"type",      "server_sync_status"},
                        {"user_id",   user_id},
                        {"server_id", server_id}
                    };
                    send_json(req);
                    return;
                }

                const std::string& sub = args[0];

                if (sub == "generate") {
                    bot.fetch_member(server_id, user_id, [this, &bot, msg, user_id, server_id, channel_id]
                            (stoatpp::models::Member member, bool success) {
                        if (!success || !is_user_trusted_or_owner(bot, server_id, user_id, member)) {
                            bot.send_message(channel_id, bronx::emj_warning + " You must be an administrator or trusted user to start a server sync.");
                            return;
                        }
                        // Snapshot local channels
                        nlohmann::json channels = snapshot_server_channels(bot, server_id);
                        {
                            std::lock_guard<std::mutex> lock(pending_sync_gens_mutex_);
                            pending_sync_gens_[user_id] = [&bot, channel_id, user_id](const std::string& code) {
                                auto dm_res = bot.rest().open_dm(user_id);
                                std::string dm_id;
                                if (dm_res.body.contains("_id") && dm_res.body["_id"].is_string())
                                    dm_id = dm_res.body["_id"].get<std::string>();
                                else if (dm_res.body.contains("id") && dm_res.body["id"].is_string())
                                    dm_id = dm_res.body["id"].get<std::string>();
                                if (!dm_id.empty()) {
                                    bot.send_message(dm_id,
                                        "🔑 **Server Sync Code**\n"
                                        "Run `.serversync " + code + "` in any channel of the target server on another platform.\n"
                                        "⏳ This code will expire in 5 minutes.");
                                    bot.send_message(channel_id, "📩 **Code sent to your DMs!** (<@" + user_id + ">)");
                                } else {
                                    bot.send_message(channel_id, bronx::emj_warning + " Could not open a DM. Enable DMs from server members.");
                                }
                            };
                        }
                        nlohmann::json req = {
                            {"type",      "server_sync_generate"},
                            {"user_id",   user_id},
                            {"server_id", server_id},
                            {"channels",  channels}
                        };
                        send_json(req);
                    });

                } else if (sub == "remove") {
                    bot.fetch_member(server_id, user_id, [this, &bot, msg, user_id, server_id, channel_id, args]
                            (stoatpp::models::Member member, bool success) {
                        if (!success || !is_user_trusted_or_owner(bot, server_id, user_id, member)) {
                            bot.send_message(channel_id, bronx::emj_warning + " Permission denied.");
                            return;
                        }
                        std::string platform = (args.size() > 1) ? args[1] : "";
                        if (bronx::g_db) {
                            if (platform.empty()) {
                                // Remove all platforms — query first then delete each
                                auto pairs = bronx::g_db->get_server_sync_pairs(server_id);
                                std::unordered_set<std::string> platforms_seen;
                                for (const auto& p : pairs) platforms_seen.insert(p.remote_platform);
                                for (const auto& plat : platforms_seen)
                                    bronx::g_db->remove_server_sync_pairs(server_id, plat);
                            } else {
                                bronx::g_db->remove_server_sync_pairs(server_id, platform);
                            }
                        }
                        nlohmann::json req = {
                            {"type",      "server_sync_remove"},
                            {"user_id",   user_id},
                            {"server_id", server_id},
                            {"platform",  platform}
                        };
                        send_json(req);
                        bot.send_message(channel_id, "🎉 Server sync pairs removed.");
                    });

                } else if (sub == "yes") {
                    // Confirm wizard proposal
                    std::lock_guard<std::mutex> lock(sync_wizard_mutex_);
                    auto it = sync_wizards_.find(user_id);
                    if (it == sync_wizards_.end() || !it->second.awaiting_confirm) {
                        bot.send_message(channel_id, bronx::emj_no + " No active server sync proposal to confirm.");
                        return;
                    }
                    commit_sync_wizard(bot, it->second, user_id);
                    sync_wizards_.erase(it);

                } else if (sub == "edit") {
                    std::lock_guard<std::mutex> lock(sync_wizard_mutex_);
                    auto it = sync_wizards_.find(user_id);
                    if (it == sync_wizards_.end()) {
                        bot.send_message(channel_id, bronx::emj_no + " No active server sync proposal to edit.");
                        return;
                    }
                    it->second.awaiting_edit    = true;
                    it->second.awaiting_confirm = false;
                    it->second.channel_id       = channel_id;
                    bot.send_message(channel_id,
                        "✏️ **Manual Pairing Mode**\n"
                        "Send pairs one per line as:\n"
                        "```\nstoat_channel_id=remote_channel_id\n```\n"
                        "Type `done` when finished, or `cancel` to abort.");

                } else {
                    // Treat as a code to redeem
                    std::string code = sub;
                    bot.fetch_member(server_id, user_id, [this, &bot, msg, user_id, server_id, channel_id, code]
                            (stoatpp::models::Member member, bool success) {
                        if (!success || !is_user_trusted_or_owner(bot, server_id, user_id, member)) {
                            bot.send_message(channel_id, bronx::emj_warning + " Permission denied.");
                            return;
                        }
                        nlohmann::json channels = snapshot_server_channels(bot, server_id);
                        bot.send_message(channel_id, "🔍 Validating code and building channel map... please wait.");
                        nlohmann::json req = {
                            {"type",      "server_sync_redeem"},
                            {"user_id",   user_id},
                            {"server_id", server_id},
                            {"code",      code},
                            {"channels",  channels}
                        };
                        send_json(req);
                    });
                }
            },
            "Utility"
        });
    }

private:
    // -------------------------------------------------------------------------
    // Server-sync wizard state
    // -------------------------------------------------------------------------
    struct SyncWizardState {
        std::string server_id;
        std::string remote_platform;
        std::string remote_server_id;
        std::string channel_id;           // channel to reply to
        // pairs proposed by the broker (or edited by the user)
        std::vector<std::pair<std::string,std::string>> pairs; // local_ch_id, remote_ch_id
        bool awaiting_confirm = false;
        bool awaiting_edit    = false;
    };

    // Helper: post the proposal table, set wizard state, ask user to confirm
    void post_sync_proposal(stoatpp::cluster& bot,
                            const std::string& user_id,
                            const std::string& channel_id,
                            SyncWizardState& state,
                            const nlohmann::json& pairs_json) {
        state.pairs.clear();
        std::string table = "🔗 **Server Sync Proposal**\n"
                            "Channels matched by name (✅) or unmatched (⚠️):\n\n";

        for (const auto& p : pairs_json) {
            std::string lch  = p.value("local_channel_id",   "");
            std::string lnam = p.value("local_channel_name",  lch);
            std::string rch  = p.value("remote_channel_id",  "");
            std::string rnam = p.value("remote_channel_name", rch);
            bool matched     = p.value("auto_matched", false);

            if (!lch.empty() && !rch.empty()) {
                state.pairs.push_back({lch, rch});
                table += (matched ? "✅" : "⚠️");
                table += " **#" + lnam + "** ↔ **#" + rnam + "**\n";
            } else if (!lch.empty()) {
                table += "❌ **#" + lnam + "** → *(no match)*\n";
            }
        }

        table += "\nReply `.serversync yes` to confirm, `.serversync edit` to adjust manually, or ignore to cancel.";
        bot.send_message(channel_id, table);
        state.awaiting_confirm = true;
        state.awaiting_edit    = false;
        state.channel_id       = channel_id;
    }

    // Helper: commit wizard — send confirm to broker and auto-bridge each pair
    void commit_sync_wizard(stoatpp::cluster& bot,
                            SyncWizardState& state,
                            const std::string& user_id) {
        // Build pairs JSON
        nlohmann::json pairs_json = nlohmann::json::array();
        for (const auto& [lch, rch] : state.pairs) {
            pairs_json.push_back({
                {"local_channel_id",  lch},
                {"remote_channel_id", rch}
            });
        }

        // Persist locally
        if (bronx::g_db) {
            std::vector<bronx::ServerSyncPair> db_pairs;
            for (const auto& [lch, rch] : state.pairs) {
                bronx::ServerSyncPair p;
                p.local_server_id  = state.server_id;
                p.local_channel_id = lch;
                p.remote_platform  = state.remote_platform;
                p.remote_server_id = state.remote_server_id;
                p.remote_channel_id= rch;
                db_pairs.push_back(std::move(p));
            }
            bronx::g_db->save_server_sync_pairs(state.server_id,
                                                state.remote_platform,
                                                db_pairs);
        }

        // Tell broker to commit and auto-create bridge links per pair
        nlohmann::json confirm = {
            {"type",            "server_sync_confirm"},
            {"user_id",         user_id},
            {"server_id",       state.server_id},
            {"remote_platform", state.remote_platform},
            {"pairs",           pairs_json}
        };
        send_json(confirm);

        // Also fire a bridge_add_generate for every confirmed local channel
        // so the broker auto-bridges each pair without user intervention.
        for (const auto& [lch, rch] : state.pairs) {
            std::string gen_user = user_id;
            {
                std::lock_guard<std::mutex> lock(pending_bridge_gens_mutex_);
                // Capture the remote channel so we can redeem immediately on response
                pending_bridge_gens_[gen_user + ":" + lch] =
                    [this, &bot, lch, rch, gen_user, &state]
                    (const std::string& code, const std::string&) {
                        // Redeem the code for the remote channel automatically
                        std::string ruser = gen_user;
                        {
                            std::lock_guard<std::mutex> lock2(pending_bridge_redeems_mutex_);
                            pending_bridge_redeems_[ruser + ":" + rch] =
                                [](const std::string&, const std::string&, const std::string&) {};
                        }
                        nlohmann::json redeem = {
                            {"type",       "bridge_add_redeem"},
                            {"user_id",    ruser},
                            {"channel_id", rch},
                            {"code",       code}
                        };
                        send_json(redeem);
                    };
            }
            nlohmann::json gen = {
                {"type",       "bridge_add_generate"},
                {"user_id",   user_id + ":" + lch},   // compound key to disambiguate
                {"channel_id", lch}
            };
            send_json(gen);
        }
    }

    // Helper: process a line typed during wizard edit mode
    void handle_sync_wizard_input(stoatpp::cluster& bot,
                                  const stoatpp::events::Message& msg,
                                  SyncWizardState& state) {
        std::string text = msg.content;
        // Strip leading whitespace
        while (!text.empty() && std::isspace((unsigned char)text[0])) text.erase(0, 1);

        if (state.awaiting_confirm) {
            // In confirm mode, only .serversync yes/edit will fire (handled in command)
            // but if user types raw "yes"/"no" here, handle gracefully
            if (text == "yes" || text == "y") {
                commit_sync_wizard(bot, state, msg.author.id);
                {
                    std::lock_guard<std::mutex> lock(sync_wizard_mutex_);
                    sync_wizards_.erase(msg.author.id);
                }
                bot.send_message(state.channel_id,
                    "🎉 **Server sync committed!** Bridge links are being established for all matched channels.");
            } else if (text == "no" || text == "cancel") {
                std::lock_guard<std::mutex> lock(sync_wizard_mutex_);
                sync_wizards_.erase(msg.author.id);
                bot.send_message(state.channel_id, "❌ Server sync cancelled.");
            }
            return;
        }

        if (state.awaiting_edit) {
            if (text == "cancel") {
                std::lock_guard<std::mutex> lock(sync_wizard_mutex_);
                sync_wizards_.erase(msg.author.id);
                bot.send_message(state.channel_id, "❌ Server sync cancelled.");
                return;
            }
            if (text == "done") {
                if (state.pairs.empty()) {
                    bot.send_message(state.channel_id, bronx::emj_no + " No pairs set. Add at least one pair before confirming.");
                    return;
                }
                commit_sync_wizard(bot, state, msg.author.id);
                {
                    std::lock_guard<std::mutex> lock(sync_wizard_mutex_);
                    sync_wizards_.erase(msg.author.id);
                }
                bot.send_message(state.channel_id,
                    "🎉 **Server sync committed!** Bridge links are being established for all matched channels.");
                return;
            }
            // Parse "local_id=remote_id" pair
            size_t eq = text.find('=');
            if (eq == std::string::npos) {
                bot.send_message(state.channel_id,
                    bronx::emj_no + " Invalid format. Use `local_channel_id=remote_channel_id`. Type `done` to finish or `cancel` to abort.");
                return;
            }
            std::string lch = text.substr(0, eq);
            std::string rch = text.substr(eq + 1);
            // Remove duplicates for this local channel
            state.pairs.erase(
                std::remove_if(state.pairs.begin(), state.pairs.end(),
                    [&lch](const auto& pr) { return pr.first == lch; }),
                state.pairs.end());
            state.pairs.push_back({lch, rch});
            bot.send_message(state.channel_id,
                "✅ Pair added: `" + lch + "` ↔ `" + rch + "`. "
                "Send another pair or type `done` to confirm.");
        }
    }

    // Helper: snapshot all visible channels for a server
    nlohmann::json snapshot_server_channels(stoatpp::cluster& bot,
                                            const std::string& server_id) {
        nlohmann::json arr = nlohmann::json::array();
        auto server_opt = bot.get_server(server_id);
        if (!server_opt) return arr;
        for (const auto& ch_id : server_opt->channels) {
            auto ch_opt = bot.get_channel(ch_id);
            if (!ch_opt) continue;
            arr.push_back({
                {"id",   ch_id},
                {"name", ch_opt->name ? *ch_opt->name : ""}
            });
        }
        return arr;
    }

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
                std::string url_to_upload = author_avatar;
                {
                    static const std::string fluxer_prefix = "https://cdn.fluxer.app/assets/avatars/";
                    static const std::string fluxer_replacement = "https://fluxerusercontent.com/avatars/";
                    if (url_to_upload.rfind(fluxer_prefix, 0) == 0) {
                        url_to_upload = fluxer_replacement + url_to_upload.substr(fluxer_prefix.size());
                        size_t dot = url_to_upload.rfind('.');
                        if (dot != std::string::npos) url_to_upload = url_to_upload.substr(0, dot) + ".webp?size=160";
                    }
                }
                std::string uploaded = bronx::helpers::upload_avatar(bot, url_to_upload);
                if (!uploaded.empty() && uploaded.rfind(bot.config().autumn_url, 0) == 0) {
                    payload.masquerade->avatar = uploaded;
                }
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
                                helpers::ban_user_with_links(bot, srv.id, stoat_id, reason, bot.current_user().id);
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
            bot.send_message(channel_id, "" + bronx::emj_warning + " This channel has been unbridged.");
        }
        // --- Server Sync events ---
        else if (type == "server_sync_generated") {
            std::string user_id = j["user_id"].get<std::string>();
            std::string code    = j["code"].get<std::string>();
            std::function<void(const std::string&)> cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(pending_sync_gens_mutex_);
                auto it = pending_sync_gens_.find(user_id);
                if (it != pending_sync_gens_.end()) {
                    cb = it->second;
                    pending_sync_gens_.erase(it);
                }
            }
            if (cb) cb(code);
        }
        else if (type == "server_sync_proposal") {
            std::string user_id         = j["user_id"].get<std::string>();
            std::string server_id       = j.value("server_id", "");
            std::string remote_platform = j.value("remote_platform", "");
            std::string remote_server_id= j.value("remote_server_id", "");
            nlohmann::json pairs_json   = j.contains("pairs") ? j["pairs"] : nlohmann::json::array();

            // Build / replace wizard state
            SyncWizardState state;
            state.server_id       = server_id;
            state.remote_platform = remote_platform;
            state.remote_server_id= remote_server_id;

            // Find a reply channel — use any bridged channel in the server,
            // or fall back to the first channel the bot can see.
            std::string reply_ch;
            {
                std::lock_guard<std::mutex> lock(channels_mutex_);
                for (const auto& ch : bridged_channels_) {
                    auto ch_opt = bot.get_channel(ch);
                    if (ch_opt && ch_opt->server && *ch_opt->server == server_id) {
                        reply_ch = ch;
                        break;
                    }
                }
            }
            if (reply_ch.empty()) {
                auto srv_opt = bot.get_server(server_id);
                if (srv_opt && !srv_opt->channels.empty())
                    reply_ch = srv_opt->channels[0];
            }

            post_sync_proposal(bot, user_id, reply_ch, state, pairs_json);
            {
                std::lock_guard<std::mutex> lock(sync_wizard_mutex_);
                sync_wizards_[user_id] = std::move(state);
            }
        }
        else if (type == "server_sync_complete") {
            std::string user_id   = j["user_id"].get<std::string>();
            std::string server_id = j.value("server_id", "");
            // Notify all channels in this server that are bridged
            std::lock_guard<std::mutex> lock(channels_mutex_);
            for (const auto& ch : bridged_channels_) {
                auto ch_opt = bot.get_channel(ch);
                if (ch_opt && ch_opt->server && *ch_opt->server == server_id) {
                    bot.send_message(ch, "✅ **Server sync complete!** All matched channels are now linked.");
                    break; // one notification per server is enough
                }
            }
        }
        else if (type == "server_sync_error") {
            std::string user_id = j["user_id"].get<std::string>();
            std::string message = j["message"].get<std::string>();
            std::function<void(const std::string&)> cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(pending_sync_gens_mutex_);
                auto it = pending_sync_gens_.find(user_id);
                if (it != pending_sync_gens_.end()) {
                    cb = it->second;
                    pending_sync_gens_.erase(it);
                }
            }
            // Clean up any wizard state too
            {
                std::lock_guard<std::mutex> lock(sync_wizard_mutex_);
                auto it = sync_wizards_.find(user_id);
                if (it != sync_wizards_.end()) {
                    bot.send_message(it->second.channel_id,
                        bronx::emj_no + " **Server Sync Error:** " + message);
                    sync_wizards_.erase(it);
                }
            }
        }
        else if (type == "server_sync_status_response") {
            std::string user_id    = j["user_id"].get<std::string>();
            bool found             = j.value("found", false);
            nlohmann::json pairs   = found && j.contains("pairs") ? j["pairs"] : nlohmann::json::array();
            std::function<void(bool, const nlohmann::json&)> cb = nullptr;
            {
                std::lock_guard<std::mutex> lock(pending_sync_status_mutex_);
                auto it = pending_sync_status_.find(user_id);
                if (it != pending_sync_status_.end()) {
                    cb = it->second;
                    pending_sync_status_.erase(it);
                }
            }
            if (cb) cb(found, pairs);
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

    bool download_and_create_emoji(stoatpp::cluster& bot, const std::string& server_id, const std::string& emoji_name, const std::string& img_url, bool is_animated) {
        if (img_url.empty()) return false;

        std::string host, path;
        bool is_https = (img_url.rfind("https://", 0) == 0);
        size_t prefix_len = is_https ? 8 : 7;
        size_t path_pos = img_url.find('/', prefix_len);
        if (path_pos == std::string::npos) {
            host = img_url.substr(prefix_len);
            path = "/";
        } else {
            host = img_url.substr(prefix_len, path_pos - prefix_len);
            path = img_url.substr(path_pos);
        }

        std::string base_url = (is_https ? "https://" : "http://") + host;
        httplib::Client cli(base_url);
        cli.set_connection_timeout(std::chrono::milliseconds(5000));
        cli.set_read_timeout(std::chrono::milliseconds(5000));
        cli.set_follow_location(true);

        auto download_res = cli.Get(path);
        if (!download_res || download_res->status != 200) {
            return false;
        }

        std::vector<uint8_t> data(download_res->body.begin(), download_res->body.end());
        std::string mime_type = download_res->get_header_value("Content-Type");
        if (mime_type.empty()) {
            mime_type = is_animated ? "image/gif" : "image/png";
        }

        std::string ext = (mime_type == "image/gif") ? ".gif" : ".png";
        std::string filename = emoji_name + ext;

        httplib::Client upload_cli(bot.config().cdn_url);
        upload_cli.set_connection_timeout(std::chrono::milliseconds(10000));
        upload_cli.set_read_timeout(std::chrono::milliseconds(10000));
        upload_cli.set_follow_location(true);

        httplib::Headers upload_headers;
        if (bot.config().token_type == stoatpp::TokenType::BOT) {
            upload_headers.emplace("X-Bot-Token", bot.token());
        } else {
            upload_headers.emplace("X-Session-Token", bot.token());
        }
        for (const auto& [k, v] : bot.config().extra_headers) {
            upload_headers.emplace(k, v);
        }

        std::string file_content(data.begin(), data.end());
        httplib::MultipartFormDataItems upload_items = {
            { "file", file_content, filename, mime_type }
        };

        auto upload_res = upload_cli.Post("/emojis", upload_headers, upload_items);
        if (!upload_res || (upload_res->status != 200 && upload_res->status != 201)) {
            return false;
        }

        nlohmann::json upload_json;
        try {
            upload_json = nlohmann::json::parse(upload_res->body);
        } catch (...) {
            return false;
        }

        if (!upload_json.contains("id") || !upload_json["id"].is_string()) {
            return false;
        }

        std::string new_file_id = upload_json["id"].get<std::string>();
        auto create_res = bot.rest().create_custom_emoji(emoji_name, new_file_id, server_id);
        return create_res.success();
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

    // Server-sync state
    std::mutex sync_wizard_mutex_;
    std::unordered_map<std::string, SyncWizardState> sync_wizards_;

    std::mutex pending_sync_gens_mutex_;
    std::unordered_map<std::string, std::function<void(const std::string&)>> pending_sync_gens_;

    std::mutex pending_sync_status_mutex_;
    std::unordered_map<std::string, std::function<void(bool, const nlohmann::json&)>> pending_sync_status_;
};

} // namespace bronx::cogs
