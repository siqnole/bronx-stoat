#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include <map>
#include <set>
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include <iostream>

namespace bronx::cogs {

class HelpModule : public stoatpp::bot_module {
public:
    struct HelpSession {
        int current_category_idx = 0;
        int current_page_idx = 0;
        std::string user_id;
    };

    struct PromptSession {
        std::string message_id;
        std::string user_id;
    };

private:
    std::unordered_map<std::string, HelpSession> help_sessions_;
    std::mutex help_mutex_;

    std::unordered_map<std::string, PromptSession> pending_prompts_;
    std::mutex prompt_mutex_;

public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "help",
            {"h", "commands"},
            "custom tabbed help menu detailing all commands.",
            "",
            {},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling custom help...\n";
                
                auto all_cmds = bot.get_commands();
                std::unordered_map<std::string, std::vector<stoatpp::Command>> commands_by_cat;
                std::vector<std::string> categories;
                
                // Group commands by category
                for (const auto& cmd : all_cmds) {
                    if (!msg.server_id.empty() && g_db) {
                        std::string cmd_name = cmd.name;
                        std::string mod_name = cmd.category;
                        std::transform(cmd_name.begin(), cmd_name.end(), cmd_name.begin(), ::tolower);
                        std::transform(mod_name.begin(), mod_name.end(), mod_name.begin(), ::tolower);
                        if (g_db->is_disabled(msg.server_id, cmd_name, mod_name)) {
                            continue;
                        }
                    }
                    // Skip the help command itself to avoid listing it repetitively, or put it in General
                    std::string cat = cmd.category.empty() ? "General" : cmd.category;
                    commands_by_cat[cat].push_back(cmd);
                }

                // Sort category names, ensure "General" is first if it exists
                for (const auto& pair : commands_by_cat) {
                    categories.push_back(pair.first);
                }
                std::sort(categories.begin(), categories.end(), [](const std::string& a, const std::string& b) {
                    if (a == "General") return true;
                    if (b == "General") return false;
                    return a < b;
                });

                if (categories.empty()) {
                    bot.send_message(msg.channel_id, "No commands registered.");
                    return;
                }

                std::string prefix = bot.config().command_prefix;
                if (bot.config().prefix_resolver) {
                    auto resolved = bot.config().prefix_resolver(bot, msg);
                    if (!resolved.empty()) {
                        prefix = resolved[0];
                    }
                }

                // Sort commands within categories
                for (auto& pair : commands_by_cat) {
                    std::sort(pair.second.begin(), pair.second.end(), [](const auto& a, const auto& b) {
                        return a.name < b.name;
                    });
                }

                HelpSession session;
                session.current_category_idx = 0;
                session.current_page_idx = 0;
                session.user_id = msg.author.id;

                auto payload = render_help_page(bot, categories, commands_by_cat, session, prefix);

                bot.send_message(msg.channel_id, payload, [this, &bot, msg, categories](stoatpp::models::Message res, bool success) {
                    if (success) {
                        {
                            std::lock_guard<std::mutex> lock(help_mutex_);
                            help_sessions_[res.id] = {0, 0, msg.author.id};
                        }
                        
                        bot.react_to_message(msg.channel_id, res.id, "⏪");
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        bot.react_to_message(msg.channel_id, res.id, "◀");
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        bot.react_to_message(msg.channel_id, res.id, "🟦");
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        bot.react_to_message(msg.channel_id, res.id, "▶");
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        bot.react_to_message(msg.channel_id, res.id, "⏩");
                    }
                });
            },
            "General"
        });

        bot.on_message_react([this, &bot](const stoatpp::events::MessageReact& e) {
            std::unique_lock<std::mutex> lock(help_mutex_);
            auto it = help_sessions_.find(e.id);
            if (it == help_sessions_.end()) return;

            auto session = it->second;
            if (e.user_id == bot.current_user().id) return;
            if (e.user_id != session.user_id) {
                lock.unlock();
                bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
                return;
            }

            // Gather categories and commands again
            auto all_cmds = bot.get_commands();
            std::unordered_map<std::string, std::vector<stoatpp::Command>> commands_by_cat;
            std::vector<std::string> categories;
            for (const auto& cmd : all_cmds) {
                std::string cat = cmd.category.empty() ? "General" : cmd.category;
                commands_by_cat[cat].push_back(cmd);
            }
            for (const auto& pair : commands_by_cat) {
                categories.push_back(pair.first);
            }
            std::sort(categories.begin(), categories.end(), [](const std::string& a, const std::string& b) {
                if (a == "General") return true;
                if (b == "General") return false;
                return a < b;
            });
            for (auto& pair : commands_by_cat) {
                std::sort(pair.second.begin(), pair.second.end(), [](const auto& a, const auto& b) {
                    return a.name < b.name;
                });
            }

            if (categories.empty()) return;

            std::string prefix = bot.config().command_prefix;

            bool changed = false;
            
            // ⏪: Prev Category
            if (e.emoji_id == "⏪" || e.emoji_id == "⏮" || e.emoji_id == "⏮️") {
                if (session.current_category_idx > 0) {
                    session.current_category_idx--;
                } else {
                    session.current_category_idx = categories.size() - 1;
                }
                session.current_page_idx = 0;
                changed = true;
            }
            // ⏩: Next Category
            else if (e.emoji_id == "⏩" || e.emoji_id == "⏭" || e.emoji_id == "⏭️") {
                if (session.current_category_idx < static_cast<int>(categories.size()) - 1) {
                    session.current_category_idx++;
                } else {
                    session.current_category_idx = 0;
                }
                session.current_page_idx = 0;
                changed = true;
            }
            // ◀ / ⬅: Prev Page in Category
            else if (e.emoji_id == "◀" || e.emoji_id == "⬅" || e.emoji_id == "⬅️" || e.emoji_id == "◀️") {
                std::string cat = categories[session.current_category_idx];
                int cmds_size = commands_by_cat[cat].size();
                int max_pages = (cmds_size + 2) / 3;
                if (max_pages == 0) max_pages = 1;

                if (session.current_page_idx > 0) {
                    session.current_page_idx--;
                } else {
                    session.current_page_idx = max_pages - 1;
                }
                changed = true;
            }
            // ▶ / ➡: Next Page in Category
            else if (e.emoji_id == "▶" || e.emoji_id == "➡" || e.emoji_id == "➡️" || e.emoji_id == "▶️") {
                std::string cat = categories[session.current_category_idx];
                int cmds_size = commands_by_cat[cat].size();
                int max_pages = (cmds_size + 2) / 3;
                if (max_pages == 0) max_pages = 1;

                if (session.current_page_idx < max_pages - 1) {
                    session.current_page_idx++;
                } else {
                    session.current_page_idx = 0;
                }
                changed = true;
            }
            // 🟦: Quick Switch Prompt
            else if (e.emoji_id == "🟦" || e.emoji_id == "U+1F7E6") {
                {
                    std::lock_guard<std::mutex> prompt_lock(prompt_mutex_);
                    pending_prompts_[e.channel_id] = {e.id, e.user_id};
                }
                stoatpp::models::MessagePayload prompt_payload;
                prompt_payload.content = "<@" + e.user_id + "> enter a page number or category name to jump to:";
                prompt_payload.delete_after = 15;
                bot.send_message(e.channel_id, prompt_payload);
                
                lock.unlock();
                bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
                return;
            }

            if (changed) {
                help_sessions_[e.id] = session;
                lock.unlock();

                auto payload = render_help_page(bot, categories, commands_by_cat, session, prefix);
                bot.edit_message(e.channel_id, e.id, payload, [&bot, e](auto, bool) {
                    bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
                });
            } else {
                lock.unlock();
                bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
            }
        });

        bot.on_message([this, &bot](const stoatpp::events::Message& e) {
            std::unique_lock<std::mutex> prompt_lock(prompt_mutex_);
            auto it = pending_prompts_.find(e.channel_id);
            if (it != pending_prompts_.end() && it->second.user_id == e.author.id) {
                std::string target_msg_id = it->second.message_id;
                pending_prompts_.erase(it);
                prompt_lock.unlock();

                if (e.content.empty()) return;
                std::string input = e.content;
                std::transform(input.begin(), input.end(), input.begin(), ::tolower);

                std::unique_lock<std::mutex> lock(help_mutex_);
                auto session_it = help_sessions_.find(target_msg_id);
                if (session_it == help_sessions_.end()) return;

                auto session = session_it->second;

                auto all_cmds = bot.get_commands();
                std::unordered_map<std::string, std::vector<stoatpp::Command>> commands_by_cat;
                std::vector<std::string> categories;
                for (const auto& cmd : all_cmds) {
                    if (!e.server_id.empty() && g_db) {
                        std::string cmd_name = cmd.name;
                        std::string mod_name = cmd.category;
                        std::transform(cmd_name.begin(), cmd_name.end(), cmd_name.begin(), ::tolower);
                        std::transform(mod_name.begin(), mod_name.end(), mod_name.begin(), ::tolower);
                        if (g_db->is_disabled(e.server_id, cmd_name, mod_name)) {
                            continue;
                        }
                    }
                    std::string cat = cmd.category.empty() ? "General" : cmd.category;
                    commands_by_cat[cat].push_back(cmd);
                }
                for (const auto& pair : commands_by_cat) {
                    categories.push_back(pair.first);
                }
                std::sort(categories.begin(), categories.end(), [](const std::string& a, const std::string& b) {
                    if (a == "General") return true;
                    if (b == "General") return false;
                    return a < b;
                });
                for (auto& pair : commands_by_cat) {
                    std::sort(pair.second.begin(), pair.second.end(), [](const auto& a, const auto& b) {
                        return a.name < b.name;
                    });
                }
                if (categories.empty()) return;

                std::string prefix = bot.config().command_prefix;
                if (bot.config().prefix_resolver) {
                    auto resolved = bot.config().prefix_resolver(bot, e);
                    if (!resolved.empty()) {
                        prefix = resolved[0];
                    }
                }

                bool changed = false;
                try {
                    int page_num = std::stoi(input);
                    std::string cat = categories[session.current_category_idx];
                    int cmds_size = commands_by_cat[cat].size();
                    int max_pages = (cmds_size + 2) / 3;
                    if (max_pages == 0) max_pages = 1;
                    
                    if (page_num >= 1 && page_num <= max_pages) {
                        session.current_page_idx = page_num - 1;
                        changed = true;
                    } else {
                        stoatpp::models::MessagePayload err;
                        err.content = "invalid page number.";
                        err.delete_after = 5;
                        bot.send_message(e.channel_id, err);
                    }
                } catch (...) {
                    bool found = false;
                    for (size_t i = 0; i < categories.size(); ++i) {
                        if (to_lower(categories[i]) == input) {
                            session.current_category_idx = i;
                            session.current_page_idx = 0;
                            changed = true;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        stoatpp::models::MessagePayload err;
                        err.content = "invalid category name.";
                        err.delete_after = 5;
                        bot.send_message(e.channel_id, err);
                    }
                }

                if (changed) {
                    help_sessions_[target_msg_id] = session;
                    lock.unlock();
                    
                    bot.delete_message(e.channel_id, e.id);
                    auto payload = render_help_page(bot, categories, commands_by_cat, session, prefix);
                    bot.edit_message(e.channel_id, target_msg_id, payload);
                }
            }
        });
    }

private:
    static std::string to_lower(std::string s) {
        for (auto& c : s) c = std::tolower(static_cast<unsigned char>(c));
        return s;
    }

    stoatpp::models::MessagePayload render_help_page(
        stoatpp::cluster& bot,
        const std::vector<std::string>& categories,
        const std::unordered_map<std::string, std::vector<stoatpp::Command>>& commands_by_cat,
        const HelpSession& session,
        const std::string& prefix
    ) {
        std::string cat = categories[session.current_category_idx];
        const auto& cmds = commands_by_cat.at(cat);

        int commands_per_page = 3;
        int max_pages = (cmds.size() + commands_per_page - 1) / commands_per_page;
        if (max_pages == 0) max_pages = 1;

        int page = session.current_page_idx;
        if (page >= max_pages) page = 0;

        stoatpp::models::MessagePayload payload;
        payload.content = " ";
        
        stoatpp::models::Embed embed;
        embed.set_colour(0x5865F2)
             .set_title("bronx help — " + to_lower(cat) + " (page " + std::to_string(page + 1) + " of " + std::to_string(max_pages) + ")");

        std::string desc = "use double-arrows to switch cogs, and single-arrows to browse pages.\n\n";

        size_t start_idx = page * commands_per_page;
        for (size_t i = start_idx; i < start_idx + commands_per_page && i < cmds.size(); ++i) {
            const auto& cmd = cmds[i];
            desc += "• **" + prefix + cmd.name + "**";
            if (!cmd.aliases.empty()) {
                desc += " *(aliases: ";
                for (size_t a = 0; a < cmd.aliases.size(); ++a) {
                    desc += "`" + prefix + cmd.aliases[a] + "`" + (a + 1 < cmd.aliases.size() ? ", " : "");
                }
                desc += ")*";
            }
            desc += "\n";

            std::string syntax = prefix + cmd.name;
            if (!cmd.args.empty()) {
                for (const auto& arg : cmd.args) {
                    syntax += " " + arg;
                }
            } else if (!cmd.usage.empty()) {
                syntax = cmd.usage;
            }
            desc += "  *syntax:* `" + to_lower(syntax) + "`\n";

            if (!cmd.description.empty()) {
                desc += "  *description:* " + to_lower(cmd.description) + "\n";
            }

            if (!cmd.subcommands.empty()) {
                for (const auto& sub : cmd.subcommands) {
                    desc += "  - `" + to_lower(sub) + "`\n";
                }
            }
            desc += "\n";
        }

        desc += "--- \n**categories:** ";
        for (size_t i = 0; i < categories.size(); ++i) {
            std::string display_name = to_lower(categories[i]);
            if (i == static_cast<size_t>(session.current_category_idx)) {
                desc += "__**[" + display_name + "]**__";
            } else {
                desc += display_name;
            }
            if (i + 1 < categories.size()) desc += " · ";
        }

        embed.set_description(desc);
        payload.add_embed(embed);
        return payload;
    }
};

} // namespace bronx::cogs
