#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include <chrono>

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
            "reply",
            {},
            "reply to your message to test the reply feature.",
            "",
            {"[text...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling reply...\n";
                std::string text = "testing replies feature! hello!";
                if (!args.empty()) {
                    text = "";
                    for (size_t i = 0; i < args.size(); ++i) {
                        text += args[i] + (i + 1 < args.size() ? " " : "");
                    }
                }
                bot.send_reply(msg, text, true);
            },
            "General"
        });

        bot.register_command({
            "test_typing",
            {},
            "test the typing indicator gateway event.",
            "",
            {},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling test_typing...\n";
                bot.begin_typing(msg.channel_id);
                std::thread([&bot, msg]() {
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    bot.end_typing(msg.channel_id);
                    bot.send_message(msg.channel_id, "typing indicator tested successfully");
                }).detach();
            },
            "General"
        });

        bot.register_command({
            "embed",
            {},
            "send a formatted test embed.",
            "",
            {},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling embed...\n";
                stoatpp::models::MessagePayload payload;
                payload.content = "here is a rich markdown embed example:";
                
                auto cached_author = bot.get_user(msg.author.id);
                std::string author_name = cached_author ? cached_author->username : msg.author.id;

                std::string desc = "### markdown showcase\n"
                                   "this is testing **bold**, *italics*, and `inline code`.\n\n"
                                   "#### code block:\n"
                                   "```cpp\n"
                                   "#include <stoatpp.h>\n"
                                   "int main() {\n"
                                   "    std::cout << \"hello stoat.chat!\\n\";\n"
                                   "}\n"
                                   "```\n"
                                   "#### lists:\n"
                                   "1. crockford base32 decoding\n"
                                   "2. non-blocking rest requests\n\n"
                                   "#### link:\n"
                                   "[visit the stoat.chat homepage](https://stoat.chat)\n\n"
                                   "--- \n"
                                   "requested by " + author_name;

                auto bot_user = bot.current_user();
                if (bot_user.avatar) {
                    desc += "\n\n![bot avatar](" + bot_user.avatar_url(bot.config().cdn_url) + ")";
                }

                nlohmann::json embed = {
                    {"title", "rich embed showcase"},
                    {"description", desc},
                    {"url", "https://github.com/stoat-chat"},
                    {"icon_url", msg.author.avatar_url(bot.config().cdn_url)},
                    {"colour", "#5865f2"}
                };
                
                payload.embeds.push_back(embed);
                bot.send_message(msg.channel_id, payload);
            },
            "General"
        });

        bot.register_command({
            "pages",
            {},
            "launch a generic paginated helper demonstration.",
            "",
            {},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling pages...\n";
                
                stoatpp::models::MessagePayload payload;
                payload.content = "--- page 1: introduction ---\nwelcome to the paginated helper guide.\nuse reaction arrows to navigate.\n\n(page 1 of 3)";
                
                bot.send_message(msg.channel_id, payload, [&bot, msg](stoatpp::models::Message res, bool success) {
                    if (success) {
                        {
                            std::lock_guard<std::mutex> lock(helpers::g_pages_mutex);
                            helpers::g_pages[res.id] = {0, 3, msg.author.id, "general"};
                        }
                        bot.react_to_message(msg.channel_id, res.id, "⬅");
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        bot.react_to_message(msg.channel_id, res.id, "➡");
                    }
                });
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
    }
};

} // namespace bronx::cogs
