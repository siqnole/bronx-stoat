#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <random>
#include <chrono>
#include <thread>
#include <httplib.h>
#include <nlohmann/json.hpp>

namespace bronx::cogs {

class ImageBoardModule : public stoatpp::bot_module {
private:
    struct BooruPost {
        std::string file_url;
        std::string rating;
        std::string post_url;
        std::string tags;
        std::string id;
    };

    static std::string url_encode(const std::string& value) {
        std::ostringstream escaped;
        escaped << std::hex;
        for (char c : value) {
            if (std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
                escaped << c;
            } else {
                escaped << '%' << std::setw(2) << std::setfill('0') << ((int)(unsigned char)c);
            }
        }
        return escaped.str();
    }

    static bool is_channel_nsfw(stoatpp::cluster& bot, const std::string& channel_id) {
        auto chan_opt = bot.get_channel(channel_id);
        if (chan_opt) {
            return chan_opt->nsfw;
        }
        auto res = bot.rest().get_channel(channel_id);
        if (res.success()) {
            auto c = stoatpp::models::Channel::from_json(res.body);
            return c.nsfw;
        }
        return false;
    }

    static bool is_post_safe(const std::string& board, const std::string& rating) {
        std::string r = rating;
        std::transform(r.begin(), r.end(), r.begin(), ::tolower);
        if (board == "gelbooru" || board == "danbooru" || board == "safebooru") {
            return (r == "g" || r == "general" || r == "safe" || r.empty());
        } else { // konachan, yandere, e621 use "s" for safe
            return (r == "s" || r == "safe" || r == "g" || r == "general" || r.empty());
        }
    }

    static std::string process_tags(const std::vector<std::string>& args, bool is_nsfw, const std::string& board) {
        std::string tags_query = "";
        for (const auto& arg : args) {
            std::string normalized = arg;
            std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
            if (!is_nsfw && normalized.rfind("rating:", 0) == 0) {
                continue;
            }
            tags_query += arg + " ";
        }
        
        if (!tags_query.empty() && tags_query.back() == ' ') {
            tags_query.pop_back();
        }
        
        if (!is_nsfw) {
            if (!tags_query.empty()) {
                tags_query += " ";
            }
            if (board == "gelbooru" || board == "danbooru") {
                tags_query += "rating:general";
            } else if (board == "safebooru") {
                // Safebooru is always safe
            } else { // konachan, yandere, e621
                tags_query += "rating:safe";
            }
        }
        
        return url_encode(tags_query);
    }

    static std::vector<BooruPost> fetch_posts(const std::string& board, const std::string& query) {
        std::vector<BooruPost> results;
        httplib::Headers headers = {
            {"User-Agent", "BronxBot/1.0 (by RevoltUser)"}
        };

        if (board == "gelbooru") {
            httplib::Client cli("https://gelbooru.com");
            cli.set_connection_timeout(std::chrono::seconds(5));
            cli.set_read_timeout(std::chrono::seconds(5));
            cli.set_follow_location(true);
            std::string path = "/index.php?page=dapi&s=post&q=index&json=1&limit=100&tags=" + query;
            auto res = cli.Get(path, headers);
            if (res && res->status == 200) {
                try {
                    auto j = nlohmann::json::parse(res->body);
                    nlohmann::json posts;
                    if (j.is_array()) {
                        posts = j;
                    } else if (j.is_object() && j.contains("post") && j["post"].is_array()) {
                        posts = j["post"];
                    }
                    if (posts.is_array()) {
                        for (const auto& item : posts) {
                            BooruPost p;
                            p.file_url = item.value("file_url", "");
                            p.rating = item.value("rating", "");
                            
                            std::string id_str = "";
                            if (item.contains("id")) {
                                if (item["id"].is_number()) id_str = std::to_string(item["id"].get<int64_t>());
                                else if (item["id"].is_string()) id_str = item["id"].get<std::string>();
                            }
                            p.id = id_str;
                            p.post_url = "https://gelbooru.com/index.php?page=post&s=view&id=" + id_str;
                            p.tags = item.value("tags", "");
                            if (!p.file_url.empty()) {
                                results.push_back(p);
                            }
                        }
                    }
                } catch (...) {}
            }
        }
        else if (board == "danbooru") {
            httplib::Client cli("https://danbooru.donmai.us");
            cli.set_connection_timeout(std::chrono::seconds(5));
            cli.set_read_timeout(std::chrono::seconds(5));
            cli.set_follow_location(true);
            std::string path = "/posts.json?limit=100&tags=" + query;
            auto res = cli.Get(path, headers);
            if (res && res->status == 200) {
                try {
                    auto j = nlohmann::json::parse(res->body);
                    if (j.is_array()) {
                        for (const auto& item : j) {
                            BooruPost p;
                            p.file_url = item.value("file_url", "");
                            if (p.file_url.empty()) p.file_url = item.value("large_file_url", "");
                            if (p.file_url.empty()) p.file_url = item.value("preview_file_url", "");
                            
                            p.rating = item.value("rating", "");
                            
                            std::string id_str = "";
                            if (item.contains("id")) {
                                if (item["id"].is_number()) id_str = std::to_string(item["id"].get<int64_t>());
                                else if (item["id"].is_string()) id_str = item["id"].get<std::string>();
                            }
                            p.id = id_str;
                            p.post_url = "https://danbooru.donmai.us/posts/" + id_str;
                            p.tags = item.value("tag_string", "");
                            if (!p.file_url.empty()) {
                                results.push_back(p);
                            }
                        }
                    }
                } catch (...) {}
            }
        }
        else if (board == "safebooru") {
            httplib::Client cli("https://safebooru.org");
            cli.set_connection_timeout(std::chrono::seconds(5));
            cli.set_read_timeout(std::chrono::seconds(5));
            cli.set_follow_location(true);
            std::string path = "/index.php?page=dapi&s=post&q=index&json=1&limit=100&tags=" + query;
            auto res = cli.Get(path, headers);
            if (res && res->status == 200) {
                try {
                    auto j = nlohmann::json::parse(res->body);
                    if (j.is_array()) {
                        for (const auto& item : j) {
                            BooruPost p;
                            p.file_url = item.value("file_url", "");
                            if (p.file_url.empty()) {
                                std::string directory = item.value("directory", "");
                                std::string image = item.value("image", "");
                                if (!directory.empty() && !image.empty()) {
                                    p.file_url = "https://safebooru.org/images/" + directory + "/" + image;
                                }
                            }
                            p.rating = "safe";
                            
                            std::string id_str = "";
                            if (item.contains("id")) {
                                if (item["id"].is_number()) id_str = std::to_string(item["id"].get<int64_t>());
                                else if (item["id"].is_string()) id_str = item["id"].get<std::string>();
                            }
                            p.id = id_str;
                            p.post_url = "https://safebooru.org/index.php?page=post&s=view&id=" + id_str;
                            p.tags = item.value("tags", "");
                            if (!p.file_url.empty()) {
                                results.push_back(p);
                            }
                        }
                    }
                } catch (...) {}
            }
        }
        else if (board == "konachan") {
            httplib::Client cli("https://konachan.com");
            cli.set_connection_timeout(std::chrono::seconds(5));
            cli.set_read_timeout(std::chrono::seconds(5));
            cli.set_follow_location(true);
            std::string path = "/post.json?limit=100&tags=" + query;
            auto res = cli.Get(path, headers);
            if (res && res->status == 200) {
                try {
                    auto j = nlohmann::json::parse(res->body);
                    if (j.is_array()) {
                        for (const auto& item : j) {
                            BooruPost p;
                            p.file_url = item.value("file_url", "");
                            p.rating = item.value("rating", "");
                            
                            std::string id_str = "";
                            if (item.contains("id")) {
                                if (item["id"].is_number()) id_str = std::to_string(item["id"].get<int64_t>());
                                else if (item["id"].is_string()) id_str = item["id"].get<std::string>();
                            }
                            p.id = id_str;
                            p.post_url = "https://konachan.com/post/show/" + id_str;
                            p.tags = item.value("tags", "");
                            if (!p.file_url.empty()) {
                                results.push_back(p);
                            }
                        }
                    }
                } catch (...) {}
            }
        }
        else if (board == "yandere") {
            httplib::Client cli("https://yande.re");
            cli.set_connection_timeout(std::chrono::seconds(5));
            cli.set_read_timeout(std::chrono::seconds(5));
            cli.set_follow_location(true);
            std::string path = "/post.json?limit=100&tags=" + query;
            auto res = cli.Get(path, headers);
            if (res && res->status == 200) {
                try {
                    auto j = nlohmann::json::parse(res->body);
                    if (j.is_array()) {
                        for (const auto& item : j) {
                            BooruPost p;
                            p.file_url = item.value("file_url", "");
                            p.rating = item.value("rating", "");
                            
                            std::string id_str = "";
                            if (item.contains("id")) {
                                if (item["id"].is_number()) id_str = std::to_string(item["id"].get<int64_t>());
                                else if (item["id"].is_string()) id_str = item["id"].get<std::string>();
                            }
                            p.id = id_str;
                            p.post_url = "https://yande.re/post/show/" + id_str;
                            p.tags = item.value("tags", "");
                            if (!p.file_url.empty()) {
                                results.push_back(p);
                            }
                        }
                    }
                } catch (...) {}
            }
        }
        else if (board == "e621") {
            httplib::Client cli("https://e621.net");
            cli.set_connection_timeout(std::chrono::seconds(5));
            cli.set_read_timeout(std::chrono::seconds(5));
            cli.set_follow_location(true);
            std::string path = "/posts.json?limit=100&tags=" + query;
            auto res = cli.Get(path, headers);
            if (res && res->status == 200) {
                try {
                    auto j = nlohmann::json::parse(res->body);
                    if (j.is_object() && j.contains("posts") && j["posts"].is_array()) {
                        for (const auto& item : j["posts"]) {
                            BooruPost p;
                            if (item.contains("file") && item["file"].is_object()) {
                                p.file_url = item["file"].value("url", "");
                            }
                            p.rating = item.value("rating", "");
                            
                            std::string id_str = "";
                            if (item.contains("id")) {
                                if (item["id"].is_number()) id_str = std::to_string(item["id"].get<int64_t>());
                                else if (item["id"].is_string()) id_str = item["id"].get<std::string>();
                            }
                            p.id = id_str;
                            p.post_url = "https://e621.net/posts/" + id_str;
                            
                            std::string e621_tags = "";
                            if (item.contains("tags") && item["tags"].is_object()) {
                                for (auto& [cat, tag_array] : item["tags"].items()) {
                                    if (tag_array.is_array()) {
                                        for (auto& t : tag_array) {
                                            if (t.is_string()) {
                                                e621_tags += t.get<std::string>() + " ";
                                            }
                                        }
                                    }
                                }
                            }
                            p.tags = e621_tags;
                            if (!p.file_url.empty()) {
                                results.push_back(p);
                            }
                        }
                    }
                } catch (...) {}
            }
        }
        return results;
    }

    static void execute_search(stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::string& board, const std::vector<std::string>& tags_args) {
        bot.begin_typing(msg.channel_id);
        
        auto run_search = [&bot, msg, board, tags_args]() {
            bool nsfw = is_channel_nsfw(bot, msg.channel_id);
            
            std::string query = process_tags(tags_args, nsfw, board);
            auto posts = fetch_posts(board, query);
            
            std::vector<BooruPost> filtered_posts;
            for (const auto& post : posts) {
                if (nsfw || is_post_safe(board, post.rating)) {
                    filtered_posts.push_back(post);
                }
            }
            
            if (filtered_posts.empty()) {
                bot.send_message(msg.channel_id, "" + bronx::emj_no + " No posts found matching those tags or rating requirements on " + board + ".");
                return;
            }
            
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, filtered_posts.size() - 1);
            auto chosen_post = filtered_posts[dis(gen)];
            
            std::string display_tags = chosen_post.tags;
            if (display_tags.size() > 200) {
                display_tags = display_tags.substr(0, 197) + "...";
            }
            
            std::string color = "#f0a85d";
            std::string display_name = board;
            if (board == "gelbooru") { color = "#4e7cbf"; display_name = "Gelbooru"; }
            else if (board == "danbooru") { color = "#0073ff"; display_name = "Danbooru"; }
            else if (board == "safebooru") { color = "#7fa0c7"; display_name = "Safebooru"; }
            else if (board == "konachan") { color = "#ffb480"; display_name = "Konachan"; }
            else if (board == "yandere") { color = "#e24040"; display_name = "Yande.re"; }
            else if (board == "e621") { color = "#00549e"; display_name = "e621"; }
            
            stoatpp::models::MessagePayload payload;
            payload.content = " ";
            stoatpp::models::Embed embed;
            embed.set_title(display_name + " - Post #" + chosen_post.id)
                 .set_url(chosen_post.post_url)
                 .set_colour(color);
            
            std::string desc = "🏷️ **Tags**: `" + display_tags + "`\n\n";
            desc += "![](" + chosen_post.file_url + ")";
            embed.set_description(desc);
            
            payload.add_embed(embed);
            bot.send_message(msg.channel_id, payload);
        };
        
        std::thread(run_search).detach();
    }

public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "booru",
            {},
            "Search an image board (gelbooru, danbooru, safebooru, konachan, yandere, e621) with tags",
            "booru [board] [tags...]",
            {"[board]", "[tags...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (args.empty()) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Usage: `.booru <board> [tags...]` (Supported boards: `gelbooru`, `danbooru`, `safebooru`, `konachan`, `yandere`, `e621`)");
                    return;
                }
                
                std::string board = args[0];
                std::transform(board.begin(), board.end(), board.begin(), ::tolower);
                
                if (board != "gelbooru" && board != "danbooru" && board != "safebooru" &&
                    board != "konachan" && board != "yandere" && board != "e621") {
                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Unsupported board. Choose from: `gelbooru`, `danbooru`, `safebooru`, `konachan`, `yandere`, `e621`.");
                    return;
                }
                
                std::vector<std::string> tags_args;
                if (args.size() > 1) {
                    tags_args.assign(args.begin() + 1, args.end());
                }
                
                execute_search(bot, msg, board, tags_args);
            },
            "Images",
            0,
            3
        });

        bot.register_command({
            "gelbooru",
            {"gel"},
            "Search Gelbooru for images",
            "gelbooru [tags...]",
            {"[tags...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                execute_search(bot, msg, "gelbooru", args);
            },
            "Images",
            0,
            3
        });

        bot.register_command({
            "danbooru",
            {"dan"},
            "Search Danbooru for images",
            "danbooru [tags...]",
            {"[tags...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                execute_search(bot, msg, "danbooru", args);
            },
            "Images",
            0,
            3
        });

        bot.register_command({
            "safebooru",
            {"safe"},
            "Search Safebooru for images",
            "safebooru [tags...]",
            {"[tags...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                execute_search(bot, msg, "safebooru", args);
            },
            "Images",
            0,
            3
        });

        bot.register_command({
            "konachan",
            {"kona"},
            "Search Konachan for images",
            "konachan [tags...]",
            {"[tags...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                execute_search(bot, msg, "konachan", args);
            },
            "Images",
            0,
            3
        });

        bot.register_command({
            "yandere",
            {"yan"},
            "Search Yande.re for images",
            "yandere [tags...]",
            {"[tags...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                execute_search(bot, msg, "yandere", args);
            },
            "Images",
            0,
            3
        });

        bot.register_command({
            "e621",
            {"e6"},
            "Search e621 for images",
            "e621 [tags...]",
            {"[tags...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                execute_search(bot, msg, "e621", args);
            },
            "Images",
            0,
            3
        });
    }
};

} // namespace bronx::cogs
