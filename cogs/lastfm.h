#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
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
#include <iomanip>
#include <cctype>
#include <cmath>

namespace bronx::cogs {

class LastfmModule : public stoatpp::bot_module {
private:
    std::string api_key_ = "b25b959554ed76058ac220b7b2e0a026"; // Public fallback/testing API Key

    std::string get_api_key() {
        const char* env_key = std::getenv("LASTFM_API_KEY");
        if (env_key && std::strlen(env_key) > 0) {
            return std::string(env_key);
        }
        return api_key_;
    }

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

    static std::string format_number(int64_t value) {
        std::string num_str = std::to_string(value);
        int insert_position = num_str.length() - 3;
        while (insert_position > 0) {
            num_str.insert(insert_position, ",");
            insert_position -= 3;
        }
        return num_str;
    }

    struct RecentTrack {
        std::string name;
        std::string artist;
        std::string album;
        std::string url;
        std::string image_url;
        bool now_playing = false;
        std::string date_str;
        int64_t total_scrobbles = 0;
    };

    struct ArtistEntry {
        std::string name;
        int64_t playcount = 0;
        int rank = 0;
    };

    bool fetch_user_info(const std::string& username, int64_t& out_playcount, std::string& out_image_url) {
        std::string key = get_api_key();
        httplib::Client cli("https://ws.audioscrobbler.com");
        cli.set_connection_timeout(std::chrono::seconds(5));
        cli.set_read_timeout(std::chrono::seconds(5));

        auto res = cli.Get("/2.0/?method=user.getinfo&user=" + url_encode(username) + "&api_key=" + key + "&format=json");
        if (res && res->status == 200) {
            try {
                auto j = nlohmann::json::parse(res->body);
                if (j.contains("user")) {
                    auto user_obj = j["user"];
                    if (user_obj.contains("playcount")) {
                        out_playcount = std::stoll(user_obj["playcount"].get<std::string>());
                    }
                    if (user_obj.contains("image") && user_obj["image"].is_array() && !user_obj["image"].empty()) {
                        // Get largest image
                        for (auto it = user_obj["image"].rbegin(); it != user_obj["image"].rend(); ++it) {
                            std::string img = (*it).value("#text", "");
                            if (!img.empty()) {
                                out_image_url = img;
                                break;
                            }
                        }
                    }
                    return true;
                }
            } catch (...) {}
        }
        return false;
    }

    std::optional<RecentTrack> fetch_recent_track(const std::string& username) {
        std::string key = get_api_key();
        httplib::Client cli("https://ws.audioscrobbler.com");
        cli.set_connection_timeout(std::chrono::seconds(5));
        cli.set_read_timeout(std::chrono::seconds(5));

        auto res = cli.Get("/2.0/?method=user.getrecenttracks&user=" + url_encode(username) + "&api_key=" + key + "&limit=1&format=json");
        if (res && res->status == 200) {
            try {
                auto j = nlohmann::json::parse(res->body);
                if (j.contains("recenttracks") && j["recenttracks"].contains("track")) {
                    auto tracks = j["recenttracks"]["track"];
                    if (tracks.is_array() && !tracks.empty()) {
                        auto track_obj = tracks[0];
                        RecentTrack t;
                        t.name = track_obj.value("name", "Unknown Track");
                        
                        if (track_obj.contains("artist")) {
                            t.artist = track_obj["artist"].value("#text", "Unknown Artist");
                        }
                        if (track_obj.contains("album")) {
                            t.album = track_obj["album"].value("#text", "");
                        }
                        t.url = track_obj.value("url", "");
                        
                        if (track_obj.contains("@attr")) {
                            auto attr = track_obj["@attr"];
                            t.now_playing = (attr.value("nowplaying", "") == "true");
                        }
                        
                        if (track_obj.contains("date")) {
                            t.date_str = track_obj["date"].value("#text", "");
                        }

                        if (j["recenttracks"].contains("@attr")) {
                            t.total_scrobbles = std::stoll(j["recenttracks"]["@attr"].value("total", "0"));
                        }

                        if (track_obj.contains("image") && track_obj["image"].is_array() && !track_obj["image"].empty()) {
                            // Find largest image (typically size "extralarge" or similar)
                            for (auto it = track_obj["image"].rbegin(); it != track_obj["image"].rend(); ++it) {
                                std::string img = (*it).value("#text", "");
                                if (!img.empty()) {
                                    t.image_url = img;
                                    break;
                                }
                            }
                        }
                        return t;
                    }
                }
            } catch (...) {}
        }
        return std::nullopt;
    }

    std::vector<ArtistEntry> fetch_top_artists(const std::string& username, const std::string& period, int limit) {
        std::vector<ArtistEntry> result;
        std::string key = get_api_key();
        httplib::Client cli("https://ws.audioscrobbler.com");
        cli.set_connection_timeout(std::chrono::seconds(5));
        cli.set_read_timeout(std::chrono::seconds(5));

        std::string url = "/2.0/?method=user.gettopartists&user=" + url_encode(username) + "&api_key=" + key + "&period=" + period + "&limit=" + std::to_string(limit) + "&format=json";
        auto res = cli.Get(url);
        if (res && res->status == 200) {
            try {
                auto j = nlohmann::json::parse(res->body);
                if (j.contains("topartists") && j["topartists"].contains("artist")) {
                    auto artists = j["topartists"]["artist"];
                    if (artists.is_array()) {
                        for (const auto& item : artists) {
                            ArtistEntry entry;
                            entry.name = item.value("name", "Unknown Artist");
                            entry.playcount = std::stoll(item.value("playcount", "0"));
                            if (item.contains("@attr") && item["@attr"].contains("rank")) {
                                entry.rank = std::stoi(item["@attr"].value("rank", "0"));
                            }
                            result.push_back(entry);
                        }
                    }
                }
            } catch (...) {}
        }
        return result;
    }

public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "lastfm",
            {"fm", "lf"},
            "integrates Last.fm to check now playing, top artists, and scrobble statistics.",
            "lastfm [set/unlink/topartists/compare/leaderboard] [args...]",
            {"[subcommand]", "[args...]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) {
                    bot.send_message(msg.channel_id, "⚠️ Database is currently unavailable.");
                    return;
                }

                std::string sub = args.empty() ? "" : args[0];
                std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

                // --- 1. SET SUBCOMMAND ---
                if (sub == "set" || sub == "link") {
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "❌ Usage: `.fm set <lastfm_username>`");
                        return;
                    }
                    std::string lf_user = args[1];
                    std::thread([&bot, msg, lf_user, this]() {
                        int64_t dummy_pc = 0;
                        std::string dummy_img = "";
                        if (!fetch_user_info(lf_user, dummy_pc, dummy_img)) {
                            bot.send_message(msg.channel_id, "❌ Could not find Last.fm user `" + lf_user + "`. Make sure the username is correct.");
                            return;
                        }
                        if (g_db->save_lastfm_link(msg.author.id, lf_user)) {
                            bot.send_message(msg.channel_id, "✅ Successfully linked your account to Last.fm username **" + lf_user + "**!");
                        } else {
                            bot.send_message(msg.channel_id, "⚠️ Verified but failed to write your link to the database.");
                        }
                    }).detach();
                    return;
                }

                // --- 2. UNLINK SUBCOMMAND ---
                if (sub == "unlink" || sub == "remove" || sub == "delete") {
                    std::thread([&bot, msg]() {
                        auto existing = g_db->get_lastfm_link(msg.author.id);
                        if (!existing) {
                            bot.send_message(msg.channel_id, "ℹ️ You do not have a linked Last.fm account.");
                            return;
                        }
                        if (g_db->delete_lastfm_link(msg.author.id)) {
                            bot.send_message(msg.channel_id, "✅ Successfully unlinked your Last.fm account.");
                        } else {
                            bot.send_message(msg.channel_id, "❌ Failed to unlink account from database.");
                        }
                    }).detach();
                    return;
                }

                // --- 3. TOPARTISTS SUBCOMMAND ---
                if (sub == "topartists" || sub == "top" || sub == "ta") {
                    std::thread([&bot, msg, args, this]() {
                        // parse period and user
                        std::string period = "7day";
                        std::string target_query = "";
                        int limit = 10;

                        // simple parse: check if arguments contain time periods
                        for (size_t i = 1; i < args.size(); ++i) {
                            std::string arg = args[i];
                            std::transform(arg.begin(), arg.end(), arg.begin(), ::tolower);
                            if (arg == "7d" || arg == "week" || arg == "7day") period = "7day";
                            else if (arg == "1m" || arg == "month" || arg == "1month") period = "1month";
                            else if (arg == "3m" || arg == "3month") period = "3month";
                            else if (arg == "6m" || arg == "6month") period = "6month";
                            else if (arg == "12m" || arg == "year" || arg == "12month") period = "12month";
                            else if (arg == "overall" || arg == "all") period = "overall";
                            else {
                                // check if number (limit)
                                try {
                                    int l = std::stoi(arg);
                                    if (l > 0 && l <= 50) limit = l;
                                } catch (...) {
                                    // treat as username query
                                    target_query = args[i];
                                }
                            }
                        }

                        std::string resolved_lf_user = "";
                        std::string revolt_user_name = "";

                        if (target_query.empty()) {
                            // caller
                            auto link = g_db->get_lastfm_link(msg.author.id);
                            if (!link) {
                                bot.send_message(msg.channel_id, "⚠️ You haven't linked your Last.fm account yet. Use `.fm set <username>`.");
                                return;
                            }
                            resolved_lf_user = link->lastfm_username;
                            revolt_user_name = msg.author.username;
                        } else {
                            // try resolving as member first
                            std::mutex resolve_mutex;
                            std::condition_variable resolve_cv;
                            bool resolved = false;
                            stoatpp::models::User r_user;

                            helpers::resolve_user(bot, msg.server_id, target_query, [&](stoatpp::models::User u, stoatpp::models::Member m, bool success) {
                                std::lock_guard<std::mutex> lock(resolve_mutex);
                                if (success) {
                                    r_user = u;
                                }
                                resolved = true;
                                resolve_cv.notify_one();
                            });

                            std::unique_lock<std::mutex> lock(resolve_mutex);
                            resolve_cv.wait_for(lock, std::chrono::seconds(2), [&] { return resolved; });

                            if (!r_user.id.empty()) {
                                auto link = g_db->get_lastfm_link(r_user.id);
                                if (link) {
                                    resolved_lf_user = link->lastfm_username;
                                    revolt_user_name = r_user.username;
                                }
                            }

                            if (resolved_lf_user.empty()) {
                                // Fallback: target_query is raw Last.fm username
                                resolved_lf_user = target_query;
                                revolt_user_name = target_query;
                            }
                        }

                        auto top_art = fetch_top_artists(resolved_lf_user, period, limit);
                        if (top_art.empty()) {
                            bot.send_message(msg.channel_id, "❌ Could not retrieve top artists for Last.fm user `" + resolved_lf_user + "`.");
                            return;
                        }

                        stoatpp::models::MessagePayload payload;
                        payload.content = " ";
                        stoatpp::models::Embed embed;
                        embed.set_colour("#D51007");

                        std::string p_label = "Overall";
                        if (period == "7day") p_label = "Last 7 Days";
                        else if (period == "1month") p_label = "Last Month";
                        else if (period == "3month") p_label = "Last 3 Months";
                        else if (period == "6month") p_label = "Last 6 Months";
                        else if (period == "12month") p_label = "Last Year";

                        embed.set_title("💿 Top Artists (" + p_label + ") — " + revolt_user_name)
                             .set_url("https://www.last.fm/user/" + url_encode(resolved_lf_user));

                        std::string desc = "";
                        int rank = 1;
                        for (const auto& art : top_art) {
                            desc += "**" + std::to_string(rank) + ".** " + art.name + " — **" + format_number(art.playcount) + "** plays\n";
                            rank++;
                        }
                        desc += "\n—\n👤 **Last.fm Profile**: [" + resolved_lf_user + "](https://www.last.fm/user/" + url_encode(resolved_lf_user) + ")";
                        embed.set_description(desc);

                        payload.add_embed(embed);
                        bot.send_message(msg.channel_id, payload);
                    }).detach();
                    return;
                }

                // --- 4. COMPARE SUBCOMMAND ---
                if (sub == "compare" || sub == "comp") {
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "❌ Usage: `.fm compare <@user|username>`");
                        return;
                    }
                    std::string target_query = args[1];
                    std::thread([&bot, msg, target_query, this]() {
                        // resolve self
                        auto self_link = g_db->get_lastfm_link(msg.author.id);
                        if (!self_link) {
                            bot.send_message(msg.channel_id, "⚠️ You haven't linked your Last.fm account yet. Use `.fm set <username>`.");
                            return;
                        }

                        std::string target_lf_user = "";
                        std::string target_revolt_name = "";

                        // resolve target
                        std::mutex resolve_mutex;
                        std::condition_variable resolve_cv;
                        bool resolved = false;
                        stoatpp::models::User r_user;

                        helpers::resolve_user(bot, msg.server_id, target_query, [&](stoatpp::models::User u, stoatpp::models::Member m, bool success) {
                            std::lock_guard<std::mutex> lock(resolve_mutex);
                            if (success) {
                                r_user = u;
                            }
                            resolved = true;
                            resolve_cv.notify_one();
                        });

                        std::unique_lock<std::mutex> lock(resolve_mutex);
                        resolve_cv.wait_for(lock, std::chrono::seconds(2), [&] { return resolved; });

                        if (!r_user.id.empty()) {
                            auto link = g_db->get_lastfm_link(r_user.id);
                            if (link) {
                                target_lf_user = link->lastfm_username;
                                target_revolt_name = r_user.username;
                            }
                        }

                        if (target_lf_user.empty()) {
                            // try raw Last.fm username
                            target_lf_user = target_query;
                            target_revolt_name = target_query;
                        }

                        if (self_link->lastfm_username == target_lf_user) {
                            bot.send_message(msg.channel_id, "❌ You cannot compare yourself to the same Last.fm account.");
                            return;
                        }

                        // Fetch top 50 artists for both
                        auto self_artists = fetch_top_artists(self_link->lastfm_username, "overall", 50);
                        auto target_artists = fetch_top_artists(target_lf_user, "overall", 50);

                        if (self_artists.empty() || target_artists.empty()) {
                            bot.send_message(msg.channel_id, "❌ Could not retrieve scrobble details to perform compatibility comparison.");
                            return;
                        }

                        // Compare
                        struct CommonArtist {
                            std::string name;
                            int64_t self_plays = 0;
                            int64_t target_plays = 0;
                            double weight = 0;
                        };
                        std::vector<CommonArtist> common;
                        double common_weight = 0;
                        double max_weight = 0;

                        // compute max weight
                        for (int i = 0; i < 50; ++i) {
                            max_weight += (50.0 - i) * (50.0 - i);
                        }

                        for (size_t i = 0; i < self_artists.size(); ++i) {
                            const auto& sa = self_artists[i];
                            std::string sa_lower = sa.name;
                            std::transform(sa_lower.begin(), sa_lower.end(), sa_lower.begin(), ::tolower);

                            for (size_t j = 0; j < target_artists.size(); ++j) {
                                const auto& ta = target_artists[j];
                                std::string ta_lower = ta.name;
                                std::transform(ta_lower.begin(), ta_lower.end(), ta_lower.begin(), ::tolower);

                                if (sa_lower == ta_lower) {
                                    double w = (50.0 - (double)i) * (50.0 - (double)j);
                                    common_weight += w;
                                    common.push_back({sa.name, sa.playcount, ta.playcount, w});
                                    break;
                                }
                            }
                        }

                        double pct = 0;
                        if (max_weight > 0) {
                            pct = std::sqrt(common_weight / max_weight) * 100.0;
                        }
                        if (pct > 100.0) pct = 100.0;

                        std::string level = "Low";
                        if (pct > 90.0) level = "Superb / Soulmates! 💞";
                        else if (pct > 65.0) level = "Very High 📈";
                        else if (pct > 40.0) level = "High 📊";
                        else if (pct > 15.0) level = "Medium ⚖️";

                        // Sort common artists by weight descending
                        std::sort(common.begin(), common.end(), [](const CommonArtist& a, const CommonArtist& b) {
                            return a.weight > b.weight;
                        });

                        stoatpp::models::MessagePayload payload;
                        payload.content = " ";
                        stoatpp::models::Embed embed;
                        embed.set_colour("#D51007");
                        embed.set_title("👥 Last.fm Compatibility Comparison");

                        std::string desc = "**You** (" + self_link->lastfm_username + ") vs **" + target_revolt_name + "** (" + target_lf_user + ")\n\n";
                        
                        // progress bar
                        int filled = (int)std::round(pct / 10.0);
                        std::string bar = "";
                        for (int i = 0; i < 10; ++i) {
                            if (i < filled) bar += "█";
                            else bar += "░";
                        }
                        desc += "**Compatibility**: **" + std::to_string((int)std::round(pct)) + "%** (" + level + ")\n";
                        desc += "`[" + bar + "]`\n\n";

                        if (common.empty()) {
                            desc += "*You do not share any top 50 artists.*";
                        } else {
                            desc += "**Shared Artists**:\n";
                            size_t show_limit = std::min((size_t)5, common.size());
                            for (size_t i = 0; i < show_limit; ++i) {
                                const auto& c = common[i];
                                desc += "• **" + c.name + "**\n  └─ Plays: You: `" + std::to_string(c.self_plays) + "` · " + target_revolt_name + ": `" + std::to_string(c.target_plays) + "`\n";
                            }
                        }

                        embed.set_description(desc);
                        payload.add_embed(embed);
                        bot.send_message(msg.channel_id, payload);
                    }).detach();
                    return;
                }

                // --- 5. LEADERBOARD SUBCOMMAND ---
                if (sub == "leaderboard" || sub == "lb") {
                    std::thread([&bot, msg, this]() {
                        auto links = g_db->get_all_lastfm_links();
                        if (links.empty()) {
                            bot.send_message(msg.channel_id, "ℹ️ No Last.fm accounts are linked on this server.");
                            return;
                        }

                        // get server members
                        std::unordered_set<std::string> server_member_ids;
                        auto res = bot.rest().get_server_members(msg.server_id);
                        if (res.success()) {
                            nlohmann::json members_arr;
                            if (res.body.is_array()) {
                                members_arr = res.body;
                            } else if (res.body.is_object() && res.body.contains("members") && res.body["members"].is_array()) {
                                members_arr = res.body["members"];
                            }
                            for (const auto& item : members_arr) {
                                auto m = stoatpp::models::Member::from_json(item);
                                if (!m.id.empty()) {
                                    server_member_ids.insert(m.id);
                                }
                            }
                        }

                        struct LeaderboardEntry {
                            std::string user_id;
                            std::string lastfm_username;
                            int64_t playcount = 0;
                        };

                        std::vector<LeaderboardEntry> entries;
                        std::mutex entries_mutex;
                        std::vector<std::thread> threads;

                        for (const auto& link : links) {
                            if (server_member_ids.count(link.user_id) == 0) {
                                continue;
                            }
                            threads.emplace_back([&entries, &entries_mutex, link, this]() {
                                int64_t pc = 0;
                                std::string img = "";
                                if (fetch_user_info(link.lastfm_username, pc, img)) {
                                    std::lock_guard<std::mutex> lock(entries_mutex);
                                    entries.push_back({link.user_id, link.lastfm_username, pc});
                                }
                            });
                        }

                        for (auto& t : threads) {
                            if (t.joinable()) t.join();
                        }

                        if (entries.empty()) {
                            bot.send_message(msg.channel_id, "ℹ️ No active scrobble data found for linked users.");
                            return;
                        }

                        std::sort(entries.begin(), entries.end(), [](const LeaderboardEntry& a, const LeaderboardEntry& b) {
                            return a.playcount > b.playcount;
                        });

                        stoatpp::models::MessagePayload payload;
                        payload.content = " ";
                        stoatpp::models::Embed embed;
                        embed.set_colour("#D51007");
                        embed.set_title("🏆 Server Last.fm Leaderboard");

                        std::string desc = "";
                        int rank = 1;
                        for (const auto& entry : entries) {
                            std::string medal = std::to_string(rank) + ".";
                            if (rank == 1) medal = "🥇";
                            else if (rank == 2) medal = "🥈";
                            else if (rank == 3) medal = "🥉";

                            desc += medal + " <@" + entry.user_id + "> (" + entry.lastfm_username + ") — **" + format_number(entry.playcount) + "** scrobbles\n";
                            rank++;
                        }
                        embed.set_description(desc);

                        payload.add_embed(embed);
                        bot.send_message(msg.channel_id, payload);
                    }).detach();
                    return;
                }

                // --- 6. DEFAULT: NOW PLAYING ---
                std::string target_query = "";
                if (!args.empty()) {
                    target_query = args[0];
                }

                std::thread([&bot, msg, target_query, this]() {
                    std::string resolved_lf_user = "";
                    std::string target_id = "";
                    std::string target_revolt_name = "";

                    if (target_query.empty()) {
                        auto link = g_db->get_lastfm_link(msg.author.id);
                        if (!link) {
                            bot.send_message(msg.channel_id, "⚠️ You haven't linked your Last.fm account yet. Use `.fm set <username>`.");
                            return;
                        }
                        resolved_lf_user = link->lastfm_username;
                        target_id = msg.author.id;
                        target_revolt_name = msg.author.username;
                    } else {
                        // resolve user query
                        std::mutex resolve_mutex;
                        std::condition_variable resolve_cv;
                        bool resolved = false;
                        stoatpp::models::User r_user;

                        helpers::resolve_user(bot, msg.server_id, target_query, [&](stoatpp::models::User u, stoatpp::models::Member m, bool success) {
                            std::lock_guard<std::mutex> lock(resolve_mutex);
                            if (success) {
                                r_user = u;
                            }
                            resolved = true;
                            resolve_cv.notify_one();
                        });

                        std::unique_lock<std::mutex> lock(resolve_mutex);
                        resolve_cv.wait_for(lock, std::chrono::seconds(2), [&] { return resolved; });

                        if (!r_user.id.empty()) {
                            auto link = g_db->get_lastfm_link(r_user.id);
                            if (link) {
                                resolved_lf_user = link->lastfm_username;
                                target_id = r_user.id;
                                target_revolt_name = r_user.username;
                            }
                        }

                        if (resolved_lf_user.empty()) {
                            // Fallback: target_query is raw Last.fm username
                            resolved_lf_user = target_query;
                            target_revolt_name = target_query;
                        }
                    }

                    auto track_opt = fetch_recent_track(resolved_lf_user);
                    if (!track_opt) {
                        bot.send_message(msg.channel_id, "❌ Could not fetch recent tracks for user `" + resolved_lf_user + "`.");
                        return;
                    }

                    stoatpp::models::MessagePayload payload;
                    payload.content = " ";
                    stoatpp::models::Embed embed;
                    embed.set_colour("#D51007");

                    std::string action = track_opt->now_playing ? "Now Playing" : "Recently Played";
                    embed.set_title("🎵 Last.fm — " + action + " (" + target_revolt_name + ")")
                         .set_url("https://www.last.fm/user/" + url_encode(resolved_lf_user));

                    std::string desc = "🎶 **Track**: [" + track_opt->name + "](" + track_opt->url + ")\n";
                    desc += "👤 **Artist**: **" + track_opt->artist + "**\n";
                    if (!track_opt->album.empty()) {
                        desc += "💿 **Album**: *" + track_opt->album + "*\n";
                    }
                    desc += "\n📊 **Total Scrobbles**: `" + format_number(track_opt->total_scrobbles) + "`\n";
                    desc += "\n—\n👤 **Last.fm Profile**: [" + resolved_lf_user + "](https://www.last.fm/user/" + url_encode(resolved_lf_user) + ")\n";
                    if (track_opt->now_playing) {
                        desc += "🟢 *Listening now*";
                    } else if (!track_opt->date_str.empty()) {
                        desc += "🕒 *Played: " + track_opt->date_str + "*";
                    }

                    embed.set_description(desc);

                    // Add rich image/thumbnail
                    if (!track_opt->image_url.empty()) {
                        std::string file_id = bot.rest().upload_from_url(track_opt->image_url);
                        if (!file_id.empty()) {
                            embed.set_icon_url(bot.config().cdn_url + "/attachments/" + file_id + "/album.png");
                        } else {
                            // fallback direct URL if upload fails
                            embed.set_icon_url(track_opt->image_url);
                        }
                    }

                    payload.add_embed(embed);
                    bot.send_message(msg.channel_id, payload);
                }).detach();
            },
            "Utility",
            0,
            0,
            {"[subcommand]", "[args...]"}
        });
    }
};

} // namespace bronx::cogs
