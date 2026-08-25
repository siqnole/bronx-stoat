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
        std::string prefix;
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

                std::string prefix = bot.config().command_prefix;
                if (bot.config().prefix_resolver) {
                    auto resolved = bot.config().prefix_resolver(bot, msg);
                    bool found_clean = false;
                    for (const auto& r : resolved) {
                        if (r.rfind("<@", 0) != 0) {
                            prefix = r;
                            found_clean = true;
                            break;
                        }
                    }
                    if (!found_clean && !resolved.empty()) {
                        prefix = resolved[0];
                    }
                }

                auto run_help = [this, &bot, msg, args, prefix](std::optional<stoatpp::models::Member> mem_opt) {
                    auto has_cmd_permission = [&](const stoatpp::Command& cmd) -> bool {
                        if (cmd.required_permissions == 0) return true;
                        if (msg.server_id.empty()) return false;
                        if (!mem_opt) return false;
                        auto server = bot.get_server(msg.server_id);
                        if (!server) return false;
                        return bot.has_permission(*server, *mem_opt, cmd.required_permissions);
                    };

                    if (!args.empty()) {
                        std::string target_cmd_name = args[0];
                        std::transform(target_cmd_name.begin(), target_cmd_name.end(), target_cmd_name.begin(), [](unsigned char c) { return std::tolower(c); });

                        auto all_cmds = bot.get_commands();
                        std::optional<stoatpp::Command> matched_cmd;
                        bool is_owner = bot.is_owner(msg.author.id);
                        for (const auto& cmd : all_cmds) {
                            if (cmd.category == "Owner" && !is_owner) continue;
                            if (!has_cmd_permission(cmd)) continue;
                            std::string cmd_name = cmd.name;
                            std::transform(cmd_name.begin(), cmd_name.end(), cmd_name.begin(), [](unsigned char c) { return std::tolower(c); });
                            if (cmd_name == target_cmd_name) {
                                matched_cmd = cmd;
                                break;
                            }
                            for (const auto& alias : cmd.aliases) {
                                std::string alias_lower = alias;
                                std::transform(alias_lower.begin(), alias_lower.end(), alias_lower.begin(), [](unsigned char c) { return std::tolower(c); });
                                if (alias_lower == target_cmd_name) {
                                    matched_cmd = cmd;
                                    break;
                                }
                            }
                            if (matched_cmd) break;
                        }

                        if (!matched_cmd) {
                            // Fall back: check if the arg is a cog/category name
                            auto all_cmds_cog = bot.get_commands();
                            std::unordered_map<std::string, std::vector<stoatpp::Command>> commands_by_cat_cog;
                            std::vector<std::string> categories_cog;
                            for (const auto& cmd : all_cmds_cog) {
                                if (cmd.category == "Owner" && !is_owner) continue;
                                if (!has_cmd_permission(cmd)) continue;
                                if (!msg.server_id.empty() && g_db) {
                                    std::string cn = cmd.name, mn = cmd.category;
                                    std::transform(cn.begin(), cn.end(), cn.begin(), ::tolower);
                                    std::transform(mn.begin(), mn.end(), mn.begin(), ::tolower);
                                    if (g_db->is_disabled(msg.server_id, cn, mn)) continue;
                                }
                                std::string cat = cmd.category.empty() ? "General" : cmd.category;
                                commands_by_cat_cog[cat].push_back(cmd);
                            }
                            for (const auto& pair : commands_by_cat_cog)
                                categories_cog.push_back(pair.first);
                            std::sort(categories_cog.begin(), categories_cog.end(), [](const std::string& a, const std::string& b) {
                                if (a == "General") return true;
                                if (b == "General") return false;
                                return a < b;
                            });
                            for (auto& pair : commands_by_cat_cog)
                                std::sort(pair.second.begin(), pair.second.end(), [](const auto& a, const auto& b) { return a.name < b.name; });

                            int cog_idx = -1;
                            for (size_t i = 0; i < categories_cog.size(); ++i) {
                                if (to_lower(categories_cog[i]) == target_cmd_name) {
                                    cog_idx = static_cast<int>(i);
                                    break;
                                }
                            }

                            if (cog_idx >= 0) {
                                HelpSession cog_session;
                                cog_session.current_category_idx = cog_idx;
                                cog_session.current_page_idx = 0;
                                cog_session.user_id = msg.author.id;
                                cog_session.prefix = prefix;

                                auto cog_payload = render_help_page(bot, categories_cog, commands_by_cat_cog, cog_session, prefix);
                                cog_payload.set_interactions({"⏪", "◀", "🟦", "▶", "⏩"});

                                bot.send_message(msg.channel_id, cog_payload, [this, msg, prefix, cog_idx](stoatpp::models::Message res, bool success) {
                                    if (success) {
                                        std::lock_guard<std::mutex> lock(help_mutex_);
                                        help_sessions_[res.id] = {cog_idx, 0, msg.author.id, prefix};
                                    }
                                });
                                return;
                            }

                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " command or cog `" + args[0] + "` not found. type `" + prefix + "help` for the full list.");
                            return;
                        }

                        if (!msg.server_id.empty() && g_db) {
                            std::string cmd_name = matched_cmd->name;
                            std::string mod_name = matched_cmd->category;
                            std::transform(cmd_name.begin(), cmd_name.end(), cmd_name.begin(), [](unsigned char c) { return std::tolower(c); });
                            std::transform(mod_name.begin(), mod_name.end(), mod_name.begin(), [](unsigned char c) { return std::tolower(c); });
                            if (g_db->is_disabled(msg.server_id, cmd_name, mod_name)) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " command `" + matched_cmd->name + "` is currently disabled in this server.");
                                return;
                            }
                        }

                        stoatpp::models::MessagePayload payload;
                        payload.content = " ";
                        
                        stoatpp::models::Embed embed;
                        embed.set_colour(0x5865F2)
                             .set_title("help — " + to_lower(matched_cmd->name));

                        std::string desc = "";
                        if (!matched_cmd->description.empty()) {
                            desc += "**Description:** " + to_lower(matched_cmd->description) + "\n\n";
                        }

                        std::string syntax = prefix + matched_cmd->name;
                        if (!matched_cmd->args.empty()) {
                            for (const auto& arg : matched_cmd->args) {
                                syntax += " " + arg;
                            }
                        } else if (!matched_cmd->usage.empty()) {
                            syntax = matched_cmd->usage;
                        }
                        desc += "**Syntax:** `" + to_lower(syntax) + "`\n";

                        if (!matched_cmd->aliases.empty()) {
                            desc += "**Aliases:** ";
                            for (size_t a = 0; a < matched_cmd->aliases.size(); ++a) {
                                desc += "`" + prefix + to_lower(matched_cmd->aliases[a]) + "`" + (a + 1 < matched_cmd->aliases.size() ? ", " : "");
                            }
                            desc += "\n";
                        }

                        if (!matched_cmd->category.empty()) {
                            desc += "**Category:** " + to_lower(matched_cmd->category) + "\n";
                        }

                        if (matched_cmd->required_permissions != 0) {
                            desc += "**Permissions Required:** " + format_permission_mask(matched_cmd->required_permissions) + "\n";
                        }

                        if (matched_cmd->cooldown_seconds > 0) {
                            desc += "**Cooldown:** " + std::to_string(matched_cmd->cooldown_seconds) + " seconds\n";
                        }

                        if (!matched_cmd->subcommands.empty()) {
                            desc += "\n**Subcommands / Options:**\n";
                            for (const auto& sub : matched_cmd->subcommands) {
                                desc += "• `" + to_lower(sub) + "`\n";
                            }
                        }

                        embed.set_description(desc);
                        payload.add_embed(embed);
                        bot.send_message(msg.channel_id, payload);
                        return;
                    }

                    auto all_cmds = bot.get_commands();
                    std::unordered_map<std::string, std::vector<stoatpp::Command>> commands_by_cat;
                    std::vector<std::string> categories;
                    
                    bool is_owner = bot.is_owner(msg.author.id);
                    // Group commands by category
                    for (const auto& cmd : all_cmds) {
                        if (cmd.category == "Owner" && !is_owner) continue;
                        if (!has_cmd_permission(cmd)) continue;
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
                    session.prefix = prefix;

                    auto payload = render_help_page(bot, categories, commands_by_cat, session, prefix);
                    payload.set_interactions({"⏪", "◀", "🟦", "▶", "⏩"});

                    bot.send_message(msg.channel_id, payload, [this, msg, prefix](stoatpp::models::Message res, bool success) {
                        if (success) {
                            {
                                std::lock_guard<std::mutex> lock(help_mutex_);
                                help_sessions_[res.id] = {0, 0, msg.author.id, prefix};
                            }
                        }
                    });
                };

                if (!msg.server_id.empty()) {
                    bot.fetch_member(msg.server_id, msg.author.id, [run_help](stoatpp::models::Member mem, bool success) {
                        if (success) {
                            run_help(mem);
                        } else {
                            run_help(std::nullopt);
                        }
                    });
                } else {
                    run_help(std::nullopt);
                }
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

            std::string server_id;
            auto chan_opt = bot.get_channel(e.channel_id);
            if (chan_opt && chan_opt->server) {
                server_id = *chan_opt->server;
            }

            std::optional<stoatpp::models::Member> mem_opt;
            if (!server_id.empty()) {
                mem_opt = bot.get_member(server_id, session.user_id);
            }

            auto has_cmd_permission_react = [&](const stoatpp::Command& cmd) -> bool {
                if (cmd.required_permissions == 0) return true;
                if (server_id.empty()) return false;
                if (!mem_opt) return false;
                auto server = bot.get_server(server_id);
                if (!server) return false;
                return bot.has_permission(*server, *mem_opt, cmd.required_permissions);
            };

            // Gather categories and commands again
            auto all_cmds = bot.get_commands();
            std::unordered_map<std::string, std::vector<stoatpp::Command>> commands_by_cat;
            std::vector<std::string> categories;
            bool is_owner = bot.is_owner(e.user_id);
            for (const auto& cmd : all_cmds) {
                if (cmd.category == "Owner" && !is_owner) continue;
                if (!has_cmd_permission_react(cmd)) continue;
                // Check if disabled on the server
                if (!server_id.empty() && g_db) {
                    std::string cmd_name = cmd.name;
                    std::string mod_name = cmd.category;
                    std::transform(cmd_name.begin(), cmd_name.end(), cmd_name.begin(), ::tolower);
                    std::transform(mod_name.begin(), mod_name.end(), mod_name.begin(), ::tolower);
                    if (g_db->is_disabled(server_id, cmd_name, mod_name)) {
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

            std::string prefix = session.prefix.empty() ? bot.config().command_prefix : session.prefix;

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

                std::optional<stoatpp::models::Member> mem_opt;
                if (!e.server_id.empty()) {
                    mem_opt = bot.get_member(e.server_id, e.author.id);
                }

                auto has_cmd_permission_msg = [&](const stoatpp::Command& cmd) -> bool {
                    if (cmd.required_permissions == 0) return true;
                    if (e.server_id.empty()) return false;
                    if (!mem_opt) return false;
                    auto server = bot.get_server(e.server_id);
                    if (!server) return false;
                    return bot.has_permission(*server, *mem_opt, cmd.required_permissions);
                };

                auto all_cmds = bot.get_commands();
                std::unordered_map<std::string, std::vector<stoatpp::Command>> commands_by_cat;
                std::vector<std::string> categories;
                bool is_owner = bot.is_owner(e.author.id);
                for (const auto& cmd : all_cmds) {
                    if (cmd.category == "Owner" && !is_owner) continue;
                    if (!has_cmd_permission_msg(cmd)) continue;
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

                std::string prefix = session.prefix.empty() ? bot.config().command_prefix : session.prefix;

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

    static std::string format_permission_mask(int64_t mask) {
        if (mask == 0) return "none";
        std::vector<std::string> perms;
        if (mask & stoatpp::permissions::ManageServer) perms.push_back("manage server");
        if (mask & stoatpp::permissions::ManageChannels) perms.push_back("manage channels");
        if (mask & stoatpp::permissions::ManagePermissions) perms.push_back("manage permissions");
        if (mask & stoatpp::permissions::ManageRole) perms.push_back("manage roles");
        if (mask & stoatpp::permissions::ManageCustomisation) perms.push_back("manage customisation");
        if (mask & stoatpp::permissions::KickMembers) perms.push_back("kick members");
        if (mask & stoatpp::permissions::BanMembers) perms.push_back("ban members");
        if (mask & stoatpp::permissions::TimeoutMembers) perms.push_back("timeout members");
        if (mask & stoatpp::permissions::AssignRoles) perms.push_back("assign roles");
        if (mask & stoatpp::permissions::ChangeNickname) perms.push_back("change nickname");
        if (mask & stoatpp::permissions::ManageNicknames) perms.push_back("manage nicknames");
        if (mask & stoatpp::permissions::ManageAvatar) perms.push_back("manage avatar");
        if (mask & stoatpp::permissions::RemoveAvatars) perms.push_back("remove avatars");
        if (mask & stoatpp::permissions::ViewChannel) perms.push_back("view channel");
        if (mask & stoatpp::permissions::SendMessages) perms.push_back("send messages");
        if (mask & stoatpp::permissions::ManageMessages) perms.push_back("manage messages");
        if (mask & stoatpp::permissions::ManageWebhooks) perms.push_back("manage webhooks");
        if (mask & stoatpp::permissions::CreateInvites) perms.push_back("create invites");
        if (mask & stoatpp::permissions::EmbedLinks) perms.push_back("embed links");
        if (mask & stoatpp::permissions::UploadFiles) perms.push_back("upload files");
        if (mask & stoatpp::permissions::AddReactions) perms.push_back("add reactions");

        std::string result = "";
        for (size_t i = 0; i < perms.size(); ++i) {
            result += perms[i] + (i + 1 < perms.size() ? ", " : "");
        }
        return result;
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
             .set_title("help — " + to_lower(cat) + " (" + std::to_string(page + 1) + "/" + std::to_string(max_pages) + ")");

        std::string desc = "";

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
