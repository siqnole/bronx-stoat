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

namespace bronx::cogs {

struct FeedItem {
    std::string guid;
    std::string title;
    std::string link;
    std::string description;
    std::string image_url;
    std::string feed_logo_url;
};

struct TargetCheckState {
    int index = 0;
    std::vector<std::string> args;
    std::vector<std::string> valid_channel_ids;
    std::vector<std::string> errors;
};

// Extract a tag's content from XML
inline std::string extract_tag(const std::string& xml, const std::string& tag) {
    std::string start_tag = "<" + tag;
    size_t pos = xml.find(start_tag);
    if (pos == std::string::npos) return "";

    // Find end of start tag (e.g. <title> or <link href="...">)
    size_t start_tag_end = xml.find('>', pos);
    if (start_tag_end == std::string::npos) return "";
    start_tag_end += 1;

    std::string end_tag = "</" + tag + ">";
    size_t end_pos = xml.find(end_tag, start_tag_end);
    if (end_pos == std::string::npos) {
        // Maybe self-closing or contains attributes, return the start tag block
        return xml.substr(pos, start_tag_end - pos);
    }

    std::string content = xml.substr(start_tag_end, end_pos - start_tag_end);
    // Remove CDATA if present
    if (content.rfind("<![CDATA[", 0) == 0) {
        content = content.substr(9);
        size_t cdata_end = content.find("]]>");
        if (cdata_end != std::string::npos) {
            content = content.substr(0, cdata_end);
        }
    }
    return content;
}

// Extract an attribute value from a tag block (e.g. href="..." or href='...')
inline std::string extract_attribute(const std::string& tag_block, const std::string& attr) {
    std::string search = attr + "=\"";
    size_t pos = tag_block.find(search);
    if (pos == std::string::npos) {
        search = attr + "='";
        pos = tag_block.find(search);
    }
    if (pos == std::string::npos) return "";

    pos += search.length();
    size_t end_pos = tag_block.find(search.back(), pos);
    if (end_pos == std::string::npos) return "";

    return tag_block.substr(pos, end_pos - pos);
}

inline std::string unescape_html(std::string html) {
    auto replace_all = [](std::string& s, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.length(), to);
            pos += to.length();
        }
    };
    replace_all(html, "&amp;", "&");
    replace_all(html, "&lt;", "<");
    replace_all(html, "&gt;", ">");
    replace_all(html, "&quot;", "\"");
    replace_all(html, "&#39;", "'");
    replace_all(html, "&apos;", "'");
    replace_all(html, "&#038;", "&");
    replace_all(html, "&#38;", "&");
    return html;
}

inline std::string clean_image_url(std::string url) {
    if (url.find("cbc.ca") != std::string::npos) {
        size_t q = url.find('?');
        if (q != std::string::npos) {
            url = url.substr(0, q);
        }
    }
    return url;
}

inline std::string strip_html_tags(std::string html) {
    std::string out;
    out.reserve(html.size());
    bool in_tag = false;
    for (size_t i = 0; i < html.size(); ++i) {
        if (html[i] == '<') {
            in_tag = true;
        } else if (html[i] == '>') {
            in_tag = false;
        } else if (!in_tag) {
            out += html[i];
        }
    }
    // Capped at 400 chars to avoid huge messages
    if (out.length() > 400) {
        out = out.substr(0, 397) + "...";
    }
    // Trim leading/trailing whitespace
    size_t first = out.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = out.find_last_not_of(" \t\r\n");
    return out.substr(first, (last - first + 1));
}

inline int parse_feed_interval(const std::string& body) {
    // Default is 20 minutes (1200 seconds)
    int interval = 1200;

    // Check RSS <ttl> tag
    std::string ttl_str = extract_tag(body, "ttl");
    if (!ttl_str.empty()) {
        try {
            int ttl_min = std::stoi(ttl_str);
            if (ttl_min > 0) {
                interval = ttl_min * 60;
            }
        } catch (...) {}
    }

    // Check sy:updatePeriod and sy:updateFrequency
    std::string sy_period = extract_tag(body, "sy:updatePeriod");
    if (!sy_period.empty()) {
        std::string sy_freq_str = extract_tag(body, "sy:updateFrequency");
        int sy_freq = 1;
        if (!sy_freq_str.empty()) {
            try {
                int f = std::stoi(sy_freq_str);
                if (f > 0) sy_freq = f;
            } catch (...) {}
        }

        int period_secs = 3600; // hourly default
        if (sy_period == "hourly") period_secs = 3600;
        else if (sy_period == "daily") period_secs = 86400;
        else if (sy_period == "weekly") period_secs = 604800;
        else if (sy_period == "monthly") period_secs = 2592000;
        else if (sy_period == "yearly") period_secs = 31536000;

        interval = period_secs * sy_freq;
    }

    // Check JSON feed TTL
    nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
    if (!j.is_discarded()) {
        if (j.contains("ttl")) {
            try {
                int ttl_min = j["ttl"].get<int>();
                if (ttl_min > 0) interval = ttl_min * 60;
            } catch (...) {}
        } else if (j.contains("_ttl")) {
            try {
                int ttl_min = j["_ttl"].get<int>();
                if (ttl_min > 0) interval = ttl_min * 60;
            } catch (...) {}
        }
    }

    // Cap to a reasonable range (minimum 1 min, maximum 1 day)
    if (interval < 60) interval = 60;
    if (interval > 86400) interval = 86400;

    return interval;
}

// Global checking sets to prevent overlapping checks
inline std::mutex g_rss_checking_mutex;
inline std::unordered_set<int> g_rss_checking_ids;

inline void fetch_feed_items(const std::string& url_str, std::function<void(std::vector<FeedItem> items, int interval, std::string err)> callback) {
    std::string url = url_str;
    std::string scheme;
    if (url.rfind("https://", 0) == 0) { scheme = "https://"; url = url.substr(8); }
    else if (url.rfind("http://", 0) == 0) { scheme = "http://"; url = url.substr(7); }
    else {
        callback({}, 1200, "Invalid URL scheme. Must be http:// or https://");
        return;
    }
    auto slash = url.find('/');
    std::string host = (slash == std::string::npos) ? url : url.substr(0, slash);
    std::string path = (slash == std::string::npos) ? "/" : url.substr(slash);

    std::thread([scheme, host, path, callback]() {
        httplib::Client client(scheme + host);
        client.set_connection_timeout(std::chrono::seconds(10));
        client.set_read_timeout(std::chrono::seconds(15));
        client.set_follow_location(true);

        auto res = client.Get(path);
        if (!res || res->status < 200 || res->status >= 300) {
            std::string err = res ? ("HTTP status " + std::to_string(res->status)) : "Connection failed";
            callback({}, 1200, err);
            return;
        }

        std::string body = res->body;
        std::vector<FeedItem> items;
        int parsed_interval = parse_feed_interval(body);

        // Try parsing JSON feed
        nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
        if (!j.is_discarded() && j.contains("items") && j["items"].is_array()) {
            std::string feed_image_url;
            if (j.contains("icon") && j["icon"].is_string()) {
                feed_image_url = j["icon"].get<std::string>();
            } else if (j.contains("favicon") && j["favicon"].is_string()) {
                feed_image_url = j["favicon"].get<std::string>();
            }
            feed_image_url = clean_image_url(unescape_html(feed_image_url));

            for (const auto& item_j : j["items"]) {
                FeedItem item;
                if (item_j.contains("id") && item_j["id"].is_string()) {
                    item.guid = item_j["id"].get<std::string>();
                }
                if (item_j.contains("title") && item_j["title"].is_string()) {
                    item.title = item_j["title"].get<std::string>();
                }
                if (item_j.contains("url") && item_j["url"].is_string()) {
                    item.link = item_j["url"].get<std::string>();
                } else if (item_j.contains("external_url") && item_j["external_url"].is_string()) {
                    item.link = item_j["external_url"].get<std::string>();
                }
                if (item_j.contains("summary") && item_j["summary"].is_string()) {
                    item.description = item_j["summary"].get<std::string>();
                } else if (item_j.contains("content_text") && item_j["content_text"].is_string()) {
                    item.description = item_j["content_text"].get<std::string>();
                } else if (item_j.contains("content_html") && item_j["content_html"].is_string()) {
                    item.description = strip_html_tags(item_j["content_html"].get<std::string>());
                }

                if (item_j.contains("image") && item_j["image"].is_string()) {
                    item.image_url = unescape_html(item_j["image"].get<std::string>());
                } else if (item_j.contains("banner_image") && item_j["banner_image"].is_string()) {
                    item.image_url = unescape_html(item_j["banner_image"].get<std::string>());
                }

                if (item.image_url.empty() && item_j.contains("content_html") && item_j["content_html"].is_string()) {
                    std::string content_html = item_j["content_html"].get<std::string>();
                    size_t img_pos = content_html.find("<img");
                    if (img_pos != std::string::npos) {
                        size_t img_end = content_html.find('>', img_pos);
                        if (img_end != std::string::npos) {
                            std::string img_tag = content_html.substr(img_pos, img_end - img_pos + 1);
                            item.image_url = unescape_html(extract_attribute(img_tag, "src"));
                        }
                    }
                }

                item.image_url = clean_image_url(item.image_url);
                item.feed_logo_url = feed_image_url;
                if (item.guid.empty()) item.guid = item.link;
                if (!item.guid.empty()) items.push_back(item);
            }
        }
        // Try parsing Atom
        else if (body.find("<feed") != std::string::npos) {
            std::string feed_image_url = extract_tag(body, "logo");
            if (feed_image_url.empty()) {
                feed_image_url = extract_tag(body, "icon");
            }
            feed_image_url = clean_image_url(unescape_html(feed_image_url));

            size_t pos = 0;
            while (true) {
                size_t entry_start = body.find("<entry>", pos);
                if (entry_start == std::string::npos) {
                    entry_start = body.find("<entry ", pos);
                }
                if (entry_start == std::string::npos) break;

                size_t entry_end = body.find("</entry>", entry_start);
                if (entry_end == std::string::npos) break;

                std::string entry_xml = body.substr(entry_start, entry_end + 8 - entry_start);
                pos = entry_end + 8;

                FeedItem item;
                item.title = unescape_html(extract_tag(entry_xml, "title"));
                item.guid = extract_tag(entry_xml, "id");
                
                std::string link_tag = extract_tag(entry_xml, "link");
                if (link_tag.empty()) {
                    size_t link_pos = entry_xml.find("<link");
                    if (link_pos != std::string::npos) {
                        size_t link_end = entry_xml.find('>', link_pos);
                        if (link_end != std::string::npos) {
                            link_tag = entry_xml.substr(link_pos, link_end - link_pos + 1);
                        }
                    }
                }
                item.link = extract_attribute(link_tag, "href");
                if (item.link.empty()) {
                    item.link = extract_tag(entry_xml, "link");
                }

                if (item.guid.empty()) item.guid = item.link;

                std::string summary = extract_tag(entry_xml, "summary");
                if (summary.empty()) {
                    summary = extract_tag(entry_xml, "content");
                }
                item.description = strip_html_tags(unescape_html(summary));

                // Parse Atom enclosures / links for image
                size_t enc_pos = entry_xml.find("<link rel=\"enclosure\"");
                if (enc_pos == std::string::npos) {
                    enc_pos = entry_xml.find("<link rel='enclosure'");
                }
                if (enc_pos != std::string::npos) {
                    size_t enc_end = entry_xml.find('>', enc_pos);
                    if (enc_end != std::string::npos) {
                        std::string enc_tag = entry_xml.substr(enc_pos, enc_end - enc_pos + 1);
                        std::string type = extract_attribute(enc_tag, "type");
                        if (type.rfind("image/", 0) == 0) {
                            item.image_url = unescape_html(extract_attribute(enc_tag, "href"));
                        }
                    }
                }

                if (item.image_url.empty()) {
                    size_t med_pos = entry_xml.find("<media:content");
                    if (med_pos != std::string::npos) {
                        size_t med_end = entry_xml.find('>', med_pos);
                        if (med_end != std::string::npos) {
                            std::string med_tag = entry_xml.substr(med_pos, med_end - med_pos + 1);
                            item.image_url = unescape_html(extract_attribute(med_tag, "url"));
                        }
                    }
                }

                if (item.image_url.empty()) {
                    size_t thumb_pos = entry_xml.find("<media:thumbnail");
                    if (thumb_pos != std::string::npos) {
                        size_t thumb_end = entry_xml.find('>', thumb_pos);
                        if (thumb_end != std::string::npos) {
                            std::string thumb_tag = entry_xml.substr(thumb_pos, thumb_end - thumb_pos + 1);
                            item.image_url = unescape_html(extract_attribute(thumb_tag, "url"));
                        }
                    }
                }

                // Check <img> in content or summary (unescape HTML first so we find <img instead of &lt;img)
                if (item.image_url.empty()) {
                    std::string content_or_summary = unescape_html(extract_tag(entry_xml, "content"));
                    if (content_or_summary.empty() || content_or_summary.find("<img") == std::string::npos) {
                        std::string summary_tag = extract_tag(entry_xml, "summary");
                        if (!summary_tag.empty()) {
                            content_or_summary = unescape_html(summary_tag);
                        }
                    }
                    size_t img_pos = content_or_summary.find("<img");
                    if (img_pos != std::string::npos) {
                        size_t img_end = content_or_summary.find('>', img_pos);
                        if (img_end != std::string::npos) {
                            std::string img_tag = content_or_summary.substr(img_pos, img_end - img_pos + 1);
                            item.image_url = unescape_html(extract_attribute(img_tag, "src"));
                        }
                    }
                }

                item.image_url = clean_image_url(item.image_url);
                item.feed_logo_url = feed_image_url;
                if (!item.guid.empty()) items.push_back(item);
            }
        }
        // Try parsing RSS
        else {
            std::string feed_image_url;
            size_t channel_img_pos = body.find("<image>");
            if (channel_img_pos != std::string::npos) {
                size_t channel_img_end = body.find("</image>", channel_img_pos);
                if (channel_img_end != std::string::npos) {
                    std::string img_xml = body.substr(channel_img_pos, channel_img_end + 8 - channel_img_pos);
                    feed_image_url = extract_tag(img_xml, "url");
                }
            }
            feed_image_url = clean_image_url(unescape_html(feed_image_url));

            size_t pos = 0;
            while (true) {
                size_t item_start = body.find("<item>", pos);
                if (item_start == std::string::npos) {
                    item_start = body.find("<item ", pos);
                }
                if (item_start == std::string::npos) break;

                size_t item_end = body.find("</item>", item_start);
                if (item_end == std::string::npos) break;

                std::string item_xml = body.substr(item_start, item_end + 7 - item_start);
                pos = item_end + 7;

                FeedItem item;
                item.title = unescape_html(extract_tag(item_xml, "title"));
                item.link = extract_tag(item_xml, "link");
                item.guid = extract_tag(item_xml, "guid");
                if (item.guid.empty()) item.guid = item.link;

                // Verge feed content is often in <content:encoded>, let's unescape it BEFORE looking for <img>!
                std::string desc_raw = extract_tag(item_xml, "description");
                std::string desc_un = unescape_html(desc_raw);
                item.description = strip_html_tags(desc_un);

                // Check enclosure
                size_t enc_pos = item_xml.find("<enclosure");
                if (enc_pos != std::string::npos) {
                    size_t enc_end = item_xml.find('>', enc_pos);
                    if (enc_end != std::string::npos) {
                        std::string enc_tag = item_xml.substr(enc_pos, enc_end - enc_pos + 1);
                        item.image_url = unescape_html(extract_attribute(enc_tag, "url"));
                    }
                }

                // Check media:content / media:thumbnail
                if (item.image_url.empty()) {
                    size_t med_pos = item_xml.find("<media:content");
                    if (med_pos != std::string::npos) {
                        size_t med_end = item_xml.find('>', med_pos);
                        if (med_end != std::string::npos) {
                            std::string med_tag = item_xml.substr(med_pos, med_end - med_pos + 1);
                            item.image_url = unescape_html(extract_attribute(med_tag, "url"));
                        }
                    }
                }
                if (item.image_url.empty()) {
                    size_t thumb_pos = item_xml.find("<media:thumbnail");
                    if (thumb_pos != std::string::npos) {
                        size_t thumb_end = item_xml.find('>', thumb_pos);
                        if (thumb_end != std::string::npos) {
                            std::string thumb_tag = item_xml.substr(thumb_pos, thumb_end - thumb_pos + 1);
                            item.image_url = unescape_html(extract_attribute(thumb_tag, "url"));
                        }
                    }
                }

                // Check <img> in description or content:encoded (unescape HTML first so we find <img instead of &lt;img)
                if (item.image_url.empty()) {
                    std::string content_encoded = extract_tag(item_xml, "content:encoded");
                    std::string desc_or_content = desc_un;
                    if (desc_or_content.find("<img") == std::string::npos && !content_encoded.empty()) {
                        desc_or_content = unescape_html(content_encoded);
                    }
                    size_t img_pos = desc_or_content.find("<img");
                    if (img_pos != std::string::npos) {
                        size_t img_end = desc_or_content.find('>', img_pos);
                        if (img_end != std::string::npos) {
                            std::string img_tag = desc_or_content.substr(img_pos, img_end - img_pos + 1);
                            item.image_url = unescape_html(extract_attribute(img_tag, "src"));
                        }
                    }
                }

                item.image_url = clean_image_url(item.image_url);
                item.feed_logo_url = feed_image_url;
                if (!item.guid.empty()) items.push_back(item);
            }
        }

        callback(items, parsed_interval, "");
    }).detach();
}

struct RssReadSession {
    int current_index = 0;
    int total_items = 0;
    std::string user_id;
    std::vector<FeedItem> items;
    std::string feed_title;
    std::string feed_colour;
};
inline std::mutex g_rss_read_sessions_mutex;
inline std::unordered_map<std::string, RssReadSession> g_rss_read_sessions;

inline void check_and_post_feed(stoatpp::cluster& bot, Database::RssFeed feed, bool is_manual = false, bool is_test = false, std::function<void(int posted, int interval, std::string err)> callback = nullptr) {
    fetch_feed_items(feed.url, [&bot, feed, is_manual, is_test, callback](std::vector<FeedItem> items, int parsed_interval, std::string err) {
        if (!err.empty()) {
            if (callback) callback(0, feed.check_interval, err);
            return;
        }

        if (g_db && feed.id > 0 && !is_test && parsed_interval != feed.check_interval) {
            g_db->update_rss_feed_interval(feed.id, parsed_interval);
        }

        // Parse seen items
        nlohmann::json seen_arr = nlohmann::json::array();
        if (!feed.seen_items.empty()) {
            try {
                seen_arr = nlohmann::json::parse(feed.seen_items);
            } catch (...) {}
        }
        std::unordered_set<std::string> seen_set;
        if (seen_arr.is_array()) {
            for (const auto& val : seen_arr) {
                if (val.is_string()) seen_set.insert(val.get<std::string>());
            }
        }

        std::vector<FeedItem> new_items;
        if (is_test) {
            if (!items.empty()) {
                new_items.push_back(items[0]);
            }
        } else {
            for (const auto& item : items) {
                if (seen_set.find(item.guid) == seen_set.end()) {
                    new_items.push_back(item);
                }
            }

            bool is_first_run = (feed.last_checked == 0);
            if (is_first_run) {
                // Mark all as seen
                for (const auto& item : items) {
                    seen_set.insert(item.guid);
                }
                // Keep only the newest to post
                if (!items.empty()) {
                    new_items.clear();
                    new_items.push_back(items[0]);
                }
            }
        }

        // Post new items (in reverse order, so oldest new item is posted first)
        int posted_count = 0;
        if (g_db) {
            std::vector<std::string> target_channels;
            std::stringstream ss(feed.channels);
            std::string ch;
            while (std::getline(ss, ch, ',')) {
                if (!ch.empty()) target_channels.push_back(ch);
            }

            for (auto it_item = new_items.rbegin(); it_item != new_items.rend(); ++it_item) {
                stoatpp::models::MessagePayload payload;
                payload.content = " ";
                
                nlohmann::json embed = nlohmann::json::object();
                std::string title = it_item->title.empty() ? "New Feed Item" : it_item->title;
                if (title.length() > 100) {
                    title = title.substr(0, 97) + "...";
                }
                embed["title"] = title;
                embed["colour"] = "#f26522"; // Sleek HSL tailored feed color (RSS orange)
                embed["description"] = it_item->description;
                if (!it_item->link.empty()) {
                    if (embed["description"].get<std::string>().empty()) {
                        embed["description"] = "[Read Article](" + it_item->link + ")";
                    } else {
                        embed["description"] = embed["description"].get<std::string>() + "\n\n[Read Article](" + it_item->link + ")";
                    }
                }
                if (!it_item->image_url.empty()) {
                    std::string file_id = bot.rest().upload_from_url(it_item->image_url);
                    if (!file_id.empty()) {
                        embed["media"] = file_id;
                    }
                }
                if (!it_item->feed_logo_url.empty()) {
                    std::string file_id = bot.rest().upload_from_url(it_item->feed_logo_url);
                    if (!file_id.empty()) {
                        embed["icon_url"] = bot.config().cdn_url + "/attachments/" + file_id + "/logo.png";
                    }
                }
                payload.embeds.push_back(embed);

                for (const auto& ch_id : target_channels) {
                    bot.send_message(ch_id, payload);
                }
                seen_set.insert(it_item->guid);
                posted_count++;
            }

            if (!is_test) {
                // Cap seen items at 100 to prevent database bloat
                std::vector<std::string> seen_vec(seen_set.begin(), seen_set.end());
                if (seen_vec.size() > 100) {
                    seen_vec.erase(seen_vec.begin(), seen_vec.begin() + (seen_vec.size() - 100));
                }
                nlohmann::json new_seen_json = seen_vec;
                uint64_t now_ts = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());
                g_db->update_rss_feed_check(feed.id, now_ts, new_seen_json.dump());
            }
        }

        if (callback) callback(posted_count, parsed_interval, "");
    });
}

inline std::optional<Database::RssFeed> resolve_feed(const std::string& server_id, const std::string& query) {
    if (!g_db) return std::nullopt;
    bool all_digits = !query.empty() && std::all_of(query.begin(), query.end(), [](unsigned char c) { return std::isdigit(c); });
    if (all_digits) {
        try {
            int id = std::stoi(query);
            auto by_id = g_db->get_rss_by_id(id, server_id);
            if (by_id) return by_id;
        } catch (...) {}
    }
    return g_db->get_rss_by_url(server_id, query);
}

class RSSModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.on_message_react([&bot](const stoatpp::events::MessageReact& e) {
            std::unique_lock<std::mutex> lock(g_rss_read_sessions_mutex);
            auto it = g_rss_read_sessions.find(e.id);
            if (it != g_rss_read_sessions.end()) {
                auto& session = it->second;
                if (e.user_id == bot.current_user().id) return;
                if (e.user_id != session.user_id) {
                    lock.unlock();
                    bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
                    return;
                }

                bool index_changed = false;
                if (e.emoji_id == "⬅️" || e.emoji_id == "◀" || e.emoji_id == "⬅") {
                    if (session.current_index > 0) {
                        session.current_index--;
                    } else {
                        session.current_index = session.total_items - 1;
                    }
                    index_changed = true;
                } else if (e.emoji_id == "➡️" || e.emoji_id == "▶" || e.emoji_id == "➡") {
                    if (session.current_index < session.total_items - 1) {
                        session.current_index++;
                    } else {
                        session.current_index = 0;
                    }
                    index_changed = true;
                }

                if (index_changed && session.total_items > 0) {
                    int index = session.current_index;
                    auto item = session.items[index];
                    std::string feed_title = session.feed_title;
                    std::string feed_colour = session.feed_colour;
                    lock.unlock();

                    std::thread([&bot, e, item, feed_title, feed_colour, index, total_items = session.total_items]() {
                        stoatpp::models::MessagePayload payload;
                        payload.content = " ";
                        nlohmann::json embed = nlohmann::json::object();
                        
                        std::string title = item.title.empty() ? "New Feed Item" : item.title;
                        if (title.length() > 100) {
                            title = title.substr(0, 97) + "...";
                        }
                        embed["title"] = "[" + feed_title + "] " + title;
                        embed["colour"] = feed_colour;
                        embed["description"] = item.description;

                        std::string pagination_str = " (" + std::to_string(index + 1) + "/" + std::to_string(total_items) + ")";
                        if (!item.link.empty()) {
                            if (embed["description"].get<std::string>().empty()) {
                                embed["description"] = "[Read Article](" + item.link + ")" + pagination_str;
                            } else {
                                embed["description"] = embed["description"].get<std::string>() + "\n\n[Read Article](" + item.link + ")" + pagination_str;
                            }
                        } else {
                            if (embed["description"].get<std::string>().empty()) {
                                embed["description"] = pagination_str;
                            } else {
                                embed["description"] = embed["description"].get<std::string>() + "\n\n" + pagination_str;
                            }
                        }

                        if (!item.image_url.empty()) {
                            std::string file_id = bot.rest().upload_from_url(item.image_url);
                            if (!file_id.empty()) {
                                embed["media"] = file_id;
                            }
                        }
                        if (!item.feed_logo_url.empty()) {
                            std::string file_id = bot.rest().upload_from_url(item.feed_logo_url);
                            if (!file_id.empty()) {
                                embed["icon_url"] = bot.config().cdn_url + "/attachments/" + file_id + "/logo.png";
                            }
                        }
                        payload.embeds.push_back(embed);

                        bot.edit_message(e.channel_id, e.id, payload, [&bot, e](auto, bool) {
                            bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
                        });
                    }).detach();
                } else {
                    lock.unlock();
                    bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
                }
            }
        });

        auto handle_rss = [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
            if (msg.server_id.empty()) {
                bot.send_message(msg.channel_id, "⚠️ This command can only be used in a server channel.");
                return;
            }
            if (!g_db) {
                bot.send_message(msg.channel_id, "⚠️ Database is unavailable.");
                return;
            }

            stoatpp::cluster* bot_ptr = &bot;
            std::string pfx = msg.prefix.empty() ? bot_ptr->config().command_prefix : msg.prefix;

            if (args.empty()) {
                bot_ptr->send_message(msg.channel_id, "📋 **RSS Feed Commands:**\n"
                                                 "• `" + pfx + "rss add <url>` - Watch a new feed (defaults to current channel).\n"
                                                 "• `" + pfx + "rss target <feed_id|url> <channel> [channels...]` - Route feed posts to target channels.\n"
                                                 "• `" + pfx + "rss remove <feed_id|url>` - Remove a watched feed.\n"
                                                 "• `" + pfx + "rss list` - List watched feeds on this server.\n"
                                                 "• `" + pfx + "rss check [feed_id|url]` - Manually trigger checking feed(s).\n"
                                                 "• `" + pfx + "rss read <feed_id|url>` - Read the latest items from a feed.\n"
                                                 "• `" + pfx + "rss test <feed_id|url>` - Fetch and post the newest item immediately as a test.");
                return;
            }

            std::string subcommand = args[0];
            std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);

            bool requires_permission = (subcommand == "add" || subcommand == "remove" || subcommand == "target" || subcommand == "check" || subcommand == "test");

            auto run_command_logic = [bot_ptr, msg, args, subcommand, pfx]() {
                // --- ADD SUBCOMMAND ---
                if (subcommand == "add") {
                    if (args.size() < 2) {
                        bot_ptr->send_message(msg.channel_id, "usage: `" + pfx + "rss add <url>`");
                        return;
                    }
                    std::string feed_url = args[1];

                    // Validate URL format roughly
                    if (feed_url.rfind("http://", 0) != 0 && feed_url.rfind("https://", 0) != 0) {
                        bot_ptr->send_message(msg.channel_id, "⚠️ Invalid URL scheme. Feed URL must start with `http://` or `https://`.");
                        return;
                    }

                    // Check if already registered on this server
                    auto existing = g_db->get_rss_by_url(msg.server_id, feed_url);
                    if (existing) {
                        bot_ptr->send_message(msg.channel_id, "⚠️ Feed is already added on this server as Feed ID **" + std::to_string(existing->id) + "**.");
                        return;
                    }

                    bot_ptr->send_message(msg.channel_id, "🔍 Fetching and validating feed URL...");

                    // Attempt a test fetch
                    Database::RssFeed test_feed;
                    test_feed.server_id = msg.server_id;
                    test_feed.url = feed_url;
                    test_feed.channels = msg.channel_id;

                    check_and_post_feed(*bot_ptr, test_feed, true, false, [bot_ptr, msg, feed_url](int posted, int interval, std::string err) {
                        if (!err.empty()) {
                            bot_ptr->send_message(msg.channel_id, "⚠️ Failed to fetch or parse feed: " + err + "\nPlease check that the URL is a valid RSS, Atom, or JSON feed.");
                            return;
                        }

                        // Success! Add to database
                        Database::RssFeed db_feed;
                        db_feed.server_id = msg.server_id;
                        db_feed.url = feed_url;
                        db_feed.channels = msg.channel_id;
                        db_feed.seen_items = "[]";
                        db_feed.check_interval = interval;

                        if (g_db->add_rss_feed(db_feed)) {
                            bot_ptr->send_message(msg.channel_id, "✅ **Feed added successfully!**\n"
                                                             "• **ID**: " + std::to_string(db_feed.id) + "\n"
                                                             "• **URL**: " + feed_url + "\n"
                                                             "• **Default Target**: <#" + msg.channel_id + ">\n"
                                                             "• **Check Interval**: " + std::to_string(interval / 60) + " minutes\n"
                                                             "• Posted the newest item as a test.");
                        } else {
                            bot_ptr->send_message(msg.channel_id, "⚠️ Failed to save feed to the database.");
                        }
                    });
                    return;
                }

                // --- TEST SUBCOMMAND ---
                if (subcommand == "test") {
                    if (args.size() < 2) {
                        bot_ptr->send_message(msg.channel_id, "usage: `" + pfx + "rss test <feed_id|url>`");
                        return;
                    }
                    std::string query = args[1];
                    Database::RssFeed test_feed;
                    auto feed_opt = resolve_feed(msg.server_id, query);
                    if (feed_opt) {
                        test_feed = *feed_opt;
                        // Always send test output to the channel where it was triggered
                        test_feed.channels = msg.channel_id;
                    } else {
                        // Validate URL format roughly
                        if (query.rfind("http://", 0) != 0 && query.rfind("https://", 0) != 0) {
                            bot_ptr->send_message(msg.channel_id, "⚠️ Invalid feed ID or URL scheme. URL must start with `http://` or `https://`.");
                            return;
                        }
                        test_feed.server_id = msg.server_id;
                        test_feed.url = query;
                        test_feed.channels = msg.channel_id;
                    }

                    bot_ptr->send_message(msg.channel_id, "🔄 Fetching feed and posting the newest item as a test...");

                    check_and_post_feed(*bot_ptr, test_feed, true, true, [bot_ptr, msg](int posted, int interval, std::string err) {
                        if (!err.empty()) {
                            bot_ptr->send_message(msg.channel_id, "⚠️ Failed to fetch or parse feed: " + err);
                            return;
                        }
                        if (posted == 0) {
                            bot_ptr->send_message(msg.channel_id, "⚠️ Feed contains no items to post.");
                        } else {
                            bot_ptr->send_message(msg.channel_id, "✅ Test completed successfully. Posted the newest item.");
                        }
                    });
                    return;
                }

                // --- REMOVE SUBCOMMAND ---
                if (subcommand == "remove") {
                    if (args.size() < 2) {
                        bot_ptr->send_message(msg.channel_id, "usage: `" + pfx + "rss remove <feed_id|url>`");
                        return;
                    }
                    std::string query = args[1];
                    auto feed = resolve_feed(msg.server_id, query);
                    if (!feed) {
                        bot_ptr->send_message(msg.channel_id, "⚠️ No matching RSS feed found for: `" + query + "`");
                        return;
                    }

                    if (g_db->remove_rss_feed(feed->id, msg.server_id)) {
                        bot_ptr->send_message(msg.channel_id, "✅ Removed feed ID **" + std::to_string(feed->id) + "** (" + feed->url + ").");
                    } else {
                        bot_ptr->send_message(msg.channel_id, "⚠️ Failed to remove feed from the database.");
                    }
                    return;
                }

                // --- LIST SUBCOMMAND ---
                if (subcommand == "list") {
                    auto list = g_db->get_rss_feeds_for_server(msg.server_id);
                    if (list.empty()) {
                        bot_ptr->send_message(msg.channel_id, "ℹ️ No RSS feeds registered on this server. Add one with `" + pfx + "rss add <url>`.");
                        return;
                    }

                    std::string out = "📡 **Watched RSS Feeds:**\n";
                    for (const auto& feed : list) {
                        out += "• **ID " + std::to_string(feed.id) + "**: " + feed.url + "\n";
                        out += "  Check Interval: " + std::to_string(feed.check_interval / 60) + " minutes\n";
                        
                        // Parse target channels
                        std::vector<std::string> ch_ids;
                        std::stringstream ss(feed.channels);
                        std::string ch_id;
                        while (std::getline(ss, ch_id, ',')) {
                            if (!ch_id.empty()) ch_ids.push_back(ch_id);
                        }

                        out += "  Targets: ";
                        for (size_t i = 0; i < ch_ids.size(); ++i) {
                            if (i > 0) out += ", ";
                            out += "<#" + ch_ids[i] + ">";
                        }
                        out += "\n";

                        if (feed.last_checked > 0) {
                            out += "  Last Checked: " + helpers::format_epoch_ms(feed.last_checked * 1000) + "\n";
                        } else {
                            out += "  Last Checked: Never\n";
                        }
                    }
                    bot_ptr->send_message(msg.channel_id, out);
                    return;
                }

                // --- READ SUBCOMMAND ---
                if (subcommand == "read") {
                    if (args.size() < 2) {
                        bot_ptr->send_message(msg.channel_id, "usage: `" + pfx + "rss read <feed_id|url>`");
                        return;
                    }
                    std::string query = args[1];
                    std::string feed_url;
                    std::string feed_title = "RSS Feed";
                    std::string feed_colour = "#f26522";

                    auto feed_opt = resolve_feed(msg.server_id, query);
                    if (feed_opt) {
                        feed_url = feed_opt->url;
                        feed_title = "Feed #" + std::to_string(feed_opt->id);
                    } else {
                        if (query.rfind("http://", 0) != 0 && query.rfind("https://", 0) != 0) {
                            bot_ptr->send_message(msg.channel_id, "⚠️ Invalid feed ID or URL scheme. URL must start with `http://` or `https://`.");
                            return;
                        }
                        feed_url = query;
                    }

                    bot_ptr->send_message(msg.channel_id, "📖 Fetching and loading feed for reading...");

                    fetch_feed_items(feed_url, [bot_ptr, msg, feed_title, feed_colour](std::vector<FeedItem> items, int interval, std::string err) {
                        if (!err.empty()) {
                            bot_ptr->send_message(msg.channel_id, "⚠️ Failed to fetch feed: " + err);
                            return;
                        }
                        if (items.empty()) {
                            bot_ptr->send_message(msg.channel_id, "⚠️ Feed contains no items.");
                            return;
                        }

                        // Cap to 20 items max
                        if (items.size() > 20) {
                            items.resize(20);
                        }

                        auto item = items[0];

                        // Prepare initial embed
                        stoatpp::models::MessagePayload payload;
                        payload.content = " ";
                        nlohmann::json embed = nlohmann::json::object();
                        std::string title = item.title.empty() ? "New Feed Item" : item.title;
                        if (title.length() > 100) {
                            title = title.substr(0, 97) + "...";
                        }
                        embed["title"] = "[" + feed_title + "] " + title;
                        embed["colour"] = feed_colour;
                        embed["description"] = item.description;

                        std::string pagination_str = " (1/" + std::to_string(items.size()) + ")";
                        if (!item.link.empty()) {
                            if (embed["description"].get<std::string>().empty()) {
                                embed["description"] = "[Read Article](" + item.link + ")" + pagination_str;
                            } else {
                                embed["description"] = embed["description"].get<std::string>() + "\n\n[Read Article](" + item.link + ")" + pagination_str;
                            }
                        } else {
                            if (embed["description"].get<std::string>().empty()) {
                                embed["description"] = pagination_str;
                            } else {
                                embed["description"] = embed["description"].get<std::string>() + "\n\n" + pagination_str;
                            }
                        }

                        if (!item.image_url.empty()) {
                            std::string file_id = bot_ptr->rest().upload_from_url(item.image_url);
                            if (!file_id.empty()) {
                                embed["media"] = file_id;
                            }
                        }
                        if (!item.feed_logo_url.empty()) {
                            std::string file_id = bot_ptr->rest().upload_from_url(item.feed_logo_url);
                            if (!file_id.empty()) {
                                embed["icon_url"] = bot_ptr->config().cdn_url + "/attachments/" + file_id + "/logo.png";
                            }
                        }
                        payload.embeds.push_back(embed);

                        bot_ptr->send_message(msg.channel_id, payload, [items, feed_title, feed_colour, msg, bot_ptr](stoatpp::models::Message res, bool success) {
                            if (success) {
                                {
                                    std::lock_guard<std::mutex> lock(g_rss_read_sessions_mutex);
                                    g_rss_read_sessions[res.id] = {
                                        0,
                                        static_cast<int>(items.size()),
                                        msg.author.id,
                                        items,
                                        feed_title,
                                        feed_colour
                                    };
                                }
                                if (items.size() > 1) {
                                    bot_ptr->react_to_message(msg.channel_id, res.id, "⬅");
                                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                                    bot_ptr->react_to_message(msg.channel_id, res.id, "➡");
                                }
                            }
                        });
                    });
                    return;
                }

                // --- TARGET SUBCOMMAND ---
                if (subcommand == "target") {
                    if (args.size() < 2) {
                        bot_ptr->send_message(msg.channel_id, "usage: `" + pfx + "rss target <feed_id|url> <channel> [channels...]`\n"
                                                         "Or if you have only 1 feed: `" + pfx + "rss target <channel> [channels...]`");
                        return;
                    }

                    std::optional<Database::RssFeed> target_feed;
                    std::vector<std::string> channel_args;

                    // Check if first arg is channel
                    std::string first_arg = args[1];
                    bool first_is_channel = (first_arg.rfind("<#", 0) == 0 && first_arg.back() == '>') || 
                                             (first_arg.length() == 26 && first_arg.find_first_not_of("0123456789ABCDEFGHJKMNPQRSTVWXYZabcdefghjkmnpqrstvwxyz") == std::string::npos);

                    if (first_is_channel) {
                        // User omitted feed URL/ID. We check if there's exactly 1 feed.
                        auto server_feeds = g_db->get_rss_feeds_for_server(msg.server_id);
                        if (server_feeds.empty()) {
                            bot_ptr->send_message(msg.channel_id, "⚠️ There are no RSS feeds configured on this server.");
                            return;
                        }
                        if (server_feeds.size() > 1) {
                            bot_ptr->send_message(msg.channel_id, "⚠️ Multiple feeds found on this server. Please specify feed ID: `" + pfx + "rss target <feed_id|url> <channel> [channels...]`.");
                            return;
                        }
                        target_feed = server_feeds[0];
                        for (size_t i = 1; i < args.size(); ++i) {
                            channel_args.push_back(args[i]);
                        }
                    } else {
                        // First arg is feed ID/URL
                        target_feed = resolve_feed(msg.server_id, first_arg);
                        if (!target_feed) {
                            bot_ptr->send_message(msg.channel_id, "⚠️ No matching RSS feed found for: `" + first_arg + "`");
                            return;
                        }
                        if (args.size() < 3) {
                            bot_ptr->send_message(msg.channel_id, "usage: `" + pfx + "rss target <feed_id|url> <channel> [channels...]`");
                            return;
                        }
                        for (size_t i = 2; i < args.size(); ++i) {
                            channel_args.push_back(args[i]);
                        }
                    }

                    int feed_id = target_feed->id;
                    auto state = std::make_shared<TargetCheckState>();
                    state->args = channel_args;

                    // Spin up the thread for target processing
                    std::thread([bot_ptr, msg, state, feed_id, pfx]() {
                        for (const auto& raw_arg : state->args) {
                            std::string target_cid = raw_arg;
                            if (target_cid.rfind("<#", 0) == 0 && target_cid.back() == '>') {
                                target_cid = target_cid.substr(2, target_cid.size() - 3);
                            }

                            // Fetch channel
                            std::optional<stoatpp::models::Channel> ch_opt = bot_ptr->get_channel(target_cid);
                            if (!ch_opt) {
                                auto res = bot_ptr->rest().get_channel(target_cid);
                                if (res.success()) {
                                    ch_opt = stoatpp::models::Channel::from_json(res.body);
                                }
                            }

                            if (!ch_opt) {
                                state->errors.push_back("Could not resolve target channel ID/mention: " + target_cid);
                                continue;
                            }

                            std::string target_srv = ch_opt->server.value_or("");
                            if (target_srv.empty()) {
                                state->errors.push_back("Channel " + target_cid + " is not a server channel.");
                                continue;
                            }

                            if (target_srv == msg.server_id) {
                                state->valid_channel_ids.push_back(target_cid);
                                continue;
                            }

                            // Cross-server check
                            if (bot_ptr->is_owner(msg.author.id)) {
                                state->valid_channel_ids.push_back(target_cid);
                                continue;
                            }

                            // We need to fetch member asynchronously and wait for the result
                            std::mutex cv_m;
                            std::condition_variable cv;
                            bool done = false;
                            bool member_ok = false;
                            stoatpp::models::Member fetched_mem;

                            bot_ptr->fetch_member(target_srv, msg.author.id, [&cv_m, &cv, &done, &member_ok, &fetched_mem](stoatpp::models::Member mem, bool ok) {
                                std::lock_guard<std::mutex> lk(cv_m);
                                fetched_mem = mem;
                                member_ok = ok;
                                done = true;
                                cv.notify_one();
                            });

                            std::unique_lock<std::mutex> lk(cv_m);
                            cv.wait(lk, [&done] { return done; });

                            if (!member_ok) {
                                state->errors.push_back("You are not a member of the target server for <#" + target_cid + ">.");
                                continue;
                            }

                            auto srv_opt = bot_ptr->get_server(target_srv);
                            if (!srv_opt) {
                                state->errors.push_back("The bot is not a member of the target server for <#" + target_cid + ">.");
                                continue;
                            }

                            if (!bot_ptr->has_permission(*srv_opt, fetched_mem, stoatpp::permissions::ManageServer)) {
                                state->errors.push_back("You need Manage Server permission in the server for <#" + target_cid + ">.");
                                continue;
                            }

                            state->valid_channel_ids.push_back(target_cid);
                        }

                        // Now all channels are processed
                        if (!state->errors.empty()) {
                            std::string err_msg = "⚠️ Target configuration failed with errors:\n";
                            for (const auto& err : state->errors) {
                                err_msg += "• " + err + "\n";
                            }
                            bot_ptr->send_message(msg.channel_id, err_msg);
                            return;
                        }

                        if (state->valid_channel_ids.empty()) {
                            bot_ptr->send_message(msg.channel_id, "⚠️ No valid channels were specified.");
                            return;
                        }

                        std::string channels_str;
                        for (size_t i = 0; i < state->valid_channel_ids.size(); ++i) {
                            if (i > 0) channels_str += ",";
                            channels_str += state->valid_channel_ids[i];
                        }

                        if (g_db->update_rss_feed_channels(feed_id, msg.server_id, channels_str)) {
                            std::string ch_mentions;
                            for (const auto& cid : state->valid_channel_ids) {
                                if (!ch_mentions.empty()) ch_mentions += ", ";
                                ch_mentions += "<#" + cid + ">";
                            }
                            bot_ptr->send_message(msg.channel_id, "✅ Updated target channels for feed #" + std::to_string(feed_id) + " to: " + ch_mentions);
                        } else {
                            bot_ptr->send_message(msg.channel_id, "⚠️ Failed to update database.");
                        }
                    }).detach();
                    return;
                }

                // --- CHECK SUBCOMMAND ---
                if (subcommand == "check") {
                    std::vector<Database::RssFeed> feeds_to_check;
                    if (args.size() >= 2) {
                        std::string query = args[1];
                        auto feed = resolve_feed(msg.server_id, query);
                        if (!feed) {
                            bot_ptr->send_message(msg.channel_id, "⚠️ No matching RSS feed found for: `" + query + "`");
                            return;
                        }
                        feeds_to_check.push_back(*feed);
                    } else {
                        feeds_to_check = g_db->get_rss_feeds_for_server(msg.server_id);
                        if (feeds_to_check.empty()) {
                            bot_ptr->send_message(msg.channel_id, "ℹ️ No RSS feeds registered on this server.");
                            return;
                        }
                    }

                    bot_ptr->send_message(msg.channel_id, "🔄 Checking feed(s)...");

                    struct CheckState {
                        std::atomic<int> completed{0};
                        std::atomic<int> total_new_posted{0};
                        std::vector<std::string> feed_errors;
                        std::mutex check_mutex;
                    };
                    auto check_state = std::make_shared<CheckState>();

                    int num_feeds = feeds_to_check.size();
                    for (const auto& feed : feeds_to_check) {
                        {
                            std::lock_guard<std::mutex> lock(g_rss_checking_mutex);
                            if (g_rss_checking_ids.count(feed.id)) {
                                std::lock_guard<std::mutex> st_lock(check_state->check_mutex);
                                check_state->feed_errors.push_back("Feed #" + std::to_string(feed.id) + " is already currently being checked.");
                                check_state->completed++;
                                if (check_state->completed == num_feeds) {
                                    std::string sum = "✅ Feed check complete!\n• Posted " + std::to_string(check_state->total_new_posted) + " new items.";
                                    if (!check_state->feed_errors.empty()) {
                                        sum += "\nErrors:";
                                        for (const auto& e : check_state->feed_errors) sum += "\n- " + e;
                                    }
                                    bot_ptr->send_message(msg.channel_id, sum);
                                }
                                continue;
                            }
                            g_rss_checking_ids.insert(feed.id);
                        }

                        check_and_post_feed(*bot_ptr, feed, false, false, [bot_ptr, msg, feed, check_state, num_feeds](int posted, int interval, std::string err) {
                            {
                                std::lock_guard<std::mutex> lock(g_rss_checking_mutex);
                                g_rss_checking_ids.erase(feed.id);
                            }

                            std::lock_guard<std::mutex> st_lock(check_state->check_mutex);
                            check_state->total_new_posted += posted;
                            if (!err.empty()) {
                                check_state->feed_errors.push_back("Feed #" + std::to_string(feed.id) + ": " + err);
                            }
                            check_state->completed++;

                            if (check_state->completed == num_feeds) {
                                std::string sum = "✅ Feed check complete!\n• Posted " + std::to_string(check_state->total_new_posted) + " new items.";
                                if (!check_state->feed_errors.empty()) {
                                    sum += "\nErrors:";
                                    for (const auto& e : check_state->feed_errors) sum += "\n- " + e;
                                }
                                bot_ptr->send_message(msg.channel_id, sum);
                            }
                        });
                    }
                    return;
                }

                bot_ptr->send_message(msg.channel_id, "⚠️ Unknown subcommand. Use `add`, `remove`, `list`, `target`, `check`, `read`, or `test`.");
            };

            if (requires_permission) {
                bot.fetch_member(msg.server_id, msg.author.id, [bot_ptr, msg, run_command_logic](stoatpp::models::Member member, bool success) {
                    if (!success) {
                        bot_ptr->send_message(msg.channel_id, "⚠️ Failed to verify your permissions.");
                        return;
                    }
                    if (!is_user_trusted_or_owner(*bot_ptr, msg.server_id, msg.author.id, member)) {
                        bot_ptr->send_message(msg.channel_id, "permission error: you need to be trusted or have Manage Server permission to configure RSS settings.");
                        return;
                    }
                    run_command_logic();
                });
            } else {
                run_command_logic();
            }
        };

        bot.register_command({
            "rss",
            {},
            "manage RSS, Atom, and JSON feed watchers.",
            "",
            {"<subcommand>", "[args...]"},
            handle_rss,
            "Utility",
            0,
            0,
            {"add <url>", "target <feed_id> <channel>", "remove <feed_id>", "list", "check [feed_id]", "read <feed_id|url>", "test <feed_id|url>"}
        });

        // Start background checking timer (runs every 60 seconds)
        bot.start_timer([&bot](stoatpp::timer) {
            uint64_t now_ts = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());

            auto feeds = g_db->get_all_rss_feeds();
            for (const auto& feed : feeds) {
                // Check dynamically based on feed's check_interval
                if (now_ts - feed.last_checked >= static_cast<uint64_t>(feed.check_interval)) {
                    {
                        std::lock_guard<std::mutex> lock(g_rss_checking_mutex);
                        if (g_rss_checking_ids.count(feed.id)) continue;
                        g_rss_checking_ids.insert(feed.id);
                    }

                    check_and_post_feed(bot, feed, false, false, [feed](int posted, int interval, std::string err) {
                        std::lock_guard<std::mutex> lock(g_rss_checking_mutex);
                        g_rss_checking_ids.erase(feed.id);
                    });
                }
            }
        }, 60);
    }
};

} // namespace bronx::cogs
