#pragma once
#include "../database.h"
#include "helpers.h"
#include "stoatpp/bot_module.h"
#include <algorithm>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace bronx::cogs {

class SetupModule : public stoatpp::bot_module {
public:
    static void execute_server_setup(stoatpp::cluster& bot, const std::string& server_id, const std::string& channel_id, const std::string& admin_id) {
        std::thread([&bot, server_id, channel_id, admin_id]() {
            bot.send_message(channel_id, "🛠️ **Bronx Auto-Setup:** Initializing configurations for your server...");

            // 1. Create #bronx-logs
            std::string log_chan_id = "";
            auto log_res = bot.rest().create_channel(server_id, "Text", "bronx-logs");
            if (log_res.success()) {
                log_chan_id = log_res.body["_id"].get<std::string>();
                // Save log configurations
                const std::vector<std::string> log_events = {
                    "message_delete", "message_edit", "member_join", "member_leave",
                    "role_create", "role_delete", "role_update", "member_role_update",
                    "channel_create", "channel_delete", "channel_update"
                };
                for (const auto& ev : log_events) {
                    g_db->save_logging_channel(server_id, ev, log_chan_id);
                }
                g_db->reload_logging_config(server_id);
            }

            // 2. Create @Jailed role & #jail channel
            std::string jail_role_id = "";
            std::string jail_chan_id = "";
            auto role_res = bot.rest().create_role(server_id, "Jailed");
            if (role_res.success()) {
                if (role_res.body.contains("id") && role_res.body["id"].is_string()) {
                    jail_role_id = role_res.body["id"].get<std::string>();
                } else if (role_res.body.contains("role") && role_res.body["role"].is_object() && role_res.body["role"].contains("id") && role_res.body["role"]["id"].is_string()) {
                    jail_role_id = role_res.body["role"]["id"].get<std::string>();
                }
            }
            auto jail_res = bot.rest().create_channel(server_id, "Text", "jail");
            if (jail_res.success()) {
                jail_chan_id = jail_res.body["_id"].get<std::string>();
            }

            if (!jail_role_id.empty() && !jail_chan_id.empty()) {
                Database::JailConfig jail_cfg;
                jail_cfg.server_id = server_id;
                jail_cfg.jail_role_id = jail_role_id;
                jail_cfg.jail_channel_id = jail_chan_id;
                g_db->save_jail_config(jail_cfg);
            }

            // 3. Create #honeypot channel
            std::string hp_chan_id = "";
            auto hp_res = bot.rest().create_channel(server_id, "Text", "honeypot");
            if (hp_res.success()) {
                hp_chan_id = hp_res.body["_id"].get<std::string>();
                
                // Configure Honeypot
                Database::HoneypotConfig hp_cfg;
                hp_cfg.server_id = server_id;
                hp_cfg.channel_id = hp_chan_id;
                hp_cfg.honeypot_message = "🍯 **Honeypot Channel**\nDo not send messages or add reactions here.";
                hp_cfg.disclaimer_message = "⚠️ **Disclaimer**\nBy interacting with this channel, you will trigger automated moderation actions on your account.";
                hp_cfg.action = "ban";
                hp_cfg.message_triggers = true;
                hp_cfg.react_triggers = true;
                hp_cfg.enabled = true;
                
                g_db->save_honeypot_config(hp_cfg);

                // Send setup messages to honeypot
                bot.send_message(hp_chan_id, hp_cfg.honeypot_message);
                bot.send_message(hp_chan_id, hp_cfg.disclaimer_message);
            }

            // Build setup summary embed
            stoatpp::models::MessagePayload payload;
            nlohmann::json embed = nlohmann::json::object();
            embed["title"] = "✅ Bronx Setup Complete";
            embed["colour"] = "#2ecc71"; // Green
            
            std::string desc = "All modules have been successfully configured and initialized:\n\n";
            if (!log_chan_id.empty()) {
                desc += "• **Logging:** Logs are now routed to <#" + log_chan_id + ">\n";
            } else {
                desc += "• **Logging:** ⚠️ Failed to create logs channel.\n";
            }

            if (!jail_chan_id.empty() && !jail_role_id.empty()) {
                desc += "• **Jail:** Restricted channel <#" + jail_chan_id + "> and role created.\n";
            } else {
                desc += "• **Jail:** ⚠️ Failed to create jail components.\n";
            }

            if (!hp_chan_id.empty()) {
                desc += "• **Honeypot:** Active trap channel created at <#" + hp_chan_id + "> (Action: Ban).\n";
            } else {
                desc += "• **Honeypot:** ⚠️ Failed to create honeypot channel.\n";
            }
            
            desc += "\n*Your server is now protected! Use `.help` to see all moderation commands.*";
            embed["description"] = desc;
            payload.embeds.push_back(embed);
            bot.send_message(channel_id, payload);
        }).detach();
    }

    void register_handlers(stoatpp::cluster& bot) override {
        // --- COMMAND: .setup ---
        bot.register_command({
            "setup",
            {},
            "runs the one-time automated setup wizard to configure logging, jail, and honeypot channels.",
            "",
            {},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id, "⚠️ This command can only be used in a server.");
                    return;
                }

                // Check permissions (Admin/Server Owner/Manage Server)
                bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg](stoatpp::models::Member member, bool ok) {
                    if (!ok) {
                        bot.send_message(msg.channel_id, "⚠️ Failed to verify permissions.");
                        return;
                    }

                    bool can_setup = bot.is_owner(msg.author.id) || bot.is_server_owner(msg.server_id, msg.author.id);
                    if (!can_setup) {
                        auto server_opt = bot.get_server(msg.server_id);
                        if (server_opt) {
                            can_setup = bot.has_permission(*server_opt, member, stoatpp::permissions::ManageServer);
                        }
                    }

                    if (!can_setup) {
                        bot.send_message(msg.channel_id, "⚠️ Only server owners and administrators can run the setup wizard.");
                        return;
                    }

                    execute_server_setup(bot, msg.server_id, msg.channel_id, msg.author.id);
                });
            },
            "Moderation"
        });

        // --- EVENT: on_message_react (check introduction reaction buttons) ---
        bot.on_message_react([&bot](const stoatpp::events::MessageReact& e) {
            if (e.user_id == bot.current_user().id) return;
            if (e.emoji_id != "🛠️" && e.emoji_id != "🛠" && e.emoji_id != "📖") return;

            // Retrieve message to check if it's the welcome message
            std::thread([&bot, e]() {
                auto res = bot.rest().get_message(e.channel_id, e.id);
                if (!res.success()) return;

                std::string content = res.body.value("content", "");
                if (content.rfind("👋 **Hello, I am Bronx!**", 0) != 0) return;

                // Resolve server ID
                std::string server_id = "";
                auto chan_opt = bot.get_channel(e.channel_id);
                if (chan_opt && chan_opt->server) {
                    server_id = *chan_opt->server;
                } else {
                    auto ch_res = bot.rest().get_channel(e.channel_id);
                    if (ch_res.success() && ch_res.body.contains("server") && ch_res.body["server"].is_string()) {
                        server_id = ch_res.body["server"].get<std::string>();
                    }
                }
                if (server_id.empty()) return;

                // Verify permissions of the user adding reaction
                bot.fetch_member(server_id, e.user_id, [&bot, e, server_id](stoatpp::models::Member member, bool ok) {
                    if (!ok) return;

                    bool can_setup = bot.is_owner(e.user_id) || bot.is_server_owner(server_id, e.user_id);
                    if (!can_setup) {
                        auto server_opt = bot.get_server(server_id);
                        if (server_opt) {
                            can_setup = bot.has_permission(*server_opt, member, stoatpp::permissions::ManageServer);
                        }
                    }

                    if (!can_setup) {
                        // Notify only if they don't have permission
                        stoatpp::models::MessagePayload err_msg;
                        err_msg.content = "⚠️ Only server owners and administrators can run the setup wizard.";
                        err_msg.delete_after = 5;
                        bot.send_message(e.channel_id, err_msg);
                        return;
                    }

                    // Remove their reaction to keep it clean
                    bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);

                    if (e.emoji_id == "🛠️" || e.emoji_id == "🛠") {
                        execute_server_setup(bot, server_id, e.channel_id, e.user_id);
                    } else if (e.emoji_id == "📖") {
                        // Print help link / info
                        stoatpp::models::MessagePayload help_payload;
                        nlohmann::json help_embed = nlohmann::json::object();
                        help_embed["title"] = "📖 Bronx Documentation & Commands";
                        help_embed["colour"] = "#3498db"; // Blue
                        help_embed["description"] = 
                            "Here are some useful commands to get started:\n\n"
                            "• `.help` - Show all commands\n"
                            "• `.honeypot status` - Configure honeypot trap\n"
                            "• `.antiraid status` - Configure anti-raid guard\n"
                            "• `.logs` - Manage logging channels\n"
                            "• `.jail` - Setup/manage jail role & channel\n"
                            "• `.tupper` - Manage your masquerade profiles\n\n"
                            "Need detailed docs? Type `.help` for category listings.";
                        help_payload.embeds.push_back(help_embed);
                        bot.send_message(e.channel_id, help_payload);
                    }
                });
            }).detach();
        });
    }
};

} // namespace bronx::cogs
