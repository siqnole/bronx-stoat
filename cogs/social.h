#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "command_config.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <algorithm>
#include <vector>
#include <string>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>
#include <regex>

namespace bronx::cogs {

struct SocialCheckState {
    std::atomic<int> completed{0};
    std::atomic<int> total_new_posted{0};
    std::vector<std::string> errors;
    std::mutex check_mutex;
};

// Help helper
inline void send_social_help(stoatpp::cluster& bot, const std::string& channel_id, const std::string& platform, const std::string& pfx) {
    std::string pl_cap = platform;
    pl_cap[0] = std::toupper(pl_cap[0]);
    bot.send_message(channel_id,
        "📣 **" + pl_cap + " Feed Watcher Commands:**\n"
        "• `" + pfx + platform + " add <channel_handle>` - Add a new channel to follow (defaults to current channel).\n"
        "• `" + pfx + platform + " remove <sub_id>` - Stop following a channel.\n"
        "• `" + pfx + platform + " target <sub_id> <#channel>` - Set target channel(s) (comma-separated for multiple).\n"
        "• `" + pfx + platform + " events <sub_id> <events...>` - Choose events (comma-separated: `live,vod,post`).\n"
        "• `" + pfx + platform + " list` - List followed channels.\n"
        "• `" + pfx + platform + " test <sub_id> [event_type]` - Test formatting.\n"
        "• `" + pfx + platform + " edit <sub_id> <event_type> <key:val...>` - Customize embed fields (keys: `title`, `description`, `color`, `content`, `image`).\n"
        "• `" + pfx + platform + " json <sub_id> <event_type> <json>` - Import custom embed JSON.\n"
        "• `" + pfx + platform + " reset <sub_id> <event_type>` - Reset embed config to default."
    );
}

// Global checking sets to prevent overlapping checks
inline std::mutex g_social_checking_mutex;
inline std::unordered_set<int> g_social_checking_ids;

class SocialModule : public stoatpp::bot_module {
private:
    static std::string format_placeholders(std::string text, const std::string& author, const std::string& title, const std::string& url, const std::string& desc) {
        auto replace_all = [](std::string& str, const std::string& from, const std::string& to) {
            size_t start_pos = 0;
            while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
                str.replace(start_pos, from.length(), to);
                start_pos += to.length();
            }
        };
        replace_all(text, "{author}", author);
        replace_all(text, "{title}", title);
        replace_all(text, "{url}", url);
        replace_all(text, "{description}", desc);
        replace_all(text, "{desc}", desc);
        return text;
    }

    static std::unordered_map<std::string, std::string> parse_pairs(const std::string& text) {
        std::unordered_map<std::string, std::string> pairs;
        std::vector<std::string> keys = {"title:", "description:", "desc:", "colour:", "color:", "image:", "content:"};
        
        struct KeyPos {
            std::string key;
            size_t pos;
        };
        std::vector<KeyPos> occurrences;
        for (const auto& key : keys) {
            size_t pos = 0;
            while ((pos = text.find(key, pos)) != std::string::npos) {
                if (pos == 0 || std::isspace(text[pos - 1])) {
                    occurrences.push_back({key, pos});
                }
                pos += key.length();
            }
        }
        std::sort(occurrences.begin(), occurrences.end(), [](const KeyPos& a, const KeyPos& b) {
            return a.pos < b.pos;
        });
        for (size_t i = 0; i < occurrences.size(); ++i) {
            size_t start_val = occurrences[i].pos + occurrences[i].key.length();
            size_t end_val = (i + 1 < occurrences.size()) ? occurrences[i + 1].pos : text.length();
            std::string val = text.substr(start_val, end_val - start_val);
            val.erase(0, val.find_first_not_of(" \t\r\n"));
            val.erase(val.find_last_not_of(" \t\r\n") + 1);
            if (!val.empty() && val.back() == ',') {
                val.pop_back();
                val.erase(val.find_last_not_of(" \t\r\n") + 1);
            }
            std::string normalized_key = occurrences[i].key;
            normalized_key.pop_back(); // remove colon
            if (normalized_key == "desc") normalized_key = "description";
            if (normalized_key == "colour") normalized_key = "color";
            pairs[normalized_key] = val;
        }
        return pairs;
    }

    static std::string get_raw_trailing_args(const std::string& content, const std::string& prefix, const std::string& platform, const std::string& subcommand) {
        std::string search = prefix + platform;
        std::string lower_content = content;
        std::transform(lower_content.begin(), lower_content.end(), lower_content.begin(), ::tolower);
        std::string lower_search = search;
        std::transform(lower_search.begin(), lower_search.end(), lower_search.begin(), ::tolower);
        
        size_t pos = lower_content.find(lower_search);
        if (pos == std::string::npos) return "";
        pos += lower_search.length();
        while (pos < content.length() && std::isspace(content[pos])) pos++;

        std::string lower_sub = subcommand;
        std::transform(lower_sub.begin(), lower_sub.end(), lower_sub.begin(), ::tolower);
        
        if (pos + lower_sub.length() <= content.length() && lower_content.compare(pos, lower_sub.length(), lower_sub) == 0) {
            pos += lower_sub.length();
            while (pos < content.length() && std::isspace(content[pos])) pos++;
            return content.substr(pos);
        }
        return "";
    }

    // Resolve Twitch Login to User ID and get status
    static void check_twitch(const std::string& handle, std::function<void(bool live, std::string title, std::string url, std::string game, std::string thumbnail, std::string err)> cb) {
        std::thread([handle, cb]() {
            // Fill in client ID and access token. 
            // In a real setup, we use Client ID "wgd4smtfavychud7smmql4xg4o7k9v" and obtain App Token.
            // Let's implement Twitch OAuth2 Client Credentials flow first!
            const char* secret_env = std::getenv("TWITCH_CLIENT_SECRET");
            std::string client_secret = secret_env ? secret_env : "";
            std::string client_id = "wgd4smtfavychud7smmql4xg4o7k9v";

            if (client_secret.empty()) {
                cb(false, "", "", "", "", "TWITCH_CLIENT_SECRET env var is not configured.");
                return;
            }

            // Get Twitch OAuth token
            httplib::Client auth_cli("https://id.twitch.tv");
            auth_cli.set_connection_timeout(std::chrono::seconds(5));
            auth_cli.set_read_timeout(std::chrono::seconds(5));

            httplib::Params params;
            params.emplace("client_id", client_id);
            params.emplace("client_secret", client_secret);
            params.emplace("grant_type", "client_credentials");

            auto auth_res = auth_cli.Post("/oauth2/token", params);
            if (!auth_res || auth_res->status != 200) {
                cb(false, "", "", "", "", "Failed to authenticate with Twitch API (HTTP " + std::to_string(auth_res ? auth_res->status : 0) + ")");
                return;
            }

            std::string token;
            try {
                auto j = nlohmann::json::parse(auth_res->body);
                token = j.value("access_token", "");
            } catch (...) {
                cb(false, "", "", "", "", "Failed to parse Twitch auth response");
                return;
            }

            // Call Twitch helix/streams
            httplib::Client api_cli("https://api.twitch.tv");
            api_cli.set_connection_timeout(std::chrono::seconds(5));
            api_cli.set_read_timeout(std::chrono::seconds(5));

            httplib::Headers headers = {
                { "Client-Id", client_id },
                { "Authorization", "Bearer " + token }
            };

            auto api_res = api_cli.Get("/helix/streams?user_login=" + handle, headers);
            if (!api_res || api_res->status != 200) {
                cb(false, "", "", "", "", "Failed to fetch stream details from Twitch (HTTP " + std::to_string(api_res ? api_res->status : 0) + ")");
                return;
            }

            try {
                auto j = nlohmann::json::parse(api_res->body);
                if (j.contains("data") && j["data"].is_array() && !j["data"].empty()) {
                    auto stream = j["data"][0];
                    std::string title = stream.value("title", "No Title");
                    std::string game = stream.value("game_name", "Unknown Game");
                    std::string thumbnail = stream.value("thumbnail_url", "");
                    // Replace {width}x{height} with standard size
                    size_t w_pos = thumbnail.find("{width}");
                    if (w_pos != std::string::npos) {
                        thumbnail.replace(w_pos, 7, "1280");
                    }
                    size_t h_pos = thumbnail.find("{height}");
                    if (h_pos != std::string::npos) {
                        thumbnail.replace(h_pos, 8, "720");
                    }
                    cb(true, title, "https://twitch.tv/" + handle, game, thumbnail, "");
                } else {
                    // Offline
                    cb(false, "", "", "", "", "");
                }
            } catch (...) {
                cb(false, "", "", "", "", "Failed to parse Twitch stream JSON payload");
            }
        }).detach();
    }

    // Scrape/Resolve YouTube channel ID or poll videos feed
    static void check_youtube(const std::string& handle, std::function<void(std::vector<nlohmann::json> videos, std::string err)> cb) {
        std::thread([handle, cb]() {
            std::string channel_id = handle;
            // If handle starts with @, resolve channel ID first by loading the page
            if (handle[0] == '@') {
                httplib::Client yt_cli("https://www.youtube.com");
                yt_cli.set_connection_timeout(std::chrono::seconds(10));
                yt_cli.set_read_timeout(std::chrono::seconds(10));
                yt_cli.set_follow_location(true);

                auto res = yt_cli.Get("/" + handle);
                if (res && res->status == 200) {
                    std::regex cid_reg("<meta itemprop=\"channelId\" content=\"([^\"]+)\"");
                    std::smatch m;
                    if (std::regex_search(res->body, m, cid_reg) && m.size() > 1) {
                        channel_id = m[1].str();
                    } else {
                        // Fallback search in page body for channelId
                        size_t pos = res->body.find("channelId\":\"");
                        if (pos != std::string::npos) {
                            size_t end = res->body.find('"', pos + 12);
                            if (end != std::string::npos) {
                                channel_id = res->body.substr(pos + 12, end - (pos + 12));
                            }
                        }
                    }
                }
            }

            // Fetch RSS feed
            httplib::Client feed_cli("https://www.youtube.com");
            feed_cli.set_connection_timeout(std::chrono::seconds(10));
            feed_cli.set_read_timeout(std::chrono::seconds(10));
            auto res = feed_cli.Get("/feeds/videos.xml?channel_id=" + channel_id);
            if (!res || res->status != 200) {
                cb({}, "Failed to fetch YouTube XML feed for channel ID " + channel_id);
                return;
            }

            // Extract items from XML
            std::vector<nlohmann::json> videos;
            std::string body = res->body;
            size_t pos = 0;
            while (true) {
                size_t entry_start = body.find("<entry>", pos);
                if (entry_start == std::string::npos) break;
                size_t entry_end = body.find("</entry>", entry_start);
                if (entry_end == std::string::npos) break;

                std::string entry = body.substr(entry_start, entry_end + 8 - entry_start);
                pos = entry_end + 8;

                // Simple XML tags parser
                auto extract = [](const std::string& xml, const std::string& tag) -> std::string {
                    size_t t_pos = xml.find("<" + tag + ">");
                    if (t_pos == std::string::npos) {
                        t_pos = xml.find("<" + tag);
                        if (t_pos == std::string::npos) return "";
                        size_t t_end = xml.find('>', t_pos);
                        if (t_end == std::string::npos) return "";
                        size_t close = xml.find("</" + tag + ">", t_end);
                        if (close == std::string::npos) return "";
                        return xml.substr(t_end + 1, close - (t_end + 1));
                    }
                    size_t close = xml.find("</" + tag + ">", t_pos);
                    if (close == std::string::npos) return "";
                    return xml.substr(t_pos + tag.length() + 2, close - (t_pos + tag.length() + 2));
                };

                std::string vid_id = extract(entry, "yt:videoId");
                std::string title = extract(entry, "title");
                std::string url = "https://www.youtube.com/watch?v=" + vid_id;
                std::string thumbnail = "https://img.youtube.com/vi/" + vid_id + "/maxresdefault.jpg";
                
                nlohmann::json v = {
                    {"id", vid_id},
                    {"title", title},
                    {"url", url},
                    {"thumbnail", thumbnail}
                };
                videos.push_back(v);
            }
            cb(videos, "");
        }).detach();
    }

    // Scrape TikTok posts using standard RSS bridge or public scraper
    static void check_tiktok(const std::string& handle, std::function<void(std::vector<nlohmann::json> posts, std::string err)> cb) {
        std::thread([handle, cb]() {
            // We use Urlebird which is a popular public mirror for TikTok without API limits or logins
            std::string clean_handle = handle;
            if (clean_handle[0] == '@') clean_handle = clean_handle.substr(1);

            httplib::Client cli("https://urlebird.com");
            cli.set_connection_timeout(std::chrono::seconds(10));
            cli.set_read_timeout(std::chrono::seconds(10));
            cli.set_follow_location(true);

            auto res = cli.Get("/user/" + clean_handle + "/");
            if (!res || res->status != 200) {
                cb({}, "Failed to fetch TikTok details from mirror for @" + clean_handle);
                return;
            }

            // Simple HTML parsing to extract video links & titles
            std::vector<nlohmann::json> posts;
            std::string body = res->body;
            
            // Search for thumbnails & video links
            // Pattern: <div class="thumb"> ... <a href="https://urlebird.com/video/..." title="..."> ... </div>
            std::regex link_regex("href=\"(https://urlebird.com/video/[^\"]+)\"");
            std::regex title_regex("alt=\"([^\"]*)\"");
            std::regex img_regex("src=\"([^\"]+)\"");

            auto words_begin = std::sregex_iterator(body.begin(), body.end(), link_regex);
            auto words_end = std::sregex_iterator();

            int count = 0;
            for (std::sregex_iterator i = words_begin; i != words_end && count < 5; ++i, ++count) {
                std::string link = i->str(1);
                
                // Fetch video ID from URL
                size_t v_pos = link.find("/video/");
                if (v_pos == std::string::npos) continue;
                std::string vid_id = link.substr(v_pos + 7);
                if (vid_id.back() == '/') vid_id.pop_back();

                // Simple title & img extraction in surrounding block
                size_t offset = body.find(link);
                std::string block = body.substr(offset > 500 ? offset - 500 : 0, 1000);

                std::smatch tm, im;
                std::string title = "TikTok video by @" + clean_handle;
                std::string img_url = "";

                if (std::regex_search(block, tm, title_regex) && tm.size() > 1) title = tm[1].str();
                if (std::regex_search(block, im, img_regex) && im.size() > 1) img_url = im[1].str();

                nlohmann::json post = {
                    {"id", vid_id},
                    {"title", title},
                    {"url", "https://www.tiktok.com/@" + clean_handle + "/video/" + vid_id},
                    {"thumbnail", img_url}
                };
                posts.push_back(post);
            }
            cb(posts, "");
        }).detach();
    }

    // Scrape Twitter/X posts via public Nitter RSS feeds
    static void check_twitter(const std::string& handle, std::function<void(std::vector<nlohmann::json> tweets, std::string err)> cb) {
        std::thread([handle, cb]() {
            // Use privacydev.net Nitter instance
            httplib::Client cli("https://nitter.privacydev.net");
            cli.set_connection_timeout(std::chrono::seconds(10));
            cli.set_read_timeout(std::chrono::seconds(10));
            cli.set_follow_location(true);

            auto res = cli.Get("/" + handle + "/rss");
            if (!res || res->status != 200) {
                cb({}, "Failed to fetch Twitter posts from RSS (Nitter proxy down or user private)");
                return;
            }

            std::vector<nlohmann::json> tweets;
            std::string body = res->body;
            size_t pos = 0;
            while (true) {
                size_t item_start = body.find("<item>", pos);
                if (item_start == std::string::npos) break;
                size_t item_end = body.find("</item>", item_start);
                if (item_end == std::string::npos) break;

                std::string item = body.substr(item_start, item_end + 7 - item_start);
                pos = item_end + 7;

                auto extract = [](const std::string& xml, const std::string& tag) -> std::string {
                    size_t t_pos = xml.find("<" + tag + ">");
                    if (t_pos == std::string::npos) return "";
                    size_t close = xml.find("</" + tag + ">", t_pos);
                    if (close == std::string::npos) return "";
                    return xml.substr(t_pos + tag.length() + 2, close - (t_pos + tag.length() + 2));
                };

                std::string link = extract(item, "link");
                std::string title = extract(item, "title");
                std::string description = extract(item, "description");

                // Get Twitter URL instead of Nitter URL
                size_t net_pos = link.find("nitter.privacydev.net/");
                if (net_pos != std::string::npos) {
                    link.replace(net_pos, 22, "twitter.com/");
                }

                // ID from link
                size_t status_pos = link.find("/status/");
                std::string tweet_id = link;
                if (status_pos != std::string::npos) {
                    tweet_id = link.substr(status_pos + 8);
                    size_t hash_pos = tweet_id.find('#');
                    if (hash_pos != std::string::npos) tweet_id = tweet_id.substr(0, hash_pos);
                }

                // Check for image in description (Nitter RSS includes img tags)
                std::regex img_reg("<img[^>]+src=\"([^\"]+)\"");
                std::smatch m;
                std::string image_url = "";
                if (std::regex_search(description, m, img_reg) && m.size() > 1) {
                    image_url = m[1].str();
                }

                // Strip HTML from description
                std::string clean_desc = "";
                bool in_tag = false;
                for (char c : description) {
                    if (c == '<') in_tag = true;
                    else if (c == '>') in_tag = false;
                    else if (!in_tag) clean_desc += c;
                }

                nlohmann::json tweet = {
                    {"id", tweet_id},
                    {"title", title.empty() ? clean_desc.substr(0, 100) : title},
                    {"url", link},
                    {"description", clean_desc},
                    {"thumbnail", image_url}
                };
                tweets.push_back(tweet);
            }
            cb(tweets, "");
        }).detach();
    }

    // Fetch Bluesky posts using official public API without auth
    static void check_bluesky(const std::string& handle, std::function<void(std::vector<nlohmann::json> posts, std::string err)> cb) {
        std::thread([handle, cb]() {
            httplib::Client cli("https://public.api.bsky.app");
            cli.set_connection_timeout(std::chrono::seconds(10));
            cli.set_read_timeout(std::chrono::seconds(10));

            auto res = cli.Get("/xrpc/app.bsky.feed.getAuthorFeed?actor=" + handle + "&limit=10");
            if (!res || res->status != 200) {
                cb({}, "Failed to fetch Bluesky feed for actor: " + handle);
                return;
            }

            try {
                auto j = nlohmann::json::parse(res->body);
                std::vector<nlohmann::json> posts;
                if (j.contains("feed") && j["feed"].is_array()) {
                    for (const auto& item : j["feed"]) {
                        if (item.contains("post") && item["post"].is_object()) {
                            auto post_obj = item["post"];
                            std::string uri = post_obj.value("uri", "");
                            std::string cid = post_obj.value("cid", "");
                            
                            // Extract post ID from URI: at://did:plc:.../app.bsky.feed.post/ID
                            size_t last_slash = uri.find_last_of('/');
                            std::string post_id = (last_slash != std::string::npos) ? uri.substr(last_slash + 1) : cid;

                            auto record = post_obj.value("record", nlohmann::json::object());
                            std::string text = record.value("text", "");
                            
                            auto author = post_obj.value("author", nlohmann::json::object());
                            std::string bsky_handle = author.value("handle", handle);

                            std::string url = "https://bsky.app/profile/" + bsky_handle + "/post/" + post_id;
                            
                            std::string image_url = "";
                            if (post_obj.contains("embed") && post_obj["embed"].is_object()) {
                                auto embed = post_obj["embed"];
                                if (embed.value("$type", "") == "app.bsky.embed.images#view" && embed.contains("images") && embed["images"].is_array() && !embed["images"].empty()) {
                                    image_url = embed["images"][0].value("thumb", "");
                                }
                            }

                            nlohmann::json p = {
                                {"id", post_id},
                                {"title", text.substr(0, 100)},
                                {"url", url},
                                {"description", text},
                                {"thumbnail", image_url}
                            };
                            posts.push_back(p);
                        }
                    }
                }
                cb(posts, "");
            } catch (...) {
                cb({}, "Failed to parse Bluesky response payload");
            }
        }).detach();
    }

public:
    static void check_and_post_social_sub(stoatpp::cluster& bot, Database::SocialSubscription sub, bool is_test = false, std::string test_event = "post", std::function<void(int posted, std::string err)> callback = nullptr) {
        auto run_post = [&bot, sub, is_test, test_event, callback](std::vector<nlohmann::json> items, std::string fetch_err) {
            if (!fetch_err.empty()) {
                if (callback) callback(0, fetch_err);
                return;
            }

            // Parse seen items
            nlohmann::json seen_arr = nlohmann::json::array();
            if (!sub.seen_items.empty()) {
                try { seen_arr = nlohmann::json::parse(sub.seen_items); } catch(...) {}
            }
            std::unordered_set<std::string> seen_set;
            if (seen_arr.is_array()) {
                for (const auto& v : seen_arr) if (v.is_string()) seen_set.insert(v.get<std::string>());
            }

            std::vector<nlohmann::json> new_items;
            if (is_test) {
                if (!items.empty()) new_items.push_back(items[0]);
            } else {
                for (const auto& item : items) {
                    std::string item_id = item.value("id", "");
                    if (!item_id.empty() && seen_set.find(item_id) == seen_set.end()) {
                        new_items.push_back(item);
                    }
                }
                // If it is the first check, just mark all as seen
                if (sub.last_checked == 0) {
                    for (const auto& item : items) {
                        std::string item_id = item.value("id", "");
                        if (!item_id.empty()) seen_set.insert(item_id);
                    }
                    new_items.clear();
                }
            }

            int posted_count = 0;
            if (g_db) {
                // Get target channels
                std::vector<std::string> target_channels;
                std::stringstream ss(sub.channels);
                std::string ch;
                while (std::getline(ss, ch, ',')) if (!ch.empty()) target_channels.push_back(ch);

                // Check allowed events (filter items based on events list)
                std::unordered_set<std::string> allowed_events;
                std::stringstream ess(sub.events);
                std::string ev;
                while (std::getline(ess, ev, ',')) if (!ev.empty()) allowed_events.insert(ev);

                std::string active_event = is_test ? test_event : (sub.platform == "twitch" ? "live" : "post");

                for (auto it = new_items.rbegin(); it != new_items.rend(); ++it) {
                    std::string item_title = it->value("title", "");
                    std::string item_url = it->value("url", "");
                    std::string item_thumbnail = it->value("thumbnail", "");
                    std::string item_desc = it->value("description", "");

                    // Check if event configuration exists
                    auto embed_cfg_opt = g_db->get_social_embed(sub.id, active_event);

                    stoatpp::models::MessagePayload payload;
                    payload.content = " ";

                    if (embed_cfg_opt) {
                        const auto& ec = *embed_cfg_opt;
                        if (!ec.content.empty()) {
                            payload.content = format_placeholders(ec.content, sub.channel_handle, item_title, item_url, item_desc);
                        }

                        if (ec.message_type == "embed") {
                            nlohmann::json emb_json = nlohmann::json::object();
                            try { emb_json = nlohmann::json::parse(ec.embed_json); } catch(...) {}

                            nlohmann::json embed = nlohmann::json::object();
                            if (emb_json.contains("title")) {
                                embed["title"] = format_placeholders(emb_json["title"].get<std::string>(), sub.channel_handle, item_title, item_url, item_desc);
                            }
                            if (emb_json.contains("description")) {
                                embed["description"] = format_placeholders(emb_json["description"].get<std::string>(), sub.channel_handle, item_title, item_url, item_desc);
                            }
                            if (emb_json.contains("color")) {
                                embed["colour"] = emb_json["color"];
                            } else if (emb_json.contains("colour")) {
                                embed["colour"] = emb_json["colour"];
                            } else {
                                embed["colour"] = "#4b7bec"; // Blue default
                            }

                            std::string img_url = "";
                            if (emb_json.contains("image")) {
                                img_url = format_placeholders(emb_json["image"].get<std::string>(), sub.channel_handle, item_title, item_url, item_desc);
                            } else if (!item_thumbnail.empty()) {
                                img_url = item_thumbnail;
                            }

                            if (!img_url.empty()) {
                                std::string media_id = bot.rest().upload_from_url(img_url);
                                if (!media_id.empty()) {
                                    embed["media"] = media_id;
                                } else {
                                    // Fallback: append inline markdown image
                                    std::string d = embed.value("description", "");
                                    if (!d.empty()) d += "\n";
                                    d += "![](" + img_url + ")";
                                    embed["description"] = d;
                                }
                            }
                            payload.embeds.push_back(embed);
                        }
                    } else {
                        // Default flag-ship looking embed
                        payload.content = "📢 **" + sub.channel_handle + "** just updated!";
                        nlohmann::json embed = nlohmann::json::object();
                        embed["title"] = item_title;
                        embed["colour"] = (sub.platform == "twitch") ? "#6441a5" :
                                          (sub.platform == "youtube") ? "#ff0000" :
                                          (sub.platform == "tiktok") ? "#010101" :
                                          (sub.platform == "twitter") ? "#1da1f2" : "#0285ff";
                        
                        embed["description"] = item_desc.empty() ? "[Click to view](" + item_url + ")" : item_desc + "\n\n[Click to view](" + item_url + ")";

                        if (!item_thumbnail.empty()) {
                            std::string media_id = bot.rest().upload_from_url(item_thumbnail);
                            if (!media_id.empty()) embed["media"] = media_id;
                        }
                        payload.embeds.push_back(embed);
                    }

                    for (const auto& ch_id : target_channels) {
                        bot.send_message(ch_id, payload);
                    }

                    if (!is_test) {
                        seen_set.insert(it->value("id", ""));
                        posted_count++;
                    }
                }

                if (!is_test) {
                    std::vector<std::string> seen_vec(seen_set.begin(), seen_set.end());
                    if (seen_vec.size() > 100) seen_vec.erase(seen_vec.begin(), seen_vec.begin() + (seen_vec.size() - 100));
                    nlohmann::json new_seen_json = seen_vec;
                    uint64_t now_ts = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                    g_db->update_social_sub_check(sub.id, now_ts, new_seen_json.dump());
                }
            }
            if (callback) callback(posted_count, "");
        };

        if (sub.platform == "twitch") {
            check_twitch(sub.channel_handle, [run_post](bool live, std::string title, std::string url, std::string game, std::string thumbnail, std::string err) {
                if (!err.empty()) {
                    run_post({}, err);
                    return;
                }
                std::vector<nlohmann::json> items;
                if (live) {
                    nlohmann::json stream = {
                        {"id", title + "_" + game}, // Simple stream identifier to avoid re-triggering
                        {"title", title},
                        {"url", url},
                        {"thumbnail", thumbnail},
                        {"description", "Streaming: " + game}
                    };
                    items.push_back(stream);
                }
                run_post(items, "");
            });
        } else if (sub.platform == "youtube") {
            check_youtube(sub.channel_handle, run_post);
        } else if (sub.platform == "tiktok") {
            check_tiktok(sub.channel_handle, run_post);
        } else if (sub.platform == "twitter") {
            check_twitter(sub.channel_handle, run_post);
        } else if (sub.platform == "bluesky") {
            check_bluesky(sub.channel_handle, run_post);
        } else {
            if (callback) callback(0, "Unknown platform");
        }
    }

    void register_handlers(stoatpp::cluster& bot) override {
        // Shared Command Logic
        auto handle_social = [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args, const std::string& platform) {
            if (msg.server_id.empty()) {
                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " This command can only be used in a server channel.");
                return;
            }
            if (!g_db) {
                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Database is unavailable.");
                return;
            }

            std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
            if (args.empty()) {
                send_social_help(bot, msg.channel_id, platform, pfx);
                return;
            }

            std::string subcommand = args[0];
            std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);

            bool requires_permission = (subcommand == "add" || subcommand == "remove" || subcommand == "target" || subcommand == "events" || subcommand == "edit" || subcommand == "json" || subcommand == "reset" || subcommand == "test");

            auto run_cmd = [&bot, msg, args, subcommand, platform, pfx]() {
                if (subcommand == "add") {
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + platform + " add <channel_handle>`");
                        return;
                    }
                    std::string handle = args[1];

                    // Save subscription
                    Database::SocialSubscription sub;
                    sub.server_id = msg.server_id;
                    sub.platform = platform;
                    sub.channel_handle = handle;
                    sub.channels = msg.channel_id;
                    sub.events = "live,vod,post";
                    sub.seen_items = "[]";

                    if (g_db->add_social_sub(sub)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Now following **" + handle + "** on **" + platform + "**! Posts will be sent to this channel. Sub ID is **" + std::to_string(sub.id) + "**.");
                    } else {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save subscription to database.");
                    }
                }
                else if (subcommand == "remove") {
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + platform + " remove <sub_id>`");
                        return;
                    }
                    try {
                        int id = std::stoi(args[1]);
                        auto sub = g_db->get_social_sub_by_id(id, msg.server_id);
                        if (!sub || sub->platform != platform) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Subscription not found.");
                            return;
                        }

                        if (g_db->remove_social_sub(id, msg.server_id)) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Stopped following channel.");
                        } else {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to remove subscription.");
                        }
                    } catch (...) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid subscription ID format.");
                    }
                }
                else if (subcommand == "target") {
                    if (args.size() < 3) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + platform + " target <sub_id> <#channel>`");
                        return;
                    }
                    try {
                        int id = std::stoi(args[1]);
                        auto sub = g_db->get_social_sub_by_id(id, msg.server_id);
                        if (!sub || sub->platform != platform) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Subscription not found.");
                            return;
                        }

                        std::string target_channel = args[2];
                        if (target_channel.rfind("<#", 0) == 0 && target_channel.back() == '>') {
                            target_channel = target_channel.substr(2, target_channel.size() - 3);
                        }

                        if (g_db->update_social_sub_channels(id, msg.server_id, target_channel)) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Target channel updated to <#" + target_channel + ">.");
                        } else {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to update target channel.");
                        }
                    } catch (...) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid format.");
                    }
                }
                else if (subcommand == "events") {
                    if (args.size() < 3) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + platform + " events <sub_id> <events>`\nExample: `" + pfx + platform + " events 5 live,vod`");
                        return;
                    }
                    try {
                        int id = std::stoi(args[1]);
                        auto sub = g_db->get_social_sub_by_id(id, msg.server_id);
                        if (!sub || sub->platform != platform) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Subscription not found.");
                            return;
                        }

                        std::string evs = args[2];
                        if (g_db->update_social_sub_events(id, msg.server_id, evs)) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Followed events updated to: `" + evs + "`");
                        } else {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to update events list.");
                        }
                    } catch (...) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid format.");
                    }
                }
                else if (subcommand == "list") {
                    auto subs = g_db->get_social_subs_for_server(msg.server_id);
                    std::string res = "📋 **Followed " + platform + " channels on this server:**\n";
                    int count = 0;
                    for (const auto& s : subs) {
                        if (s.platform == platform) {
                            res += "• **ID " + std::to_string(s.id) + "**: `" + s.channel_handle + "` ➔ <#" + s.channels + "> (events: `" + s.events + "`)\n";
                            count++;
                        }
                    }
                    if (count == 0) res += "*No followed channels on this platform.*";
                    bot.send_message(msg.channel_id, res);
                }
                else if (subcommand == "test") {
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + platform + " test <sub_id> [event_type]`");
                        return;
                    }
                    try {
                        int id = std::stoi(args[1]);
                        auto sub = g_db->get_social_sub_by_id(id, msg.server_id);
                        if (!sub || sub->platform != platform) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Subscription not found.");
                            return;
                        }

                        std::string ev_type = (args.size() >= 3) ? args[2] : "post";
                        bot.send_message(msg.channel_id, "🔄 Running simulated feed test...");
                        check_and_post_social_sub(bot, *sub, true, ev_type, [&bot, msg](int posted, std::string err) {
                            if (!err.empty()) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Test failed: " + err);
                            }
                        });
                    } catch (...) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid subscription ID.");
                    }
                }
                else if (subcommand == "edit") {
                    std::string trailing = get_raw_trailing_args(msg.content, pfx, platform, "edit");
                    // Format: <sub_id> <event_type> key:val ...
                    std::stringstream ss(trailing);
                    std::string id_str, ev_type;
                    ss >> id_str >> ev_type;

                    if (id_str.empty() || ev_type.empty()) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + platform + " edit <sub_id> <event_type> key:val ...`\nKeys: `title`, `description`, `color`, `content`, `image`.");
                        return;
                    }

                    try {
                        int id = std::stoi(id_str);
                        auto sub = g_db->get_social_sub_by_id(id, msg.server_id);
                        if (!sub || sub->platform != platform) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Subscription not found.");
                            return;
                        }

                        // Parse the remaining trailing text for pairs
                        size_t offset = trailing.find(ev_type);
                        std::string pairs_str = trailing.substr(offset + ev_type.length());
                        auto pairs = parse_pairs(pairs_str);

                        if (pairs.empty()) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " No valid key-value pairs found.");
                            return;
                        }

                        auto ec = g_db->get_social_embed(id, ev_type).value_or(Database::SocialEmbedConfig{id, ev_type});
                        nlohmann::json emb = nlohmann::json::object();
                        if (!ec.embed_json.empty()) {
                            try { emb = nlohmann::json::parse(ec.embed_json); } catch(...) {}
                        }

                        for (const auto& [k, v] : pairs) {
                            if (k == "content") {
                                if (v == "none" || v == "null") ec.content = "";
                                else ec.content = v;
                            } else if (v == "none" || v == "null") {
                                emb.erase(k);
                            } else {
                                emb[k] = v;
                            }
                        }

                        ec.embed_json = emb.dump();
                        if (g_db->save_social_embed(ec)) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Embed customization saved for event `" + ev_type + "`!");
                        } else {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save customization.");
                        }
                    } catch (...) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid format or subscription ID.");
                    }
                }
                else if (subcommand == "json") {
                    std::string trailing = get_raw_trailing_args(msg.content, pfx, platform, "json");
                    std::stringstream ss(trailing);
                    std::string id_str, ev_type;
                    ss >> id_str >> ev_type;

                    if (id_str.empty() || ev_type.empty()) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + platform + " json <sub_id> <event_type> <raw_json>`");
                        return;
                    }

                    try {
                        int id = std::stoi(id_str);
                        auto sub = g_db->get_social_sub_by_id(id, msg.server_id);
                        if (!sub || sub->platform != platform) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Subscription not found.");
                            return;
                        }

                        size_t offset = trailing.find(ev_type);
                        std::string json_str = trailing.substr(offset + ev_type.length());
                        json_str.erase(0, json_str.find_first_not_of(" \t\r\n"));

                        nlohmann::json parsed_emb = nlohmann::json::parse(json_str);
                        auto ec = g_db->get_social_embed(id, ev_type).value_or(Database::SocialEmbedConfig{id, ev_type});

                        if (parsed_emb.contains("content") && parsed_emb["content"].is_string()) {
                            ec.content = parsed_emb["content"].get<std::string>();
                            parsed_emb.erase("content");
                        }

                        ec.embed_json = parsed_emb.dump();
                        if (g_db->save_social_embed(ec)) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Custom JSON embed configuration imported!");
                        } else {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save embed JSON.");
                        }
                    } catch (const std::exception& ex) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid JSON format: " + std::string(ex.what()));
                    }
                }
                else if (subcommand == "reset") {
                    if (args.size() < 3) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + platform + " reset <sub_id> <event_type>`");
                        return;
                    }
                    try {
                        int id = std::stoi(args[1]);
                        std::string ev_type = args[2];
                        auto sub = g_db->get_social_sub_by_id(id, msg.server_id);
                        if (!sub || sub->platform != platform) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Subscription not found.");
                            return;
                        }

                        if (g_db->delete_social_embed(id, ev_type)) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Embed customization reset to default.");
                        } else {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to reset customization.");
                        }
                    } catch (...) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid parameters.");
                    }
                }
                else {
                    send_social_help(bot, msg.channel_id, platform, pfx);
                }
            };

            if (requires_permission) {
                bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, run_cmd](stoatpp::models::Member member, bool success) {
                    if (!success) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to verify permissions.");
                        return;
                    }
                    if (!is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, member)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Permission error: you need to be trusted or have Manage Server permission to manage social feeds.");
                        return;
                    }
                    run_cmd();
                });
            } else {
                run_cmd();
            }
        };

        // Register each platform command
        for (const std::string& plat : {"youtube", "tiktok", "twitter", "twitch", "bluesky"}) {
            bot.register_command({
                plat,
                {},
                "manage " + plat + " feeds and notifications.",
                "",
                {"<subcommand>", "[args...]"},
                [plat, handle_social](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                    handle_social(bot, msg, args, plat);
                },
                "Utility"
            });
        }

        // Start background polling timer (every 90 seconds)
        bot.start_timer([&bot](stoatpp::timer) {
            uint64_t now_ts = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            auto subs = g_db->get_all_social_subs();
            
            for (const auto& sub : subs) {
                // Poll every 5 minutes (300 seconds) for each subscription
                if (now_ts - sub.last_checked >= 300) {
                    {
                        std::lock_guard<std::mutex> lock(g_social_checking_mutex);
                        if (g_social_checking_ids.count(sub.id)) continue;
                        g_social_checking_ids.insert(sub.id);
                    }

                    check_and_post_social_sub(bot, sub, false, "post", [sub](int posted, std::string err) {
                        std::lock_guard<std::mutex> lock(g_social_checking_mutex);
                        g_social_checking_ids.erase(sub.id);
                    });
                }
            }
        }, 90);
    }
};

} // namespace bronx::cogs
