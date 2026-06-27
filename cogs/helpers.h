#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <thread>
#include <functional>
#include <cstdlib>
#include <algorithm>
#include "stoatpp/cluster.h"
#include "stoatpp/utils/ulid.h"
#include <nlohmann/json.hpp>

namespace bronx::helpers {

inline std::string format_epoch_ms(uint64_t ms) {
    std::time_t sec = ms / 1000;
    struct tm buf;
    localtime_r(&sec, &buf);
    
    char date_str[64];
    std::strftime(date_str, sizeof(date_str), "%b %d %Y", &buf);
    return date_str;
}

inline std::string format_iso_string(const std::string& iso) {
    if (iso.empty()) return "none";
    struct tm t = {};
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) >= 3) {
        t.tm_year = year - 1900;
        t.tm_mon = month - 1;
        t.tm_mday = day;
        t.tm_hour = hour;
        t.tm_min = minute;
        t.tm_sec = second;
        t.tm_isdst = -1;
        
        std::time_t sec = std::mktime(&t);
        struct tm buf;
        localtime_r(&sec, &buf);
        char date_str[64];
        std::strftime(date_str, sizeof(date_str), "%b %d %Y", &buf);
        return date_str;
    }
    return iso;
}

struct PaginationSession {
    int current_page = 0;
    int max_pages = 3;
    std::string user_id;
    std::string type = "general";
};

inline std::map<std::string, PaginationSession> g_pages;
inline std::mutex g_pages_mutex;

inline int get_highest_rank(stoatpp::cluster& bot, const std::string& server_id, const stoatpp::models::Member& member) {
    auto server_opt = bot.get_server(server_id);
    if (!server_opt) return 999999;
    
    if (server_opt->owner == member.id) {
        return -1;
    }
    
    int highest = 999999;
    for (const auto& r_id : member.roles) {
        for (const auto& r : server_opt->roles) {
            if (r.id == r_id) {
                if (r.rank < highest) {
                    highest = r.rank;
                }
            }
        }
    }
    return highest;
}

inline bool check_hierarchy(stoatpp::cluster& bot, const std::string& server_id, const stoatpp::models::Member& a, const stoatpp::models::Member& b) {
    int rank_a = get_highest_rank(bot, server_id, a);
    int rank_b = get_highest_rank(bot, server_id, b);
    return rank_a < rank_b;
}

inline int get_role_rank(stoatpp::cluster& bot, const std::string& server_id, const std::string& role_id) {
    auto server_opt = bot.get_server(server_id);
    if (!server_opt) return 999999;
    for (const auto& r : server_opt->roles) {
        if (r.id == role_id) {
            return r.rank;
        }
    }
    return 999999;
}

inline std::string get_future_iso_timestamp(int offset_seconds) {
    auto now = std::chrono::system_clock::now();
    auto future = now + std::chrono::seconds(offset_seconds);
    std::time_t future_time = std::chrono::system_clock::to_time_t(future);
    struct tm buf;
    gmtime_r(&future_time, &buf);
    char out_str[64];
    std::strftime(out_str, sizeof(out_str), "%Y-%m-%dT%H:%M:%SZ", &buf);
    return out_str;
}

inline void resolve_user(stoatpp::cluster& bot, const std::string& server_id, const std::string& query, std::function<void(stoatpp::models::User, stoatpp::models::Member, bool)> callback) {
    if (query.empty()) {
        callback({}, {}, false);
        return;
    }

    std::string target_id = query;
    if (target_id.rfind("<@", 0) == 0 && target_id.back() == '>') {
        target_id = target_id.substr(2, target_id.size() - 3);
    }

    if (target_id.size() == 26 && target_id.find_first_not_of("0123456789ABCDEFGHJKMNPQRSTVWXYZabcdefghjkmnpqrstvwxyz") == std::string::npos) {
        bot.fetch_user(target_id, [&bot, server_id, callback](stoatpp::models::User user, bool success) {
            if (!success) {
                callback({}, {}, false);
                return;
            }
            if (!server_id.empty()) {
                bot.fetch_member(server_id, user.id, [user, callback](stoatpp::models::Member member, bool success_mem) {
                    callback(user, member, success_mem);
                });
            } else {
                callback(user, {}, true);
            }
        });
        return;
    }

    std::string search_username = target_id;
    std::string search_discrim = "";
    auto hash_pos = target_id.find('#');
    if (hash_pos != std::string::npos) {
        search_username = target_id.substr(0, hash_pos);
        search_discrim = target_id.substr(hash_pos + 1);
    }

    std::thread([&bot, server_id, search_username, search_discrim, callback]() {
        try {
            auto res = bot.rest().get_server_members(server_id);
            if (!res.success()) {
                callback({}, {}, false);
                return;
            }

            nlohmann::json users_arr = nlohmann::json::array();
            nlohmann::json members_arr = nlohmann::json::array();

            if (res.body.is_array()) {
                members_arr = res.body;
            } else if (res.body.is_object()) {
                if (res.body.contains("members") && res.body["members"].is_array()) {
                    members_arr = res.body["members"];
                }
                if (res.body.contains("users") && res.body["users"].is_array()) {
                    users_arr = res.body["users"];
                }
            }

            std::map<std::string, stoatpp::models::User> user_map;
            for (const auto& u : users_arr) {
                auto user = stoatpp::models::User::from_json(u);
                user_map[user.id] = user;
            }

            auto to_lower = [](std::string s) {
                for (auto& c : s) c = std::tolower(c);
                return s;
            };

            std::string lower_username = to_lower(search_username);

            for (const auto& m_json : members_arr) {
                auto member = stoatpp::models::Member::from_json(m_json);
                stoatpp::models::User user;
                
                auto it = user_map.find(member.id);
                if (it != user_map.end()) {
                    user = it->second;
                } else {
                    continue;
                }

                std::string user_name_lower = to_lower(user.username);
                std::string nickname_lower = member.nickname ? to_lower(*member.nickname) : "";

                bool name_match = (user_name_lower == lower_username || nickname_lower == lower_username);
                bool discrim_match = search_discrim.empty() || (user.discriminator == search_discrim);

                if (name_match && discrim_match) {
                    bot.fetch_user(user.id, [member, callback](stoatpp::models::User full_user, bool success_user) {
                        if (success_user) {
                            callback(full_user, member, true);
                        } else {
                            callback({}, {}, false);
                        }
                    });
                    return;
                }
            }

            auto search_res = bot.rest().get("/users/search?query=" + search_username);
            if (search_res.success() && search_res.body.is_array()) {
                for (const auto& u : search_res.body) {
                    auto user = stoatpp::models::User::from_json(u);
                    if (to_lower(user.username) == lower_username && (search_discrim.empty() || user.discriminator == search_discrim)) {
                        bot.fetch_user(user.id, [callback](stoatpp::models::User full_user, bool success_user) {
                            if (success_user) {
                                callback(full_user, {}, true);
                            } else {
                                callback({}, {}, false);
                            }
                        });
                        return;
                    }
                }
            }

            callback({}, {}, false);
        } catch (...) {
            callback({}, {}, false);
        }
    }).detach();
}

} // namespace bronx::helpers
