#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "../database.h"
#include <algorithm>
#include <cctype>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <stdexcept>

namespace bronx::cogs {

class CountingModule : public stoatpp::bot_module {
private:
    // Mutex protecting per-server in-flight state so rapid messages can't
    // race past each other.
    std::mutex state_mutex_;

    // ── helpers ──────────────────────────────────────────────────────────────

    static bool is_positive_int(const std::string& s) {
        if (s.empty()) return false;
        for (char c : s) if (!std::isdigit((unsigned char)c)) return false;
        return true;
    }

    struct MathParser {
        std::string src;
        size_t pos = 0;

        void skip_whitespace() {
            while (pos < src.size() && std::isspace((unsigned char)src[pos])) {
                pos++;
            }
        }

        char peek() {
            skip_whitespace();
            if (pos >= src.size()) return '\0';
            return src[pos];
        }

        char get() {
            skip_whitespace();
            if (pos >= src.size()) return '\0';
            return src[pos++];
        }

        double parse_number() {
            skip_whitespace();
            size_t start = pos;
            if (peek() == '-' || peek() == '+') {
                get();
            }
            while (pos < src.size() && (std::isdigit((unsigned char)src[pos]) || src[pos] == '.')) {
                pos++;
            }
            if (start == pos) {
                throw std::runtime_error("expected number");
            }
            std::string num_str = src.substr(start, pos - start);
            try {
                return std::stod(num_str);
            } catch (...) {
                throw std::runtime_error("invalid number");
            }
        }

        double parse_factor() {
            skip_whitespace();
            char c = peek();
            if (c == '(') {
                get(); // consume '('
                double val = parse_expression();
                if (get() != ')') {
                    throw std::runtime_error("expected ')'");
                }
                return val;
            }
            return parse_number();
        }

        double parse_term() {
            double val = parse_factor();
            while (true) {
                char op = peek();
                if (op == '*') {
                    get();
                    val *= parse_factor();
                } else if (op == '/') {
                    get();
                    double denom = parse_factor();
                    if (denom == 0.0) {
                        throw std::runtime_error("division by zero");
                    }
                    val /= denom;
                } else {
                    break;
                }
            }
            return val;
        }

        double parse_expression() {
            double val = parse_term();
            while (true) {
                char op = peek();
                if (op == '+') {
                    get();
                    val += parse_term();
                } else if (op == '-') {
                    get();
                    val -= parse_term();
                } else {
                    break;
                }
            }
            return val;
        }

        explicit MathParser(std::string s) : src(std::move(s)) {}

        double parse() {
            pos = 0;
            double val = parse_expression();
            skip_whitespace();
            if (pos < src.size()) {
                throw std::runtime_error("unexpected trailing characters");
            }
            return val;
        }
    };

    // Try to parse the message body as a math expression or equation.
    // Trims leading/trailing whitespace and filters out commas.
    // Returns the parsed integer value, or -1 on failure.
    static int64_t parse_count_message(const std::string& content) {
        std::string s = content;
        // 1. Remove all commas
        s.erase(std::remove(s.begin(), s.end(), ','), s.end());

        // 2. Trim whitespace
        s.erase(0, s.find_first_not_of(" \t\r\n"));
        s.erase(s.find_last_not_of(" \t\r\n") + 1);
        if (s.empty()) return -1;

        // 3. Check character set. We only allow digits, ., +, -, *, /, (, ), =, and whitespace.
        for (char c : s) {
            if (!std::isdigit((unsigned char)c) && c != '.' && c != '+' && c != '-' &&
                c != '*' && c != '/' && c != '(' && c != ')' && c != '=' && !std::isspace((unsigned char)c)) {
                return -1;
            }
        }

        // 4. Split on '=' if present
        std::string lhs = s;
        std::string rhs = "";
        size_t eq_pos = s.find('=');
        if (eq_pos != std::string::npos) {
            // Ensure only one '=' sign
            if (s.find('=', eq_pos + 1) != std::string::npos) {
                return -1;
            }
            lhs = s.substr(0, eq_pos);
            rhs = s.substr(eq_pos + 1);

            // Trim lhs and rhs
            lhs.erase(0, lhs.find_first_not_of(" \t\r\n"));
            lhs.erase(lhs.find_last_not_of(" \t\r\n") + 1);
            rhs.erase(0, rhs.find_first_not_of(" \t\r\n"));
            rhs.erase(rhs.find_last_not_of(" \t\r\n") + 1);
        }

        // 5. Evaluate LHS
        double lhs_val = 0;
        try {
            MathParser parser(lhs);
            lhs_val = parser.parse();
        } catch (...) {
            return -1;
        }

        // 6. If RHS is present, evaluate/parse RHS and compare
        if (!rhs.empty()) {
            double rhs_val = 0;
            try {
                MathParser parser(rhs);
                rhs_val = parser.parse();
            } catch (...) {
                return -1;
            }

            // Check if they are equal (within a small threshold)
            if (std::abs(lhs_val - rhs_val) > 1e-5) {
                return -1;
            }
        }

        // Return rounded integer if valid and non-negative
        if (lhs_val < 0.0) return -1;
        
        return (int64_t)std::round(lhs_val);
    }

    // Format seconds into a human-readable string (e.g. "1h 30m")
    static std::string format_duration(uint64_t secs) {
        if (secs == 0) return "permanent";
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

    // Parse a duration string like "10m", "1h30m", "2d" into seconds.
    static uint64_t parse_duration_secs(const std::string& input) {
        uint64_t total = 0;
        std::string num;
        for (size_t i = 0; i < input.size(); ++i) {
            char c = input[i];
            if (std::isdigit((unsigned char)c)) {
                num += c;
            } else {
                if (num.empty()) continue;
                std::string unit;
                while (i < input.size() && std::isalpha((unsigned char)input[i])) {
                    unit += std::tolower((unsigned char)input[i++]);
                }
                --i;
                uint64_t v = 0;
                try { v = std::stoull(num); } catch (...) { return 0; }
                if      (unit == "s" || unit == "sec"   || unit == "seconds") total += v;
                else if (unit == "m" || unit == "min"   || unit == "minutes") total += v * 60;
                else if (unit == "h" || unit == "hr"    || unit == "hours"  ) total += v * 3600;
                else if (unit == "d" || unit == "day"   || unit == "days"   ) total += v * 86400;
                else if (unit == "w" || unit == "week"  || unit == "weeks"  ) total += v * 604800;
                else return 0;
                num.clear();
            }
        }
        // bare number -> minutes
        if (!num.empty()) {
            try { total += std::stoull(num) * 60; } catch (...) {}
        }
        return total;
    }

public:
    void register_handlers(stoatpp::cluster& bot) override {

        // ── .count command ───────────────────────────────────────────────────
        bot.register_command({
            "count",
            {"counting"},
            "configure the counting channel.",
            "",
            {"<subcommand>", "[args...]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg,
                   const std::vector<std::string>& args) {

                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id,
                        "" + bronx::emj_warning + " This command can only be used in a server.");
                    return;
                }
                if (!g_db) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Database is unavailable.");
                    return;
                }

                // Build a clean prefix string for help text
                std::string pfx = msg.prefix.empty()
                    ? bot.config().command_prefix : msg.prefix;
                if (pfx.rfind("<@", 0) == 0) pfx = bot.config().command_prefix;

                // ── help ──────────────────────────────────────────────────────
                if (args.empty()) {
                    auto cfg_opt = g_db->get_counting_config(msg.server_id);
                    stoatpp::models::MessagePayload payload;
                    nlohmann::json embed = nlohmann::json::object();
                    embed["title"]  = "🔢 Counting System";
                    embed["colour"] = "#6C5CE7";

                    std::string desc =
                        "• `" + pfx + "count set <#channel>` — set the counting channel.\n"
                        "• `" + pfx + "count number <n>` — set the current count to `n`.\n"
                        "• `" + pfx + "count role <@role> [time]` — role to give on a failed count (e.g. `30m`).\n"
                        "• `" + pfx + "count passive` — toggle passive mode (delete bad msgs silently instead of breaking).\n"
                        "• `" + pfx + "count leaderboard` — see who has counted the most.\n"
                        "• `" + pfx + "count stats [@user]` — show counting stats for a user.\n"
                        "• `" + pfx + "count config` — show current settings.\n";

                    if (cfg_opt) {
                        auto& c = *cfg_opt;
                        desc += "\n**Current config:**\n"
                            "Channel: " + (c.channel_id.empty() ? "*not set*" : "<#" + c.channel_id + ">") + "\n"
                            "Next number: **" + std::to_string(c.current_number + 1) + "**\n"
                            "Passive: " + (c.passive ? "" + bronx::emj_yes + " yes" : "" + bronx::emj_no + " no") + "\n"
                            "Highest count: **" + std::to_string(c.highest_count) + "**";
                    }
                    embed["description"] = desc;
                    payload.embeds.push_back(embed);
                    bot.send_message(msg.channel_id, payload);
                    return;
                }

                std::string sub = args[0];
                std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

                // ── leaderboard (no perms needed) ─────────────────────────────
                if (sub == "leaderboard" || sub == "lb") {
                    auto lb   = g_db->get_counting_leaderboard(msg.server_id, 10);
                    auto hl   = g_db->get_counting_highlights(msg.server_id);
                    auto self = g_db->get_counting_stats(msg.server_id, msg.author.id);

                    stoatpp::models::MessagePayload payload;
                    nlohmann::json embed = nlohmann::json::object();
                    embed["title"]  = "🏆 Counting Leaderboard";
                    embed["colour"] = "#FDCB6E";

                    std::string desc;

                    // Highlights row
                    if (!hl.top_counter_id.empty())
                        desc += "👑 **Most counts:** <@" + hl.top_counter_id + "> (" + std::to_string(hl.top_counter_score) + ")\n";
                    if (hl.highest_count > 0)
                        desc += "📈 **Highest count reached:** **" + std::to_string(hl.highest_count) + "**\n";
                    if (!hl.top_failer_id.empty())
                        desc += "💀 **Most failed:** <@" + hl.top_failer_id + "> (" + std::to_string(hl.top_failer_score) + ")\n";

                    desc += "\n**Top counters:**\n";

                    static const std::array<std::string, 3> medals = {"🥇", "🥈", "🥉"};
                    if (lb.empty()) {
                        desc += "*No one has counted yet!*\n";
                    } else {
                        for (size_t i = 0; i < lb.size(); ++i) {
                            std::string medal = i < 3 ? medals[i] : std::to_string(i + 1) + ".";
                            desc += medal + " <@" + lb[i].user_id + "> — **"
                                    + std::to_string(lb[i].correct_counts) + "** correct";
                            if (lb[i].failed_counts > 0)
                                desc += " / " + std::to_string(lb[i].failed_counts) + " failed";
                            desc += "\n";
                        }
                    }

                    // Show the caller's own stats if not in top 10
                    bool in_lb = false;
                    for (const auto& e : lb) if (e.user_id == msg.author.id) { in_lb = true; break; }
                    if (!in_lb && (self.correct_counts > 0 || self.failed_counts > 0)) {
                        desc += "\n*Your stats: **" + std::to_string(self.correct_counts) + "** correct / "
                                + std::to_string(self.failed_counts) + " failed*";
                    }

                    embed["description"] = desc;
                    payload.embeds.push_back(embed);
                    bot.send_message(msg.channel_id, payload);
                    return;
                }

                // ── stats (no perms needed) ──────────────────────────────────
                if (sub == "stats" || sub == "stat") {
                    std::string target_user_id = msg.author.id;
                    if (args.size() >= 2) {
                        std::string query = args[1];
                        helpers::resolve_user(bot, msg.server_id, query, [msg, &bot](stoatpp::models::User target_user, stoatpp::models::Member target_member, bool success_resolve) {
                            if (!success_resolve) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " User not found.");
                                return;
                            }
                            auto stats = g_db->get_counting_stats(msg.server_id, target_user.id);
                            stoatpp::models::MessagePayload payload;
                            nlohmann::json embed = nlohmann::json::object();
                            embed["title"] = target_user.username + "'s Counting Stats";
                            embed["colour"] = "#6C5CE7";
                            
                            std::string desc = "📊 **Stats for** <@" + target_user.id + ">:\n\n";
                            desc += "• Correct counts: **" + std::to_string(stats.correct_counts) + "**\n";
                            desc += "• Failed counts: **" + std::to_string(stats.failed_counts) + "**\n";
                            uint64_t total = stats.correct_counts + stats.failed_counts;
                            uint64_t accuracy = total > 0 ? (stats.correct_counts * 100) / total : 0;
                            desc += "• Accuracy: **" + std::to_string(accuracy) + "%**";
                            
                            embed["description"] = desc;
                            payload.embeds.push_back(embed);
                            bot.send_message(msg.channel_id, payload);
                        });
                    } else {
                        auto stats = g_db->get_counting_stats(msg.server_id, target_user_id);
                        stoatpp::models::MessagePayload payload;
                        nlohmann::json embed = nlohmann::json::object();
                        embed["title"] = "Your Counting Stats";
                        embed["colour"] = "#6C5CE7";
                        
                        std::string desc = "📊 **Stats for** <@" + target_user_id + ">:\n\n";
                        desc += "• Correct counts: **" + std::to_string(stats.correct_counts) + "**\n";
                        desc += "• Failed counts: **" + std::to_string(stats.failed_counts) + "**\n";
                        uint64_t total = stats.correct_counts + stats.failed_counts;
                        uint64_t accuracy = total > 0 ? (stats.correct_counts * 100) / total : 0;
                        desc += "• Accuracy: **" + std::to_string(accuracy) + "%**";
                        
                        embed["description"] = desc;
                        payload.embeds.push_back(embed);
                        bot.send_message(msg.channel_id, payload);
                    }
                    return;
                }

                // All remaining subcommands need Manage Server
                bot.fetch_member(msg.server_id, msg.author.id,
                    [this, &bot, msg, args, sub, pfx](stoatpp::models::Member member, bool ok) {
                        if (!ok) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to verify permissions.");
                            return;
                        }
                        if (!is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, member)) {
                            bot.send_message(msg.channel_id,
                                "" + bronx::emj_warning + " You need **Manage Server** permission to configure counting.");
                            return;
                        }

                        auto cfg_opt = g_db->get_counting_config(msg.server_id);
                        Database::CountingConfig cfg;
                        if (cfg_opt) cfg = *cfg_opt;
                        cfg.server_id = msg.server_id;

                        // ── set <#channel> ────────────────────────────────────
                        if (sub == "set") {
                            if (args.size() < 2) {
                                bot.send_message(msg.channel_id,
                                    "usage: `" + pfx + "count set <#channel|channel_id>`");
                                return;
                            }
                            std::string raw = args[1];
                            std::string chan_id = raw;
                            if (raw.rfind("<#", 0) == 0 && raw.back() == '>')
                                chan_id = raw.substr(2, raw.size() - 3);

                            cfg.channel_id     = chan_id;
                            cfg.current_number = 0;
                            cfg.last_user_id   = "";

                            if (g_db->save_counting_config(cfg)) {
                                bot.send_message(msg.channel_id,
                                    "" + bronx::emj_yes + " Counting channel set to <#" + chan_id + ">. Count starts at **1**.");
                            } else {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save config.");
                            }
                            return;
                        }

                        // ── number <n> ────────────────────────────────────────
                        if (sub == "number") {
                            if (args.size() < 2 || !is_positive_int(args[1])) {
                                bot.send_message(msg.channel_id,
                                    "usage: `" + pfx + "count number <positive number>`");
                                return;
                            }
                            int64_t n = 0;
                            try { n = std::stoll(args[1]); } catch (...) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid number.");
                                return;
                            }
                            // current_number is the last confirmed count; next expected = n
                            // so we set current_number = n - 1
                            cfg.current_number = n - 1;
                            cfg.last_user_id   = "";
                            if (g_db->save_counting_config(cfg)) {
                                bot.send_message(msg.channel_id,
                                    "" + bronx::emj_yes + " Count reset. Next expected number: **" + std::to_string(n) + "**.");
                            } else {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save config.");
                            }
                            return;
                        }

                        // ── role <@role> [duration] ───────────────────────────
                        if (sub == "role") {
                            if (args.size() < 2) {
                                bot.send_message(msg.channel_id,
                                    "usage: `" + pfx + "count role <@role|role_id|none> [duration]`\n"
                                    "duration examples: `30m`, `1h`, `2d`, omit for permanent.");
                                return;
                            }
                            std::string raw = args[1];
                            std::transform(raw.begin(), raw.end(), raw.begin(), ::tolower);

                            if (raw == "none" || raw == "clear" || raw == "off") {
                                cfg.fail_role_id       = "";
                                cfg.fail_role_duration = 0;
                                if (g_db->save_counting_config(cfg))
                                    bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Fail role cleared.");
                                else
                                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save config.");
                                return;
                            }

                            std::string role_id = args[1]; // preserve original case for ID
                            if (role_id.rfind("<%", 0) == 0 && role_id.back() == '>')
                                role_id = role_id.substr(2, role_id.size() - 3);
                            else {
                                // Try to match by name
                                auto srv = bot.get_server(msg.server_id);
                                if (srv) {
                                    bool found = false;
                                    for (const auto& r : srv->roles) {
                                        if (r.id == args[1] || r.name == args[1]) {
                                            role_id = r.id;
                                            found = true;
                                            break;
                                        }
                                    }
                                    if (!found && !is_positive_int(args[1])) {
                                        bot.send_message(msg.channel_id,
                                            "" + bronx::emj_warning + " Role not found. Mention the role or provide its ID.");
                                        return;
                                    }
                                }
                            }

                            uint64_t dur = 0;
                            if (args.size() >= 3) {
                                dur = parse_duration_secs(args[2]);
                                if (dur == 0) {
                                    bot.send_message(msg.channel_id,
                                        "" + bronx::emj_warning + " Invalid duration format (e.g. `30m`, `1h`, `2d`).");
                                    return;
                                }
                            }

                            cfg.fail_role_id       = role_id;
                            cfg.fail_role_duration = dur;
                            if (g_db->save_counting_config(cfg)) {
                                std::string resp = "" + bronx::emj_yes + " Fail role set to <%" + role_id + ">";
                                resp += dur == 0 ? " (permanent)." : " for " + format_duration(dur) + ".";
                                bot.send_message(msg.channel_id, resp);
                            } else {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save config.");
                            }
                            return;
                        }

                        // ── passive ───────────────────────────────────────────
                        if (sub == "passive") {
                            cfg.passive = !cfg.passive;
                            if (g_db->save_counting_config(cfg)) {
                                bot.send_message(msg.channel_id,
                                    std::string("" + bronx::emj_yes + " Passive mode ") + (cfg.passive
                                        ? "**enabled** — wrong numbers are silently ignored; non-numbers allowed freely."
                                        : "**disabled** — wrong numbers break the count (" + bronx::emj_no + "); non-numbers are deleted to keep the channel clean."));
                            } else {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save config.");
                            }
                            return;
                        }

                        // ── warning ───────────────────────────────────────────
                        if (sub == "warning") {
                            cfg.warn_double_count = !cfg.warn_double_count;
                            if (g_db->save_counting_config(cfg)) {
                                bot.send_message(msg.channel_id,
                                    std::string("" + bronx::emj_yes + " Double-count warning ") + (cfg.warn_double_count
                                        ? "**enabled** — users who count twice in a row get a " + bronx::emj_warning + " warning instead of immediately breaking the count."
                                        : "**disabled** — double-counts immediately break the count."));
                            } else {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save config.");
                            }
                            return;
                        }

                        // ── config ────────────────────────────────────────────
                        if (sub == "config") {
                            stoatpp::models::MessagePayload payload;
                            nlohmann::json embed = nlohmann::json::object();
                            embed["title"]  = "🔢 Counting Configuration";
                            embed["colour"] = "#6C5CE7";
                            std::string desc =
                                "• **Channel:** " + (cfg.channel_id.empty()  ? "*not set*" : "<#" + cfg.channel_id + ">") + "\n"
                                "• **Next expected:** **" + std::to_string(cfg.current_number + 1) + "**\n"
                                "• **Highest count:** **" + std::to_string(cfg.highest_count) + "**\n"
                                "• **Fail role:** " + (cfg.fail_role_id.empty() ? "*none*" : "<%" + cfg.fail_role_id + "> (" + format_duration(cfg.fail_role_duration) + ")") + "\n"
                                "• **Passive mode:** " + (cfg.passive ? "" + bronx::emj_yes + " yes" : "" + bronx::emj_no + " no") + "\n"
                                "• **Double-count warning:** " + (cfg.warn_double_count ? "" + bronx::emj_yes + " yes" : "" + bronx::emj_no + " no");
                            embed["description"] = desc;
                            payload.embeds.push_back(embed);
                            bot.send_message(msg.channel_id, payload);
                            return;
                        }

                        bot.send_message(msg.channel_id,
                            "" + bronx::emj_warning + " Unknown subcommand. Run `" + pfx + "count` to see all options.");
                    });
            },
            "Utility"
        });

        // ── on_message: counting logic ───────────────────────────────────────
        bot.on_message([this, &bot](const stoatpp::events::Message& ev) {
            if (ev.server_id.empty()) return;
            if (!g_db) return;

            // Ignore bot's own messages
            auto bot_user = bot.current_user();
            if (ev.author.id == bot_user.id) return;

            auto cfg_opt = g_db->get_counting_config(ev.server_id);
            if (!cfg_opt || cfg_opt->channel_id.empty()) return;
            auto cfg = *cfg_opt;
            if (ev.channel_id != cfg.channel_id) return;

            int64_t num = parse_count_message(ev.content);

            // ── non-number message ────────────────────────────────────────────
            // In passive mode users can freely chat. In strict mode the channel
            // is numbers-only so we delete any non-number message.
            if (num < 0) {
                if (!cfg.passive) {
                    bot.rest().delete_messages_bulk(ev.channel_id, {ev.id});
                }
                return;
            }

            // ── correct count ─────────────────────────────────────────────────
            if (num == cfg.current_number + 1 && ev.author.id != cfg.last_user_id) {
                {
                    std::lock_guard<std::mutex> lk(state_mutex_);
                    cfg.current_number = num;
                    cfg.last_user_id   = ev.author.id;
                    if (num > cfg.highest_count) cfg.highest_count = num;
                    g_db->save_counting_config(cfg);
                }
                g_db->counting_increment_correct(ev.server_id, ev.author.id);

                // React " + bronx::emj_yes + " in strict mode; passive mode stays silent
                if (!cfg.passive) {
                    bot.rest().add_reaction(ev.channel_id, ev.id, "01KK7EC8VP8CMKZW8SB64TCVTC");
                }
                return;
            }

            // ── wrong count or same-user double-count ─────────────────────────
            // Passive mode: silently ignore — count is not reset, message stays
            if (cfg.passive) return;

            bool same_user = (ev.author.id == cfg.last_user_id);

            // Warning mode: " + bronx::emj_warning + " react + message but do NOT reset the count
            if (same_user && cfg.warn_double_count) {
                bot.rest().add_reaction(ev.channel_id, ev.id, "01KK7ECQ187S2FZ2X3JS65R9PW");
                stoatpp::models::MessagePayload warn_payload;
                warn_payload.content = "" + bronx::emj_warning + " <@" + ev.author.id + "> you can't count twice in a row! Let someone else go next.";
                bot.send_message(ev.channel_id, warn_payload);
                return;
            }

            // Strict mode: react " + bronx::emj_no + ", announce, reset (never delete the message)
            bot.rest().add_reaction(ev.channel_id, ev.id, "01KK7ECH9ES9SRSB9REFGKZMFB");

            std::string reason = same_user
                ? "<@" + ev.author.id + "> counted **twice in a row**! "
                : "<@" + ev.author.id + "> broke the count! ";
            reason += "The count was **" + std::to_string(cfg.current_number)
                    + "** — it resets to **1**.";

            g_db->counting_increment_failed(ev.server_id, ev.author.id);

            {
                std::lock_guard<std::mutex> lk(state_mutex_);
                cfg.current_number = 0;
                cfg.last_user_id   = "";
                g_db->save_counting_config(cfg);
            }

            // Assign fail role if configured
            if (!cfg.fail_role_id.empty()) {
                bot.add_role_to_member(ev.server_id, ev.author.id, cfg.fail_role_id);

                if (cfg.fail_role_duration > 0) {
                    uint64_t dur    = cfg.fail_role_duration;
                    std::string srv_id  = ev.server_id;
                    std::string user_id = ev.author.id;
                    std::string role_id = cfg.fail_role_id;
                    std::thread([&bot, srv_id, user_id, role_id, dur]() {
                        std::this_thread::sleep_for(std::chrono::seconds(dur));
                        bot.remove_role_from_member(srv_id, user_id, role_id);
                    }).detach();
                }
            }

            stoatpp::models::MessagePayload payload;
            payload.content = reason;
            bot.send_message(ev.channel_id, payload);
        });
    }
};

} // namespace bronx::cogs
