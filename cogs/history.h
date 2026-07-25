#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "../database.h"
#include <chrono>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <mutex>
#include <thread>

namespace bronx::cogs {

class HistoryModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
        // --- HISTORY COMMAND ---
        bot.register_command({
            "history",
            {},
            "get the execution history of a command or user on this server.",
            "history [user | command]",
            {"[user | command]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling history...\n";
                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id, "⚠️ This command can only be used inside a server channel.");
                    return;
                }
                if (!g_db) {
                    bot.send_message(msg.channel_id, "⚠️ Database is unavailable.");
                    return;
                }
                if (args.empty()) {
                    bot.send_message(msg.channel_id, "⚠️ Usage: `.history [user | command]`\nExample: `.history @user` or `.history ban`");
                    return;
                }

                std::string query = args[0];

                helpers::resolve_user(bot, msg.server_id, query, [&bot, msg, query](stoatpp::models::User target_user, stoatpp::models::Member, bool success) {
                    std::string final_query = query;
                    std::string type = "cmd_history_command";
                    int total_count = 0;
                    int limit = 10;

                    if (success) {
                        final_query = target_user.id;
                        type = "cmd_history_user";
                        g_db->get_server_command_history_by_user(msg.server_id, final_query, 0, limit, total_count);
                    } else {
                        // Check if it starts with . and strip it
                        std::string cmd_name = query;
                        std::transform(cmd_name.begin(), cmd_name.end(), cmd_name.begin(), ::tolower);
                        if (cmd_name.rfind(".", 0) == 0) {
                            cmd_name = cmd_name.substr(1);
                        }
                        final_query = cmd_name;
                        type = "cmd_history_command";
                        g_db->get_server_command_history_by_command(msg.server_id, final_query, 0, limit, total_count);
                    }

                    int max_pages = (total_count + limit - 1) / limit;
                    if (max_pages < 1) max_pages = 1;

                    stoatpp::models::MessagePayload payload;
                    payload.content = " ";
                    stoatpp::models::Embed embed;
                    embed.set_colour(0x6C5CE7);

                    std::string desc;
                    if (type == "cmd_history_user") {
                        std::string username = target_user.username.empty() ? target_user.id : target_user.username;
                        embed.set_title("📋 Command History — User: " + username);
                        auto entries = g_db->get_server_command_history_by_user(msg.server_id, final_query, 0, limit, total_count);
                        if (entries.empty()) {
                            desc = "*No command history found for this user.*";
                        } else {
                            for (const auto& entry : entries) {
                                desc += "• `." + entry.command_name + "` | used in <#" + entry.channel_id + "> | " + stoatpp::utils::format_timestamp(entry.created_at, "R") + "\n";
                            }
                        }
                    } else {
                        embed.set_title("📋 Command History — Command: ." + final_query);
                        auto entries = g_db->get_server_command_history_by_command(msg.server_id, final_query, 0, limit, total_count);
                        if (entries.empty()) {
                            desc = "*No command history found for this command.*";
                        } else {
                            for (const auto& entry : entries) {
                                desc += "• <@" + entry.user_id + "> | used in <#" + entry.channel_id + "> | " + stoatpp::utils::format_timestamp(entry.created_at, "R") + "\n";
                            }
                        }
                    }

                    desc += "\nPage 1 of " + std::to_string(max_pages) + " (" + std::to_string(total_count) + " total executions)";
                    embed.set_description(desc);
                    payload.add_embed(embed);

                    bot.send_message(msg.channel_id, payload, [&bot, msg, max_pages, type, final_query](stoatpp::models::Message res, bool ok) {
                        if (ok && max_pages > 1) {
                            {
                                std::lock_guard<std::mutex> lock(helpers::g_pages_mutex);
                                helpers::g_pages[res.id] = {0, max_pages, msg.author.id, type, final_query, msg.server_id};
                            }
                            bot.react_to_message(res.channel, res.id, "⬅");
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            bot.react_to_message(res.channel, res.id, "➡");
                        }
                    });
                });
            },
            "Moderation",
            stoatpp::permissions::KickMembers
        });

        // --- MODHISTORY COMMAND ---
        bot.register_command({
            "modhistory",
            {},
            "lists all moderation commands executed on this server.",
            "modhistory [user]",
            {"[user]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling modhistory...\n";
                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id, "⚠️ This command can only be used inside a server channel.");
                    return;
                }
                if (!g_db) {
                    bot.send_message(msg.channel_id, "⚠️ Database is unavailable.");
                    return;
                }

                auto proceed_mod_history = [&bot, msg](const std::string& target_user_id) {
                    int total_count = 0;
                    int limit = 10;
                    auto entries = g_db->get_server_mod_history(msg.server_id, target_user_id, 0, limit, total_count);

                    int max_pages = (total_count + limit - 1) / limit;
                    if (max_pages < 1) max_pages = 1;

                    stoatpp::models::MessagePayload payload;
                    payload.content = " ";
                    stoatpp::models::Embed embed;
                    embed.set_colour(0x6C5CE7);

                    if (target_user_id.empty()) {
                        embed.set_title("📋 Moderation History");
                    } else {
                        embed.set_title("📋 Moderation History — User: <@" + target_user_id + ">");
                    }

                    auto action_emoji = [](const std::string& a) -> std::string {
                        if (a == "Ban")     return "🔨";
                        if (a == "Kick")    return "👢";
                        if (a == "Timeout") return "🔇";
                        if (a == "Warn")    return "⚠️";
                        if (a == "Unban")   return "✅";
                        if (a == "Jail")    return "⛓️";
                        if (a == "Unjail")  return "🔓";
                        return "🛡️";
                    };

                    std::string desc;
                    if (entries.empty()) {
                        desc = "*No moderation actions found.*";
                    } else {
                        for (const auto& entry : entries) {
                            desc += action_emoji(entry.action) + " **" + entry.action + "**";
                            desc += " `#" + std::to_string(entry.id) + "`\n";
                            desc += "  **invoker:** <@" + entry.moderator_id + ">\n";
                            desc += "  **target:** <@" + entry.target_id + ">\n";
                            if (!entry.channel_id.empty()) {
                                desc += "  **channel:** <#" + entry.channel_id + ">\n";
                            }
                            if (!entry.reason.empty()) {
                                desc += "  **reason:** " + entry.reason + "\n";
                            }
                            if (!entry.extra_details.empty()) {
                                desc += "  **details:** " + entry.extra_details + "\n";
                            }
                            desc += "  **time:** " + stoatpp::utils::format_timestamp(entry.created_at, "R") + "\n\n";
                        }
                    }

                    desc += "Page 1 of " + std::to_string(max_pages) + " (" + std::to_string(total_count) + " total actions)";
                    embed.set_description(desc);
                    payload.add_embed(embed);

                    bot.send_message(msg.channel_id, payload, [&bot, msg, max_pages, target_user_id](stoatpp::models::Message res, bool ok) {
                        if (ok && max_pages > 1) {
                            {
                                std::lock_guard<std::mutex> lock(helpers::g_pages_mutex);
                                helpers::g_pages[res.id] = {0, max_pages, msg.author.id, "mod_history", target_user_id, msg.server_id};
                            }
                            bot.react_to_message(res.channel, res.id, "⬅");
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            bot.react_to_message(res.channel, res.id, "➡");
                        }
                    });
                };

                if (args.empty()) {
                    proceed_mod_history("");
                } else {
                    helpers::resolve_user(bot, msg.server_id, args[0], [&bot, msg, proceed_mod_history](stoatpp::models::User target_user, stoatpp::models::Member, bool success) {
                        if (!success) {
                            bot.send_message(msg.channel_id, "⚠️ Could not resolve target user.");
                            return;
                        }
                        proceed_mod_history(target_user.id);
                    });
                }
            },
            "Moderation",
            stoatpp::permissions::KickMembers
        });

        // --- PAGINATION REACTION HANDLER ---
        bot.on_message_react([&bot](const stoatpp::events::MessageReact& e) {
            if (e.user_id == bot.current_user().id) return;

            std::unique_lock<std::mutex> lock(helpers::g_pages_mutex);
            auto it = helpers::g_pages.find(e.id);
            if (it != helpers::g_pages.end()) {
                auto& session = it->second;
                if (session.type == "cmd_history_user" || session.type == "cmd_history_command" || session.type == "mod_history") {
                    if (e.user_id != session.user_id) {
                        lock.unlock();
                        bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
                        return;
                    }

                    bool page_changed = false;
                    if (e.emoji_id == "⬅️" || e.emoji_id == "◀" || e.emoji_id == "⬅") {
                        if (session.current_page > 0) {
                            session.current_page--;
                        } else {
                            session.current_page = session.max_pages - 1;
                        }
                        page_changed = true;
                    } else if (e.emoji_id == "➡️" || e.emoji_id == "▶" || e.emoji_id == "➡") {
                        if (session.current_page < session.max_pages - 1) {
                            session.current_page++;
                        } else {
                            session.current_page = 0;
                        }
                        page_changed = true;
                    }

                    if (page_changed) {
                        int page = session.current_page;
                        int max_pages = session.max_pages;
                        std::string type = session.type;
                        std::string target_query = session.target_query;
                        lock.unlock();

                        std::string server_id = session.server_id;
                        if (server_id.empty()) {
                            auto chan = bot.get_channel(e.channel_id);
                            if (chan && chan->server) {
                                server_id = *chan->server;
                            }
                        }

                        stoatpp::models::MessagePayload payload;
                        payload.content = " ";
                        stoatpp::models::Embed embed;
                        embed.set_colour(0x6C5CE7);

                        int limit = 10;
                        int offset = page * limit;
                        int total_count = 0;

                        if (type == "cmd_history_user") {
                            auto entries = g_db->get_server_command_history_by_user(server_id, target_query, offset, limit, total_count);
                            // We don't have the user object in the reaction event to easily get the username, 
                            // but we can query it using target_query as user ID in the mention.
                            embed.set_title("📋 Command History — User: <@" + target_query + ">");
                            
                            std::string desc;
                            if (entries.empty()) {
                                desc = "*No command history found for this user.*";
                            } else {
                                for (const auto& entry : entries) {
                                    desc += "• `." + entry.command_name + "` | used in <#" + entry.channel_id + "> | " + stoatpp::utils::format_timestamp(entry.created_at, "R") + "\n";
                                }
                            }
                            desc += "\nPage " + std::to_string(page + 1) + " of " + std::to_string(max_pages) + " (" + std::to_string(total_count) + " total executions)";
                            embed.set_description(desc);
                        }
                        else if (type == "cmd_history_command") {
                            auto entries = g_db->get_server_command_history_by_command(server_id, target_query, offset, limit, total_count);
                            embed.set_title("📋 Command History — Command: ." + target_query);
                            
                            std::string desc;
                            if (entries.empty()) {
                                desc = "*No command history found for this command.*";
                            } else {
                                for (const auto& entry : entries) {
                                    desc += "• <@" + entry.user_id + "> | used in <#" + entry.channel_id + "> | " + stoatpp::utils::format_timestamp(entry.created_at, "R") + "\n";
                                }
                            }
                            desc += "\nPage " + std::to_string(page + 1) + " of " + std::to_string(max_pages) + " (" + std::to_string(total_count) + " total executions)";
                            embed.set_description(desc);
                        }
                        else if (type == "mod_history") {
                            auto entries = g_db->get_server_mod_history(server_id, target_query, offset, limit, total_count);
                            if (target_query.empty()) {
                                embed.set_title("📋 Moderation History");
                            } else {
                                embed.set_title("📋 Moderation History — User: <@" + target_query + ">");
                            }

                            auto action_emoji = [](const std::string& a) -> std::string {
                                if (a == "Ban")     return "🔨";
                                if (a == "Kick")    return "👢";
                                if (a == "Timeout") return "🔇";
                                if (a == "Warn")    return "⚠️";
                                if (a == "Unban")   return "✅";
                                if (a == "Jail")    return "⛓️";
                                if (a == "Unjail")  return "🔓";
                                return "🛡️";
                            };

                            std::string desc;
                            if (entries.empty()) {
                                desc = "*No moderation actions found.*";
                            } else {
                                for (const auto& entry : entries) {
                                    desc += action_emoji(entry.action) + " **" + entry.action + "**";
                                    desc += " `#" + std::to_string(entry.id) + "`\n";
                                    desc += "  **invoker:** <@" + entry.moderator_id + ">\n";
                                    desc += "  **target:** <@" + entry.target_id + ">\n";
                                    if (!entry.channel_id.empty()) {
                                        desc += "  **channel:** <#" + entry.channel_id + ">\n";
                                    }
                                    if (!entry.reason.empty()) {
                                        desc += "  **reason:** " + entry.reason + "\n";
                                    }
                                    if (!entry.extra_details.empty()) {
                                        desc += "  **details:** " + entry.extra_details + "\n";
                                    }
                                    desc += "  **time:** " + stoatpp::utils::format_timestamp(entry.created_at, "R") + "\n\n";
                                }
                            }
                            desc += "Page " + std::to_string(page + 1) + " of " + std::to_string(max_pages) + " (" + std::to_string(total_count) + " total actions)";
                            embed.set_description(desc);
                        }

                        payload.add_embed(embed);
                        bot.edit_message(e.channel_id, e.id, payload, [&bot, e](auto, bool) {
                            bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
                        });
                    } else {
                        lock.unlock();
                        bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
                    }
                }
            }
        });
    }
};

} // namespace bronx::cogs
