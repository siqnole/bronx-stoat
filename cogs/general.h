#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

namespace bronx::cogs {

class GeneralModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
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
            {"bug", "feedback", "invite"},
            "show support server, website, docs, source code, and invite links.",
            "",
            {},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling support/invite...\n";
                stoatpp::models::MessagePayload payload;
                payload.content = " ";

                std::string desc = "**[Invite](https://stoat.chat/bot/01FZB4GBHDVYY6KT8JH4RBX4KR)** - Invite bronx to your own server! :01KWKZDHDNYYFJYS6BHM5CW2V5:\n\n"
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
                for (const auto& opt : parsed_options) {
                    desc += opt.emoji + " " + opt.text + "\n\n";
                }
                embed.set_description(desc);
                
                auto cached_author = bot.get_user(msg.author.id);
                std::string author_name = cached_author ? cached_author->username : msg.author.id;
                embed.set_icon_url(msg.author.avatar_url(bot.config().cdn_url));
                
                payload.add_embed(embed);
                
                bot.send_message(msg.channel_id, payload, [&bot, msg, parsed_options](stoatpp::models::Message res, bool success) {
                    if (success) {
                        std::thread([&bot, msg, res, parsed_options]() {
                            for (const auto& opt : parsed_options) {
                                bot.react_to_message(msg.channel_id, res.id, clean_emoji(opt.emoji));
                                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                            }
                        }).detach();
                    }
                });
            },
            "General"
        });
    }

private:
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
        if (emoji_arg.size() >= 28 && emoji_arg.front() == ':' && emoji_arg.back() == ':') {
            std::string inner = emoji_arg.substr(1, emoji_arg.size() - 2);
            if (inner.size() == 26 && inner.find_first_not_of("0123456789ABCDEFGHJKMNPQRSTVWXYZabcdefghjkmnpqrstvwxyz") == std::string::npos) {
                return inner;
            }
        }
        return emoji_arg;
    }

    static std::string clean_option_text(const std::string& text) {
        if (text.empty()) return "";
        size_t start = 0;
        while (start < text.size()) {
            char c = text[start];
            if (c == ' ' || c == '\t' || c == '-' || c == '*' || c == '•') {
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
        
        std::vector<std::string> default_emojis = {"1️⃣", "2️⃣", "3️⃣", "4️⃣", "5️⃣", "6️⃣", "7️⃣", "8️⃣", "9️⃣"};
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
