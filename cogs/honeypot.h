#pragma once
#include "../database.h"
#include "helpers.h"
#include "logging.h"
#include "stoatpp/bot_module.h"
#include <algorithm>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace bronx::cogs {

class HoneypotModule : public stoatpp::bot_module {
private:
    // Helper to execute automated moderation actions on honeypot interaction
    void execute_honeypot_action(stoatpp::cluster& bot,
                                 const std::string& server_id,
                                 const std::string& user_id,
                                 const std::string& username,
                                 const std::string& actions,
                                 const std::string& channel_id = "",
                                 const std::string& msg_id = "") {
        if (actions == "none" || actions.empty()) return;

        // Log the combined action string once
        auto do_log = [&bot, server_id, actions, user_id, username]() {
            log_moderation_action(bot, server_id, "Honeypot Triggered", bot.current_user().id, user_id, "Interacted with honeypot channel", "Action executed: " + actions);
        };

        // Split and execute each action (comma-separated for multi-action capability)
        std::istringstream ss(actions);
        std::string act;
        bool logged = false;
        while (std::getline(ss, act, ',')) {
            act.erase(0, act.find_first_not_of(" \t"));
            act.erase(act.find_last_not_of(" \t") + 1);
            if (act.empty() || act == "none") continue;

            if (act == "ban") {
                if (!logged) { logged = true;
                    bot.ban_user(server_id, user_id, "[honeypot] Triggered honeypot channel", [do_log](bool ok) {
                        if (ok) do_log();
                    });
                } else {
                    bot.ban_user(server_id, user_id, "[honeypot] Triggered honeypot channel", [](bool){});
                }
            } else if (act == "kick") {
                bot.kick_member(server_id, user_id, [do_log, &logged](bool ok) {
                    if (ok && !logged) { logged = true; do_log(); }
                });
            } else if (act == "timeout") {
                // Default timeout: 10 minutes
                bot.timeout_member(server_id, user_id, "PT10M", [do_log, &logged](bool ok) {
                    if (ok && !logged) { logged = true; do_log(); }
                });
            } else if (act == "warn") {
                std::thread([&bot, user_id, do_log, &logged]() {
                    auto r = bot.rest().open_dm(user_id);
                    if (r.success() && r.body.contains("_id")) {
                        std::string dm_id = r.body["_id"].get<std::string>();
                        bot.send_message(dm_id, "⚠️ **Honeypot Warning**\nYou have been flagged by the automated honeypot system for interacting with a restricted honeypot channel.");
                    }
                    if (!logged) { logged = true; do_log(); }
                }).detach();
            } else if (act == "jail") {
                auto jail_cfg_opt = g_db->get_jail_config(server_id);
                if (jail_cfg_opt && !jail_cfg_opt->jail_role_id.empty() && !jail_cfg_opt->jail_channel_id.empty()) {
                    bot.fetch_member(server_id, user_id, [&bot, server_id, user_id, jail_cfg_opt, do_log, &logged](stoatpp::models::Member target_member, bool ok) {
                        if (ok) {
                            auto existing_jail = g_db->get_active_jail(server_id, user_id);
                            if (!existing_jail) {
                                std::string original_roles_str = "";
                                for (size_t i = 0; i < target_member.roles.size(); ++i) {
                                    original_roles_str += target_member.roles[i] + (i + 1 < target_member.roles.size() ? "," : "");
                                }
                                std::thread([&bot, server_id, user_id, jail_cfg_opt, original_roles_str, do_log, &logged]() {
                                    nlohmann::json edit_fields;
                                    edit_fields["roles"] = { jail_cfg_opt->jail_role_id };
                                    auto edit_res = bot.rest().edit_member(server_id, user_id, edit_fields);
                                    if (edit_res.success()) {
                                        auto now_unix = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                                        Database::ActiveJail jail;
                                        jail.server_id = server_id;
                                        jail.user_id = user_id;
                                        jail.roles = original_roles_str;
                                        jail.jailed_at = now_unix;
                                        jail.expires_at = 0;
                                        g_db->add_active_jail(jail);

                                        std::string default_jail_msg = "<@" + user_id + "> you have been jailed for triggering a honeypot trap. The staff will speak to you soon.";
                                        bot.send_message(jail_cfg_opt->jail_channel_id, default_jail_msg);
                                        if (!logged) { logged = true; do_log(); }
                                    }
                                }).detach();
                            }
                        }
                    });
                }
            } else if (act == "log") {
                if (!logged) { logged = true; do_log(); }
            }
        }

        // Delete the triggering message
        if (!channel_id.empty() && !msg_id.empty()) {
            bot.delete_message(channel_id, msg_id, [](bool){});
        }

        if (!logged) do_log();
    }

    // Helper: check if caller is allowed to manage honeypot settings
    static bool can_manage_honeypot(stoatpp::cluster& bot,
                                    const std::string& server_id,
                                    const std::string& user_id,
                                    const stoatpp::models::Member& member) {
        if (bot.is_owner(user_id)) return true;
        if (bot.is_server_owner(server_id, user_id)) return true;
        if (g_db && g_db->is_trusted(server_id, user_id, member.roles)) return true;
        auto server_opt = bot.get_server(server_id);
        if (server_opt) {
            if (bot.has_permission(*server_opt, member, stoatpp::permissions::ManageServer)) return true;
        }
        return false;
    }

    // Helper: format configuration status for display
    static std::string format_config(const Database::HoneypotConfig& cfg) {
        std::string enabled_mark = cfg.enabled ? "✅ **Enabled**" : "⚠️ **Disabled**";
        std::string chan_str = cfg.channel_id.empty() ? "*None (Run `.honeypot setup` to create)*" : "<#" + cfg.channel_id + ">";
        std::string trig_str;
        if (cfg.message_triggers && cfg.react_triggers) trig_str = "Messages & Reactions";
        else if (cfg.message_triggers) trig_str = "Messages Only";
        else if (cfg.react_triggers) trig_str = "Reactions Only";
        else trig_str = "None";

        return "🍯 **Honeypot Configuration**\n" +
               enabled_mark + "\n\n" +
               "• **Monitored Channel:** " + chan_str + "\n" +
               "• **Automated Action:** `" + cfg.action + "`\n" +
               "• **Triggers:** " + trig_str + "\n" +
               "• **Honeypot Message:** " + (cfg.honeypot_message.empty() ? "*None*" : cfg.honeypot_message) + "\n" +
               "• **Disclaimer Message:** " + (cfg.disclaimer_message.empty() ? "*None*" : cfg.disclaimer_message) + "\n\n" +
               "*Use `.honeypot setup` to initialize the channel, or `.honeypot <action|message|disclaimer|triggers|channel|enable|disable>` to adjust settings.*";
    }

public:
    void register_handlers(stoatpp::cluster& bot) override {

        // --- COMMAND: .honeypot / .hp ---
        bot.register_command({
            "honeypot",
            {"hp"},
            "manage server honeypot monitoring settings.",
            "",
            {"<setup|status|action|message|disclaimer|triggers|channel|enable|disable>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling honeypot...\n";

                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id, "⚠️ This command can only be used in a server.");
                    return;
                }

                if (!g_db) {
                    bot.send_message(msg.channel_id, "⚠️ Database unavailable.");
                    return;
                }

                // Permission check
                bot.fetch_member(msg.server_id, msg.author.id, [this, &bot, msg, args](stoatpp::models::Member member, bool ok) {
                    if (!ok) {
                        bot.send_message(msg.channel_id, "⚠️ Failed to verify your member details.");
                        return;
                    }

                    if (!can_manage_honeypot(bot, msg.server_id, msg.author.id, member)) {
                        bot.send_message(msg.channel_id, "⚠️ Only the server owner, trusted users, or administrators may manage honeypot settings.");
                        return;
                    }

                    // Load or create config
                    auto cfg_opt = g_db->get_honeypot_config(msg.server_id);
                    Database::HoneypotConfig cfg;
                    if (cfg_opt) {
                        cfg = *cfg_opt;
                    } else {
                        cfg.server_id = msg.server_id;
                    }

                    // Dispatch subcommand
                    if (args.empty()) {
                        bot.send_message(msg.channel_id, format_config(cfg));
                        return;
                    }

                    std::string sub = args[0];
                    std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

                    if (sub == "status") {
                        bot.send_message(msg.channel_id, format_config(cfg));
                        return;
                    }

                    if (sub == "setup") {
                        std::string ch_name = "honeypot";
                        if (args.size() > 1) {
                            ch_name = args[1];
                        }
                        
                        std::string hp_msg = cfg.honeypot_message.empty() ? "🍯 **Honeypot Channel**\nDo not send messages or add reactions here." : cfg.honeypot_message;
                        std::string disc_msg = cfg.disclaimer_message.empty() ? "⚠️ **Disclaimer**\nBy interacting with this channel, you will trigger automated moderation actions on your account." : cfg.disclaimer_message;

                        std::thread([&bot, msg, ch_name, cfg, hp_msg, disc_msg]() mutable {
                            auto res = bot.rest().create_channel(msg.server_id, "Text", ch_name);
                            if (!res.success()) {
                                bot.send_message(msg.channel_id, "⚠️ Failed to create honeypot channel: " + res.error_message());
                                return;
                            }

                            std::string new_chan_id = res.body["_id"].get<std::string>();

                            // Send setup messages
                            bot.send_message(new_chan_id, hp_msg);
                            bot.send_message(new_chan_id, disc_msg);

                            // Update and save config
                            cfg.channel_id = new_chan_id;
                            cfg.honeypot_message = hp_msg;
                            cfg.disclaimer_message = disc_msg;
                            cfg.enabled = true;
                            if (g_db->save_honeypot_config(cfg)) {
                                bot.send_message(msg.channel_id, "✅ Honeypot setup complete! Created channel <#" + new_chan_id + "> and enabled monitoring.");
                            } else {
                                bot.send_message(msg.channel_id, "⚠️ Channel created, but failed to save configuration to database.");
                            }
                        }).detach();
                        return;
                    }

                    if (sub == "action") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "usage: `.honeypot action <jail|warn|kick|ban|log|timeout>`");
                            return;
                        }
                        std::string act = args[1];
                        std::transform(act.begin(), act.end(), act.begin(), ::tolower);
                        if (act != "jail" && act != "warn" && act != "kick" && act != "ban" && act != "log" && act != "timeout") {
                            bot.send_message(msg.channel_id, "⚠️ Invalid action. Must be one of: `jail`, `warn`, `kick`, `ban`, `log`, `timeout`.");
                            return;
                        }
                        cfg.action = act;
                        g_db->save_honeypot_config(cfg);
                        bot.send_message(msg.channel_id, "✅ Honeypot action updated to `" + act + "`.");
                        return;
                    }

                    if (sub == "message") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "usage: `.honeypot message <content>`");
                            return;
                        }
                        std::string text = "";
                        for (size_t i = 1; i < args.size(); ++i) {
                            text += args[i] + (i + 1 < args.size() ? " " : "");
                        }
                        cfg.honeypot_message = text;
                        g_db->save_honeypot_config(cfg);
                        bot.send_message(msg.channel_id, "✅ Honeypot message updated.");
                        if (!cfg.channel_id.empty()) {
                            bot.send_message(cfg.channel_id, text);
                        }
                        return;
                    }

                    if (sub == "disclaimer") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "usage: `.honeypot disclaimer <content>`");
                            return;
                        }
                        std::string text = "";
                        for (size_t i = 1; i < args.size(); ++i) {
                            text += args[i] + (i + 1 < args.size() ? " " : "");
                        }
                        cfg.disclaimer_message = text;
                        g_db->save_honeypot_config(cfg);
                        bot.send_message(msg.channel_id, "✅ Honeypot disclaimer message updated.");
                        if (!cfg.channel_id.empty()) {
                            bot.send_message(cfg.channel_id, text);
                        }
                        return;
                    }

                    if (sub == "triggers") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "usage: `.honeypot triggers <message|react|both>`");
                            return;
                        }
                        std::string mode = args[1];
                        std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
                        if (mode == "message" || mode == "messages") {
                            cfg.message_triggers = true;
                            cfg.react_triggers = false;
                        } else if (mode == "react" || mode == "reaction" || mode == "reactions") {
                            cfg.message_triggers = false;
                            cfg.react_triggers = true;
                        } else if (mode == "both") {
                            cfg.message_triggers = true;
                            cfg.react_triggers = true;
                        } else {
                            bot.send_message(msg.channel_id, "⚠️ Invalid trigger mode. Choose `message`, `react`, or `both`.");
                            return;
                        }
                        g_db->save_honeypot_config(cfg);
                        bot.send_message(msg.channel_id, "✅ Honeypot triggers updated.");
                        return;
                    }

                    if (sub == "channel") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "usage: `.honeypot channel <#channel|channel_id>`");
                            return;
                        }
                        std::string chan_query = args[1];
                        if (chan_query.rfind("<#", 0) == 0 && chan_query.back() == '>') {
                            chan_query = chan_query.substr(2, chan_query.size() - 3);
                        }
                        cfg.channel_id = chan_query;
                        g_db->save_honeypot_config(cfg);
                        bot.send_message(msg.channel_id, "✅ Honeypot monitored channel set to <#" + chan_query + ">.");
                        return;
                    }

                    if (sub == "enable" || sub == "disable") {
                        cfg.enabled = (sub == "enable");
                        g_db->save_honeypot_config(cfg);
                        bot.send_message(msg.channel_id, cfg.enabled ? "✅ Honeypot system has been enabled." : "✅ Honeypot system has been disabled.");
                        return;
                    }

                    bot.send_message(msg.channel_id, "⚠️ Unknown subcommand. Use `.honeypot` to list subcommands.");
                });
            },
            "Moderation"
        });

        // --- EVENT: on_message (Honeypot detection) ---
        bot.on_message([this, &bot](const stoatpp::events::Message& msg) {
            if (msg.server_id.empty() || !g_db) return;

            auto cfg_opt = g_db->get_honeypot_config(msg.server_id);
            if (!cfg_opt) return;
            const auto cfg = *cfg_opt;

            if (!cfg.enabled || !cfg.message_triggers || cfg.channel_id.empty()) return;

            // Only trigger if message is sent in the configured honeypot channel
            if (msg.channel_id != cfg.channel_id) return;

            // Skip bots
            bool is_bot = msg.author.bot;
            if (!is_bot) {
                auto cached = bot.get_user(msg.author.id);
                if (cached && cached->bot) is_bot = true;
            }
            if (is_bot) return;

            // Skip bot owner, server owner, and trusted/moderators
            if (bot.is_owner(msg.author.id)) return;
            if (bot.is_server_owner(msg.server_id, msg.author.id)) return;
            if (g_db->is_trusted(msg.server_id, msg.author.id, {})) return;

            // Trigger action!
            std::string username = msg.author.username;
            execute_honeypot_action(bot, msg.server_id, msg.author.id, username, cfg.action, msg.channel_id, msg.id);
        });

        // --- EVENT: on_message_react (Honeypot detection) ---
        bot.on_message_react([this, &bot](const stoatpp::events::MessageReact& e) {
            if (!g_db) return;
            if (e.user_id == bot.current_user().id) return;

            // Resolve server ID
            std::string server_id = "";
            auto chan_opt = bot.get_channel(e.channel_id);
            if (chan_opt && chan_opt->server) {
                server_id = *chan_opt->server;
            } else {
                auto res = bot.rest().get_channel(e.channel_id);
                if (res.success() && res.body.contains("server") && res.body["server"].is_string()) {
                    server_id = res.body["server"].get<std::string>();
                }
            }
            if (server_id.empty()) return;

            auto cfg_opt = g_db->get_honeypot_config(server_id);
            if (!cfg_opt) return;
            const auto cfg = *cfg_opt;

            if (!cfg.enabled || !cfg.react_triggers || cfg.channel_id.empty()) return;

            // Only trigger if reaction is added in the configured honeypot channel
            if (e.channel_id != cfg.channel_id) return;

            // Get user info to check if bot or moderator
            auto usr_opt = bot.get_user(e.user_id);
            std::string username = usr_opt ? usr_opt->username : "user";
            bool is_bot = usr_opt ? usr_opt->bot : false;
            if (is_bot) return;

            // Skip bot owner, server owner, and trusted/moderators
            if (bot.is_owner(e.user_id)) return;
            if (bot.is_server_owner(server_id, e.user_id)) return;
            if (g_db->is_trusted(server_id, e.user_id, {})) return;

            // Trigger action!
            execute_honeypot_action(bot, server_id, e.user_id, username, cfg.action, e.channel_id, e.id);
        });
    }
};

} // namespace bronx::cogs
