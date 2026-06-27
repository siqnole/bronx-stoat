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
            "manage custom command prefixes (subcommands: list, add, remove).",
            "",
            {"<subcommand>", "[args...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling prefix...\n";
                if (args.empty()) {
                    bot.send_message(msg.channel_id, "usage: `!prefix <list|add|remove> [args...]`\nexamples:\n• `!prefix list`\n• `!prefix add ?`\n• `!prefix add user .`\n• `!prefix remove user .`");
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
                    if (!msg.server_id.empty()) {
                        server_pfx = g_db->get_prefixes("server", msg.server_id);
                    }

                    stoatpp::models::MessagePayload payload;
                    nlohmann::json embed = nlohmann::json::object();
                    embed["title"] = "custom prefix configuration";
                    embed["colour"] = "#FFD700";

                    std::string desc = "";
                    desc += "• **default prefix**: `" + bot.config().command_prefix + "`\n\n";

                    desc += "• **your custom user prefixes**: ";
                    if (user_pfx.empty()) {
                        desc += "none configured (falling back)\n";
                    } else {
                        for (size_t i = 0; i < user_pfx.size(); ++i) {
                            desc += "`" + user_pfx[i] + "`" + (i + 1 < user_pfx.size() ? ", " : "\n");
                        }
                    }

                    if (!msg.server_id.empty()) {
                        desc += "• **server custom prefixes**: ";
                        if (server_pfx.empty()) {
                            desc += "none configured (falling back)\n";
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

                if (subcommand == "add") {
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "usage: `!prefix add [user|server] <prefix>`");
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
                        if (server_opt->owner != msg.author.id) {
                            bot.send_message(msg.channel_id, "permission error: only the server owner can modify server prefixes.");
                            return;
                        }
                    }

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
                    return;
                }

                if (subcommand == "remove") {
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "usage: `!prefix remove [user|server] <prefix>`");
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
                        if (server_opt->owner != msg.author.id) {
                            bot.send_message(msg.channel_id, "permission error: only the server owner can modify server prefixes.");
                            return;
                        }
                    }

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
                    return;
                }

                bot.send_message(msg.channel_id, "unknown subcommand. use list, add, or remove.");
            }
        });
    }
};

} // namespace bronx::cogs
