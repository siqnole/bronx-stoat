#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"

namespace bronx::cogs {

class AliasModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
        // ── 1. Server Aliases Cog (.alias) ──
        bot.register_command({
            "alias",
            {},
            "manage custom command aliases for this server (subcommands: list, add, remove, useraliases).",
            "",
            {"<subcommand>", "[args...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling server alias...\n";
                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id, "❌ this command can only be used inside a server.");
                    return;
                }

                if (!g_db) {
                    bot.send_message(msg.channel_id, "❌ database connection is not initialized.");
                    return;
                }

                std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                if (args.empty()) {
                    bot.send_message(msg.channel_id,
                        "**custom server aliases usage:**\n"
                        "• `" + pfx + "alias list` — list all server-wide aliases\n"
                        "• `" + pfx + "alias add <alias> <target>` — add or update a server alias\n"
                        "• `" + pfx + "alias remove <alias>` — delete a server alias\n"
                        "• `" + pfx + "alias useraliases <on|off>` — enable or disable user personal aliases in this server");
                    return;
                }

                std::string subcommand = args[0];

                if (subcommand == "list") {
                    auto aliases = g_db->get_server_aliases(msg.server_id);
                    bool user_disabled = g_db->get_user_aliases_disabled(msg.server_id);

                    stoatpp::models::MessagePayload payload;
                    nlohmann::json embed = nlohmann::json::object();
                    embed["title"] = "server command aliases";
                    embed["colour"] = "#5865F2";

                    std::string desc = "";
                    desc += "• **user-level aliases status**: " + std::string(user_disabled ? "🚫 **disabled** in this server\n" : "✅ **enabled** in this server\n");
                    desc += "\n### active server aliases:\n";
                    if (aliases.empty()) {
                        desc += "_no server-wide aliases configured._\n";
                    } else {
                        for (const auto& [alias, target] : aliases) {
                            desc += "• `" + alias + "` ➔ `" + target + "`\n";
                        }
                    }
                    embed["description"] = desc;
                    payload.embeds.push_back(embed);
                    bot.send_message(msg.channel_id, payload);
                    return;
                }

                // Subcommands requiring ManageServer permission
                auto server_opt = bot.get_server(msg.server_id);
                if (!server_opt) {
                    bot.send_message(msg.channel_id, "❌ failed to resolve server details.");
                    return;
                }

                bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, server_opt, subcommand, args, pfx](stoatpp::models::Member member, bool success) {
                    if (!success) {
                        bot.send_message(msg.channel_id, "❌ failed to verify server member permissions.");
                        return;
                    }

                    if (!bot.is_owner(msg.author.id) && !bot.has_permission(*server_opt, member, stoatpp::permissions::ManageServer)) {
                        bot.send_message(msg.channel_id, "❌ permission error: you need the **Manage Server** permission to run this subcommand.");
                        return;
                    }

                    if (subcommand == "add") {
                        if (args.size() < 3) {
                            bot.send_message(msg.channel_id, "usage: `" + pfx + "alias add <alias> <target>`");
                            return;
                        }
                        std::string alias = args[1];
                        std::string target = args[2];

                        // Prevent recursive aliases / invalid names
                        if (alias == target) {
                            bot.send_message(msg.channel_id, "❌ you cannot map a command to itself.");
                            return;
                        }

                        if (g_db->set_server_alias(msg.server_id, alias, target)) {
                            bot.send_message(msg.channel_id, "✅ successfully mapped `" + alias + "` to `" + target + "` for this server!");
                        } else {
                            bot.send_message(msg.channel_id, "❌ failed to save configuration to database.");
                        }
                    } 
                    else if (subcommand == "remove") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "usage: `" + pfx + "alias remove <alias>`");
                            return;
                        }
                        std::string alias = args[1];
                        if (g_db->delete_server_alias(msg.server_id, alias)) {
                            bot.send_message(msg.channel_id, "✅ successfully removed server alias `" + alias + "`.");
                        } else {
                            bot.send_message(msg.channel_id, "❌ failed to delete alias or alias not found.");
                        }
                    } 
                    else if (subcommand == "useraliases") {
                        if (args.size() < 2) {
                            bool cur = g_db->get_user_aliases_disabled(msg.server_id);
                            bot.send_message(msg.channel_id, "user-level aliases are currently **" + std::string(cur ? "disabled" : "enabled") + "** in this server.");
                            return;
                        }
                        std::string val = args[1];
                        std::transform(val.begin(), val.end(), val.begin(), [](unsigned char c){ return std::tolower(c); });
                        
                        bool disable;
                        if (val == "off" || val == "false" || val == "0" || val == "no" || val == "disabled") {
                            disable = true; // disable them
                        } else if (val == "on" || val == "true" || val == "1" || val == "yes" || val == "enabled") {
                            disable = false; // enable them
                        } else {
                            bot.send_message(msg.channel_id, "❌ invalid setting. use `on` (enable) or `off` (disable).");
                            return;
                        }

                        if (g_db->set_user_aliases_disabled(msg.server_id, disable)) {
                            bot.send_message(msg.channel_id, "✅ user-level aliases are now **" + std::string(disable ? "disabled" : "enabled") + "** in this server.");
                        } else {
                            bot.send_message(msg.channel_id, "❌ failed to update settings.");
                        }
                    } 
                    else {
                        bot.send_message(msg.channel_id, "❌ unknown subcommand. use list, add, remove, or useraliases.");
                    }
                });
            },
            "Alias",
            0,
            0,
            {"list", "add <alias> <target>", "remove <alias>", "useraliases <on|off>"}
        });

        // ── 2. User Personal Aliases Cog (.myalias) ──
        bot.register_command({
            "myalias",
            {},
            "manage your own personal custom command aliases (subcommands: list, add, remove).",
            "",
            {"<subcommand>", "[args...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling user personal alias...\n";
                if (!g_db) {
                    bot.send_message(msg.channel_id, "❌ database connection is not initialized.");
                    return;
                }

                std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                if (args.empty()) {
                    bot.send_message(msg.channel_id,
                        "**personal command aliases usage:**\n"
                        "• `" + pfx + "myalias list` — list your personal aliases\n"
                        "• `" + pfx + "myalias add <alias> <target>` — add or update a personal alias\n"
                        "• `" + pfx + "myalias remove <alias>` — delete a personal alias");
                    return;
                }

                std::string subcommand = args[0];

                if (subcommand == "list") {
                    auto aliases = g_db->get_user_aliases(msg.author.id);

                    stoatpp::models::MessagePayload payload;
                    nlohmann::json embed = nlohmann::json::object();
                    embed["title"] = "your personal command aliases";
                    embed["colour"] = "#10B981"; // elegant emerald green

                    std::string desc = "";
                    if (!msg.server_id.empty() && g_db->get_user_aliases_disabled(msg.server_id)) {
                        desc += "⚠️ _note: user aliases are disabled by administrators in this server._\n\n";
                    }
                    if (aliases.empty()) {
                        desc += "_you have no personal aliases configured._\n";
                    } else {
                        for (const auto& [alias, target] : aliases) {
                            desc += "• `" + alias + "` ➔ `" + target + "`\n";
                        }
                    }
                    embed["description"] = desc;
                    payload.embeds.push_back(embed);
                    bot.send_message(msg.channel_id, payload);
                    return;
                }

                if (subcommand == "add") {
                    if (args.size() < 3) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "myalias add <alias> <target>`");
                        return;
                    }
                    std::string alias = args[1];
                    std::string target = args[2];

                    if (alias == target) {
                        bot.send_message(msg.channel_id, "❌ you cannot map a command to itself.");
                        return;
                    }

                    if (g_db->set_user_alias(msg.author.id, alias, target)) {
                        bot.send_message(msg.channel_id, "✅ successfully mapped `" + alias + "` to `" + target + "` for your account!");
                    } else {
                        bot.send_message(msg.channel_id, "❌ failed to save personal alias configuration.");
                    }
                } 
                else if (subcommand == "remove") {
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "myalias remove <alias>`");
                        return;
                    }
                    std::string alias = args[1];
                    if (g_db->delete_user_alias(msg.author.id, alias)) {
                        bot.send_message(msg.channel_id, "✅ successfully removed personal alias `" + alias + "`.");
                    } else {
                        bot.send_message(msg.channel_id, "❌ failed to delete personal alias or alias not found.");
                    }
                } 
                else {
                    bot.send_message(msg.channel_id, "❌ unknown subcommand. use list, add, or remove.");
                }
            },
            "Alias",
            0,
            0,
            {"list", "add <alias> <target>", "remove <alias>"}
        });
    }
};

} // namespace bronx::cogs
