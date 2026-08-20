#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"

namespace bronx::cogs {

class PrefixModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "prefix",
            {},
            "manage custom command prefixes (subcommands: list, add, remove, default).",
            "",
            {"<subcommand>", "[args...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling prefix...\n";
                if (args.empty()) {
                    std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                    bot.send_message(msg.channel_id,
                        "usage: `" + pfx + "prefix <list|add|remove|default> [args...]`\n"
                        "examples:\n"
                        "• `" + pfx + "prefix list`\n"
                        "• `" + pfx + "prefix add ?`\n"
                        "• `" + pfx + "prefix add user .`\n"
                        "• `" + pfx + "prefix remove user .`\n"
                        "• `" + pfx + "prefix default off` — stop responding to default prefix when custom server prefix is set\n"
                        "• `" + pfx + "prefix default on` — always respond to default prefix");
                    return;
                }

                if (!g_db) {
                    bot.send_message(msg.channel_id, "database connection is not initialized.");
                    return;
                }

                std::string subcommand = args[0];
                
                if (subcommand == "list") {
                    std::vector<std::string> user_pfx = g_db->get_prefixes("user", msg.author.id);
                    std::vector<std::string> server_pfx;
                    bool default_enabled = true;
                    if (!msg.server_id.empty()) {
                        server_pfx = g_db->get_prefixes("server", msg.server_id);
                        default_enabled = g_db->get_prefix_default_enabled(msg.server_id);
                    }

                    stoatpp::models::MessagePayload payload;
                    nlohmann::json embed = nlohmann::json::object();
                    embed["title"] = "custom prefix configuration";
                    embed["colour"] = "#FFD700";

                    std::string desc = "";
                    desc += "• **default prefix**: `" + bot.config().command_prefix + "` — ";
                    if (!msg.server_id.empty() && !server_pfx.empty()) {
                        desc += default_enabled ? "**on** (active alongside server prefixes)\n" : "**off** (suppressed by server prefixes)\n";
                    } else {
                        desc += "**on** (always active)\n";
                    }
                    desc += "\n";

                    desc += "• **your custom user prefixes**: ";
                    if (user_pfx.empty()) {
                        desc += "none configured\n";
                    } else {
                        for (size_t i = 0; i < user_pfx.size(); ++i) {
                            desc += "`" + user_pfx[i] + "`" + (i + 1 < user_pfx.size() ? ", " : "\n");
                        }
                    }

                    if (!msg.server_id.empty()) {
                        desc += "• **server custom prefixes**: ";
                        if (server_pfx.empty()) {
                            desc += "none configured\n";
                        } else {
                            for (size_t i = 0; i < server_pfx.size(); ++i) {
                                desc += "`" + server_pfx[i] + "`" + (i + 1 < server_pfx.size() ? ", " : "\n");
                            }
                        }
                    }
                    embed["description"] = desc;
                    payload.embeds.push_back(embed);
                    bot.send_message(msg.channel_id, payload);
                    return;
                }

                if (subcommand == "default") {
                    if (msg.server_id.empty()) {
                        bot.send_message(msg.channel_id, "this command can only be used inside a server.");
                        return;
                    }
                    if (args.size() < 2) {
                        std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                        bool cur = g_db->get_prefix_default_enabled(msg.server_id);
                        bot.send_message(msg.channel_id,
                            "default prefix is currently **" + std::string(cur ? "on" : "off") + "**.\n"
                            "use `" + pfx + "prefix default on` or `" + pfx + "prefix default off` to change it.");
                        return;
                    }

                    std::string val = args[1];
                    std::transform(val.begin(), val.end(), val.begin(), [](unsigned char c){ return std::tolower(c); });
                    bool enable;
                    if (val == "on" || val == "true" || val == "1" || val == "yes") {
                        enable = true;
                    } else if (val == "off" || val == "false" || val == "0" || val == "no") {
                        enable = false;
                    } else {
                        bot.send_message(msg.channel_id, "invalid value. use `on` or `off`.");
                        return;
                    }

                    auto server_opt = bot.get_server(msg.server_id);
                    if (!server_opt) {
                        bot.send_message(msg.channel_id, "failed to resolve server details.");
                        return;
                    }

                    bot.fetch_member(msg.server_id, msg.author.id,
                        [&bot, msg, server_opt, enable](stoatpp::models::Member member, bool success) {
                            if (!success) {
                                bot.send_message(msg.channel_id, "failed to verify your server member permissions.");
                                return;
                            }
                            if (!bot.is_owner(msg.author.id) &&
                                !bot.has_permission(*server_opt, member, stoatpp::permissions::ManageServer)) {
                                bot.send_message(msg.channel_id,
                                    "permission error: you need the Manage Server permission to change this.");
                                return;
                            }

                            if (g_db->set_prefix_default_enabled(msg.server_id, enable)) {
                                std::string def_pfx = bot.config().command_prefix;
                                if (enable) {
                                    bot.send_message(msg.channel_id,
                                        "" + bronx::emj_yes + " default prefix `" + def_pfx + "` is now **enabled** — "
                                        "the bot will respond to both server custom prefixes and `" + def_pfx + "`.");
                                } else {
                                    auto server_pfx = g_db->get_prefixes("server", msg.server_id);
                                    if (server_pfx.empty()) {
                                        bot.send_message(msg.channel_id,
                                            "" + bronx::emj_warning + " no custom server prefixes are set. the default prefix `" + def_pfx + "` will remain active "
                                            "until you add a custom server prefix first (`.prefix add <prefix>`).");
                                    } else {
                                        bot.send_message(msg.channel_id,
                                            "" + bronx::emj_yes + " default prefix `" + def_pfx + "` is now **disabled** — "
                                            "the bot will only respond to custom server prefixes.");
                                    }
                                }
                            } else {
                                bot.send_message(msg.channel_id, "failed to save setting to database.");
                            }
                        });
                    return;
                }

                if (subcommand == "add") {
                    if (args.size() < 2) {
                        std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "prefix add [user|server] <prefix>`");
                        return;
                    }
                    if (args.size() == 2 && (args[1] == "user" || args[1] == "server")) {
                        std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "prefix add " + args[1] + " <prefix>`");
                        return;
                    }

                    std::string target_type = "server";
                    std::string target_id = msg.server_id;
                    std::string new_prefix = "";

                    if (args.size() >= 3) {
                        if (args[1] == "user") {
                            target_type = "user";
                            target_id = msg.author.id;
                            new_prefix = args[2];
                        } else if (args[1] == "server") {
                            target_type = "server";
                            target_id = msg.server_id;
                            new_prefix = args[2];
                        } else {
                            new_prefix = args[1];
                        }
                    } else {
                        new_prefix = args[1];
                        if (msg.server_id.empty()) {
                            target_type = "user";
                            target_id = msg.author.id;
                        }
                    }

                    if (new_prefix.empty()) {
                        bot.send_message(msg.channel_id, "prefix cannot be empty.");
                        return;
                    }

                    // Enforce server permissions if setting server prefix
                    if (target_type == "server") {
                        if (msg.server_id.empty()) {
                            bot.send_message(msg.channel_id, "cannot set server prefix outside of a server channel.");
                            return;
                        }
                        auto server_opt = bot.get_server(msg.server_id);
                        if (!server_opt) {
                            bot.send_message(msg.channel_id, "failed to resolve server details.");
                            return;
                        }
                        
                        auto proceed_add = [&bot, msg, target_type, target_id, new_prefix]() {
                            std::vector<std::string> current = g_db->get_prefixes(target_type, target_id);
                            if (std::find(current.begin(), current.end(), new_prefix) != current.end()) {
                                bot.send_message(msg.channel_id, "prefix `" + new_prefix + "` is already configured for this " + target_type + ".");
                                return;
                            }

                            current.push_back(new_prefix);
                            if (g_db->set_prefixes(target_type, target_id, current)) {
                                bot.send_message(msg.channel_id, "successfully added custom prefix `" + new_prefix + "` to " + target_type + ".");
                            } else {
                                bot.send_message(msg.channel_id, "failed to save prefix configuration to database.");
                            }
                        };

                        bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, server_opt, proceed_add](stoatpp::models::Member member, bool success) {
                            if (!success) {
                                bot.send_message(msg.channel_id, "failed to verify your server member permissions.");
                                return;
                            }
                            if (!bot.is_owner(msg.author.id) && !bot.has_permission(*server_opt, member, stoatpp::permissions::ManageServer)) {
                                bot.send_message(msg.channel_id, "permission error: you need the Manage Server permission to modify server prefixes.");
                                return;
                            }
                            proceed_add();
                        });
                    } else {
                        std::vector<std::string> current = g_db->get_prefixes(target_type, target_id);
                        if (std::find(current.begin(), current.end(), new_prefix) != current.end()) {
                            bot.send_message(msg.channel_id, "prefix `" + new_prefix + "` is already configured for this " + target_type + ".");
                            return;
                        }

                        current.push_back(new_prefix);
                        if (g_db->set_prefixes(target_type, target_id, current)) {
                            bot.send_message(msg.channel_id, "successfully added custom prefix `" + new_prefix + "` to " + target_type + ".");
                        } else {
                            bot.send_message(msg.channel_id, "failed to save prefix configuration to database.");
                        }
                    }
                    return;
                }

                if (subcommand == "remove") {
                    if (args.size() < 2) {
                        std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "prefix remove [user|server] <prefix>`");
                        return;
                    }
                    if (args.size() == 2 && (args[1] == "user" || args[1] == "server")) {
                        std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "prefix remove " + args[1] + " <prefix>`");
                        return;
                    }

                    std::string target_type = "server";
                    std::string target_id = msg.server_id;
                    std::string to_remove = "";

                    if (args.size() >= 3) {
                        if (args[1] == "user") {
                            target_type = "user";
                            target_id = msg.author.id;
                            to_remove = args[2];
                        } else if (args[1] == "server") {
                            target_type = "server";
                            target_id = msg.server_id;
                            to_remove = args[2];
                        } else {
                            to_remove = args[1];
                        }
                    } else {
                        to_remove = args[1];
                        if (msg.server_id.empty()) {
                            target_type = "user";
                            target_id = msg.author.id;
                        }
                    }

                    if (to_remove.empty()) {
                        bot.send_message(msg.channel_id, "prefix cannot be empty.");
                        return;
                    }

                    // Enforce server permissions if setting server prefix
                    if (target_type == "server") {
                        if (msg.server_id.empty()) {
                            bot.send_message(msg.channel_id, "cannot modify server prefix outside of a server channel.");
                            return;
                        }
                        auto server_opt = bot.get_server(msg.server_id);
                        if (!server_opt) {
                            bot.send_message(msg.channel_id, "failed to resolve server details.");
                            return;
                        }

                        auto proceed_remove = [&bot, msg, target_type, target_id, to_remove]() {
                            std::vector<std::string> current = g_db->get_prefixes(target_type, target_id);
                            auto it = std::find(current.begin(), current.end(), to_remove);
                            if (it == current.end()) {
                                bot.send_message(msg.channel_id, "prefix `" + to_remove + "` is not configured for this " + target_type + ".");
                                return;
                            }

                            current.erase(it);
                            if (current.empty()) {
                                if (g_db->remove_prefixes(target_type, target_id)) {
                                    bot.send_message(msg.channel_id, "removed last custom prefix. custom configuration cleared for " + target_type + " (reverting to fallbacks).");
                                } else {
                                    bot.send_message(msg.channel_id, "failed to delete configuration from database.");
                                }
                            } else {
                                if (g_db->set_prefixes(target_type, target_id, current)) {
                                    bot.send_message(msg.channel_id, "successfully removed prefix `" + to_remove + "` from " + target_type + ".");
                                } else {
                                    bot.send_message(msg.channel_id, "failed to save prefix configuration to database.");
                                }
                            }
                        };

                        bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, server_opt, proceed_remove](stoatpp::models::Member member, bool success) {
                            if (!success) {
                                bot.send_message(msg.channel_id, "failed to verify your server member permissions.");
                                return;
                            }
                            if (!bot.is_owner(msg.author.id) && !bot.has_permission(*server_opt, member, stoatpp::permissions::ManageServer)) {
                                bot.send_message(msg.channel_id, "permission error: you need the Manage Server permission to modify server prefixes.");
                                return;
                            }
                            proceed_remove();
                        });
                    } else {
                        std::vector<std::string> current = g_db->get_prefixes(target_type, target_id);
                        auto it = std::find(current.begin(), current.end(), to_remove);
                        if (it == current.end()) {
                            bot.send_message(msg.channel_id, "prefix `" + to_remove + "` is not configured for this " + target_type + ".");
                            return;
                        }

                        current.erase(it);
                        if (current.empty()) {
                            if (g_db->remove_prefixes(target_type, target_id)) {
                                bot.send_message(msg.channel_id, "removed last custom prefix. custom configuration cleared for " + target_type + " (reverting to fallbacks).");
                            } else {
                                bot.send_message(msg.channel_id, "failed to delete configuration from database.");
                            }
                        } else {
                            if (g_db->set_prefixes(target_type, target_id, current)) {
                                bot.send_message(msg.channel_id, "successfully removed prefix `" + to_remove + "` from " + target_type + ".");
                            } else {
                                bot.send_message(msg.channel_id, "failed to save prefix configuration to database.");
                            }
                        }
                    }
                    return;
                }

                bot.send_message(msg.channel_id, "unknown subcommand. use list, add, remove, or default.");
            },
            "Prefix",
            0,
            0,
            {"list", "add [user|server] <prefix>", "remove [user|server] <prefix>", "default [on|off]"}
        });
    }
};

} // namespace bronx::cogs
