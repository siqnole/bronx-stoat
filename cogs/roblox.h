#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include <httplib.h>
#include <sstream>
#include <algorithm>
#include <random>
#include <thread>
#include <chrono>
#include <shared_mutex>
#include <mutex>

namespace bronx::cogs {

class RobloxModule : public stoatpp::bot_module {
private:
    struct RobloxProfile {
        uint64_t id = 0;
        std::string username;
        std::string displayName;
        std::string description;
        std::string created;
    };

    struct RobloxSocials {
        int64_t friends = 0;
        int64_t followers = 0;
        int64_t following = 0;
    };

    struct RolimonsItem {
        std::string name;
        int64_t rap = 0;
        int64_t value = 0;
    };

    std::unordered_map<uint64_t, RolimonsItem> rolimons_cache_;
    std::shared_mutex rolimons_cache_mutex_;
    std::time_t last_rolimons_update_ = 0;

    void update_rolimons_cache() {
        std::cout << "[bronx roblox] fetching Rolimons itemdetails...\n";
        httplib::Client cli("https://www.rolimons.com");
        auto res = cli.Get("/itemapi/itemdetails");
        if (res && res->status == 200) {
            try {
                auto j = nlohmann::json::parse(res->body);
                if (j.contains("items") && j["items"].is_object()) {
                    std::unique_lock<std::shared_mutex> lock(rolimons_cache_mutex_);
                    rolimons_cache_.clear();
                    for (auto& [id_str, val_array] : j["items"].items()) {
                        uint64_t asset_id = std::stoull(id_str);
                        if (val_array.is_array() && val_array.size() >= 4) {
                            RolimonsItem item;
                            item.name = val_array[0].is_string() ? val_array[0].get<std::string>() : "";
                            item.rap = val_array[2].is_number() ? val_array[2].get<int64_t>() : 0;
                            
                            int64_t val = val_array[3].is_number() ? val_array[3].get<int64_t>() : -1;
                            item.value = (val == -1) ? item.rap : val;
                            rolimons_cache_[asset_id] = item;
                        }
                    }
                    last_rolimons_update_ = std::time(nullptr);
                    std::cout << "[bronx roblox] successfully cached " << rolimons_cache_.size() << " items from Rolimons.\n";
                }
            } catch (...) {
                std::cerr << "[bronx roblox] failed to parse Rolimons itemdetails JSON.\n";
            }
        } else {
            std::cerr << "[bronx roblox] failed to fetch Rolimons itemdetails. status: " << (res ? res->status : 0) << "\n";
        }
    }

    void ensure_rolimons_cache() {
        bool needs_update = false;
        {
            std::shared_lock<std::shared_mutex> lock(rolimons_cache_mutex_);
            if (rolimons_cache_.empty() || std::time(nullptr) - last_rolimons_update_ > 1800) {
                needs_update = true;
            }
        }
        if (needs_update) {
            std::thread([this]() {
                update_rolimons_cache();
            }).detach();
        }
    }

    static std::optional<uint64_t> resolve_roblox_username(const std::string& username, std::string& out_name, std::string& out_display_name) {
        httplib::Client cli("https://users.roblox.com");
        nlohmann::json body = {
            {"usernames", {username}},
            {"excludeBannedUsers", false}
        };
        auto res = cli.Post("/v1/usernames/users", body.dump(), "application/json");
        if (res && res->status == 200) {
            try {
                auto j = nlohmann::json::parse(res->body);
                if (j.contains("data") && j["data"].is_array() && !j["data"].empty()) {
                    auto user_data = j["data"][0];
                    out_name = user_data["name"].get<std::string>();
                    out_display_name = user_data["displayName"].get<std::string>();
                    return user_data["id"].get<uint64_t>();
                }
            } catch (...) {}
        }
        return std::nullopt;
    }

    static std::optional<RobloxProfile> fetch_roblox_profile(uint64_t userId) {
        const char* api_key = std::getenv("ROBLOX_API_KEY");
        if (api_key && std::strlen(api_key) > 0) {
            httplib::Client cli("https://apis.roblox.com");
            httplib::Headers headers = {
                {"x-api-key", api_key}
            };
            auto res = cli.Get("/cloud/v2/users/" + std::to_string(userId), headers);
            if (res && res->status == 200) {
                try {
                    auto j = nlohmann::json::parse(res->body);
                    RobloxProfile profile;
                    profile.id = userId;
                    if (j.contains("name") && j["name"].is_string()) profile.username = j["name"].get<std::string>();
                    if (j.contains("displayName") && j["displayName"].is_string()) profile.displayName = j["displayName"].get<std::string>();
                    if (j.contains("about") && j["about"].is_string()) profile.description = j["about"].get<std::string>();
                    if (j.contains("createTime") && j["createTime"].is_string()) profile.created = j["createTime"].get<std::string>();
                    return profile;
                } catch (...) {}
            }
        }

        // Fallback to public API
        httplib::Client cli("https://users.roblox.com");
        auto res = cli.Get("/v1/users/" + std::to_string(userId));
        if (res && res->status == 200) {
            try {
                auto j = nlohmann::json::parse(res->body);
                RobloxProfile profile;
                profile.id = userId;
                if (j.contains("name") && j["name"].is_string()) profile.username = j["name"].get<std::string>();
                if (j.contains("displayName") && j["displayName"].is_string()) profile.displayName = j["displayName"].get<std::string>();
                if (j.contains("description") && j["description"].is_string()) profile.description = j["description"].get<std::string>();
                if (j.contains("created") && j["created"].is_string()) profile.created = j["created"].get<std::string>();
                return profile;
            } catch (...) {}
        }
        return std::nullopt;
    }

    static std::string fetch_roblox_avatar_url(uint64_t userId) {
        httplib::Client cli("https://thumbnails.roblox.com");
        auto res = cli.Get("/v1/users/avatar?userIds=" + std::to_string(userId) + "&size=352x352&format=Png&isCircular=false");
        if (res && res->status == 200) {
            try {
                auto j = nlohmann::json::parse(res->body);
                if (j.contains("data") && j["data"].is_array() && !j["data"].empty()) {
                    auto item = j["data"][0];
                    if (item.contains("imageUrl") && item["imageUrl"].is_string()) {
                        return item["imageUrl"].get<std::string>();
                    }
                }
            } catch (...) {}
        }
        return "";
    }

    static std::string fetch_roblox_headshot_url(uint64_t userId) {
        httplib::Client cli("https://thumbnails.roblox.com");
        auto res = cli.Get("/v1/users/avatar-headshot?userIds=" + std::to_string(userId) + "&size=150x150&format=Png&isCircular=false");
        if (res && res->status == 200) {
            try {
                auto j = nlohmann::json::parse(res->body);
                if (j.contains("data") && j["data"].is_array() && !j["data"].empty()) {
                    auto item = j["data"][0];
                    if (item.contains("imageUrl") && item["imageUrl"].is_string()) {
                        return item["imageUrl"].get<std::string>();
                    }
                }
            } catch (...) {}
        }
        return "";
    }

    static RobloxSocials fetch_roblox_socials(uint64_t userId) {
        RobloxSocials socials;
        httplib::Client cli("https://friends.roblox.com");
        
        // Friends
        auto res_friends = cli.Get("/v1/users/" + std::to_string(userId) + "/friends/count");
        if (res_friends && res_friends->status == 200) {
            try {
                auto j = nlohmann::json::parse(res_friends->body);
                if (j.contains("count") && j["count"].is_number()) socials.friends = j["count"].get<int64_t>();
            } catch(...) {}
        }
        
        // Followers
        auto res_followers = cli.Get("/v1/users/" + std::to_string(userId) + "/followers/count");
        if (res_followers && res_followers->status == 200) {
            try {
                auto j = nlohmann::json::parse(res_followers->body);
                if (j.contains("count") && j["count"].is_number()) socials.followers = j["count"].get<int64_t>();
            } catch(...) {}
        }
        
        // Following
        auto res_following = cli.Get("/v1/users/" + std::to_string(userId) + "/followings/count");
        if (res_following && res_following->status == 200) {
            try {
                auto j = nlohmann::json::parse(res_following->body);
                if (j.contains("count") && j["count"].is_number()) socials.following = j["count"].get<int64_t>();
            } catch(...) {}
        }
        
        return socials;
    }

    static std::string generate_keywords() {
        const std::vector<std::string> words = {
            "stoat", "bronx", "roblox", "apple", "banana", "cherry",
            "dragon", "eagle", "forest", "galaxy", "harbor", "island",
            "jungle", "koala", "lemon", "mountain", "ocean", "penguin",
            "quartz", "river", "shadow", "tiger", "umbrella", "valley",
            "winter", "yellow", "zenith", "builder", "studio", "gamer"
        };
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, words.size() - 1);
        std::string result = "";
        for (int i = 0; i < 6; ++i) {
            if (i > 0) result += " ";
            result += words[dis(gen)];
        }
        return result;
    }

    static bool contains_keywords(const std::string& text, const std::string& keywords) {
        std::string text_lower = text;
        std::transform(text_lower.begin(), text_lower.end(), text_lower.begin(), ::tolower);
        
        std::stringstream ss(keywords);
        std::string kw;
        while (ss >> kw) {
            std::transform(kw.begin(), kw.end(), kw.begin(), ::tolower);
            if (text_lower.find(kw) == std::string::npos) {
                return false;
            }
        }
        return true;
    }

    void edit_profile_embed(stoatpp::cluster& bot, const std::string& channel_id, const std::string& temp_msg_id, const RobloxProfile& profile, const std::optional<std::string>& linked_revolt_id, const RobloxSocials& socials, uint64_t rap_value, uint64_t total_value, bool rap_private) {
        std::string full_avatar_url = fetch_roblox_avatar_url(profile.id);
        std::string headshot_url = fetch_roblox_headshot_url(profile.id);

        stoatpp::models::MessagePayload payload;
        payload.content = " ";
        
        stoatpp::models::Embed embed;
        embed.set_title("Roblox Profile — " + profile.displayName + " (@" + profile.username + ")")
             .set_url("https://www.roblox.com/users/" + std::to_string(profile.id) + "/profile")
             .set_colour("#00A2FF");

        if (!headshot_url.empty()) {
            std::string headshot_id = bot.rest().upload_from_url(headshot_url);
            if (!headshot_id.empty()) {
                embed.set_icon_url(bot.config().cdn_url + "/attachments/" + headshot_id + "/headshot.png");
            }
        }

        if (!full_avatar_url.empty()) {
            std::string file_id = bot.rest().upload_from_url(full_avatar_url);
            if (!file_id.empty()) {
                embed.set_image(file_id);
            }
        }

        std::string date_str = profile.created;
        if (date_str.size() >= 10) {
            date_str = date_str.substr(0, 10);
        }

        std::string desc = "• **Roblox ID**: `" + std::to_string(profile.id) + "`\n";
        desc += "• **Created**: " + date_str + "\n\n";

        desc += "**Social Statistics**:\n";
        desc += "• **Friends**: " + std::to_string(socials.friends) + "\n";
        desc += "• **Followers**: " + std::to_string(socials.followers) + "\n";
        desc += "• **Following**: " + std::to_string(socials.following) + "\n\n";

        desc += "**Economic Statistics**:\n";
        if (rap_private) {
            desc += "• **Collectibles RAP**: *Private Inventory*\n";
            desc += "• **Estimated Value**: *Private Inventory*\n\n";
        } else {
            desc += "• **Collectibles RAP**: `" + std::to_string(rap_value) + "`\n";
            desc += "• **Estimated Value**: `" + std::to_string(total_value) + "`\n\n";
        }
        
        desc += "**About**:\n";
        if (profile.description.empty()) {
            desc += "*No description set.*";
        } else {
            desc += profile.description;
        }
        desc += "\n\n";

        desc += "**Linked User**: ";
        if (linked_revolt_id) {
            desc += "<@" + *linked_revolt_id + ">";
        } else {
            desc += "*None*";
        }

        embed.set_description(desc);
        payload.add_embed(embed);
        bot.edit_message(channel_id, temp_msg_id, payload);
    }

public:
    void register_handlers(stoatpp::cluster& bot) override {
        // Pre-warm Rolimons cache
        std::thread([this]() {
            update_rolimons_cache();
        }).detach();

        bot.register_command({
            "roblox",
            {"rbx"},
            "view Roblox profile info or link accounts.",
            "",
            {"[user|username|set]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling roblox...\n";

                if (!g_db) {
                    bot.send_message(msg.channel_id, "⚠️ Database is currently unavailable.");
                    return;
                }

                if (!args.empty() && args[0] == "set") {
                    // LINK ACCOUNT WIZARD
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "usage: `.roblox set <username>`");
                        return;
                    }

                    std::string username = args[1];
                    std::thread([&bot, msg, username]() {
                        std::string clean_name, disp_name;
                        auto roblox_id_opt = resolve_roblox_username(username, clean_name, disp_name);
                        if (!roblox_id_opt) {
                            bot.send_message(msg.channel_id, "❌ Could not find a Roblox account matching `" + username + "`.");
                            return;
                        }

                        uint64_t roblox_id = *roblox_id_opt;
                        std::string keywords = generate_keywords();

                        bot.send_message(msg.channel_id, 
                            "ℹ️ **Roblox Account Linking Verification**\n"
                            "To verify that you own the Roblox account **" + clean_name + "** (" + std::to_string(roblox_id) + "), please edit your Roblox profile bio/description to include the following 6 keywords:\n"
                            "```\n" + keywords + "\n```\n"
                            "Once done, reply with `verify` or `done` in this channel within 5 minutes.\n"
                            "Reply with `cancel` to abort."
                        );

                        struct Session {
                            int attempts_left = 5;
                            std::chrono::steady_clock::time_point expires_at = std::chrono::steady_clock::now() + std::chrono::minutes(5);
                        };

                        auto session = std::make_shared<Session>();

                        // Recursive await message handler
                        auto await_handler = std::make_shared<std::function<void(std::optional<stoatpp::events::Message>)>>();
                        *await_handler = [&bot, msg, roblox_id, clean_name, keywords, session, await_handler](std::optional<stoatpp::events::Message> rep) {
                            if (!rep) {
                                bot.send_message(msg.channel_id, "⏱️ Verification session timed out. Linking canceled.");
                                return;
                            }

                            std::string content = rep->content;
                            std::transform(content.begin(), content.end(), content.begin(), ::tolower);
                            content.erase(0, content.find_first_not_of(" \t\r\n"));
                            content.erase(content.find_last_not_of(" \t\r\n") + 1);

                            if (content == "cancel") {
                                bot.send_message(msg.channel_id, "🚫 Account linking canceled.");
                                return;
                            }

                            if (content == "verify" || content == "done") {
                                auto profile_opt = fetch_roblox_profile(roblox_id);
                                if (!profile_opt) {
                                    bot.send_message(msg.channel_id, "⚠️ Failed to fetch your Roblox profile description. Please try again.");
                                    return;
                                }

                                if (contains_keywords(profile_opt->description, keywords)) {
                                    if (g_db->save_roblox_link(msg.author.id, roblox_id, clean_name)) {
                                        bot.send_message(msg.channel_id, "✅ Successfully linked your account to Roblox account **" + clean_name + "**!");
                                    } else {
                                        bot.send_message(msg.channel_id, "⚠️ Account verified but failed to write link to the database.");
                                    }
                                    return;
                                }

                                session->attempts_left--;
                                if (session->attempts_left <= 0) {
                                    bot.send_message(msg.channel_id, "❌ Maximum verification attempts reached. Account linking canceled.");
                                    return;
                                }

                                auto now = std::chrono::steady_clock::now();
                                if (now >= session->expires_at) {
                                    bot.send_message(msg.channel_id, "⏱️ Verification session timed out. Linking canceled.");
                                    return;
                                }

                                bot.send_message(msg.channel_id, "❌ Could not find the verification keywords in your bio. Please make sure you saved the changes and reply `verify` again. (" + std::to_string(session->attempts_left) + " attempts left)");
                                bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(std::chrono::duration_cast<std::chrono::seconds>(session->expires_at - now).count()), *await_handler);
                            } else {
                                // Ignore non-command responses and keep listening
                                auto now = std::chrono::steady_clock::now();
                                if (now < session->expires_at) {
                                    bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(std::chrono::duration_cast<std::chrono::seconds>(session->expires_at - now).count()), *await_handler);
                                }
                            }
                        };

                        bot.await_message(msg.channel_id, msg.author.id, std::chrono::minutes(5), *await_handler);
                    }).detach();
                    return;
                }

                // LOOKUP PROFILE
                std::string target = args.empty() ? msg.author.id : args[0];
                
                bot.send_message(msg.channel_id, "⏳ *Fetching Roblox profile information...*", [this, &bot, msg, target](stoatpp::models::Message temp_msg, bool ok) {
                    if (!ok) return;
                    std::string temp_msg_id = temp_msg.id;

                    helpers::resolve_user(bot, msg.server_id, target, [this, &bot, msg, temp_msg_id, target](stoatpp::models::User user, stoatpp::models::Member member, bool success) {
                        if (success) {
                            std::thread([this, &bot, msg, temp_msg_id, user]() {
                                ensure_rolimons_cache();
                                
                                auto link_opt = g_db->get_roblox_link(user.id);
                                if (!link_opt) {
                                    bot.edit_message(msg.channel_id, temp_msg_id, "⚠️ **" + user.username + "** does not have a linked Roblox account.");
                                    return;
                                }

                                auto profile_opt = fetch_roblox_profile(link_opt->roblox_id);
                                if (!profile_opt) {
                                    bot.edit_message(msg.channel_id, temp_msg_id, "❌ Linked Roblox account not found or API failed.");
                                    return;
                                }

                                bool rap_private = false;
                                uint64_t rap_value = 0;
                                uint64_t total_value = 0;
                                
                                // Fetch collectibles
                                std::string cursor = "";
                                httplib::Client cli("https://inventory.roblox.com");
                                for (int page = 0; page < 5; ++page) {
                                    std::string path = "/v1/users/" + std::to_string(profile_opt->id) + "/assets/collectibles?sortOrder=Asc&limit=100";
                                    if (!cursor.empty()) path += "&cursor=" + cursor;
                                    auto res = cli.Get(path);
                                    if (!res) break;
                                    if (res->status == 403) {
                                        rap_private = true;
                                        break;
                                    }
                                    if (res->status != 200) break;
                                    try {
                                        auto j = nlohmann::json::parse(res->body);
                                        if (j.contains("data") && j["data"].is_array()) {
                                            std::shared_lock<std::shared_mutex> lock(rolimons_cache_mutex_);
                                            for (const auto& item : j["data"]) {
                                                uint64_t asset_id = item.contains("assetId") ? item["assetId"].get<uint64_t>() : 0;
                                                uint64_t item_rap = item.contains("recentAveragePrice") ? item["recentAveragePrice"].get<uint64_t>() : 0;
                                                rap_value += item_rap;
                                                
                                                auto rit = rolimons_cache_.find(asset_id);
                                                if (rit != rolimons_cache_.end()) {
                                                    total_value += rit->second.value;
                                                } else {
                                                    total_value += item_rap;
                                                }
                                            }
                                        }
                                        if (j.contains("nextPageCursor") && j["nextPageCursor"].is_string() && !j["nextPageCursor"].get<std::string>().empty()) {
                                            cursor = j["nextPageCursor"].get<std::string>();
                                        } else {
                                            break;
                                        }
                                    } catch (...) {
                                        break;
                                    }
                                }

                                RobloxSocials socials = fetch_roblox_socials(profile_opt->id);
                                edit_profile_embed(bot, msg.channel_id, temp_msg_id, *profile_opt, user.id, socials, rap_value, total_value, rap_private);
                            }).detach();
                        } else {
                            if (target.rfind("<@", 0) == 0 && target.back() == '>') {
                                bot.edit_message(msg.channel_id, temp_msg_id, "❌ Could not find that Revolt user.");
                                return;
                            }

                            std::thread([this, &bot, msg, temp_msg_id, target]() {
                                ensure_rolimons_cache();

                                auto cached_link_opt = g_db->get_roblox_link_by_roblox_username(target);
                                std::optional<std::string> linked_revolt_id;
                                uint64_t roblox_id = 0;

                                if (cached_link_opt) {
                                    roblox_id = cached_link_opt->roblox_id;
                                    linked_revolt_id = cached_link_opt->user_id;
                                } else {
                                    std::string clean_name, disp_name;
                                    auto roblox_id_opt = resolve_roblox_username(target, clean_name, disp_name);
                                    if (!roblox_id_opt) {
                                        bot.edit_message(msg.channel_id, temp_msg_id, "❌ Could not find Roblox user or Revolt member matching `" + target + "`.");
                                        return;
                                    }
                                    roblox_id = *roblox_id_opt;

                                    auto cached_id_link_opt = g_db->get_roblox_link_by_roblox_id(roblox_id);
                                    if (cached_id_link_opt) {
                                        linked_revolt_id = cached_id_link_opt->user_id;
                                    }
                                }

                                auto profile_opt = fetch_roblox_profile(roblox_id);
                                if (!profile_opt) {
                                    bot.edit_message(msg.channel_id, temp_msg_id, "❌ Failed to fetch Roblox profile info.");
                                    return;
                                }

                                bool rap_private = false;
                                uint64_t rap_value = 0;
                                uint64_t total_value = 0;
                                
                                // Fetch collectibles
                                std::string cursor = "";
                                httplib::Client cli("https://inventory.roblox.com");
                                for (int page = 0; page < 5; ++page) {
                                    std::string path = "/v1/users/" + std::to_string(profile_opt->id) + "/assets/collectibles?sortOrder=Asc&limit=100";
                                    if (!cursor.empty()) path += "&cursor=" + cursor;
                                    auto res = cli.Get(path);
                                    if (!res) break;
                                    if (res->status == 403) {
                                        rap_private = true;
                                        break;
                                    }
                                    if (res->status != 200) break;
                                    try {
                                        auto j = nlohmann::json::parse(res->body);
                                        if (j.contains("data") && j["data"].is_array()) {
                                            std::shared_lock<std::shared_mutex> lock(rolimons_cache_mutex_);
                                            for (const auto& item : j["data"]) {
                                                uint64_t asset_id = item.contains("assetId") ? item["assetId"].get<uint64_t>() : 0;
                                                uint64_t item_rap = item.contains("recentAveragePrice") ? item["recentAveragePrice"].get<uint64_t>() : 0;
                                                rap_value += item_rap;
                                                
                                                auto rit = rolimons_cache_.find(asset_id);
                                                if (rit != rolimons_cache_.end()) {
                                                    total_value += rit->second.value;
                                                } else {
                                                    total_value += item_rap;
                                                }
                                            }
                                        }
                                        if (j.contains("nextPageCursor") && j["nextPageCursor"].is_string() && !j["nextPageCursor"].get<std::string>().empty()) {
                                            cursor = j["nextPageCursor"].get<std::string>();
                                        } else {
                                            break;
                                        }
                                    } catch (...) {
                                        break;
                                    }
                                }

                                RobloxSocials socials = fetch_roblox_socials(profile_opt->id);
                                edit_profile_embed(bot, msg.channel_id, temp_msg_id, *profile_opt, linked_revolt_id, socials, rap_value, total_value, rap_private);
                            }).detach();
                        }
                    });
                });
            },
            "Utility",
            0,
            0,
            {"set <username>", "<@user|username>"}
        });
    }
};

} // namespace bronx::cogs
