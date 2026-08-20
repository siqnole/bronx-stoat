#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include <chrono>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <unordered_map>
#include <optional>

namespace bronx::cogs {

class TimezoneModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
        
        auto tz_handler = [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
            std::cout << "[bronx command] handling tz/time/timezone...\n";
            
            if (args.empty()) {
                // Show author's timezone/time
                if (!g_db) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Database error: DB connection is unavailable.");
                    return;
                }
                auto tz_opt = g_db->get_user_timezone(msg.author.id);
                if (!tz_opt) {
                    stoatpp::models::MessagePayload payload;
                    payload.content = " ";
                    stoatpp::models::Embed embed;
                    embed.set_colour(0xFF7675) // Soft red
                         .set_title("🕒 Timezone Not Set")
                         .set_description("You haven't set your timezone yet!\n\nUse `.tz set <timezone>` to set it.\nExample: `.tz set Toronto` or `.tz set EST+7`.");
                    payload.add_embed(embed);
                    bot.send_message(msg.channel_id, payload);
                    return;
                }
                
                std::string tz_name = *tz_opt;
                int offset_seconds = 0;
                std::string resolved_name;
                if (!parse_timezone_spec(tz_name, offset_seconds, resolved_name)) {
                    offset_seconds = 0;
                    resolved_name = "UTC";
                }
                
                std::string time_str = format_time_with_offset(offset_seconds);
                std::string utc_offset_str = format_utc_offset(offset_seconds);
                
                stoatpp::models::MessagePayload payload;
                payload.content = " ";
                stoatpp::models::Embed embed;
                embed.set_colour(0x6C5CE7) // Sleek purple
                     .set_title("🕒 Your Local Time")
                     .set_description("Your timezone is set to **" + resolved_name + "** (" + utc_offset_str + ").\n\n**Current Time:**\n`" + time_str + "`");
                payload.add_embed(embed);
                bot.send_message(msg.channel_id, payload);
                return;
            }
            
            if (args[0] == "set") {
                if (args.size() < 2) {
                    stoatpp::models::MessagePayload payload;
                    payload.content = " ";
                    stoatpp::models::Embed embed;
                    embed.set_colour(0xFF7675)
                         .set_title("🕒 Timezone Set Error")
                         .set_description("Please specify a timezone name, abbreviation, or offset.\n\nExamples:\n• `.tz set toronto`\n• `.tz set America/New_York`\n• `.tz set EST+7`\n• `.tz set UTC-5`\n• `.tz set +5:30`\n• `.tz set london`\n• `.tz set PST`.");
                    payload.add_embed(embed);
                    bot.send_message(msg.channel_id, payload);
                    return;
                }
                
                // Join remaining arguments
                std::string tz_input = "";
                for (size_t i = 1; i < args.size(); ++i) {
                    if (i > 1) tz_input += " ";
                    tz_input += args[i];
                }
                
                int offset_seconds = 0;
                std::string resolved_name;
                if (!parse_timezone_spec(tz_input, offset_seconds, resolved_name)) {
                    stoatpp::models::MessagePayload payload;
                    payload.content = " ";
                    stoatpp::models::Embed embed;
                    embed.set_colour(0xFF7675)
                         .set_title("🕒 Invalid Timezone")
                         .set_description("Could not resolve timezone: `" + tz_input + "`\n\nPlease try using a major city, standard offset, or abbreviation.\n\nExamples:\n• `.tz set toronto`\n• `.tz set America/New_York`\n• `.tz set EST+7`\n• `.tz set UTC-5`\n• `.tz set +5:30`\n• `.tz set london`\n• `.tz set PST`.");
                    payload.add_embed(embed);
                    bot.send_message(msg.channel_id, payload);
                    return;
                }
                
                if (!g_db) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Database error: DB connection is unavailable.");
                    return;
                }
                if (g_db->save_user_timezone(msg.author.id, resolved_name)) {
                    std::string time_str = format_time_with_offset(offset_seconds);
                    std::string utc_offset_str = format_utc_offset(offset_seconds);
                    
                    stoatpp::models::MessagePayload payload;
                    payload.content = " ";
                    stoatpp::models::Embed embed;
                    embed.set_colour(0x00CEC9) // Turquoise success color
                         .set_title("🕒 Timezone Saved")
                         .set_description("Successfully updated your timezone to **" + resolved_name + "** (" + utc_offset_str + ").\n\n**Current Local Time:**\n`" + time_str + "`");
                    payload.add_embed(embed);
                    bot.send_message(msg.channel_id, payload);
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Database error: failed to save your timezone. Please try again later.");
                }
                return;
            }
            
            // Check other user
            std::string query_str = "";
            for (size_t i = 0; i < args.size(); ++i) {
                if (i > 0) query_str += " ";
                query_str += args[i];
            }
            
            helpers::resolve_user(bot, msg.server_id, query_str, [this, &bot, msg, query_str](stoatpp::models::User user, stoatpp::models::Member member, bool success) {
                if (!success || user.id.empty()) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Could not find user matching: `" + query_str + "`");
                    return;
                }
                
                if (!g_db) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Database error: DB connection is unavailable.");
                    return;
                }
                auto tz_opt = g_db->get_user_timezone(user.id);
                if (!tz_opt) {
                    stoatpp::models::MessagePayload payload;
                    payload.content = " ";
                    stoatpp::models::Embed embed;
                    embed.set_colour(0xFF7675)
                         .set_title("🕒 Timezone Not Set")
                         .set_description("**" + user.username + "** has not set their timezone yet.");
                    payload.add_embed(embed);
                    bot.send_message(msg.channel_id, payload);
                    return;
                }
                
                std::string tz_name = *tz_opt;
                int offset_seconds = 0;
                std::string resolved_name;
                if (!parse_timezone_spec(tz_name, offset_seconds, resolved_name)) {
                    offset_seconds = 0;
                    resolved_name = "UTC";
                }
                
                std::string time_str = format_time_with_offset(offset_seconds);
                std::string utc_offset_str = format_utc_offset(offset_seconds);
                
                stoatpp::models::MessagePayload payload;
                payload.content = " ";
                stoatpp::models::Embed embed;
                embed.set_colour(0x6C5CE7)
                     .set_title("🕒 Local Time for " + user.username)
                     .set_description("**User:** <@" + user.id + ">\n**Timezone:** " + resolved_name + " (" + utc_offset_str + ")\n\n**Current Local Time:**\n`" + time_str + "`");
                payload.add_embed(embed);
                bot.send_message(msg.channel_id, payload);
            });
        };
        
        bot.register_command({
            "tz",
            {"time", "timezone"},
            "view or set user local timezone and current time.",
            ".tz | .tz set <timezone> | .tz <@user>",
            {"[set <timezone> | @user]"},
            tz_handler,
            "General"
        });
    }

private:
    static inline uint32_t read_be32(const char* p) {
        return (static_cast<uint32_t>(static_cast<unsigned char>(p[0])) << 24) |
               (static_cast<uint32_t>(static_cast<unsigned char>(p[1])) << 16) |
               (static_cast<uint32_t>(static_cast<unsigned char>(p[2])) << 8)  |
               (static_cast<uint32_t>(static_cast<unsigned char>(p[3])));
    }

    static inline int32_t read_be32_signed(const char* p) {
        uint32_t val = read_be32(p);
        return *reinterpret_cast<int32_t*>(&val);
    }

    static inline uint64_t read_be64(const char* p) {
        return (static_cast<uint64_t>(static_cast<unsigned char>(p[0])) << 56) |
               (static_cast<uint64_t>(static_cast<unsigned char>(p[1])) << 48) |
               (static_cast<uint64_t>(static_cast<unsigned char>(p[2])) << 40) |
               (static_cast<uint64_t>(static_cast<unsigned char>(p[3])) << 32) |
               (static_cast<uint64_t>(static_cast<unsigned char>(p[4])) << 24) |
               (static_cast<uint64_t>(static_cast<unsigned char>(p[5])) << 16) |
               (static_cast<uint64_t>(static_cast<unsigned char>(p[6])) << 8)  |
               (static_cast<uint64_t>(static_cast<unsigned char>(p[7])));
    }

    struct LocalTimeType {
        int32_t utoff;
        bool isdst;
        uint8_t desig_idx;
    };

    static std::optional<int32_t> get_timezone_offset_from_file(const std::string& tz_name, std::time_t now) {
        std::string path = "/usr/share/zoneinfo/" + tz_name;
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return std::nullopt;

        char header[44];
        if (!f.read(header, 44)) return std::nullopt;

        if (header[0] != 'T' || header[1] != 'Z' || header[2] != 'i' || header[3] != 'f') {
            return std::nullopt;
        }

        char version = header[4];

        auto parse_block = [&](bool is_64) -> std::optional<int32_t> {
            uint32_t tzh_ttisgmtcnt = read_be32(&header[20]);
            uint32_t tzh_ttisstdcnt = read_be32(&header[24]);
            uint32_t tzh_leapcnt    = read_be32(&header[28]);
            uint32_t tzh_timecnt    = read_be32(&header[32]);
            uint32_t tzh_typecnt    = read_be32(&header[36]);
            uint32_t tzh_charcnt    = read_be32(&header[40]);

            std::vector<int64_t> transition_times(tzh_timecnt);
            if (is_64) {
                std::vector<char> time_buf(tzh_timecnt * 8);
                if (tzh_timecnt > 0) {
                    if (!f.read(time_buf.data(), time_buf.size())) return std::nullopt;
                    for (uint32_t i = 0; i < tzh_timecnt; ++i) {
                        transition_times[i] = read_be64(&time_buf[i * 8]);
                    }
                }
            } else {
                std::vector<char> time_buf(tzh_timecnt * 4);
                if (tzh_timecnt > 0) {
                    if (!f.read(time_buf.data(), time_buf.size())) return std::nullopt;
                    for (uint32_t i = 0; i < tzh_timecnt; ++i) {
                        transition_times[i] = read_be32_signed(&time_buf[i * 4]);
                    }
                }
            }

            std::vector<uint8_t> transition_types(tzh_timecnt);
            if (tzh_timecnt > 0) {
                if (!f.read(reinterpret_cast<char*>(transition_types.data()), tzh_timecnt)) return std::nullopt;
            }

            std::vector<LocalTimeType> ttinfo(tzh_typecnt);
            for (uint32_t i = 0; i < tzh_typecnt; ++i) {
                char info_buf[6];
                if (!f.read(info_buf, 6)) return std::nullopt;
                ttinfo[i].utoff = read_be32_signed(&info_buf[0]);
                ttinfo[i].isdst = info_buf[4] != 0;
                ttinfo[i].desig_idx = static_cast<uint8_t>(info_buf[5]);
            }

            std::vector<char> abbrs(tzh_charcnt);
            if (tzh_charcnt > 0) {
                if (!f.read(abbrs.data(), tzh_charcnt)) return std::nullopt;
            }

            f.seekg(tzh_leapcnt * (is_64 ? 12 : 8), std::ios::cur);
            f.seekg(tzh_ttisstdcnt, std::ios::cur);
            f.seekg(tzh_ttisgmtcnt, std::ios::cur);

            if (tzh_timecnt == 0 || now < transition_times[0]) {
                for (const auto& info : ttinfo) {
                    if (!info.isdst) return info.utoff;
                }
                if (!ttinfo.empty()) return ttinfo[0].utoff;
                return 0;
            }

            auto it = std::upper_bound(transition_times.begin(), transition_times.end(), now);
            size_t idx = std::distance(transition_times.begin(), it);
            if (idx == 0) {
                return ttinfo[0].utoff;
            }
            uint8_t type_idx = transition_types[idx - 1];
            if (type_idx < ttinfo.size()) {
                return ttinfo[type_idx].utoff;
            }
            return 0;
        };

        if (version == '2' || version == '3') {
            uint32_t tzh_ttisgmtcnt = read_be32(&header[20]);
            uint32_t tzh_ttisstdcnt = read_be32(&header[24]);
            uint32_t tzh_leapcnt    = read_be32(&header[28]);
            uint32_t tzh_timecnt    = read_be32(&header[32]);
            uint32_t tzh_typecnt    = read_be32(&header[36]);
            uint32_t tzh_charcnt    = read_be32(&header[40]);

            uint32_t skip_bytes = tzh_timecnt * 4 + tzh_timecnt + tzh_typecnt * 6 + tzh_charcnt + tzh_leapcnt * 8 + tzh_ttisstdcnt + tzh_ttisgmtcnt;
            f.seekg(skip_bytes, std::ios::cur);

            if (!f.read(header, 44)) return std::nullopt;
            if (header[0] != 'T' || header[1] != 'Z' || header[2] != 'i' || header[3] != 'f') {
                return std::nullopt;
            }
            return parse_block(true);
        } else {
            return parse_block(false);
        }
    }

    static bool parse_offset_part(const std::string& str, double& hours_out) {
        if (str.empty()) return false;
        size_t start = 0;
        bool negative = false;
        if (str[0] == '+') {
            start = 1;
        } else if (str[0] == '-') {
            negative = true;
            start = 1;
        }
        
        std::string num_part = str.substr(start);
        if (num_part.empty()) return false;
        
        size_t colon = num_part.find(':');
        if (colon != std::string::npos) {
            std::string hrs_str = num_part.substr(0, colon);
            std::string mins_str = num_part.substr(colon + 1);
            try {
                double hrs = std::stod(hrs_str);
                double mins = std::stod(mins_str);
                hours_out = hrs + (mins / 60.0);
                if (negative) hours_out = -hours_out;
                return true;
            } catch (...) {
                return false;
            }
        }
        
        try {
            hours_out = std::stod(num_part);
            if (negative) hours_out = -hours_out;
            return true;
        } catch (...) {
            return false;
        }
    }

    static std::string clean_string(const std::string& s) {
        std::string res;
        for (char c : s) {
            if (std::isalnum(static_cast<unsigned char>(c))) {
                res += std::tolower(static_cast<unsigned char>(c));
            }
        }
        return res;
    }

    static bool parse_timezone_spec(const std::string& input, int& offset_out, std::string& name_out) {
        std::string clean = clean_string(input);
        if (clean.empty()) return false;

        if (clean == "globally" || clean == "global" || clean == "utc" || clean == "gmt" || clean == "z") {
            offset_out = 0;
            name_out = "UTC";
            return true;
        }

        // Maps of geographical timezones
        static const std::unordered_map<std::string, std::string> GEOGRAPHIC_MAP = {
            {"toronto", "America/Toronto"},
            {"torontocanada", "America/Toronto"},
            {"ontario", "America/Toronto"},
            {"newyork", "America/New_York"},
            {"newyorkcity", "America/New_York"},
            {"nyc", "America/New_York"},
            {"losangeles", "America/Los_Angeles"},
            {"la", "America/Los_Angeles"},
            {"california", "America/Los_Angeles"},
            {"chicago", "America/Chicago"},
            {"denver", "America/Denver"},
            {"london", "Europe/London"},
            {"londonuk", "Europe/London"},
            {"londonengland", "Europe/London"},
            {"uk", "Europe/London"},
            {"gb", "Europe/London"},
            {"paris", "Europe/Paris"},
            {"france", "Europe/Paris"},
            {"berlin", "Europe/Berlin"},
            {"germany", "Europe/Berlin"},
            {"tokyo", "Asia/Tokyo"},
            {"tokyojapan", "Asia/Tokyo"},
            {"japan", "Asia/Tokyo"},
            {"sydney", "Australia/Sydney"},
            {"sydneyaustralia", "Australia/Sydney"},
            {"melbourne", "Australia/Melbourne"},
            {"singapore", "Asia/Singapore"},
            {"hongkong", "Asia/Hong_Kong"}
        };

        auto geo_it = GEOGRAPHIC_MAP.find(clean);
        if (geo_it != GEOGRAPHIC_MAP.end()) {
            auto offset_opt = get_timezone_offset_from_file(geo_it->second, std::time(nullptr));
            if (offset_opt) {
                offset_out = *offset_opt;
                name_out = geo_it->second;
                return true;
            }
        }

        auto offset_opt = get_timezone_offset_from_file(input, std::time(nullptr));
        if (offset_opt) {
            offset_out = *offset_opt;
            name_out = input;
            return true;
        }

        static const std::unordered_map<std::string, double> ABBR_MAP = {
            {"UTC", 0.0},
            {"GMT", 0.0},
            {"Z", 0.0},
            {"EST", -5.0},
            {"EDT", -4.0},
            {"CST", -6.0},
            {"CDT", -5.0},
            {"MST", -7.0},
            {"MDT", -6.0},
            {"PST", -8.0},
            {"PDT", -7.0},
            {"AKST", -9.0},
            {"AKDT", -8.0},
            {"HST", -10.0},
            {"BST", 1.0},
            {"CET", 1.0},
            {"CEST", 2.0},
            {"EET", 2.0},
            {"EEST", 3.0},
            {"MSK", 3.0},
            {"GST", 4.0},
            {"IST", 5.5},
            {"ICT", 7.0},
            {"JST", 9.0},
            {"KST", 9.0},
            {"AEST", 10.0},
            {"AEDT", 11.0},
            {"NZST", 12.0},
            {"NZDT", 13.0}
        };

        std::string uppercase_input = input;
        std::transform(uppercase_input.begin(), uppercase_input.end(), uppercase_input.begin(), ::toupper);
        uppercase_input.erase(std::remove_if(uppercase_input.begin(), uppercase_input.end(), ::isspace), uppercase_input.end());

        std::string matched_abbr = "";
        double base_offset_hours = 0.0;
        for (const auto& [abbr, offset] : ABBR_MAP) {
            if (uppercase_input.rfind(abbr, 0) == 0) {
                if (abbr.length() > matched_abbr.length()) {
                    matched_abbr = abbr;
                    base_offset_hours = offset;
                }
            }
        }

        if (!matched_abbr.empty()) {
            std::string suffix = uppercase_input.substr(matched_abbr.length());
            if (suffix.empty()) {
                offset_out = static_cast<int>(base_offset_hours * 3600);
                name_out = matched_abbr;
                return true;
            }

            double suffix_hours = 0.0;
            if (parse_offset_part(suffix, suffix_hours)) {
                double total_hours = base_offset_hours + suffix_hours;
                offset_out = static_cast<int>(total_hours * 3600);
                name_out = matched_abbr + (suffix[0] == '+' || suffix[0] == '-' ? "" : "+") + suffix;
                return true;
            }
        }

        double direct_hours = 0.0;
        if (parse_offset_part(uppercase_input, direct_hours)) {
            offset_out = static_cast<int>(direct_hours * 3600);
            name_out = std::string("UTC") + (uppercase_input[0] == '+' || uppercase_input[0] == '-' ? "" : "+") + uppercase_input;
            return true;
        }

        return false;
    }

    static std::string format_time_with_offset(int offset_seconds) {
        auto now = std::chrono::system_clock::now();
        std::time_t utc_time = std::chrono::system_clock::to_time_t(now);
        std::time_t local_time = utc_time + offset_seconds;
        
        struct tm buf;
        gmtime_r(&local_time, &buf);
        
        char date_str[128];
        std::strftime(date_str, sizeof(date_str), "%A, %B %d, %Y, %I:%M %p", &buf);
        return date_str;
    }

    static std::string format_utc_offset(int offset_seconds) {
        if (offset_seconds == 0) return "UTC+0";
        int total_mins = std::abs(offset_seconds) / 60;
        int hrs = total_mins / 60;
        int mins = total_mins % 60;
        
        std::string sign = offset_seconds > 0 ? "+" : "-";
        std::string res = "UTC" + sign + std::to_string(hrs);
        if (mins > 0) {
            res += std::string(":") + (mins < 10 ? "0" : "") + std::to_string(mins);
        }
        return res;
    }
};

} // namespace bronx::cogs
