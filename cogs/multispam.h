#pragma once
#include "../database.h"
#include "helpers.h"
#include "logging.h"
#include "stoatpp/bot_module.h"
#include <algorithm>
#include <chrono>
#include <deque>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace bronx::cogs {

class MultiSpamModule : public stoatpp::bot_module {
private:
    struct UserSpamHistory {
        std::string last_content;
        std::chrono::steady_clock::time_point last_timestamp;
        int count = 0;
        std::vector<std::pair<std::string, std::string>> message_refs; // (channel_id, message_id)
    };

    // server_id:user_id -> UserSpamHistory
    std::unordered_map<std::string, UserSpamHistory> user_history_;
    std::mutex history_mutex_;

    static bool is_valid_single_action(const std::string& a) {
        return a == "ban" || a == "kick" || a == "timeout" || a == "warn" || a == "delete" || a == "jail" || a == "log" || a == "none";
    }

    static bool is_valid_action(const std::string& a) {
        if (a.empty() || a == "none") return true;
        std::istringstream ss(a);
        std::string token;
        while (std::getline(ss, token, ',')) {
            token.erase(0, token.find_first_not_of(" \t"));
            token.erase(token.find_last_not_of(" \t") + 1);
            if (!is_valid_single_action(token)) return false;
        }
        return true;
    }

    static std::string normalise_actions(const std::string& raw) {
        if (raw.empty() || raw == "none") return "none";
        std::istringstream ss(raw);
        std::string token, result;
        bool first = true;
        while (std::getline(ss, token, ',')) {
            token.erase(0, token.find_first_not_of(" \t"));
            token.erase(token.find_last_not_of(" \t") + 1);
            std::transform(token.begin(), token.end(), token.begin(), ::tolower);
            if (token.empty()) continue;
            if (!first) result += ",";
            result += token;
            first = false;
        }
        return result.empty() ? "none" : result;
    }

    static bool can_manage_multispam(stoatpp::cluster& bot,
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

    void execute_multispam_action(stoatpp::cluster& bot,
                                  const std::string& server_id,
                                  const std::string& user_id,
                                  const std::string& username,
                                  const std::string& actions,
                                  const std::vector<std::pair<std::string, std::string>>& message_refs,
                                  bool auto_delete_flag) {
        if (actions == "none" && !auto_delete_flag) return;

        auto do_log = [&bot, server_id, actions, user_id, username]() {
            log_moderation_action(bot, server_id, "Multi-Channel Spam Triggered", bot.current_user().id, user_id, "Sent repeated message spam across channels", "Action: " + actions);
        };

        std::istringstream ss(actions);
        std::string act;
        bool logged = false;
        bool has_delete_in_actions = false;

        while (std::getline(ss, act, ',')) {
            act.erase(0, act.find_first_not_of(" \t"));
            act.erase(act.find_last_not_of(" \t") + 1);
            if (act.empty() || act == "none") continue;

            if (act == "delete") {
                has_delete_in_actions = true;
            } else if (act == "ban") {
                if (!logged) { logged = true;
                    helpers::ban_user_with_links(bot, server_id, user_id, "[multispam] Multi-channel message spam", bot.current_user().id, [do_log](bool ok) {
                        if (ok) do_log();
                    });
                } else {
                    helpers::ban_user_with_links(bot, server_id, user_id, "[multispam] Multi-channel message spam", bot.current_user().id, [](bool){});
                }
            } else if (act == "kick") {
                bot.kick_member(server_id, user_id, [do_log, &logged](bool ok) {
                    if (ok && !logged) { logged = true; do_log(); }
                });
            } else if (act == "timeout") {
                bot.timeout_member(server_id, user_id, "PT10M", [do_log, &logged](bool ok) {
                    if (ok && !logged) { logged = true; do_log(); }
                });
            } else if (act == "warn") {
                std::thread([&bot, user_id, do_log, &logged]() {
                    auto r = bot.rest().open_dm(user_id);
                    if (r.success() && r.body.contains("_id")) {
                        std::string dm_id = r.body["_id"].get<std::string>();
                        bot.send_message(dm_id, "" + bronx::emj_warning + " **Multi-Channel Spam Warning**\nYou have been flagged by the automated anti-spam system for sending repeated messages across channels.");
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

                                        std::string default_jail_msg = "<@" + user_id + "> you have been jailed for multi-channel message spam. Staff will review shortly.";
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

        if (auto_delete_flag || has_delete_in_actions) {
            for (const auto& ref : message_refs) {
                bot.delete_message(ref.first, ref.second, [](bool){});
            }
        }

        if (!logged) do_log();
    }

    static std::string format_config(const Database::MultiSpamConfig& cfg) {
        std::string enabled_mark = cfg.enabled ? "" + bronx::emj_yes + " **Enabled**" : "" + bronx::emj_warning + " **Disabled**";
        std::string window_str = (cfg.time_window <= 0) ? "`unlimited`" : "`" + std::to_string(cfg.time_window) + "s`";
        std::string del_str = cfg.delete_messages ? "`yes`" : "`no`";

        std::string wl_str = "*None*";
        if (!cfg.whitelist.empty()) {
            wl_str = "";
            for (size_t i = 0; i < cfg.whitelist.size(); ++i) {
                wl_str += "`" + cfg.whitelist[i] + "`" + (i + 1 < cfg.whitelist.size() ? ", " : "");
            }
        }

        return "📢 **Multi-Channel Spam Protection Configuration**\n" +
               enabled_mark + "\n\n" +
               "• **Automated Action:** `" + cfg.action + "`\n" +
               "• **Repeat Threshold:** `" + std::to_string(cfg.threshold_count) + "` identical messages\n" +
               "• **Time Window:** " + window_str + "\n" +
               "• **Auto-Delete Messages:** " + del_str + "\n" +
               "• **Whitelisted Words/Phrases:** " + wl_str + "\n\n" +
               "*Commands:* `.multispam <enable|disable|action|threshold|window|delete|whitelist|setup|reset>`";
    }

    static std::string trim_and_lower(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        std::string trimmed = str.substr(first, (last - first + 1));
        std::transform(trimmed.begin(), trimmed.end(), trimmed.begin(), ::tolower);
        return trimmed;
    }

public:
    void register_handlers(stoatpp::cluster& bot) override {

        // ── COMMAND: .multispam / .mcspam / .multichannelspam / .antispam ─────
        bot.register_command({
            "multispam",
            {"mcspam", "multichannelspam", "antispam"},
            "manage multi-channel repeated message spam detection.",
            "",
            {"<status|setup|enable|disable|action|threshold|window|delete|whitelist|reset>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " This command can only be used in a server.");
                    return;
                }

                if (!g_db) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Database unavailable.");
                    return;
                }

                bot.fetch_member(msg.server_id, msg.author.id, [this, &bot, msg, args](stoatpp::models::Member member, bool ok) {
                    if (!ok) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to verify member details.");
                        return;
                    }

                    if (!can_manage_multispam(bot, msg.server_id, msg.author.id, member)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Only server owners, trusted users, or administrators may manage multi-channel spam settings.");
                        return;
                    }

                    auto cfg_opt = g_db->get_multispam_config(msg.server_id);
                    Database::MultiSpamConfig cfg;
                    if (cfg_opt) {
                        cfg = *cfg_opt;
                    } else {
                        cfg.server_id = msg.server_id;
                    }

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

                    if (sub == "enable" || sub == "disable") {
                        cfg.enabled = (sub == "enable");
                        if (g_db->save_multispam_config(cfg)) {
                            bot.send_message(msg.channel_id, cfg.enabled ? "" + bronx::emj_yes + " Multi-channel spam protection enabled." : "" + bronx::emj_yes + " Multi-channel spam protection disabled.");
                        } else {
                            bot.send_message(msg.channel_id, "" + bronx::emj_no + " Failed to save settings.");
                        }
                        return;
                    }

                    if (sub == "action") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "Usage: `.multispam action <timeout|ban|kick|warn|delete|jail|log|none>` (combine with commas)");
                            return;
                        }
                        std::string action_raw;
                        for (size_t i = 1; i < args.size(); ++i) {
                            if (!action_raw.empty()) action_raw += ",";
                            action_raw += args[i];
                        }
                        std::transform(action_raw.begin(), action_raw.end(), action_raw.begin(), ::tolower);
                        std::string action = normalise_actions(action_raw);
                        if (!is_valid_action(action)) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid action. Choose from: `timeout`, `ban`, `kick`, `warn`, `delete`, `jail`, `log`, `none`.");
                            return;
                        }
                        cfg.action = action;
                        if (g_db->save_multispam_config(cfg)) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Automated action set to `" + action + "`.");
                        } else {
                            bot.send_message(msg.channel_id, "" + bronx::emj_no + " Failed to save action.");
                        }
                        return;
                    }

                    if (sub == "threshold") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "Usage: `.multispam threshold <count>` (minimum 2)");
                            return;
                        }
                        try {
                            int count = std::stoi(args[1]);
                            if (count < 2) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Threshold must be at least 2 consecutive identical messages.");
                                return;
                            }
                            cfg.threshold_count = count;
                            if (g_db->save_multispam_config(cfg)) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Repeat threshold set to `" + std::to_string(count) + "` messages.");
                            } else {
                                bot.send_message(msg.channel_id, "" + bronx::emj_no + " Failed to save threshold.");
                            }
                        } catch (...) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid number.");
                        }
                        return;
                    }

                    if (sub == "window") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "Usage: `.multispam window <seconds>` (0 = unlimited)");
                            return;
                        }
                        try {
                            int sec = std::stoi(args[1]);
                            if (sec < 0) sec = 0;
                            cfg.time_window = sec;
                            if (g_db->save_multispam_config(cfg)) {
                                std::string win_label = (sec == 0) ? "unlimited" : std::to_string(sec) + " seconds";
                                bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Time window set to `" + win_label + "`.");
                            } else {
                                bot.send_message(msg.channel_id, "" + bronx::emj_no + " Failed to save window.");
                            }
                        } catch (...) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid number.");
                        }
                        return;
                    }

                    if (sub == "delete") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "Usage: `.multispam delete <true|false|enable|disable>`");
                            return;
                        }
                        std::string val = args[1];
                        std::transform(val.begin(), val.end(), val.begin(), ::tolower);
                        bool flag = (val == "true" || val == "yes" || val == "enable" || val == "1");
                        cfg.delete_messages = flag;
                        if (g_db->save_multispam_config(cfg)) {
                            bot.send_message(msg.channel_id, flag ? "" + bronx::emj_yes + " Auto-deletion of spam messages enabled." : "" + bronx::emj_yes + " Auto-deletion disabled.");
                        } else {
                            bot.send_message(msg.channel_id, "" + bronx::emj_no + " Failed to save setting.");
                        }
                        return;
                    }

                    if (sub == "whitelist") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "Usage: `.multispam whitelist <add|remove|list|clear> [words...]`");
                            return;
                        }
                        std::string action = args[1];
                        std::transform(action.begin(), action.end(), action.begin(), ::tolower);

                        if (action == "list") {
                            std::string wl_str = "*None*";
                            if (!cfg.whitelist.empty()) {
                                wl_str = "";
                                for (size_t i = 0; i < cfg.whitelist.size(); ++i) {
                                    wl_str += "`" + cfg.whitelist[i] + "`" + (i + 1 < cfg.whitelist.size() ? ", " : "");
                                }
                            }
                            bot.send_message(msg.channel_id, "⚪ **Whitelisted Words/Phrases:**\n" + wl_str);
                            return;
                        }

                        if (action == "clear") {
                            cfg.whitelist.clear();
                            if (g_db->save_multispam_config(cfg)) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Whitelist cleared.");
                            } else {
                                bot.send_message(msg.channel_id, "" + bronx::emj_no + " Failed to clear whitelist.");
                            }
                            return;
                        }

                        if (action == "add") {
                            if (args.size() < 3) {
                                bot.send_message(msg.channel_id, "Usage: `.multispam whitelist add <word...>`");
                                return;
                            }
                            std::string word_raw = "";
                            for (size_t i = 2; i < args.size(); ++i) {
                                word_raw += args[i] + (i + 1 < args.size() ? " " : "");
                            }
                            std::string word_norm = trim_and_lower(word_raw);
                            if (word_norm.empty()) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid word.");
                                return;
                            }
                            if (std::find(cfg.whitelist.begin(), cfg.whitelist.end(), word_norm) == cfg.whitelist.end()) {
                                cfg.whitelist.push_back(word_norm);
                                if (g_db->save_multispam_config(cfg)) {
                                    bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Added `" + word_norm + "` to the whitelist.");
                                } else {
                                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Failed to update whitelist.");
                                }
                            } else {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " `" + word_norm + "` is already whitelisted.");
                            }
                            return;
                        }

                        if (action == "remove") {
                            if (args.size() < 3) {
                                bot.send_message(msg.channel_id, "Usage: `.multispam whitelist remove <word...>`");
                                return;
                            }
                            std::string word_raw = "";
                            for (size_t i = 2; i < args.size(); ++i) {
                                word_raw += args[i] + (i + 1 < args.size() ? " " : "");
                            }
                            std::string word_norm = trim_and_lower(word_raw);
                            auto it = std::find(cfg.whitelist.begin(), cfg.whitelist.end(), word_norm);
                            if (it != cfg.whitelist.end()) {
                                cfg.whitelist.erase(it);
                                if (g_db->save_multispam_config(cfg)) {
                                    bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Removed `" + word_norm + "` from the whitelist.");
                                } else {
                                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Failed to update whitelist.");
                                }
                            } else {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " `" + word_norm + "` was not found in the whitelist.");
                            }
                            return;
                        }

                        bot.send_message(msg.channel_id, "Usage: `.multispam whitelist <add|remove|list|clear> [words...]`");
                        return;
                    }

                    if (sub == "setup") {
                        cfg.enabled = true;
                        if (cfg.whitelist.empty()) {
                            cfg.whitelist = {"hi", "hello", "hey", "lol", "gg"};
                        }
                        if (g_db->save_multispam_config(cfg)) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " **Multi-Channel Spam Protection setup complete & enabled!**\n\n" + format_config(cfg));
                        } else {
                            bot.send_message(msg.channel_id, "" + bronx::emj_no + " Failed to save configuration.");
                        }
                        return;
                    }

                    if (sub == "reset") {
                        cfg.enabled = false;
                        cfg.action = "timeout";
                        cfg.threshold_count = 2;
                        cfg.time_window = 60;
                        cfg.delete_messages = true;
                        cfg.whitelist.clear();
                        if (g_db->save_multispam_config(cfg)) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Multi-channel spam protection settings reset to defaults.");
                        } else {
                            bot.send_message(msg.channel_id, "" + bronx::emj_no + " Failed to reset settings.");
                        }
                        return;
                    }

                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Unknown subcommand. Use `.multispam` to view settings.");
                });
            },
            "Moderation",
            stoatpp::permissions::ManageServer
        });

        // ── EVENT: on_message (Multi-channel spam detection) ─────────────────
        bot.on_message([this, &bot](const stoatpp::events::Message& msg) {
            if (msg.server_id.empty() || !g_db) return;

            auto cfg_opt = g_db->get_multispam_config(msg.server_id);
            if (!cfg_opt || !cfg_opt->enabled) return;
            const auto cfg = *cfg_opt;

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

            // Normalize content (trimmed, lowercase)
            std::string norm_content = trim_and_lower(msg.content);
            if (norm_content.empty()) return;

            // Check whitelist — if message content is in the whitelist, skip completely!
            for (const auto& w : cfg.whitelist) {
                if (norm_content == trim_and_lower(w)) {
                    return; // Ignored for spam detection, does not reset or update history
                }
            }

            auto now = std::chrono::steady_clock::now();
            std::string user_key = msg.server_id + ":" + msg.author.id;
            bool trigger_spam = false;
            std::vector<std::pair<std::string, std::string>> refs_to_delete;
            std::string username = msg.author.username;

            {
                std::lock_guard<std::mutex> lock(history_mutex_);
                auto& hist = user_history_[user_key];

                // Check time window expiration
                if (cfg.time_window > 0 && hist.count > 0) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - hist.last_timestamp).count();
                    if (elapsed > cfg.time_window) {
                        hist.count = 0;
                        hist.last_content.clear();
                        hist.message_refs.clear();
                    }
                }

                if (hist.last_content == norm_content) {
                    hist.count++;
                    hist.message_refs.push_back({msg.channel_id, msg.id});
                    hist.last_timestamp = now;

                    if (hist.count >= cfg.threshold_count) {
                        trigger_spam = true;
                        refs_to_delete = hist.message_refs;

                        // Clear history after trigger to avoid immediate re-firing
                        hist.count = 0;
                        hist.last_content.clear();
                        hist.message_refs.clear();
                    }
                } else {
                    hist.last_content = norm_content;
                    hist.last_timestamp = now;
                    hist.count = 1;
                    hist.message_refs = {{msg.channel_id, msg.id}};
                }
            }

            if (trigger_spam) {
                execute_multispam_action(bot, msg.server_id, msg.author.id, username, cfg.action, refs_to_delete, cfg.delete_messages);
            }
        });
    }
};

} // namespace bronx::cogs
