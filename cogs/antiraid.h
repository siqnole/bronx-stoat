#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "logging.h"
#include "command_config.h"
#include <unordered_map>
#include <deque>
#include <mutex>
#include <chrono>
#include <thread>
#include <algorithm>
#include <sstream>

namespace bronx::cogs {

// ──────────────────────────────────────────────────────────────────────────────
//  Anti-Raid Module
//  Detects: rapid join floods, message spam, and default-profile accounts.
//  Owner/trusted users may configure thresholds and actions via .antiraid setup.
// ──────────────────────────────────────────────────────────────────────────────

class AntiRaidModule : public stoatpp::bot_module {
private:
    // Per-server join timestamp window (server_id -> recent join timestamps)
    std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> join_windows_;
    std::mutex join_mutex_;

    // Per-server per-user spam window (server_id:user_id -> recent message timestamps)
    std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> spam_windows_;
    std::mutex spam_mutex_;

    // ── Helper: normalise action string ──────────────────────────────────────
    static bool is_valid_single_action(const std::string& a) {
        return a == "ban" || a == "kick" || a == "timeout" || a == "warn" || a == "delete" || a == "none";
    }

    // Validate a comma-separated action list (e.g. "delete,warn" or "ban")
    static bool is_valid_action(const std::string& a) {
        if (a.empty() || a == "none") return true;
        std::istringstream ss(a);
        std::string token;
        while (std::getline(ss, token, ',')) {
            // trim whitespace
            token.erase(0, token.find_first_not_of(" \t"));
            token.erase(token.find_last_not_of(" \t") + 1);
            if (!is_valid_single_action(token)) return false;
        }
        return true;
    }

    // Normalise a comma-separated action list: trim spaces, lowercase, deduplicate
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

    // ── Helper: check if caller is allowed to manage antiraid ────────────────
    static bool can_manage_antiraid(stoatpp::cluster& bot,
                                    const std::string& server_id,
                                    const std::string& user_id,
                                    const stoatpp::models::Member& member) {
        // Bot owner always allowed
        if (bot.is_owner(user_id)) return true;
        // Server owner always allowed
        if (bot.is_server_owner(server_id, user_id)) return true;
        // Trusted users
        if (g_db && g_db->is_trusted(server_id, user_id, member.roles)) return true;
        // ManageServer permission
        auto server_opt = bot.get_server(server_id);
        if (server_opt) {
            if (bot.has_permission(*server_opt, member, stoatpp::permissions::ManageServer)) return true;
        }
        return false;
    }

    // ── Helper: perform an antiraid action on a user ─────────────────────────
    //  actions: comma-separated list, e.g. "delete,warn" | "ban" | "none"
    //  channel_id / msg_id: required only when "delete" is among the actions
    //  rule:   short description of what triggered this (for logs)
    void execute_antiraid_action(stoatpp::cluster& bot,
                                 const std::string& server_id,
                                 const std::string& user_id,
                                 const std::string& username,
                                 const std::string& actions,
                                 const std::string& rule,
                                 const std::string& details,
                                 const std::string& channel_id = "",
                                 const std::string& msg_id = "") {
        if (actions == "none" || actions.empty()) return;

        // Log the combined action string once
        auto do_log = [&bot, server_id, actions, rule, user_id, username, details]() {
            log_antiraid_action(bot, server_id, actions, rule, user_id, username, details);
        };

        // Split and execute each action
        std::istringstream ss(actions);
        std::string act;
        bool logged = false;
        while (std::getline(ss, act, ',')) {
            act.erase(0, act.find_first_not_of(" \t"));
            act.erase(act.find_last_not_of(" \t") + 1);
            if (act.empty() || act == "none") continue;

            if (act == "ban") {
                if (!logged) { logged = true;
                    bot.ban_user(server_id, user_id, "[antiraid] " + rule, [do_log](bool ok) {
                        if (ok) do_log();
                    });
                } else {
                    bot.ban_user(server_id, user_id, "[antiraid] " + rule, [](bool){});
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
                // Send a DM warning — open_dm is synchronous; run on a thread to avoid blocking
                std::thread([&bot, user_id, rule, details, do_log, &logged]() {
                    auto r = bot.rest().open_dm(user_id);
                    if (r.success() && r.body.contains("_id")) {
                        std::string dm_id = r.body["_id"].get<std::string>();
                        bot.send_message(dm_id, "⚠️ **Anti-Raid Warning**\nYou have been flagged by the automated anti-raid system.\n**Rule triggered:** " + rule + "\n**Details:** " + details);
                    }
                    if (!logged) { logged = true; do_log(); }
                }).detach();
            } else if (act == "delete") {
                // Delete the triggering message (only meaningful for spam; channel_id+msg_id must be provided)
                if (!channel_id.empty() && !msg_id.empty()) {
                    bot.delete_message(channel_id, msg_id, [do_log, &logged](bool ok) {
                        if (ok && !logged) { logged = true; do_log(); }
                    });
                }
            }
        }

        // Ensure we log at least once even if all async callbacks haven't fired
        // (e.g. for "delete" when the message was already gone)
        if (!logged) do_log();
    }

    // ── Helper: pretty-print config ──────────────────────────────────────────
    static std::string format_config(const Database::AntiRaidConfig& cfg) {
        std::string enabled_mark = (cfg.joins_action != "none" || cfg.spam_action != "none" || cfg.default_profile_action != "none")
                                   ? "✅ **Enabled**" : "⚠️ **Inactive** (all actions set to `none`)"; 
        return "🛡️ **Anti-Raid Configuration**\n" +
               enabled_mark + "\n\n" +
               "**Join Flood Detection**\n" +
               "• Threshold: `" + std::to_string(cfg.joins_threshold_count) + "` joins in `" + std::to_string(cfg.joins_threshold_seconds) + "s`\n" +
               "• Actions: `" + cfg.joins_action + "`\n\n" +
               "**Message Spam Detection**\n" +
               "• Threshold: `" + std::to_string(cfg.spam_threshold_count) + "` messages in `" + std::to_string(cfg.spam_threshold_seconds) + "s`\n" +
               "• Actions: `" + cfg.spam_action + "`\n\n" +
               "**Default Profile Detection**\n" +
               "• Actions on join (no avatar): `" + cfg.default_profile_action + "`\n\n" +
               "*Use `.antiraid setup` to reconfigure, or `.antiraid <join|spam|profile> <options>` for individual settings.*\n" +
               "*Actions can be combined with commas, e.g. `delete,warn` or `ban`.*";
    }

public:
    void register_handlers(stoatpp::cluster& bot) override {

        // ── COMMAND: .antiraid / .ar ─────────────────────────────────────────
        bot.register_command({
            "antiraid",
            {"ar"},
            "manage server anti-raid protection settings.",
            "",
            {"<setup|status|join|spam|profile|enable|disable>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling antiraid...\n";

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

                    if (!can_manage_antiraid(bot, msg.server_id, msg.author.id, member)) {
                        bot.send_message(msg.channel_id, "⚠️ Only the server owner, trusted users, or administrators may manage anti-raid settings.");
                        return;
                    }

                    // Load or create config
                    auto cfg_opt = g_db->get_antiraid_config(msg.server_id);
                    Database::AntiRaidConfig cfg;
                    if (cfg_opt) {
                        cfg = *cfg_opt;
                    } else {
                        cfg.server_id = msg.server_id;
                    }

                    // Dispatch subcommand
                    if (args.empty()) {
                        // Show status
                        bot.send_message(msg.channel_id, format_config(cfg));
                        return;
                    }

                    std::string sub = args[0];
                    std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

                    // ── STATUS ───────────────────────────────────────────────
                    if (sub == "status") {
                        bot.send_message(msg.channel_id, format_config(cfg));
                        return;
                    }

                    // ── SETUP WIZARD ─────────────────────────────────────────
                    if (sub == "setup") {
                        auto pfx = bot.config().command_prefix;
                        bot.send_message(msg.channel_id,
                            "🛡️ **Anti-Raid Setup Wizard**\n"
                            "Answer each prompt. Type `skip` to keep the current value, or `cancel` to abort.\n\n"
                            "**Step 1/4 — Join Flood: How many joins within a time window should trigger the action?**\n"
                            "Format: `<count> <seconds>` (e.g. `5 60`)\n"
                            "Current: `" + std::to_string(cfg.joins_threshold_count) + " " + std::to_string(cfg.joins_threshold_seconds) + "`");

                        bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(45),
                            [this, &bot, msg, cfg](std::optional<stoatpp::events::Message> rep1) mutable {
                                if (!rep1) { bot.send_message(msg.channel_id, "⏱️ Setup timed out."); return; }
                                std::string r1 = rep1->content;
                                std::transform(r1.begin(), r1.end(), r1.begin(), ::tolower);
                                if (r1 == "cancel") { bot.send_message(msg.channel_id, "❌ Setup cancelled."); return; }

                                if (r1 != "skip") {
                                    std::istringstream ss(r1);
                                    int c = 0, s = 0;
                                    if (!(ss >> c >> s) || c < 1 || s < 1) {
                                        bot.send_message(msg.channel_id, "⚠️ Invalid format. Expected `<count> <seconds>`. Setup cancelled.");
                                        return;
                                    }
                                    cfg.joins_threshold_count = c;
                                    cfg.joins_threshold_seconds = s;
                                }

                                bot.send_message(msg.channel_id,
                                    "**Step 2/4 — Join Flood Action: What should happen when join flooding is detected?**\n"
                                    "Options: `ban`, `kick`, `timeout`, `warn`, `none` (combine with commas, e.g. `kick,warn`)\n"
                                    "Current: `" + cfg.joins_action + "`");

                                bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30),
                                    [this, &bot, msg, cfg](std::optional<stoatpp::events::Message> rep2) mutable {
                                        if (!rep2) { bot.send_message(msg.channel_id, "⏱️ Setup timed out."); return; }
                                        std::string r2 = rep2->content;
                                        std::transform(r2.begin(), r2.end(), r2.begin(), ::tolower);
                                        if (r2 == "cancel") { bot.send_message(msg.channel_id, "❌ Setup cancelled."); return; }

                                        if (r2 != "skip") {
                                            if (!is_valid_action(r2)) {
                                                bot.send_message(msg.channel_id, "⚠️ Invalid action. Choose: `ban`, `kick`, `timeout`, `warn`, `none` (or combine with commas). Setup cancelled.");
                                                return;
                                            }
                                            cfg.joins_action = normalise_actions(r2);
                                        }

                                        bot.send_message(msg.channel_id,
                                            "**Step 3/4 — Spam Detection: How many messages in a time window should be considered spam?**\n"
                                            "Format: `<count> <seconds>` (e.g. `5 2`)\n"
                                            "Current: `" + std::to_string(cfg.spam_threshold_count) + " " + std::to_string(cfg.spam_threshold_seconds) + "`");

                                        bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(45),
                                            [this, &bot, msg, cfg](std::optional<stoatpp::events::Message> rep3) mutable {
                                                if (!rep3) { bot.send_message(msg.channel_id, "⏱️ Setup timed out."); return; }
                                                std::string r3 = rep3->content;
                                                std::transform(r3.begin(), r3.end(), r3.begin(), ::tolower);
                                                if (r3 == "cancel") { bot.send_message(msg.channel_id, "❌ Setup cancelled."); return; }

                                                if (r3 != "skip") {
                                                    std::istringstream ss(r3);
                                                    int c = 0, s = 0;
                                                    if (!(ss >> c >> s) || c < 1 || s < 1) {
                                                        bot.send_message(msg.channel_id, "⚠️ Invalid format. Expected `<count> <seconds>`. Setup cancelled.");
                                                        return;
                                                    }
                                                    cfg.spam_threshold_count = c;
                                                    cfg.spam_threshold_seconds = s;
                                                }

                                                bot.send_message(msg.channel_id,
                                                    "**Step 4/4 — Spam Action: What should happen when spam is detected?**\n"
                                                    "Options: `ban`, `kick`, `timeout`, `warn`, `delete`, `none` (combine with commas, e.g. `delete,warn`)\n"
                                                    "Current: `" + cfg.spam_action + "`");

                                                bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30),
                                                    [this, &bot, msg, cfg](std::optional<stoatpp::events::Message> rep4) mutable {
                                                        if (!rep4) { bot.send_message(msg.channel_id, "⏱️ Setup timed out."); return; }
                                                        std::string r4 = rep4->content;
                                                        std::transform(r4.begin(), r4.end(), r4.begin(), ::tolower);
                                                        if (r4 == "cancel") { bot.send_message(msg.channel_id, "❌ Setup cancelled."); return; }

                                                        if (r4 != "skip") {
                                                            if (!is_valid_action(r4)) {
                                                                bot.send_message(msg.channel_id, "⚠️ Invalid action. Choose: `ban`, `kick`, `timeout`, `warn`, `delete`, `none` (or combine with commas). Setup cancelled.");
                                                                return;
                                                            }
                                                            cfg.spam_action = normalise_actions(r4);
                                                        }

                                                        bot.send_message(msg.channel_id,
                                                            "**Step 5/5 — Default Profile Action: What should happen when a user joins with no avatar (default profile)?**\n"
                                                            "Options: `ban`, `kick`, `timeout`, `warn`, `none` (combine with commas, e.g. `kick,warn`)\n"
                                                            "Current: `" + cfg.default_profile_action + "`");

                                                        bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30),
                                                            [this, &bot, msg, cfg](std::optional<stoatpp::events::Message> rep5) mutable {
                                                                if (!rep5) { bot.send_message(msg.channel_id, "⏱️ Setup timed out."); return; }
                                                                std::string r5 = rep5->content;
                                                                std::transform(r5.begin(), r5.end(), r5.begin(), ::tolower);
                                                                if (r5 == "cancel") { bot.send_message(msg.channel_id, "❌ Setup cancelled."); return; }

                                                                if (r5 != "skip") {
                                                                    if (!is_valid_action(r5)) {
                                                                        bot.send_message(msg.channel_id, "⚠️ Invalid action. Choose: `ban`, `kick`, `timeout`, `warn`, `none` (or combine with commas). Setup cancelled.");
                                                                        return;
                                                                    }
                                                                    cfg.default_profile_action = normalise_actions(r5);
                                                                }

                                                                if (g_db->save_antiraid_config(cfg)) {
                                                                    bot.send_message(msg.channel_id, "✅ **Anti-Raid configuration saved!**\n\n" + format_config(cfg));
                                                                } else {
                                                                    bot.send_message(msg.channel_id, "❌ Failed to save configuration. Please try again.");
                                                                }
                                                            });
                                                    });
                                            });
                                    });
                            });
                        return;
                    }

                    // ── JOIN FLOOD ───────────────────────────────────────────
                    // .antiraid join <count> <seconds> [action[,action...]]
                    if (sub == "join" || sub == "joins") {
                        if (args.size() < 3) {
                            bot.send_message(msg.channel_id,
                                "Usage: `.antiraid join <count> <seconds> [action]`\n"
                                "Actions: `ban`, `kick`, `timeout`, `warn`, `none` (combine with commas)\n"
                                "Example: `.antiraid join 5 60 kick` or `.antiraid join 5 60 kick,warn`");
                            return;
                        }
                        try {
                            int count = std::stoi(args[1]);
                            int seconds = std::stoi(args[2]);
                            // Collect remaining args as the action (handles spaces between commas)
                            std::string action_raw;
                            if (args.size() >= 4) {
                                for (size_t i = 3; i < args.size(); ++i) {
                                    if (!action_raw.empty()) action_raw += ",";
                                    action_raw += args[i];
                                }
                            } else {
                                action_raw = cfg.joins_action;
                            }
                            std::transform(action_raw.begin(), action_raw.end(), action_raw.begin(), ::tolower);
                            std::string action = normalise_actions(action_raw);

                            if (count < 1 || seconds < 1) throw std::invalid_argument("out of range");
                            if (!is_valid_action(action)) {
                                bot.send_message(msg.channel_id, "⚠️ Invalid action. Choose: `ban`, `kick`, `timeout`, `warn`, `none` (or combine with commas).");
                                return;
                            }

                            cfg.joins_threshold_count = count;
                            cfg.joins_threshold_seconds = seconds;
                            cfg.joins_action = action;

                            if (g_db->save_antiraid_config(cfg)) {
                                bot.send_message(msg.channel_id,
                                    "✅ Join flood detection updated: `" + std::to_string(count) + "` joins in `" + std::to_string(seconds) + "s` → `" + action + "`.");
                            } else {
                                bot.send_message(msg.channel_id, "❌ Failed to save configuration.");
                            }
                        } catch (...) {
                            bot.send_message(msg.channel_id, "⚠️ Invalid numbers. Usage: `.antiraid join <count> <seconds> [action]`");
                        }
                        return;
                    }

                    // ── SPAM ─────────────────────────────────────────────────
                    // .antiraid spam <count> <seconds> [action[,action...]]
                    if (sub == "spam") {
                        if (args.size() < 3) {
                            bot.send_message(msg.channel_id,
                                "Usage: `.antiraid spam <count> <seconds> [action]`\n"
                                "Actions: `ban`, `kick`, `timeout`, `warn`, `delete`, `none` (combine with commas)\n"
                                "Example: `.antiraid spam 5 2 delete,warn` or `.antiraid spam 5 2 timeout`");
                            return;
                        }
                        try {
                            int count = std::stoi(args[1]);
                            int seconds = std::stoi(args[2]);
                            // Collect remaining args as the action list
                            std::string action_raw;
                            if (args.size() >= 4) {
                                for (size_t i = 3; i < args.size(); ++i) {
                                    if (!action_raw.empty()) action_raw += ",";
                                    action_raw += args[i];
                                }
                            } else {
                                action_raw = cfg.spam_action;
                            }
                            std::transform(action_raw.begin(), action_raw.end(), action_raw.begin(), ::tolower);
                            std::string action = normalise_actions(action_raw);

                            if (count < 1 || seconds < 1) throw std::invalid_argument("out of range");
                            if (!is_valid_action(action)) {
                                bot.send_message(msg.channel_id, "⚠️ Invalid action. Choose: `ban`, `kick`, `timeout`, `warn`, `delete`, `none` (or combine with commas).");
                                return;
                            }

                            cfg.spam_threshold_count = count;
                            cfg.spam_threshold_seconds = seconds;
                            cfg.spam_action = action;

                            if (g_db->save_antiraid_config(cfg)) {
                                bot.send_message(msg.channel_id,
                                    "✅ Spam detection updated: `" + std::to_string(count) + "` messages in `" + std::to_string(seconds) + "s` → `" + action + "`.");
                            } else {
                                bot.send_message(msg.channel_id, "❌ Failed to save configuration.");
                            }
                        } catch (...) {
                            bot.send_message(msg.channel_id, "⚠️ Invalid numbers. Usage: `.antiraid spam <count> <seconds> [action]`");
                        }
                        return;
                    }

                    // ── DEFAULT PROFILE ──────────────────────────────────────
                    // .antiraid profile <action[,action...]>
                    if (sub == "profile") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id,
                                "Usage: `.antiraid profile <action>`\n"
                                "Actions: `ban`, `kick`, `timeout`, `warn`, `none` (combine with commas)\n"
                                "Example: `.antiraid profile kick` or `.antiraid profile kick,warn`");
                            return;
                        }
                        // Collect all remaining args as the action list
                        std::string action_raw;
                        for (size_t i = 1; i < args.size(); ++i) {
                            if (!action_raw.empty()) action_raw += ",";
                            action_raw += args[i];
                        }
                        std::transform(action_raw.begin(), action_raw.end(), action_raw.begin(), ::tolower);
                        std::string action = normalise_actions(action_raw);
                        if (!is_valid_action(action)) {
                            bot.send_message(msg.channel_id, "⚠️ Invalid action. Choose: `ban`, `kick`, `timeout`, `warn`, `none` (or combine with commas).");
                            return;
                        }
                        cfg.default_profile_action = action;
                        if (g_db->save_antiraid_config(cfg)) {
                            bot.send_message(msg.channel_id, "✅ Default profile actions set to `" + action + "`.");
                        } else {
                            bot.send_message(msg.channel_id, "❌ Failed to save configuration.");
                        }
                        return;
                    }

                    // ── RESET ────────────────────────────────────────────────
                    if (sub == "reset") {
                        cfg.joins_threshold_count = 5;
                        cfg.joins_threshold_seconds = 60;
                        cfg.joins_action = "none";
                        cfg.spam_threshold_count = 5;
                        cfg.spam_threshold_seconds = 2;
                        cfg.spam_action = "none";
                        cfg.default_profile_action = "none";

                        if (g_db->save_antiraid_config(cfg)) {
                            bot.send_message(msg.channel_id, "✅ Anti-Raid configuration reset to defaults (all actions: `none`).");
                        } else {
                            bot.send_message(msg.channel_id, "❌ Failed to reset configuration.");
                        }
                        return;
                    }

                    // Fallback: show help
                    bot.send_message(msg.channel_id,
                        "🛡️ **Anti-Raid Commands**\n"
                        "`.antiraid setup` — interactive wizard\n"
                        "`.antiraid status` — view current config\n"
                        "`.antiraid join <count> <seconds> [action]` — configure join flood detection\n"
                        "`.antiraid spam <count> <seconds> [action]` — configure spam detection\n"
                        "`.antiraid profile <action>` — action on default-profile accounts\n"
                        "`.antiraid reset` — reset all settings to defaults\n\n"
                        "**Actions:** `ban`, `kick`, `timeout`, `warn`, `delete` (spam only), `none`\n"
                        "**Multiple actions:** separate with commas, e.g. `delete,warn` or `kick,warn`\n"
                        "**Alias:** `.ar`");
                });
            },
            "AntiRaid",
            stoatpp::permissions::ManageServer
        });

        // ── EVENT: on_server_member_join ─────────────────────────────────────
        //  Check 1: default profile (no avatar)
        //  Check 2: join flood threshold
        bot.on_server_member_join([this, &bot](const stoatpp::events::ServerMemberJoin& e) {
            if (!g_db) return;

            auto cfg_opt = g_db->get_antiraid_config(e.server_id);
            if (!cfg_opt) return;
            const auto cfg = *cfg_opt;

            // Skip if all join-related actions are none
            bool join_active = (cfg.joins_action != "none" && cfg.joins_action != "");
            bool profile_active = (cfg.default_profile_action != "none" && cfg.default_profile_action != "");
            if (!join_active && !profile_active) return;

            std::thread([this, &bot, e, cfg, join_active, profile_active]() {
                auto usr_opt = bot.get_user(e.user_id);
                std::string username = usr_opt ? usr_opt->username : e.user_id;

                // ── Check 1: Default profile (no avatar) ──────────────────
                if (profile_active && usr_opt) {
                    bool has_avatar = usr_opt->avatar.has_value() && !usr_opt->avatar->empty();
                    if (!has_avatar) {
                        execute_antiraid_action(bot, e.server_id, e.user_id, username,
                                                cfg.default_profile_action,
                                                "Default Profile",
                                                "User joined with no avatar set.");
                    }
                }

                // ── Check 2: Join flood ───────────────────────────────────
                if (join_active) {
                    auto now = std::chrono::steady_clock::now();
                    auto window = std::chrono::seconds(cfg.joins_threshold_seconds);

                    std::lock_guard<std::mutex> lock(join_mutex_);
                    auto& dq = join_windows_[e.server_id];
                    dq.push_back(now);

                    // Evict entries outside window
                    while (!dq.empty() && (now - dq.front()) > window) {
                        dq.pop_front();
                    }

                    int count = static_cast<int>(dq.size());
                    if (count >= cfg.joins_threshold_count) {
                        // Clear window to prevent multi-fire per burst
                        dq.clear();

                        // Execute action on the joining user that triggered the threshold
                        execute_antiraid_action(bot, e.server_id, e.user_id, username,
                                                cfg.joins_action,
                                                "Join Flood",
                                                std::to_string(count) + " joins detected in " +
                                                std::to_string(cfg.joins_threshold_seconds) + "s window.");
                    }
                }
            }).detach();
        });

        // ── EVENT: on_message (spam detection) ───────────────────────────────
        bot.on_message([this, &bot](const stoatpp::events::Message& msg) {
            // Must be in a server
            if (msg.server_id.empty() || !g_db) return;

            auto cfg_opt = g_db->get_antiraid_config(msg.server_id);
            if (!cfg_opt) return;
            const auto cfg = *cfg_opt;

            if (cfg.spam_action == "none" || cfg.spam_action.empty()) return;

            // Skip bots
            bool is_bot = msg.author.bot;
            if (!is_bot) {
                auto cached = bot.get_user(msg.author.id);
                if (cached && cached->bot) is_bot = true;
            }
            if (is_bot) return;

            // Skip bot owner
            if (bot.is_owner(msg.author.id)) return;

            // Skip server owner
            if (bot.is_server_owner(msg.server_id, msg.author.id)) return;

            // Skip trusted users (fetched from cache — no blocking call here)
            if (g_db->is_trusted(msg.server_id, msg.author.id, {})) return;

            auto now = std::chrono::steady_clock::now();
            auto window = std::chrono::seconds(cfg.spam_threshold_seconds);
            std::string key = msg.server_id + ":" + msg.author.id;

            bool triggered = false;
            int hit_count = 0;
            {
                std::lock_guard<std::mutex> lock(spam_mutex_);
                auto& dq = spam_windows_[key];
                dq.push_back(now);

                while (!dq.empty() && (now - dq.front()) > window) {
                    dq.pop_front();
                }

                hit_count = static_cast<int>(dq.size());
                if (hit_count >= cfg.spam_threshold_count) {
                    dq.clear();
                    triggered = true;
                }
            }

            if (triggered) {
                std::string username = msg.author.username;
                std::string details = std::to_string(hit_count) + " messages in " +
                                      std::to_string(cfg.spam_threshold_seconds) + "s window.";
                // Pass channel_id and msg_id so the "delete" action can remove the triggering message
                execute_antiraid_action(bot, msg.server_id, msg.author.id, username,
                                        cfg.spam_action,
                                        "Message Spam",
                                        details,
                                        msg.channel_id,
                                        msg.id);
            }
        });
    }
};

} // namespace bronx::cogs
