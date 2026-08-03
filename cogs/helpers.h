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
#include <optional>
#include <algorithm>
#include "stoatpp/cluster.h"
#include "stoatpp/utils/ulid.h"
#include <nlohmann/json.hpp>
#include "../database.h"
#include <memory>

namespace bronx {
    inline std::shared_ptr<Database> g_db = nullptr;
}

namespace bronx::helpers {

inline std::string shell_escape(const std::string& str) {
    std::string res;
    res += "'";
    for (char c : str) {
        if (c == '\'') {
            res += "'\\''";
        } else {
            res += c;
        }
    }
    res += "'";
    return res;
}

inline std::optional<uint64_t> get_unit_multiplier(std::string unit) {
    for (char &c : unit) c = std::tolower(c);
    if (unit == "s" || unit == "sec" || unit == "second" || unit == "seconds") return 1;
    if (unit == "m" || unit == "min" || unit == "mins" || unit == "minute" || unit == "minutes") return 60;
    if (unit == "h" || unit == "hr" || unit == "hrs" || unit == "hour" || unit == "hours") return 3600;
    if (unit == "d" || unit == "day" || unit == "days") return 86400;
    if (unit == "w" || unit == "wk" || unit == "wks" || unit == "week" || unit == "weeks") return 604800;
    if (unit == "mo" || unit == "month" || unit == "months") return 2592000;
    if (unit == "y" || unit == "yr" || unit == "yrs" || unit == "year" || unit == "years") return 31536000;
    return std::nullopt;
}

inline bool parse_compound_token(const std::string& token, uint64_t& out_seconds, bool& out_has_unit) {
    uint64_t total = 0;
    bool has_unit = false;
    std::string current_num = "";
    
    for (size_t i = 0; i < token.size(); ++i) {
        char c = token[i];
        if (std::isdigit(c)) {
            current_num += c;
        } else {
            if (current_num.empty()) return false;
            
            std::string unit = "";
            while (i < token.size() && std::isalpha(token[i])) {
                unit += token[i];
                i++;
            }
            i--;
            
            auto mult = get_unit_multiplier(unit);
            if (!mult) return false;
            
            uint64_t val = 0;
            try { val = std::stoull(current_num); } catch (...) { return false; }
            total += val * (*mult);
            has_unit = true;
            current_num = "";
        }
    }
    
    if (!current_num.empty()) {
        if (has_unit) return false; // Ambiguous trailing number without unit
        uint64_t val = 0;
        try { val = std::stoull(current_num); } catch (...) { return false; }
        total = val;
    }
    
    if (!has_unit && current_num.empty()) return false;
    
    out_seconds = total;
    out_has_unit = has_unit;
    return true;
}

inline uint64_t parse_duration(const std::string& input) {
    uint64_t total_seconds = 0;
    std::string current_num = "";
    
    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        if (std::isdigit(c)) {
            current_num += c;
        } else {
            if (current_num.empty()) continue;
            
            // Skip spaces/commas between number and unit
            size_t temp_i = i;
            while (temp_i < input.size() && (std::isspace(input[temp_i]) || input[temp_i] == ',')) {
                temp_i++;
            }
            
            std::string unit = "";
            while (temp_i < input.size() && std::isalpha(input[temp_i])) {
                unit += std::tolower(input[temp_i]);
                temp_i++;
            }
            
            // Check if it's a valid unit
            auto mult = get_unit_multiplier(unit);
            if (mult) {
                uint64_t val = 0;
                try { val = std::stoull(current_num); } catch (...) { return 0; }
                total_seconds += val * (*mult);
                current_num = "";
                i = temp_i - 1; // Advance loop index to the end of the unit
            } else {
                // Check if it's just trailing whitespace/separator at the end
                bool is_end_of_string = true;
                for (size_t k = temp_i; k < input.size(); ++k) {
                    if (!std::isspace(input[k]) && input[k] != ',') {
                        is_end_of_string = false;
                        break;
                    }
                }
                if (is_end_of_string) {
                    uint64_t val = 0;
                    try { val = std::stoull(current_num); } catch (...) { return 0; }
                    total_seconds += val * 60; // default to minutes for compatibility with old helpers::parse_duration
                    current_num = "";
                    break;
                } else {
                    return 0; // Number followed by invalid unit or non-unit words
                }
            }
        }
    }
    
    if (!current_num.empty()) {
        uint64_t val = 0;
        try { val = std::stoull(current_num); } catch (...) { return 0; }
        total_seconds += val * 60; // default to minutes for compatibility with old helpers::parse_duration
    }
    
    return total_seconds;
}

inline bool parse_duration_from_args(const std::vector<std::string>& args, size_t start_idx, uint64_t& out_seconds, size_t& out_consumed, bool default_to_seconds = false) {
    uint64_t total_seconds = 0;
    bool has_any_unit = false;
    size_t i = start_idx;
    bool parsed_something = false;
    
    while (i < args.size()) {
        std::string token = args[i];
        
        // Handle "and" / "&" connector
        if (parsed_something && (token == "and" || token == "&")) {
            if (i + 1 < args.size()) {
                std::string next_token = args[i + 1];
                if (!next_token.empty() && std::isdigit(next_token[0])) {
                    i++;
                    continue;
                }
            }
            break;
        }
        
        // Check if token contains any digits.
        bool has_digit = false;
        for (char c : token) {
            if (std::isdigit(c)) {
                has_digit = true;
                break;
            }
        }
        
        if (!has_digit) {
            break;
        }
        
        uint64_t token_secs = 0;
        bool token_has_unit = false;
        
        if (!parse_compound_token(token, token_secs, token_has_unit)) {
            break;
        }
        
        if (!token_has_unit) {
            // Raw number. Check if the next token is a unit.
            if (i + 1 < args.size()) {
                std::string next_token = args[i + 1];
                auto mult = get_unit_multiplier(next_token);
                if (mult) {
                    total_seconds += token_secs * (*mult);
                    has_any_unit = true;
                    parsed_something = true;
                    i += 2;
                    continue;
                }
            }
            
            // Raw number not followed by unit
            if (!parsed_something) {
                total_seconds += token_secs;
                parsed_something = true;
                i++;
                continue;
            } else {
                break;
            }
        } else {
            total_seconds += token_secs;
            has_any_unit = true;
            parsed_something = true;
            i++;
            continue;
        }
    }
    
    if (!parsed_something) {
        return false;
    }
    
    if (!has_any_unit && !default_to_seconds) {
        total_seconds *= 60;
    }
    
    out_seconds = total_seconds;
    out_consumed = i - start_idx;
    return true;
}

inline std::string format_duration(uint64_t secs) {
    if (secs == 0) return "0s";
    uint64_t d = secs / 86400, h = (secs % 86400) / 3600,
             m = (secs % 3600) / 60, s = secs % 60;
    std::string r;
    if (d) r += std::to_string(d) + "d ";
    if (h) r += std::to_string(h) + "h ";
    if (m) r += std::to_string(m) + "m ";
    if (s) r += std::to_string(s) + "s ";
    if (!r.empty()) r.pop_back();
    return r;
}

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
    std::string target_query = "";
    std::string server_id = "";
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
                    callback(user, member, true);
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

            auto search_res = bot.rest().search_users(search_username);
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

inline std::string upload_avatar(stoatpp::cluster& bot, const std::string& input_url) {
    if (input_url.empty()) return "";
    if (input_url.rfind(bot.config().autumn_url, 0) == 0) {
        return input_url;
    }
    try {
        std::string file_id = bot.rest().upload_from_url(input_url);
        if (!file_id.empty()) {
            return bot.config().autumn_url + "/attachments/" + file_id;
        }
    } catch (...) {
        // Fall back to original url if download/upload fails
    }
    return input_url;
}

inline std::optional<std::string> extract_file_id(const std::optional<std::string>& avatar_str) {
    if (!avatar_str) return std::nullopt;
    std::string s = *avatar_str;
    if (s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0) {
        size_t q = s.find_first_of("?#");
        if (q != std::string::npos) s = s.substr(0, q);

        std::vector<std::string> parts;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, '/')) {
            if (!item.empty()) parts.push_back(item);
        }

        if (!parts.empty()) {
            std::string file_id = parts.back();
            if (parts.size() >= 2 && file_id.find('.') != std::string::npos) {
                file_id = parts[parts.size() - 2];
            }
            if (!file_id.empty() && file_id.find('.') == std::string::npos &&
                file_id != "attachments" && file_id != "avatars" &&
                file_id != "backgrounds" && file_id != "icons") {
                return file_id;
            }
        }
        return std::nullopt;
    }
    if (s.find('.') != std::string::npos) {
        return std::nullopt;
    }
    return s;
}

inline std::string get_raw_args(const stoatpp::events::Message& msg, const std::string& command_name) {
    std::string prefix = msg.prefix;
    size_t start_pos = prefix.length() + command_name.length();
    if (start_pos >= msg.content.length()) return "";
    
    size_t idx = start_pos;
    while (idx < msg.content.length()) {
        char c = msg.content[idx];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        idx++;
    }
    return msg.content.substr(idx);
}
} // namespace bronx::helpers

namespace bronx::cogs {
    using helpers::parse_duration;
    using helpers::parse_duration_from_args;
    using helpers::format_duration;
    using helpers::get_raw_args;
}
