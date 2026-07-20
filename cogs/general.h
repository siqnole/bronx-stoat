#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <random>

namespace bronx::cogs {

class GeneralModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.on_message_react([this, &bot](const stoatpp::events::MessageReact& e) {
            handle_poll_reaction(bot, e.channel_id, e.id);
        });

        bot.on_message_unreact([this, &bot](const stoatpp::events::MessageUnreact& e) {
            handle_poll_reaction(bot, e.channel_id, e.id);
        });

        bot.register_command({
            "ping",
            {},
            "measure rest and gateway round-trip latency.",
            "",
            {},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling ping...\n";
                auto start_time = std::chrono::steady_clock::now();
                
                auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();
                auto msg_sent_ms = stoatpp::utils::decode_ulid_timestamp(msg.id);
                auto immediate_ms = now_ms - msg_sent_ms;

                bot.send_message(msg.channel_id, "pong!", [start_time, &bot, msg, immediate_ms](auto res, bool success) {
                    if (success) {
                        auto end_time = std::chrono::steady_clock::now();
                        auto rest_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
                        auto gw_ms = bot.ping_latency();
                        
                        std::cout << "[bronx command] ping response sent. immediate: " << immediate_ms 
                                  << "ms, rest: " << rest_ms << "ms, gateway: " << gw_ms << "ms\n";
                        bot.edit_message(msg.channel_id, res.id, 
                            "pong! (immediate: " + std::to_string(immediate_ms) + "ms, gateway: " + std::to_string(gw_ms) + "ms, rest: " + std::to_string(rest_ms) + "ms)");
                    }
                });
            },
            "General"
        });

        bot.register_command({
            "uptime",
            {},
            "get the time since launch.",
            "",
            {},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling uptime...\n";
                auto now_unix = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();
                auto launch_unix = now_unix - bot.uptime();

                uint64_t total_seconds = bot.uptime();
                uint64_t days = total_seconds / 86400;
                uint64_t hours = (total_seconds % 86400) / 3600;
                uint64_t minutes = (total_seconds % 3600) / 60;
                uint64_t seconds = total_seconds % 60;

                std::string uptime_str = "";
                if (days > 0) uptime_str += std::to_string(days) + "d ";
                if (hours > 0 || days > 0) uptime_str += std::to_string(hours) + "h ";
                if (minutes > 0 || hours > 0 || days > 0) uptime_str += std::to_string(minutes) + "m ";
                uptime_str += std::to_string(seconds) + "s";

                bot.send_message(msg.channel_id, "⏱️ **uptime**: launch was " + stoatpp::utils::format_timestamp(launch_unix, "R") + "\n(`" + uptime_str + "`)");
            },
            "General"
        });

        bot.register_command({
            "search",
            {},
            "perform a message text search in this channel.",
            "",
            {"<query>"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling search...\n";
                if (args.empty()) {
                    bot.send_message(msg.channel_id, "please provide a search term.");
                    return;
                }

                std::string query_str = args[0];
                stoatpp::models::MessageSearchQuery query;
                query.query = query_str;
                query.limit = 5;

                std::thread([&bot, msg, query]() {
                    auto res = bot.rest().search_messages(msg.channel_id, query);
                    if (!res.success() || !res.body.is_array()) {
                        bot.send_message(msg.channel_id, "failed to search messages: " + res.error_message());
                        return;
                    }

                    std::string out = "search results for **" + *query.query + "**:\n";
                    for (const auto& item : res.body) {
                        auto m = stoatpp::models::Message::from_json(item);
                        auto cached_user = bot.get_user(m.author);
                        std::string author_name = cached_user ? cached_user->username : m.author;
                        out += "• **" + author_name + "**: " + (m.content ? *m.content : "") + "\n";
                    }

                    if (res.body.empty()) {
                        out += "no messages found.";
                    }

                    bot.send_message(msg.channel_id, out);
                }).detach();
            },
            "General"
        });

        bot.register_command({
            "permcheck",
            {"checkperms", "permissions"},
            "check what permissions the bot has and what is required for each module.",
            "",
            {},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling permcheck...\n";
                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id, "⚠️ This command can only be used in a server.");
                    return;
                }

                auto server_opt = bot.get_server(msg.server_id);
                if (!server_opt) {
                    bot.send_message(msg.channel_id, "⚠️ Failed to fetch server details.");
                    return;
                }

                bot.fetch_member(msg.server_id, bot.current_user().id, [&bot, msg, server_opt](stoatpp::models::Member bot_member, bool success) {
                    if (!success) {
                        bot.send_message(msg.channel_id, "⚠️ Failed to fetch bot member details.");
                        return;
                    }

                    // Definitions of features and their required permissions
                    struct PermRequirement {
                        std::string name;
                        int64_t mask;
                    };

                    struct ModuleConfig {
                        std::string module_name;
                        std::vector<PermRequirement> requirements;
                    };

                    std::vector<ModuleConfig> modules = {
                        {
                            "Reaction Roles",
                            {
                                {"Add Reactions", stoatpp::permissions::AddReactions},
                                {"Assign Roles (Manage Roles)", stoatpp::permissions::AssignRoles}
                            }
                        },
                        {
                            "Tickets",
                            {
                                {"View Channel", stoatpp::permissions::ViewChannel},
                                {"Send Messages", stoatpp::permissions::SendMessages},
                                {"Embed Links", stoatpp::permissions::EmbedLinks},
                                {"Upload Files", stoatpp::permissions::UploadFiles},
                                {"Add Reactions", stoatpp::permissions::AddReactions},
                                {"Manage Channels", stoatpp::permissions::ManageChannels},
                                {"Manage Permissions", stoatpp::permissions::ManagePermissions},
                                {"Manage Messages", stoatpp::permissions::ManageMessages}
                            }
                        },
                        {
                            "Anti-Raid",
                            {
                                {"Kick Members", stoatpp::permissions::KickMembers},
                                {"Ban Members", stoatpp::permissions::BanMembers},
                                {"Timeout Members", stoatpp::permissions::TimeoutMembers},
                                {"Manage Server", stoatpp::permissions::ManageServer}
                            }
                        },
                        {
                            "Logging",
                            {
                                {"View Channel", stoatpp::permissions::ViewChannel},
                                {"Send Messages", stoatpp::permissions::SendMessages},
                                {"Embed Links", stoatpp::permissions::EmbedLinks}
                            }
                        },
                        {
                            "Welcome/Leave",
                            {
                                {"Send Messages", stoatpp::permissions::SendMessages},
                                {"Assign Roles (Autorole)", stoatpp::permissions::AssignRoles}
                            }
                        },
                        {
                            "Moderation",
                            {
                                {"Kick Members", stoatpp::permissions::KickMembers},
                                {"Ban Members", stoatpp::permissions::BanMembers},
                                {"Timeout Members", stoatpp::permissions::TimeoutMembers},
                                {"Manage Messages", stoatpp::permissions::ManageMessages}
                            }
                        },
                        {
                            "General / Stats / Charting",
                            {
                                {"Send Messages", stoatpp::permissions::SendMessages},
                                {"Embed Links", stoatpp::permissions::EmbedLinks},
                                {"Upload Files", stoatpp::permissions::UploadFiles}
                            }
                        },
                        {
                            "Masquerade",
                            {
                                {"Masquerade", stoatpp::permissions::Masquerade}
                            }
                        }
                    };

                    stoatpp::models::MessagePayload payload;
                    payload.content = " ";

                    stoatpp::models::Embed embed;
                    embed.set_colour(0x5865F2)
                         .set_title("🛡️ Bot Permission Check");

                    std::string desc = "Here is a breakdown of the permissions I have in this server and which modules they affect:\n\n";

                    bool overall_perfect = true;

                    for (const auto& mod : modules) {
                        desc += "### " + mod.module_name + "\n";
                        bool module_ok = true;
                        for (const auto& req : mod.requirements) {
                            bool has_perm = bot.has_permission(*server_opt, bot_member, req.mask);
                            if (has_perm) {
                                desc += "✅ **" + req.name + "**\n";
                            } else {
                                desc += "❌ **" + req.name + "** *(missing)*\n";
                                module_ok = false;
                                overall_perfect = false;
                            }
                        }
                        desc += "\n";
                    }

                    if (overall_perfect) {
                        desc += "🎉 **All permissions are correctly configured!** Bronx has all the required access in this server.";
                    } else {
                        desc += "⚠️ **Warning:** Some permissions are missing. Bronx might not function correctly for modules marked with ❌.";
                    }

                    embed.set_description(desc);
                    payload.add_embed(embed);
                    bot.send_message(msg.channel_id, payload);
                });
            },
            "General"
        });

        bot.register_command({
            "support",
            {"bug", "feedback", "invite", "website", "report"},
            "show support server, website, docs, source code, and invite links.",
            "",
            {},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling support/invite...\n";
                stoatpp::models::MessagePayload payload;
                payload.content = " ";

                std::string desc = "**[Invite (Stoat)](https://stoat.chat/bot/01FZB4GBHDVYY6KT8JH4RBX4KR)** - Invite bronx to your Stoat server! :01KWKZDHDNYYFJYS6BHM5CW2V5:\n"
                                   "**[Invite (Discord)](https://discord.com/oauth2/authorize?client_id=1494385109648933045&permissions=8&integration_type=0&scope=bot+applications.commands)** - Invite bronx to your Discord server!\n\n"
                                   "**[Website](https://bronx.siqnole.dev)** - Learn more about Bronx\n"
                                   "**[Documentation](https://bronx.siqnole.dev/docs)** - Commands, features, and setup guides :01KWKZKZBSHJ85GFK12409YR10:\n"
                                   "**[Support Server](https://stt.gg/crBC07Ea)** - Join our community to get help, report bugs, or submit feedback\n"
                                   "**[Source Code](https://codeberg.org/siq/bronx-stoat)** - Check out our repository on Codeberg\n\n"
                                   "--- \n"
                                   "Thank you for supporting bronx!! :01KWKZKRW2J4HY0S2KRA1PNA04:";

                nlohmann::json embed = {
                    {"title", "help & support"},
                    {"description", desc},
                    {"colour", "#a29bfe"}
                };

                embed["icon_url"] = msg.author.avatar_url(bot.config().cdn_url);

                payload.embeds.push_back(embed);
                bot.send_message(msg.channel_id, payload);
            },
            "General"
        });

        bot.on_message_react([&bot](const stoatpp::events::MessageReact& e) {
            std::unique_lock<std::mutex> lock(helpers::g_pages_mutex);
            auto it = helpers::g_pages.find(e.id);
            if (it != helpers::g_pages.end()) {
                auto& session = it->second;
                if (e.user_id == bot.current_user().id) return;
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
                    std::string type = session.type;
                    lock.unlock();

                    stoatpp::models::MessagePayload payload;
                    if (type == "general") {
                        std::vector<std::string> pages = {
                            "--- page 1: introduction ---\nwelcome to the paginated helper guide.\nuse reaction arrows to navigate.",
                            "--- page 2: code examples ---\nhere is some sample code:\n`bot.send_message(channel_id, \"hello\");`",
                            "--- page 3: links & resources ---\nthank you for reading!\nvisit: https://developers.stoat.chat"
                        };
                        payload.content = pages[page] + "\n\n(page " + std::to_string(page + 1) + " of 3)";
                    }
                    bot.edit_message(e.channel_id, e.id, payload, [&bot, e](auto res, bool success) {
                        bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
                    });
                }
            }
        });

        bot.register_command({
            "poll",
            {},
            "create a poll with options and reactions.",
            "poll <title> <options...>",
            {"<title>", "<options...>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling poll...\n";
                
                // Get raw input after the command name
                std::string input = msg.content;
                std::string cmd_name = "poll";
                size_t pos = input.find(cmd_name);
                if (pos != std::string::npos) {
                    input = input.substr(pos + cmd_name.length());
                }
                
                size_t start = input.find_first_not_of(" \t\r\n");
                if (start == std::string::npos) {
                    bot.send_message(msg.channel_id, "⚠️ Usage: `.poll [title] [option1] [option2] ...`\n"
                                                     "You can use separators like `|`, `/`, `-`, or newlines. Custom emojis can be added like `:emoji: option`.");
                    return;
                }
                input = input.substr(start);
                
                std::string title = "Poll";
                std::vector<std::string> raw_options;
                
                if (input[0] == '"') {
                    size_t close_quote = input.find('"', 1);
                    if (close_quote != std::string::npos) {
                        title = input.substr(1, close_quote - 1);
                        std::string remaining = input.substr(close_quote + 1);
                        raw_options = split_poll_segments(remaining);
                    } else {
                        auto segments = split_poll_segments(input);
                        if (!segments.empty()) {
                            title = segments[0];
                            raw_options.assign(segments.begin() + 1, segments.end());
                        }
                    }
                } else {
                    auto segments = split_poll_segments(input);
                    if (!segments.empty()) {
                        title = segments[0];
                        raw_options.assign(segments.begin() + 1, segments.end());
                    }
                }
                
                if (raw_options.empty()) {
                    bot.send_message(msg.channel_id, "⚠️ Please specify at least one option. Example: `.poll \"Favorite day?\" Saturday | Sunday`");
                    return;
                }
                
                if (raw_options.size() > 10) {
                    bot.send_message(msg.channel_id, "⚠️ A poll can have a maximum of 10 options.");
                    return;
                }
                
                std::vector<PollOption> parsed_options;
                for (size_t i = 0; i < raw_options.size(); ++i) {
                    auto opt = parse_option(raw_options[i], i);
                    if (!opt.text.empty()) {
                        parsed_options.push_back(opt);
                    }
                }
                
                if (parsed_options.empty()) {
                    bot.send_message(msg.channel_id, "⚠️ No valid options found.");
                    return;
                }
                
                stoatpp::models::MessagePayload payload;
                payload.content = " ";
                
                stoatpp::models::Embed embed;
                embed.set_colour(0x6C5CE7);
                embed.set_title("📊 Poll: " + title);
                
                std::string desc = "";
                std::vector<std::string> reactions;
                for (const auto& opt : parsed_options) {
                    desc += opt.emoji + " " + opt.text + "\n\n";
                    reactions.push_back(clean_emoji(opt.emoji));
                }
                embed.set_description(desc);
                
                auto cached_author = bot.get_user(msg.author.id);
                std::string author_name = cached_author ? cached_author->username : msg.author.id;
                embed.set_icon_url(msg.author.avatar_url(bot.config().cdn_url));
                
                payload.add_embed(embed);
                payload.set_interactions(reactions, false);
                
                bot.send_message(msg.channel_id, payload);
            },
            "General"
        });

        bot.register_command({
            "dice",
            {"roll"},
            "roll one or more dice (e.g. .dice 6 or .dice 4d6 2d10).",
            "dice [dice_expression...]",
            {"[dice_expression...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::vector<std::string> dice_args = args;
                if (dice_args.empty()) {
                    dice_args.push_back("1d6"); // default to 1d6
                }
                
                struct DiceGroup {
                    std::string input;
                    int count = 0;
                    int sides = 0;
                    std::vector<int> rolls;
                    int sum = 0;
                };
                
                std::vector<DiceGroup> groups;
                int total_dice = 0;
                
                for (const auto& arg : dice_args) {
                    int count = 0;
                    int sides = 0;
                    if (!parse_dice_token(arg, count, sides)) {
                        stoatpp::models::MessagePayload err_payload;
                        err_payload.content = " ";
                        stoatpp::models::Embed err_embed;
                        err_embed.set_colour("#E5989B")
                                 .set_title("❌ Error")
                                 .set_description("Invalid dice expression: `" + arg + "`. Please use format like `6`, `d20`, or `4d6`.");
                        err_payload.add_embed(err_embed);
                        bot.send_message(msg.channel_id, err_payload);
                        return;
                    }
                    
                    total_dice += count;
                    if (total_dice > 100) {
                        stoatpp::models::MessagePayload err_payload;
                        err_payload.content = " ";
                        stoatpp::models::Embed err_embed;
                        err_embed.set_colour("#E5989B")
                                 .set_title("❌ Error")
                                 .set_description("You cannot roll more than 100 dice at once.");
                        err_payload.add_embed(err_embed);
                        bot.send_message(msg.channel_id, err_payload);
                        return;
                    }
                    
                    DiceGroup group;
                    group.input = arg;
                    group.count = count;
                    group.sides = sides;
                    groups.push_back(group);
                }
                
                // Perform rolls
                static thread_local std::mt19937 gen(std::random_device{}());
                int grand_total = 0;
                
                for (auto& group : groups) {
                    std::uniform_int_distribution<int> dis(1, group.sides);
                    for (int i = 0; i < group.count; ++i) {
                        int roll = dis(gen);
                        group.rolls.push_back(roll);
                        group.sum += roll;
                    }
                    grand_total += group.sum;
                }
                
                // Format description
                std::string desc = "🎲 **Dice Roll Results**\n\n";
                for (const auto& group : groups) {
                    std::string normalized_input = std::to_string(group.count) + "d" + std::to_string(group.sides);
                    desc += "**" + normalized_input + "** (Sum: **" + std::to_string(group.sum) + "**)\n";
                    desc += "Rolls: ";
                    for (size_t i = 0; i < group.rolls.size(); ++i) {
                        desc += "`" + std::to_string(group.rolls[i]) + "`";
                        if (i + 1 < group.rolls.size()) {
                            desc += ", ";
                        }
                    }
                    desc += "\n\n";
                }
                
                desc += "━━━━━━━━━━━━━━━━━━━━━━\n";
                desc += "**Grand Total**: 🏆 **" + std::to_string(grand_total) + "**";
                
                stoatpp::models::MessagePayload payload;
                payload.content = " ";
                
                stoatpp::models::Embed embed;
                embed.set_colour("#a29bfe")
                     .set_title("Dice Roller")
                     .set_description(desc);
                     
                payload.add_embed(embed);
                bot.send_message(msg.channel_id, payload);
            },
            "General"
        });
    }

private:
    void handle_poll_reaction(stoatpp::cluster& bot, const std::string& channel_id, const std::string& message_id) {
        // Fetch the message from Revolt to get the current reaction lists
        auto res = bot.rest().get_message(channel_id, message_id);
        if (!res.success()) return;
        
        auto msg = stoatpp::models::Message::from_json(res.body);
        
        // 1. Verify message author is the bot itself
        if (msg.author != bot.current_user().id) return;
        
        // 2. Verify there is at least one embed
        if (!msg.embeds.is_array() || msg.embeds.empty()) return;
        
        auto embed_json = msg.embeds[0];
        if (!embed_json.contains("title") || !embed_json["title"].is_string()) return;
        
        std::string title = embed_json["title"].get<std::string>();
        // Verify title starts with 📊 Poll:
        if (title.rfind("📊 Poll: ", 0) != 0) return;
        
        if (!embed_json.contains("description") || !embed_json["description"].is_string()) return;
        std::string embed_desc = embed_json["description"].get<std::string>();
        
        // 3. Parse options from the embed description
        std::vector<PollOption> parsed_options;
        std::stringstream ss(embed_desc);
        std::string line;
        while (std::getline(ss, line)) {
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            line = line.substr(start);
            size_t end = line.find_last_not_of(" \t\r\n");
            line = line.substr(0, end + 1);
            
            if (line.empty() || line.front() == '[') continue;
            
            size_t space_pos = line.find(' ');
            if (space_pos != std::string::npos) {
                PollOption opt;
                opt.emoji = line.substr(0, space_pos);
                opt.text = line.substr(space_pos + 1);
                parsed_options.push_back(opt);
            }
        }
        
        if (parsed_options.empty()) return;
        
        // 4. Count reactions
        nlohmann::json raw_reactions = nlohmann::json::object();
        if (msg.raw.contains("reactions") && msg.raw["reactions"].is_object()) {
            raw_reactions = msg.raw["reactions"];
        }
        
        int total_votes = 0;
        std::vector<int> option_votes;
        for (const auto& opt : parsed_options) {
            int votes = 0;
            std::string emoji_key = clean_emoji(opt.emoji);
            if (raw_reactions.contains(emoji_key) && raw_reactions[emoji_key].is_array()) {
                for (const auto& u : raw_reactions[emoji_key]) {
                    if (u.is_string() && u.get<std::string>() != bot.current_user().id) {
                        votes++;
                    }
                }
            }
            option_votes.push_back(votes);
            total_votes += votes;
        }
        
        // 5. Generate updated description
        std::string desc = "";
        for (size_t i = 0; i < parsed_options.size(); ++i) {
            const auto& opt = parsed_options[i];
            int votes = option_votes[i];
            
            desc += opt.emoji + " " + opt.text + "\n";
            if (total_votes > 0) {
                int percentage = (votes * 100) / total_votes;
                int filled = percentage / 10;
                std::string bar = "[";
                for (int b = 0; b < 10; ++b) {
                    if (b < filled) bar += "█";
                    else bar += "░";
                }
                bar += "] " + std::to_string(votes) + " vote" + (votes == 1 ? "" : "s") + " (" + std::to_string(percentage) + "%)";
                desc += bar + "\n";
            }
            desc += "\n";
        }
        
        // Trim trailing newlines
        while (!desc.empty() && (desc.back() == '\n' || desc.back() == '\r')) {
            desc.pop_back();
        }
        
        // 6. Build the new embed
        stoatpp::models::Embed updated_embed;
        updated_embed.set_title(title);
        updated_embed.set_description(desc);
        if (embed_json.contains("colour") && embed_json["colour"].is_string()) {
            updated_embed.set_colour(embed_json["colour"].get<std::string>());
        } else {
            updated_embed.set_colour(0x6C5CE7);
        }
        if (embed_json.contains("icon_url") && embed_json["icon_url"].is_string()) {
            updated_embed.set_icon_url(embed_json["icon_url"].get<std::string>());
        }
        
        stoatpp::models::MessagePayload payload;
        payload.content = " ";
        payload.add_embed(updated_embed);
        
        std::vector<std::string> reactions;
        for (const auto& opt : parsed_options) {
            reactions.push_back(clean_emoji(opt.emoji));
        }
        payload.set_interactions(reactions, false);
        
        bot.edit_message(channel_id, message_id, payload);
    }

    static bool parse_dice_token(const std::string& token, int& count, int& sides) {
        if (token.empty()) return false;
        
        // Find 'd' or 'D'
        size_t d_pos = token.find('d');
        if (d_pos == std::string::npos) {
            d_pos = token.find('D');
        }
        
        if (d_pos == std::string::npos) {
            // No 'd' found, check if it's a single number
            for (char c : token) {
                if (!std::isdigit(static_cast<unsigned char>(c))) return false;
            }
            try {
                count = 1;
                long long val = std::stoll(token);
                if (val < 1 || val > 1000000) return false;
                sides = static_cast<int>(val);
                return true;
            } catch (...) {
                return false;
            }
        }
        
        // Split by 'd'
        std::string prefix = token.substr(0, d_pos);
        std::string suffix = token.substr(d_pos + 1);
        
        if (suffix.empty()) return false;
        for (char c : suffix) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        }
        
        if (!prefix.empty()) {
            for (char c : prefix) {
                if (!std::isdigit(static_cast<unsigned char>(c))) return false;
            }
            try {
                long long count_val = std::stoll(prefix);
                if (count_val < 1 || count_val > 100) return false;
                count = static_cast<int>(count_val);
            } catch (...) {
                return false;
            }
        } else {
            count = 1;
        }
        
        try {
            long long sides_val = std::stoll(suffix);
            if (sides_val < 1 || sides_val > 1000000) return false;
            sides = static_cast<int>(sides_val);
        } catch (...) {
            return false;
        }
        
        return true;
    }

    static std::string get_first_utf8_char(const std::string& str, size_t& len) {
        if (str.empty()) { len = 0; return ""; }
        unsigned char c = str[0];
        if (c < 0x80) { len = 1; return str.substr(0, 1); }
        if ((c & 0xE0) == 0xC0) { len = 2; return str.substr(0, 2); }
        if ((c & 0xF0) == 0xE0) { len = 3; return str.substr(0, 3); }
        if ((c & 0xF8) == 0xF0) { len = 4; return str.substr(0, 4); }
        len = 1;
        return str.substr(0, 1);
    }

    static uint32_t decode_utf8(const std::string& utf8) {
        if (utf8.empty()) return 0;
        unsigned char c1 = utf8[0];
        if (c1 < 0x80) return c1;
        if (utf8.size() >= 2 && (c1 & 0xE0) == 0xC0) {
            return ((c1 & 0x1F) << 6) | (utf8[1] & 0x3F);
        }
        if (utf8.size() >= 3 && (c1 & 0xF0) == 0xE0) {
            return ((c1 & 0x0F) << 12) | ((utf8[1] & 0x3F) << 6) | (utf8[2] & 0x3F);
        }
        if (utf8.size() >= 4 && (c1 & 0xF0) == 0xF0) {
            return ((c1 & 0x07) << 18) | ((utf8[1] & 0x3F) << 12) | ((utf8[2] & 0x3F) << 6) | (utf8[3] & 0x3F);
        }
        return 0;
    }

    static bool is_emoji_codepoint(uint32_t cp) {
        if (cp >= 0x1F000 && cp <= 0x1FFFF) return true;
        if (cp >= 0x2600 && cp <= 0x27BF) return true;
        if (cp >= 0x2300 && cp <= 0x23FF) return true;
        if (cp >= 0x2B00 && cp <= 0x2BFF) return true;
        if (cp >= 0x1F1E0 && cp <= 0x1F1FF) return true;
        return false;
    }

    struct PollOption {
        std::string emoji;
        std::string text;
    };

    static std::string clean_emoji(const std::string& emoji_arg) {
        std::string cleaned = emoji_arg;
        std::string pua = "\xee\x83\xa6";
        size_t pos;
        while ((pos = cleaned.find(pua)) != std::string::npos) {
            cleaned.erase(pos, pua.length());
        }
        if (cleaned.size() >= 28 && cleaned.front() == ':' && cleaned.back() == ':') {
            std::string inner = cleaned.substr(1, cleaned.size() - 2);
            if (inner.size() == 26 && inner.find_first_not_of("0123456789ABCDEFGHJKMNPQRSTVWXYZabcdefghjkmnpqrstvwxyz") == std::string::npos) {
                return inner;
            }
        }
        return cleaned;
    }

    static std::string clean_option_text(const std::string& text) {
        if (text.empty()) return "";
        size_t start = 0;
        while (start < text.size()) {
            char c = text[start];
            if (c == ' ' || c == '\t' || c == '-' || c == '*') {
                start++;
            } else if (text.compare(start, 3, "•") == 0) {
                start += 3;
            } else {
                break;
            }
        }
        
        size_t end = text.size();
        while (end > start) {
            char c = text[end - 1];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                end--;
            } else {
                break;
            }
        }
        
        if (start >= end) return "";
        return text.substr(start, end - start);
    }

    static PollOption parse_option(const std::string& raw_text, size_t default_index) {
        std::string cleaned = clean_option_text(raw_text);
        PollOption opt;
        
        std::vector<std::string> default_emojis = {
            "1⃣", "2⃣", "3⃣", "4⃣", "5⃣", "6⃣", "7⃣", "8⃣", "9⃣", "🔟"
        };
        opt.emoji = (default_index < default_emojis.size()) ? default_emojis[default_index] : "❓";
        opt.text = cleaned;
        
        if (cleaned.empty()) return opt;
        
        if (cleaned[0] == ':') {
            size_t next_colon = cleaned.find(':', 1);
            if (next_colon != std::string::npos && next_colon > 1) {
                opt.emoji = cleaned.substr(0, next_colon + 1);
                opt.text = clean_option_text(cleaned.substr(next_colon + 1));
                return opt;
            }
        }
        
        size_t utf8_len = 0;
        std::string first_char = get_first_utf8_char(cleaned, utf8_len);
        if (utf8_len > 0) {
            uint32_t cp = decode_utf8(first_char);
            if (is_emoji_codepoint(cp)) {
                opt.emoji = first_char;
                opt.text = clean_option_text(cleaned.substr(utf8_len));
                return opt;
            }
        }
        
        return opt;
    }

    static std::vector<std::string> split_poll_segments(const std::string& input) {
        std::vector<std::string> segments;
        std::string current;
        for (size_t i = 0; i < input.size(); ) {
            char c = input[i];
            bool is_sep = false;
            size_t sep_len = 1;
            
            if (c == '\n') {
                is_sep = true;
            } else if (c == '|') {
                is_sep = true;
            } else if (c == '/') {
                bool is_url = false;
                if (i >= 4 && input.substr(i - 4, 4) == "http") is_url = true;
                if (i >= 5 && input.substr(i - 5, 5) == "https") is_url = true;
                if (!is_url) {
                    is_sep = true;
                }
            } else if (c == '-') {
                bool pre_space = (i == 0 || input[i - 1] == ' ' || input[i - 1] == '\t' || input[i - 1] == '\n');
                bool post_space = (i + 1 == input.size() || input[i + 1] == ' ' || input[i + 1] == '\t' || input[i + 1] == '\n');
                if (pre_space && post_space) {
                    is_sep = true;
                }
            }
            
            if (is_sep) {
                size_t s = current.find_first_not_of(" \t\r\n");
                if (s != std::string::npos) {
                    size_t e = current.find_last_not_of(" \t\r\n");
                    segments.push_back(current.substr(s, e - s + 1));
                }
                current.clear();
                i += sep_len;
                continue;
            }
            
            bool is_emoji_split = false;
            size_t emoji_len = 0;
            
            if (c == ':') {
                size_t next_colon = input.find(':', i + 1);
                if (next_colon != std::string::npos && next_colon > i + 1) {
                    emoji_len = next_colon - i + 1;
                    is_emoji_split = true;
                }
            }
            
            if (!is_emoji_split) {
                size_t utf8_len = 0;
                std::string first_char = get_first_utf8_char(input.substr(i), utf8_len);
                if (utf8_len > 0) {
                    uint32_t cp = decode_utf8(first_char);
                    if (is_emoji_codepoint(cp)) {
                        emoji_len = utf8_len;
                        is_emoji_split = true;
                    }
                }
            }
            
            if (is_emoji_split && emoji_len > 0) {
                size_t non_ws = current.find_first_not_of(" \t\r\n");
                if (non_ws != std::string::npos) {
                    size_t e = current.find_last_not_of(" \t\r\n");
                    segments.push_back(current.substr(non_ws, e - non_ws + 1));
                    current.clear();
                }
                
                current += input.substr(i, emoji_len);
                i += emoji_len;
            } else {
                current += c;
                i++;
            }
        }
        
        size_t s = current.find_first_not_of(" \t\r\n");
        if (s != std::string::npos) {
            size_t e = current.find_last_not_of(" \t\r\n");
            segments.push_back(current.substr(s, e - s + 1));
        }
        
        return segments;
    }
};

} // namespace bronx::cogs
