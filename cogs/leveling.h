#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "../database.h"
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <cmath>

namespace bronx::cogs {

class LevelingModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {

        // Command: .rank [@user]
        bot.register_command({
            "rank",
            {"xp", "levelinfo"},
            "view leveling progress and rank card.",
            "[user]",
            {"[user]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                show_rank_card(bot, msg, args);
            },
            "Leveling"
        });

        // Command: .leaderboard
        bot.register_command({
            "leaderboard",
            {"lb", "levels"},
            "view the server XP leaderboard.",
            "",
            {},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id, "this command can only be used inside a server.");
                    return;
                }
                if (!g_db) {
                    bot.send_message(msg.channel_id, "database is currently unavailable.");
                    return;
                }

                auto cfg_opt = g_db->get_guild_leveling_config(msg.server_id);
                if (!cfg_opt || !cfg_opt->enabled) {
                    bot.send_message(msg.channel_id, "⚠️ Leveling is currently disabled in this server.");
                    return;
                }

                auto entries = g_db->get_server_xp_leaderboard(msg.server_id, 10);
                if (entries.empty()) {
                    bot.send_message(msg.channel_id, "ℹ️ No XP leaderboard records yet.");
                    return;
                }

                std::string out = "🏆 **XP Leaderboard** 🏆\n\n";
                int rank = 1;
                for (const auto& e : entries) {
                    std::string rank_prefix = "";
                    if (rank == 1) rank_prefix = "🥇 ";
                    else if (rank == 2) rank_prefix = "🥈 ";
                    else if (rank == 3) rank_prefix = "🥉 ";
                    else rank_prefix = std::to_string(rank) + ". ";

                    out += rank_prefix + "<@" + e.user_id + "> — **Level " + std::to_string(e.level) + "** (" + std::to_string(e.total_xp) + " XP)\n";
                    rank++;
                }
                bot.send_message(msg.channel_id, out);
            },
            "Leveling"
        });

        // Command: .level [subcommand] [args...]
        bot.register_command({
            "level",
            {"lvl"},
            "configure the server leveling settings.",
            "[subcommand] [args...]",
            {"[subcommand]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id, "this command can only be used inside a server.");
                    return;
                }
                if (!g_db) {
                    bot.send_message(msg.channel_id, "database is currently unavailable.");
                    return;
                }

                bool is_admin_cmd = false;
                if (!args.empty()) {
                    std::string sub = args[0];
                    std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
                    if (sub == "enable" || sub == "disable" || sub == "xprange" || sub == "role" || sub == "config") {
                        is_admin_cmd = true;
                    }
                }

                if (!is_admin_cmd) {
                    show_rank_card(bot, msg, args);
                    return;
                }

                // Check permissions (ManageServer or bot owner)
                auto server_opt = bot.get_server(msg.server_id);
                if (!server_opt) {
                    bot.send_message(msg.channel_id, "failed to fetch server info.");
                    return;
                }

                bool is_authorized = bot.is_owner(msg.author.id);
                if (!is_authorized) {
                    bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, server_opt, args](stoatpp::models::Member member, bool ok) {
                        if (!ok || !bot.has_permission(*server_opt, member, stoatpp::permissions::ManageServer)) {
                            bot.send_message(msg.channel_id, "❌ You do not have permissions to manage leveling settings (Manage Server required).");
                            return;
                        }
                        // Proceed to subcommand handler
                        handle_level_cmd(bot, msg, args);
                    });
                    return;
                }

                handle_level_cmd(bot, msg, args);
            },
            "Leveling"
        });

    }

private:
    static void show_rank_card(stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
        if (msg.server_id.empty()) {
            bot.send_message(msg.channel_id, "this command can only be used inside a server.");
            return;
        }
        if (!g_db) {
            bot.send_message(msg.channel_id, "database is currently unavailable.");
            return;
        }

        auto cfg_opt = g_db->get_guild_leveling_config(msg.server_id);
        if (!cfg_opt || !cfg_opt->enabled) {
            bot.send_message(msg.channel_id, "⚠️ Leveling is currently disabled in this server.");
            return;
        }

        std::string target_query = args.empty() ? msg.author.id : args[0];

        helpers::resolve_user(bot, msg.server_id, target_query, [msg, &bot](stoatpp::models::User target_user, stoatpp::models::Member, bool success) {
            if (!success) {
                bot.send_message(msg.channel_id, "could not resolve target user.");
                return;
            }

            auto xp_opt = g_db->get_user_xp(target_user.id, msg.server_id);
            if (!xp_opt) {
                bot.send_message(msg.channel_id, "ℹ️ " + target_user.username + " has no XP progress yet (needs to send messages first).");
                return;
            }

            uint32_t level = xp_opt->level;
            uint64_t current_xp = xp_opt->total_xp;
            uint64_t base_xp = Database::calculate_xp_for_level(level);
            uint64_t next_xp = Database::calculate_xp_for_level(level + 1);

            uint64_t xp_in_level = current_xp - base_xp;
            uint64_t xp_required_for_level = next_xp - base_xp;

            int rank = g_db->get_user_server_xp_rank(target_user.id, msg.server_id);
            auto leaderboard = g_db->get_server_xp_leaderboard(msg.server_id, 9999);
            int total_users = leaderboard.size();

            // Generate a nice ASCII progress bar
            int filled = 0;
            if (xp_required_for_level > 0) {
                filled = static_cast<int>((xp_in_level * 10) / xp_required_for_level);
            }
            if (filled > 10) filled = 10;
            if (filled < 0) filled = 0;

            std::string bar = "[";
            for (int i = 0; i < 10; ++i) {
                if (i < filled) bar += "█";
                else bar += "░";
            }
            bar += "]";

            int percentage = 0;
            if (xp_required_for_level > 0) {
                percentage = static_cast<int>((xp_in_level * 100) / xp_required_for_level);
            }

            std::string out = "📊 **Rank progress for " + target_user.username + "**\n"
                              "✨ **Level**: " + std::to_string(level) + "\n"
                              "📈 **XP**: " + std::to_string(current_xp) + " / " + std::to_string(next_xp) + " (Level progress: " + std::to_string(percentage) + "%)\n" +
                              bar + "\n"
                              "🏆 **Rank**: #" + std::to_string(rank) + " of " + std::to_string(total_users);

            bot.send_message(msg.channel_id, out);
        });
    }

    static void handle_level_cmd(stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
        auto to_lower = [](std::string s) {
            for (auto& c : s) c = std::tolower(c);
            return s;
        };

        auto server_opt = bot.get_server(msg.server_id);
        if (!server_opt) {
            bot.send_message(msg.channel_id, "failed to fetch server info.");
            return;
        }

        std::vector<std::string> prefixes = { bot.config().command_prefix };
        if (bot.config().prefix_resolver) {
            prefixes = bot.config().prefix_resolver(bot, msg);
        }
        std::string pfx = bot.config().command_prefix;
        for (const auto& p : prefixes) {
            if (msg.content.rfind(p, 0) == 0) { pfx = p; break; }
        }

        auto send_help = [&bot, msg, pfx]() {
            bot.send_message(msg.channel_id,
                "**⚙️ Server Leveling Settings Guide**\n\n"
                "• `" + pfx + "level enable` — Enable the leveling system.\n"
                "• `" + pfx + "level disable` — Disable the leveling system.\n"
                "• `" + pfx + "level xprange <min-max | single_value | -1 | 0>` — Set random XP gain range per message (default: 15-25).\n"
                "• `" + pfx + "level role add <level> <@role>` — Configure a role reward for a level.\n"
                "• `" + pfx + "level role remove <level>` — Delete a level role reward.\n"
                "• `" + pfx + "level role sticky <on|off>` — Keep or remove previous level reward roles (default: on).\n"
                "• `" + pfx + "level config preview` — Show current server config.\n"
                "• `" + pfx + "level config cooldown <seconds>` — Message cooldown for XP (default: 60s).\n"
                "• `" + pfx + "level config minchars <number>` — Min message characters to earn XP (default: 5).\n"
                "• `" + pfx + "level config announce <on|off>` — Toggle level-up announcements.\n"
                "• `" + pfx + "level config channel <#channel | here | none>` — Set level-up announcement channel.\n"
                "• `" + pfx + "level config message <message...>` — Custom message (tokens: `{name}`, `{mention}`, `{level}`).\n"
                "• `" + pfx + "level config reset` — Reset server leveling configuration.\n"
                "• `" + pfx + "level config resetdata` — Clear all XP database records for the server."
            );
        };

        if (args.empty()) {
            send_help();
            return;
        }

        std::string sub = args[0];
        std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

        // Ensure config row exists
        g_db->create_guild_leveling_config(msg.server_id);
        auto cfg_opt = g_db->get_guild_leveling_config(msg.server_id);
        if (!cfg_opt) {
            bot.send_message(msg.channel_id, "⚠️ Failed to resolve guild leveling config.");
            return;
        }
        auto cfg = *cfg_opt;

        if (sub == "enable") {
            cfg.enabled = true;
            if (g_db->update_guild_leveling_config(cfg)) {
                bot.send_message(msg.channel_id, "✅ Leveling system enabled!");
            } else {
                bot.send_message(msg.channel_id, "❌ Failed to update settings.");
            }
        }
        else if (sub == "disable") {
            cfg.enabled = false;
            if (g_db->update_guild_leveling_config(cfg)) {
                bot.send_message(msg.channel_id, "✅ Leveling system disabled!");
            } else {
                bot.send_message(msg.channel_id, "❌ Failed to update settings.");
            }
        }
        else if (sub == "xprange") {
            if (args.size() < 2) {
                bot.send_message(msg.channel_id, "usage: `" + pfx + "level xprange <min-max | single_value | -1 | 0>`");
                return;
            }
            std::string range_arg = args[1];
            if (range_arg == "-1" || range_arg == "0" || range_arg == "default") {
                cfg.min_xp_per_message = 15;
                cfg.max_xp_per_message = 25;
            } else {
                auto dash_pos = range_arg.find('-');
                if (dash_pos != std::string::npos) {
                    try {
                        int min_xp = std::stoi(range_arg.substr(0, dash_pos));
                        int max_xp = std::stoi(range_arg.substr(dash_pos + 1));
                        if (min_xp <= 0 || max_xp <= 0 || min_xp > max_xp) {
                            bot.send_message(msg.channel_id, "❌ Invalid XP range. Must be positive numbers, e.g. `10-20`.");
                            return;
                        }
                        cfg.min_xp_per_message = min_xp;
                        cfg.max_xp_per_message = max_xp;
                    } catch (...) {
                        bot.send_message(msg.channel_id, "❌ Invalid range format. Try `15-25`.");
                        return;
                    }
                } else {
                    try {
                        int xp = std::stoi(range_arg);
                        if (xp <= 0) {
                            bot.send_message(msg.channel_id, "❌ XP must be positive.");
                            return;
                        }
                        cfg.min_xp_per_message = xp;
                        cfg.max_xp_per_message = xp;
                    } catch (...) {
                        bot.send_message(msg.channel_id, "❌ Invalid XP range format.");
                        return;
                    }
                }
            }

            if (g_db->update_guild_leveling_config(cfg)) {
                bot.send_message(msg.channel_id, "✅ XP gain range set to **" + std::to_string(cfg.min_xp_per_message) + "-" + std::to_string(cfg.max_xp_per_message) + "** XP per message.");
            } else {
                bot.send_message(msg.channel_id, "❌ Failed to update settings.");
            }
        }
        else if (sub == "role") {
            if (args.size() < 2) {
                bot.send_message(msg.channel_id, "usage: `" + pfx + "level role <add|remove|sticky> [args...]`");
                return;
            }
            std::string role_sub = args[1];
            std::transform(role_sub.begin(), role_sub.end(), role_sub.begin(), ::tolower);

            if (role_sub == "add") {
                if (args.size() < 4) {
                    bot.send_message(msg.channel_id, "usage: `" + pfx + "level role add <level> <@role>`");
                    return;
                }
                uint32_t lvl = 1;
                try {
                    int l = std::stoi(args[2]);
                    if (l <= 0) {
                        bot.send_message(msg.channel_id, "❌ Level must be greater than 0.");
                        return;
                    }
                    lvl = static_cast<uint32_t>(l);
                } catch (...) {
                    bot.send_message(msg.channel_id, "❌ Invalid level number.");
                    return;
                }

                std::string role_query = args[3];
                std::string role_id = clean_role_query(role_query);

                // Verify the role exists in the server
                bool found = false;
                std::string role_name = "";
                for (const auto& r : server_opt->roles) {
                    if (r.id == role_id || to_lower(r.name) == to_lower(role_id)) {
                        role_id = r.id;
                        role_name = r.name;
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    bot.send_message(msg.channel_id, "❌ Could not find specified role in this server.");
                    return;
                }

                Database::LevelRole reward;
                reward.server_id = msg.server_id;
                reward.level = lvl;
                reward.role_id = role_id;
                reward.role_name = role_name;

                if (g_db->create_level_role(reward)) {
                    bot.send_message(msg.channel_id, "✅ Added level reward: reaching **Level " + std::to_string(lvl) + "** will award role `<@&" + role_id + ">`.");
                } else {
                    bot.send_message(msg.channel_id, "❌ Failed to add level role reward.");
                }
            }
            else if (role_sub == "remove") {
                if (args.size() < 3) {
                    bot.send_message(msg.channel_id, "usage: `" + pfx + "level role remove <level>`");
                    return;
                }
                uint32_t lvl = 1;
                try {
                    lvl = std::stoul(args[2]);
                } catch (...) {
                    bot.send_message(msg.channel_id, "❌ Invalid level.");
                    return;
                }

                if (g_db->delete_level_role(msg.server_id, lvl)) {
                    bot.send_message(msg.channel_id, "✅ Removed level role reward for level **" + std::to_string(lvl) + "**.");
                } else {
                    bot.send_message(msg.channel_id, "❌ Failed to delete level role reward.");
                }
            }
            else if (role_sub == "sticky") {
                if (args.size() < 3) {
                    bot.send_message(msg.channel_id, "usage: `" + pfx + "level role sticky <on|off>`");
                    return;
                }
                std::string sticky_arg = args[2];
                std::transform(sticky_arg.begin(), sticky_arg.end(), sticky_arg.begin(), ::tolower);
                if (sticky_arg == "on" || sticky_arg == "true" || sticky_arg == "1") {
                    cfg.sticky_roles = true;
                } else if (sticky_arg == "off" || sticky_arg == "false" || sticky_arg == "0") {
                    cfg.sticky_roles = false;
                } else {
                    bot.send_message(msg.channel_id, "❌ Invalid sticky value. Choose `on` or `off`.");
                    return;
                }

                if (g_db->update_guild_leveling_config(cfg)) {
                    bot.send_message(msg.channel_id, "✅ Role stickiness set to **" + std::string(cfg.sticky_roles ? "on" : "off") + "**. " + (cfg.sticky_roles ? "(previous level roles are kept)" : "(previous level roles are cleaned up on level-up)"));
                } else {
                    bot.send_message(msg.channel_id, "❌ Failed to update settings.");
                }
            }
        }
        else if (sub == "config") {
            if (args.size() < 2) {
                bot.send_message(msg.channel_id, "usage: `" + pfx + "level config <preview|cooldown|minchars|announce|channel|message|reset|resetdata>`");
                return;
            }
            std::string cfg_sub = args[1];
            std::transform(cfg_sub.begin(), cfg_sub.end(), cfg_sub.begin(), ::tolower);

            if (cfg_sub == "preview") {
                std::string chan_label = cfg.announcement_channel ? "<#" + *cfg.announcement_channel + ">" : "*Current channel*";
                std::string rewards_desc = "";
                auto roles = g_db->get_level_roles(msg.server_id);
                if (roles.empty()) {
                    rewards_desc = "*None*";
                } else {
                    for (const auto& r : roles) {
                        rewards_desc += "• Level " + std::to_string(r.level) + ": <@&" + r.role_id + ">\n";
                    }
                }

                std::string out = "⚙️ **Server Leveling Settings Preview**\n\n"
                                  "• **System Enabled**: " + std::string(cfg.enabled ? "✅ Yes" : "❌ No") + "\n"
                                  "• **XP gain range**: " + std::to_string(cfg.min_xp_per_message) + "-" + std::to_string(cfg.max_xp_per_message) + " XP\n"
                                  "• **XP Message Cooldown**: " + std::to_string(cfg.xp_cooldown_seconds) + " seconds\n"
                                  "• **Minimum character length**: " + std::to_string(cfg.min_message_chars) + " characters\n"
                                  "• **Announce Level-Ups**: " + std::string(cfg.announce_levelup ? "✅ Yes" : "❌ No") + "\n"
                                  "• **Announcement Channel**: " + chan_label + "\n"
                                  "• **Announcement Message Template**: `" + cfg.announcement_message + "`\n"
                                  "• **Sticky Role Rewards**: " + std::string(cfg.sticky_roles ? "✅ Yes (keep old)" : "❌ No (cleanup old)") + "\n\n"
                                  "🏆 **Level Role Rewards:**\n" + rewards_desc;

                bot.send_message(msg.channel_id, out);
            }
            else if (cfg_sub == "cooldown") {
                if (args.size() < 3) {
                    bot.send_message(msg.channel_id, "usage: `" + pfx + "level config cooldown <seconds>`");
                    return;
                }
                try {
                    int cd = std::stoi(args[2]);
                    if (cd < 0) {
                        bot.send_message(msg.channel_id, "❌ Cooldown cannot be negative.");
                        return;
                    }
                    cfg.xp_cooldown_seconds = cd;
                    if (g_db->update_guild_leveling_config(cfg)) {
                        bot.send_message(msg.channel_id, "✅ Cooldown updated to **" + std::to_string(cd) + "** seconds.");
                    } else {
                        bot.send_message(msg.channel_id, "❌ Failed to save changes.");
                    }
                } catch (...) {
                    bot.send_message(msg.channel_id, "❌ Invalid cooldown number.");
                }
            }
            else if (cfg_sub == "minchars") {
                if (args.size() < 3) {
                    bot.send_message(msg.channel_id, "usage: `" + pfx + "level config minchars <number>`");
                    return;
                }
                try {
                    int mc = std::stoi(args[2]);
                    if (mc < 0) {
                        bot.send_message(msg.channel_id, "❌ Minimum characters cannot be negative.");
                        return;
                    }
                    cfg.min_message_chars = mc;
                    if (g_db->update_guild_leveling_config(cfg)) {
                        bot.send_message(msg.channel_id, "✅ Minimum character length set to **" + std::to_string(mc) + "**.");
                    } else {
                        bot.send_message(msg.channel_id, "❌ Failed to save changes.");
                    }
                } catch (...) {
                    bot.send_message(msg.channel_id, "❌ Invalid characters count.");
                }
            }
            else if (cfg_sub == "announce") {
                if (args.size() < 3) {
                    bot.send_message(msg.channel_id, "usage: `" + pfx + "level config announce <on|off>`");
                    return;
                }
                std::string ann_arg = args[2];
                std::transform(ann_arg.begin(), ann_arg.end(), ann_arg.begin(), ::tolower);
                if (ann_arg == "on" || ann_arg == "true" || ann_arg == "1") {
                    cfg.announce_levelup = true;
                } else if (ann_arg == "off" || ann_arg == "false" || ann_arg == "0") {
                    cfg.announce_levelup = false;
                } else {
                    bot.send_message(msg.channel_id, "❌ Choose `on` or `off`.");
                    return;
                }

                if (g_db->update_guild_leveling_config(cfg)) {
                    bot.send_message(msg.channel_id, "✅ Level-up announcement set to **" + std::string(cfg.announce_levelup ? "enabled" : "disabled") + "**.");
                } else {
                    bot.send_message(msg.channel_id, "❌ Failed to save changes.");
                }
            }
            else if (cfg_sub == "channel") {
                if (args.size() < 3) {
                    bot.send_message(msg.channel_id, "usage: `" + pfx + "level config channel <#channel | here | none>`");
                    return;
                }
                std::string chan_arg = args[2];
                if (chan_arg == "none" || chan_arg == "clear") {
                    cfg.announcement_channel = std::nullopt;
                    bot.send_message(msg.channel_id, "✅ Level-up announcement channel set to default *Current channel*.");
                } else if (chan_arg == "here") {
                    cfg.announcement_channel = msg.channel_id;
                    bot.send_message(msg.channel_id, "✅ Level-up announcement channel set to current channel (<#" + msg.channel_id + ">).");
                } else {
                    if (chan_arg.rfind("<#", 0) == 0 && chan_arg.back() == '>') {
                        chan_arg = chan_arg.substr(2, chan_arg.size() - 3);
                    }
                    cfg.announcement_channel = chan_arg;
                    bot.send_message(msg.channel_id, "✅ Level-up announcement channel set to <#" + chan_arg + ">.");
                }

                g_db->update_guild_leveling_config(cfg);
            }
            else if (cfg_sub == "message") {
                if (args.size() < 3) {
                    bot.send_message(msg.channel_id, "usage: `" + pfx + "level config message <message...>`");
                    return;
                }
                size_t p = msg.content.find("message");
                if (p != std::string::npos) {
                    p += 7;
                    while (p < msg.content.size() && msg.content[p] == ' ') p++;
                }
                std::string val_msg = (p != std::string::npos && p < msg.content.size()) ? msg.content.substr(p) : "";
                if (val_msg.empty()) {
                    bot.send_message(msg.channel_id, "❌ Custom message cannot be empty.");
                    return;
                }

                cfg.announcement_message = val_msg;
                if (g_db->update_guild_leveling_config(cfg)) {
                    bot.send_message(msg.channel_id, "✅ Level-up custom message updated!");
                } else {
                    bot.send_message(msg.channel_id, "❌ Failed to save changes.");
                }
            }
            else if (cfg_sub == "reset") {
                Database::GuildLevelingConfig defaults;
                defaults.server_id = msg.server_id;
                if (g_db->update_guild_leveling_config(defaults)) {
                    bot.send_message(msg.channel_id, "✅ Leveling config reset to default settings.");
                } else {
                    bot.send_message(msg.channel_id, "❌ Failed to reset settings.");
                }
            }
            else if (cfg_sub == "resetdata") {
                if (g_db->reset_guild_xp(msg.server_id)) {
                    bot.send_message(msg.channel_id, "✅ Cleared all server members' XP data.");
                } else {
                    bot.send_message(msg.channel_id, "❌ Failed to clear XP data.");
                }
            }
        } else {
            send_help();
        }
    }
};

} // namespace bronx::cogs
