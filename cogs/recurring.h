#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "command_config.h"
#include <sstream>
#include <algorithm>
#include <fstream>
#include <thread>
#include <chrono>

namespace bronx::cogs {

class RecurringModule : public stoatpp::bot_module {
private:
    // -----------------------------------------------------------------------
    // Helpers shared with welcome_leave pattern
    // -----------------------------------------------------------------------
    static std::string get_raw_trailing_args(const std::string& content,
                                             const std::string& prefix,
                                             const std::string& command_name,
                                             const std::string& subcommand) {
        std::string search1 = prefix + command_name;
        std::string lower_content = content;
        std::transform(lower_content.begin(), lower_content.end(), lower_content.begin(), ::tolower);
        std::string lower_search1 = search1;
        std::transform(lower_search1.begin(), lower_search1.end(), lower_search1.begin(), ::tolower);
        size_t pos = lower_content.find(lower_search1);
        if (pos == std::string::npos) return "";
        pos += lower_search1.length();
        while (pos < content.length() && std::isspace(content[pos])) pos++;
        std::string lower_sub = subcommand;
        std::transform(lower_sub.begin(), lower_sub.end(), lower_sub.begin(), ::tolower);
        if (pos + lower_sub.length() <= content.length() &&
            lower_content.compare(pos, lower_sub.length(), lower_sub) == 0) {
            pos += lower_sub.length();
            if (pos < content.length() && std::isspace(content[pos])) pos++;
            return content.substr(pos);
        }
        return "";
    }

    static std::unordered_map<std::string, std::string> parse_pairs(const std::string& text) {
        std::unordered_map<std::string, std::string> pairs;
        std::vector<std::string> keys = {
            "title:", "description:", "desc:", "colour:", "color:",
            "image:", "content:", "channel:", "interval:", "type:", "name:"
        };
        struct KeyPos { std::string key; size_t pos; };
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
        std::sort(occurrences.begin(), occurrences.end(),
                  [](const KeyPos& a, const KeyPos& b) { return a.pos < b.pos; });
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

    // -----------------------------------------------------------------------
    // Resolve a recurring message by name-or-id arg from a server
    // -----------------------------------------------------------------------
    static std::optional<Database::RecurringMessage> resolve_rm(
            const std::string& server_id, const std::string& query) {
        if (!g_db) return std::nullopt;
        // Only treat query as a numeric ID if every character is a digit
        bool all_digits = !query.empty() &&
            std::all_of(query.begin(), query.end(), [](unsigned char c) { return std::isdigit(c); });
        if (all_digits) {
            try {
                int id = std::stoi(query);
                auto by_id = g_db->get_recurring_by_id(id, server_id);
                if (by_id) return by_id;
            } catch (...) {}
        }
        // Fall back to name search
        return g_db->get_recurring_by_name(server_id, query);
    }

    // -----------------------------------------------------------------------
    // Format a human-readable interval string (e.g. "2h 30m")
    // -----------------------------------------------------------------------
    static std::string format_interval(uint64_t seconds) {
        uint64_t d = seconds / 86400;
        uint64_t h = (seconds % 86400) / 3600;
        uint64_t m = (seconds % 3600) / 60;
        uint64_t s = seconds % 60;
        std::string out;
        if (d) out += std::to_string(d) + "d ";
        if (h) out += std::to_string(h) + "h ";
        if (m) out += std::to_string(m) + "m ";
        if (s && !d && !h) out += std::to_string(s) + "s";
        if (out.empty()) out = std::to_string(seconds) + "s";
        // trim trailing space
        while (!out.empty() && out.back() == ' ') out.pop_back();
        return out;
    }

    // -----------------------------------------------------------------------
    // Read RAM usage from /proc/self/status (VmRSS)
    // -----------------------------------------------------------------------
    static std::string get_ram_usage() {
        std::ifstream f("/proc/self/status");
        if (!f.is_open()) return "N/A";
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("VmRSS:", 0) == 0) {
                std::istringstream ss(line.substr(6));
                long kb = 0;
                ss >> kb;
                if (kb < 1024) return std::to_string(kb) + " KB";
                return std::to_string(kb / 1024) + " MB";
            }
        }
        return "N/A";
    }

    // -----------------------------------------------------------------------
    // Read thread count from /proc/self/status (Threads:)
    // -----------------------------------------------------------------------
    static std::string get_thread_count() {
        std::ifstream f("/proc/self/status");
        if (!f.is_open()) return "N/A";
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("Threads:", 0) == 0) {
                std::istringstream ss(line.substr(8));
                long n = 0;
                ss >> n;
                return std::to_string(n);
            }
        }
        return "N/A";
    }

    // -----------------------------------------------------------------------
    // CPU usage: read /proc/self/stat, compute user+sys ticks / uptime ticks
    // -----------------------------------------------------------------------
    static std::string get_cpu_usage() {
        std::ifstream stat_f("/proc/self/stat");
        if (!stat_f.is_open()) return "N/A";
        std::string dummy;
        long utime = 0, stime = 0;
        // Fields: pid comm state ppid pgroup session tty_nr tpgid flags
        //         minflt cminflt majflt cmajflt utime stime ...
        for (int i = 0; i < 13; ++i) stat_f >> dummy;
        stat_f >> utime >> stime;
        stat_f.close();

        std::ifstream uptime_f("/proc/uptime");
        double sys_uptime = 0.0;
        if (uptime_f.is_open()) uptime_f >> sys_uptime;

        long clk_tck = sysconf(_SC_CLK_TCK);
        if (clk_tck <= 0) return "N/A";
        double proc_ticks = static_cast<double>(utime + stime) / clk_tck;
        double pct = (sys_uptime > 0) ? (proc_ticks / sys_uptime * 100.0) : 0.0;

        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f", pct);
        return std::string(buf);
    }

    // -----------------------------------------------------------------------
    // Hardcoded owner ID fallback (until library owner_id is fixed)
    // -----------------------------------------------------------------------
    static constexpr const char* HARDCODED_OWNER_ID = "01FZB2QAPRVT8PVMF11480GRCD";

    static bool is_bot_owner(stoatpp::cluster& bot, const std::string& user_id) {
        return bot.is_owner(user_id) || user_id == HARDCODED_OWNER_ID;
    }

    // -----------------------------------------------------------------------
    // Unescape literal \n sequences in stored text into real newlines.
    // Users type `\n` in their message; this converts them to actual '\n'.
    // -----------------------------------------------------------------------
    static std::string unescape_newlines(std::string s) {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') {
                out += '\n';
                ++i;
            } else {
                out += s[i];
            }
        }
        return out;
    }

    // -----------------------------------------------------------------------
    // Token substitution
    //   General tokens: {server} {membercount} {servercount} {cachecount} {uptime} {ping}
    //   Owner tokens:   {ram} {cpu} {threads}
    // -----------------------------------------------------------------------
    static std::string apply_tokens(std::string text,
                                    stoatpp::cluster& bot,
                                    const std::string& server_id,
                                    bool owner_tokens) {
        auto replace_all = [](std::string& s, const std::string& from, const std::string& to) {
            size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos) {
                s.replace(pos, from.length(), to);
                pos += to.length();
            }
        };

        // Server name
        std::string server_name = "Unknown Server";
        int member_count_val = 0;
        auto srv_opt = bot.get_server(server_id);
        if (srv_opt) {
            server_name = srv_opt->name;
            // Membercount: use raw JSON if available (Revolt sends it), else channels.size() as fallback
            if (srv_opt->raw.contains("members") && srv_opt->raw["members"].is_number()) {
                member_count_val = srv_opt->raw["members"].get<int>();
            } else {
                // Use server channel count as rough proxy — always available from cache
                member_count_val = static_cast<int>(srv_opt->channels.size());
            }
        }

        // Server count
        std::string server_count_str = std::to_string(bot.get_servers().size());

        // Cache count (users in bot's user cache via server objects)
        size_t cache_count = 0;
        for (const auto& s : bot.get_servers()) {
            (void)s;
            cache_count++; // approximation: number of servers tracks scale
        }
        // A better proxy: we can sum channels across all servers as a cache hint
        // but there's no direct user-cache size exposed. Use server count * avg as text.
        // Actually let's just show the server count as the cache reference — per the plan
        // owner gets real diagnostics, this is cosmetic for normal users.
        std::string cache_count_str = server_count_str; // same as server count for non-owner

        // Uptime
        uint64_t up_sec = bot.uptime();
        uint64_t up_d = up_sec / 86400;
        uint64_t up_h = (up_sec % 86400) / 3600;
        uint64_t up_m = (up_sec % 3600) / 60;
        std::string uptime_str;
        if (up_d) uptime_str += std::to_string(up_d) + "d ";
        if (up_h) uptime_str += std::to_string(up_h) + "h ";
        uptime_str += std::to_string(up_m) + "m";

        // Ping
        std::string ping_str = std::to_string(bot.ping_latency()) + "ms";

        replace_all(text, "{server}",      server_name);
        replace_all(text, "{membercount}", std::to_string(member_count_val));
        replace_all(text, "{servercount}", server_count_str);
        replace_all(text, "{cachecount}",  cache_count_str);
        replace_all(text, "{uptime}",      uptime_str);
        replace_all(text, "{ping}",        ping_str);

        if (owner_tokens) {
            replace_all(text, "{ram}",     get_ram_usage());
            replace_all(text, "{cpu}",     get_cpu_usage() + "%");
            replace_all(text, "{threads}", get_thread_count());
        }

        return text;
    }

    // -----------------------------------------------------------------------
    // Build and send a MessagePayload from a RecurringMessage (used by both
    // the timer loop and the `test` subcommand).
    // -----------------------------------------------------------------------
    static void send_recurring(stoatpp::cluster& bot,
                               const Database::RecurringMessage& rm,
                               const std::string& target_channel_id,
                               std::function<void(bool success, std::string error_details)> callback = nullptr) {
        stoatpp::models::MessagePayload payload;

        if (rm.message_type == "embed") {
            nlohmann::json emb_json = nlohmann::json::object();
            if (!rm.embed_json.empty()) {
                try { emb_json = nlohmann::json::parse(rm.embed_json); } catch (...) {}
            }

            // Apply tokens to content (shown outside embed)
            if (!rm.content.empty()) {
                payload.content = apply_tokens(rm.content, bot, rm.server_id, rm.owner_tokens);
            }

            nlohmann::json embed = nlohmann::json::object();
            if (emb_json.contains("title") && emb_json["title"].is_string()) {
                embed["title"] = unescape_newlines(apply_tokens(emb_json["title"].get<std::string>(), bot, rm.server_id, rm.owner_tokens));
            }
            if (emb_json.contains("description") && emb_json["description"].is_string()) {
                embed["description"] = unescape_newlines(apply_tokens(emb_json["description"].get<std::string>(), bot, rm.server_id, rm.owner_tokens));
            }
            if (emb_json.contains("color") && emb_json["color"].is_string()) {
                std::string col = emb_json["color"].get<std::string>();
                if (col == "random") {
                    char hex[8];
                    snprintf(hex, sizeof(hex), "#%06x", rand() % 0xFFFFFF);
                    col = hex;
                }
                embed["colour"] = col;
            }
            if (emb_json.contains("image") && emb_json["image"].is_string()) {
                std::string img_url = emb_json["image"].get<std::string>();
                std::string desc = embed.contains("description") ? embed["description"].get<std::string>() : "";
                if (!desc.empty()) desc += "\n";
                desc += "![](" + img_url + ")";
                embed["description"] = desc;
            }
            if (!embed.empty()) payload.embeds.push_back(embed);
        } else {
            payload.content = apply_tokens(rm.content, bot, rm.server_id, rm.owner_tokens);
        }

        std::thread([&bot, target_channel_id, payload, callback]() {
            auto res = bot.rest().post("/channels/" + target_channel_id + "/messages", payload.to_json());
            if (res.success()) {
                if (callback) callback(true, "");
            } else {
                std::string err;
                if (res.status_code == 403 || res.status_code == 404) {
                    err = "Make sure the bot has View Channel and Send Messages permissions (HTTP " + std::to_string(res.status_code) + ").";
                } else if (res.body.is_object() && res.body.contains("error") && res.body["error"].is_string()) {
                    err = res.body["error"].get<std::string>();
                } else if (!res.body.is_null()) {
                    err = res.body.dump();
                } else {
                    err = "HTTP " + std::to_string(res.status_code);
                }
                if (callback) callback(false, err);
            }
        }).detach();
    }

public:
    void register_handlers(stoatpp::cluster& bot) override {
        // -----------------------------------------------------------------------
        // RECURSE command
        // -----------------------------------------------------------------------
        bot.register_command({
            "recurse",
            {"autopost", "scheduled"},
            "configure auto recurring/scheduled messages for channels.",
            "",
            {"<subcommand>", "[args...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling recurse...\n";
                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id, "⚠️ This command can only be used in a server channel.");
                    return;
                }
                if (!g_db) {
                    bot.send_message(msg.channel_id, "⚠️ Database is unavailable.");
                    return;
                }

                bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, args](stoatpp::models::Member member, bool success) {
                    if (!success) {
                        bot.send_message(msg.channel_id, "⚠️ Failed to verify your permissions.");
                        return;
                    }

                    std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;

                    // ---------- EXAMPLE (owner only) ----------
                    if (!args.empty() && args[0] == "example") {
                        if (!is_bot_owner(bot, msg.author.id)) {
                            bot.send_message(msg.channel_id, "⚠️ The `example` subcommand is only available to the bot owner.");
                            return;
                        }
                        // Build and post the full diagnostics example to the current channel
                        Database::RecurringMessage ex;
                        ex.server_id    = msg.server_id;
                        ex.channel_id   = msg.channel_id;
                        ex.message_type = "embed";
                        ex.owner_tokens = true;
                        nlohmann::json emb;
                        emb["title"] = "📊 Bot Diagnostics";
                        emb["description"] =
                            "**Server:** {server}\n"
                            "**Total Servers:** {servercount}\n"
                            "**Members (cached):** {membercount}\n"
                            "**Cache Size:** {cachecount}\n"
                            "**Uptime:** {uptime}\n"
                            "**Ping:** {ping}\n"
                            "─────────────────\n"
                            "**RAM Usage:** {ram}\n"
                            "**CPU Usage:** {cpu}\n"
                            "**Threads:** {threads}";
                        emb["color"] = "#5865f2";
                        ex.embed_json = emb.dump();
                        send_recurring(bot, ex, msg.channel_id, [&bot, msg](bool ok) {
                            if (!ok) bot.send_message(msg.channel_id, "⚠️ Failed to send example embed.");
                        });
                        return;
                    }

                    // All other subcommands require trusted/admin permission
                    if (!is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, member)) {
                        bot.send_message(msg.channel_id, "⚠️ Permission error: you need Manage Server permission to configure recurring messages.");
                        return;
                    }

                    if (args.empty()) {
                        bot.send_message(msg.channel_id,
                            "📋 **Recurring Messages Subcommands:**\n"
                            "• `" + pfx + "recurse add <name> <#channel> <interval>` - Set up a new recurring message (5m min).\n"
                            "• `" + pfx + "recurse remove <name|id>` - Remove a recurring message.\n"
                            "• `" + pfx + "recurse list` - List all recurring messages for this server.\n"
                            "• `" + pfx + "recurse edit <name|id> [key:value ...]` - Edit a recurring message.\n"
                            "\u2022 `" + pfx + "recurse send <name|id>` - Send now and reset the interval timer.\n"
                            "\u2022 `" + pfx + "recurse test <name|id>` - Preview the message without affecting the timer.\n"
                            "\u2022 `" + pfx + "recurse example` - (Bot owner) Post a diagnostics embed showing all tokens.\n\n"
                            "**Available tokens (all users):** `{server}` `{membercount}` `{servercount}` `{cachecount}` `{uptime}` `{ping}`\n"
                            "**Owner-only tokens:** `{ram}` `{cpu}` `{threads}`");
                        return;
                    }

                    std::string sub = args[0];
                    std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

                    // ===================== ADD =====================
                    if (sub == "add") {
                        // Usage: recurse add <name> <#channel> <interval>
                        if (args.size() < 4) {
                            bot.send_message(msg.channel_id,
                                "usage: `" + pfx + "recurse add <name> <#channel> <interval>`\n"
                                "Interval examples: `5m`, `1h`, `12h`, `1d`  (minimum: 5 minutes)");
                            return;
                        }

                        // Capacity check
                        int cur_count = g_db->count_recurring_for_server(msg.server_id);
                        if (cur_count >= 10) {
                            bot.send_message(msg.channel_id, "⚠️ This server already has the maximum of **10** recurring messages. Remove one before adding another.");
                            return;
                        }

                        std::string rm_name = args[1];
                        if (rm_name.length() > 100) {
                            bot.send_message(msg.channel_id, "⚠️ Name must be 100 characters or fewer.");
                            return;
                        }

                        // Resolve channel
                        std::string target_channel;
                        std::string ch_arg = args[2];
                        if (ch_arg.rfind("<#", 0) == 0 && ch_arg.back() == '>') {
                            target_channel = ch_arg.substr(2, ch_arg.size() - 3);
                        } else {
                            target_channel = ch_arg;
                        }

                        // Check channel not already occupied
                        if (g_db->recurring_channel_occupied(target_channel)) {
                            bot.send_message(msg.channel_id, "⚠️ Channel <#" + target_channel + "> already has a recurring message configured. Each channel can only have one.");
                            return;
                        }

                        // Parse interval
                        uint64_t interval_sec = parse_duration(args[3]);
                        if (interval_sec == 0) {
                            bot.send_message(msg.channel_id, "⚠️ Invalid interval format. Examples: `5m`, `1h30m`, `2d`");
                            return;
                        }
                        if (interval_sec < 300) {
                            bot.send_message(msg.channel_id, "⚠️ Minimum interval is **5 minutes**. Please use a longer interval.");
                            return;
                        }

                        bool is_owner = is_bot_owner(bot, msg.author.id);

                        // Wizard: ask for message type
                        bot.send_message(msg.channel_id,
                            "💬 **Recurring Message Setup — \"" + rm_name + "\"**\n"
                            "**Step 1/2**: Choose the message type. Reply with `text` or `embed`.");
                        bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30),
                            [&bot, msg, args, rm_name, target_channel, interval_sec, is_owner, pfx](std::optional<stoatpp::events::Message> rep1) {
                                if (!rep1) { bot.send_message(msg.channel_id, "⏱️ Setup timed out."); return; }
                                std::string type_resp = rep1->content;
                                std::transform(type_resp.begin(), type_resp.end(), type_resp.begin(), ::tolower);
                                if (type_resp == "cancel") { bot.send_message(msg.channel_id, "🚫 Setup canceled."); return; }
                                if (type_resp != "text" && type_resp != "embed") {
                                    bot.send_message(msg.channel_id, "⚠️ Invalid type (must be `text` or `embed`). Setup canceled.");
                                    return;
                                }

                                std::string token_hint = "\n*Available tokens:* `{server}` `{membercount}` `{servercount}` `{cachecount}` `{uptime}` `{ping}`";
                                if (is_owner) token_hint += " `{ram}` `{cpu}` `{threads}`";

                                if (type_resp == "text") {
                                    bot.send_message(msg.channel_id,
                                        "💬 **Recurring Message Setup**\n"
                                        "**Step 2/2**: Type the message content." + token_hint);
                                    bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(120),
                                        [&bot, msg, rm_name, target_channel, interval_sec, is_owner, type_resp](std::optional<stoatpp::events::Message> rep2) {
                                            if (!rep2) { bot.send_message(msg.channel_id, "⏱️ Setup timed out."); return; }
                                            if (rep2->content == "cancel") { bot.send_message(msg.channel_id, "🚫 Setup canceled."); return; }
                                            Database::RecurringMessage rm;
                                            rm.server_id        = msg.server_id;
                                            rm.channel_id       = target_channel;
                                            rm.name             = rm_name;
                                            rm.message_type     = "text";
                                            rm.content          = rep2->content;
                                            rm.interval_seconds = interval_sec;
                                            rm.owner_tokens     = is_owner;
                                            if (g_db->add_recurring_message(rm)) {
                                                bot.send_message(msg.channel_id,
                                                    "✅ **Recurring message \"" + rm_name + "\" created!**\n"
                                                    "It will post to <#" + target_channel + "> every **" + format_interval(interval_sec) + "** (ID: `" + std::to_string(rm.id) + "`).");
                                            } else {
                                                bot.send_message(msg.channel_id, "⚠️ Failed to save recurring message (channel may already be in use).");
                                            }
                                        });
                                    return;
                                }

                                // Embed flow
                                bot.send_message(msg.channel_id,
                                    "💬 **Recurring Message Setup**\n"
                                    "**Step 2/2 (embed)**: Provide embed fields using `key:value` pairs.\n"
                                    "Keys: `title`, `description`, `color`, `image`, `content`\n"
                                    + token_hint + "\n\n**Example:**\n"
                                    "```\ntitle:📢 Announcement! description:Hey everyone, daily update here. color:#5865f2\n```");
                                bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(120),
                                    [&bot, msg, rm_name, target_channel, interval_sec, is_owner](std::optional<stoatpp::events::Message> rep2) {
                                        if (!rep2) { bot.send_message(msg.channel_id, "⏱️ Setup timed out."); return; }
                                        if (rep2->content == "cancel") { bot.send_message(msg.channel_id, "🚫 Setup canceled."); return; }

                                        auto pairs = parse_pairs(rep2->content);
                                        nlohmann::json emb = nlohmann::json::object();
                                        std::string plain_content;
                                        for (const auto& [k, v] : pairs) {
                                            if (k == "content") { plain_content = v; }
                                            else { emb[k] = v; }
                                        }

                                        Database::RecurringMessage rm;
                                        rm.server_id        = msg.server_id;
                                        rm.channel_id       = target_channel;
                                        rm.name             = rm_name;
                                        rm.message_type     = "embed";
                                        rm.content          = plain_content;
                                        rm.embed_json       = emb.dump();
                                        rm.interval_seconds = interval_sec;
                                        rm.owner_tokens     = is_owner;
                                        if (g_db->add_recurring_message(rm)) {
                                            bot.send_message(msg.channel_id,
                                                "✅ **Recurring embed message \"" + rm_name + "\" created!**\n"
                                                "It will post to <#" + target_channel + "> every **" + format_interval(interval_sec) + "** (ID: `" + std::to_string(rm.id) + "`).");
                                        } else {
                                            bot.send_message(msg.channel_id, "⚠️ Failed to save recurring message (channel may already be in use).");
                                        }
                                    });
                            });
                        return;
                    }

                    // ===================== REMOVE =====================
                    if (sub == "remove" || sub == "delete" || sub == "del") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "usage: `" + pfx + "recurse remove <name|id>`");
                            return;
                        }
                        std::string query;
                        for (size_t i = 1; i < args.size(); ++i) {
                            if (i > 1) query += " ";
                            query += args[i];
                        }
                        auto rm_opt = resolve_rm(msg.server_id, query);
                        if (!rm_opt) {
                            bot.send_message(msg.channel_id, "⚠️ No recurring message named `" + query + "` found on this server.");
                            return;
                        }
                        if (g_db->remove_recurring_message(rm_opt->id, msg.server_id)) {
                            bot.send_message(msg.channel_id, "✅ Recurring message **\"" + rm_opt->name + "\"** removed.");
                        } else {
                            bot.send_message(msg.channel_id, "⚠️ Failed to remove recurring message.");
                        }
                        return;
                    }

                    // ===================== LIST =====================
                    if (sub == "list" || sub == "ls") {
                        auto now_ts = static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count());
                        auto msgs = g_db->get_recurring_messages_for_server(msg.server_id);
                        if (msgs.empty()) {
                            bot.send_message(msg.channel_id, "\u2139\ufe0f No recurring messages configured on this server. Use `" + pfx + "recurse add` to set one up.");
                            return;
                        }
                        stoatpp::models::MessagePayload payload;
                        nlohmann::json embed = nlohmann::json::object();
                        embed["title"] = "\U0001f501 Recurring Messages (" + std::to_string(msgs.size()) + "/10)";
                        embed["colour"] = "#5865f2";
                        std::string desc;
                        for (const auto& rm : msgs) {
                            uint64_t next_ts = rm.last_sent + rm.interval_seconds;
                            std::string next_str;
                            if (rm.last_sent == 0) {
                                next_str = "*first send pending*";
                            } else if (next_ts <= now_ts) {
                                next_str = "*sending any moment*";
                            } else {
                                // Revolt relative timestamp: <t:unix:R>
                                next_str = "<t:" + std::to_string(next_ts) + ":R>";
                            }
                            desc += "**#" + std::to_string(rm.id) + " \u2014 " + rm.name + "**\n";
                            desc += "\u2022 Channel: <#" + rm.channel_id + ">\n";
                            desc += "\u2022 Interval: every **" + format_interval(rm.interval_seconds) + "**\n";
                            desc += "\u2022 Next send: " + next_str + "\n";
                            desc += "\u2022 Type: `" + rm.message_type + "`";
                            if (rm.owner_tokens) desc += " \U0001f511";
                            desc += "\n\n";
                        }
                        embed["description"] = desc;
                        payload.embeds.push_back(embed);
                        bot.send_message(msg.channel_id, payload);
                        return;
                    }

                    // ===================== TEST =====================
                    if (sub == "test") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "usage: `" + pfx + "recurse test <name|id>`");
                            return;
                        }
                        std::string query;
                        for (size_t i = 1; i < args.size(); ++i) {
                            if (i > 1) query += " ";
                            query += args[i];
                        }
                        auto rm_opt = resolve_rm(msg.server_id, query);
                        if (!rm_opt) {
                            bot.send_message(msg.channel_id, "\u26a0\ufe0f No recurring message named `" + query + "` found on this server.");
                            return;
                        }
                        // test = preview only, does NOT reset the timer
                        send_recurring(bot, *rm_opt, rm_opt->channel_id, [&bot, msg, rm_opt](bool ok, std::string err) {
                            if (ok) {
                                bot.send_message(msg.channel_id, "✅ Test message **\"" + rm_opt->name + "\"** sent to <#" + rm_opt->channel_id + "> *(timer unchanged)*.");
                            } else {
                                std::string details = "⚠️ Failed to send test message. Check that the bot has permission to post in <#" + rm_opt->channel_id + ">.";
                                if (!err.empty()) details += "\n*Details: " + err + "*";
                                bot.send_message(msg.channel_id, details);
                            }
                        });
                        return;
                    }

                    // ===================== SEND =====================
                    if (sub == "send") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "usage: `" + pfx + "recurse send <name|id>`");
                            return;
                        }
                        std::string query;
                        for (size_t i = 1; i < args.size(); ++i) {
                            if (i > 1) query += " ";
                            query += args[i];
                        }
                        auto rm_opt = resolve_rm(msg.server_id, query);
                        if (!rm_opt) {
                            bot.send_message(msg.channel_id, "\u26a0\ufe0f No recurring message named `" + query + "` found on this server.");
                            return;
                        }
                        // send = fire NOW and reset the timer so next post is interval from now
                        uint64_t now_ts = static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count());
                        g_db->update_recurring_last_sent(rm_opt->id, now_ts);
                        send_recurring(bot, *rm_opt, rm_opt->channel_id, [&bot, msg, rm_opt, now_ts](bool ok, std::string err) {
                            if (ok) {
                                uint64_t next_ts = now_ts + rm_opt->interval_seconds;
                                bot.send_message(msg.channel_id,
                                    "✅ Sent **\"" + rm_opt->name + "\"** to <#" + rm_opt->channel_id +
                                    "> and reset timer. Next post: <t:" + std::to_string(next_ts) + ":R>.");
                            } else {
                                std::string details = "⚠️ Failed to send message. Timer was still reset.";
                                if (!err.empty()) details += "\n*Details: " + err + "*";
                                bot.send_message(msg.channel_id, details);
                            }
                        });
                        return;
                    }

                    // ===================== EDIT =====================
                    if (sub == "edit") {
                        // Usage: recurse edit <name|id> key:value [key:value ...]
                        if (args.size() < 3) {
                            bot.send_message(msg.channel_id,
                                "usage: `" + pfx + "recurse edit <name|id> key:value ...`\n"
                                "Keys: `name`, `channel`, `interval`, `type`, `content`, `title`, `description`, `color`, `image`\n"
                                "Use `none` to clear embed fields.");
                            return;
                        }

                        // The first arg after 'edit' could be multi-word name followed by pairs.
                        // We resolve by trying args[1] alone, then args[1]+args[2], etc.
                        std::optional<Database::RecurringMessage> rm_opt;
                        size_t pairs_start = 2;
                        for (size_t end = args.size() - 1; end >= 2; --end) {
                            std::string candidate;
                            for (size_t i = 1; i <= end - 1; ++i) {
                                if (i > 1) candidate += " ";
                                candidate += args[i];
                            }
                            auto found = resolve_rm(msg.server_id, candidate);
                            if (found) {
                                rm_opt = found;
                                pairs_start = end;
                                break;
                            }
                        }
                        if (!rm_opt) rm_opt = resolve_rm(msg.server_id, args[1]);
                        if (!rm_opt) {
                            bot.send_message(msg.channel_id, "⚠️ No recurring message named `" + args[1] + "` found on this server.");
                            return;
                        }

                        // Build pairs text from remaining args
                        std::string pairs_text;
                        for (size_t i = pairs_start; i < args.size(); ++i) {
                            if (i > pairs_start) pairs_text += " ";
                            pairs_text += args[i];
                        }

                        auto pairs = parse_pairs(pairs_text);
                        if (pairs.empty()) {
                            bot.send_message(msg.channel_id, "⚠️ No valid key:value pairs found.");
                            return;
                        }

                        Database::RecurringMessage updated = *rm_opt;
                        nlohmann::json emb = nlohmann::json::object();
                        if (!updated.embed_json.empty()) {
                            try { emb = nlohmann::json::parse(updated.embed_json); } catch (...) {}
                        }

                        std::vector<std::string> change_notes;
                        for (const auto& [k, v] : pairs) {
                            if (k == "name") {
                                if (v.length() > 100) { bot.send_message(msg.channel_id, "⚠️ Name must be 100 chars or fewer."); return; }
                                updated.name = v;
                                change_notes.push_back("name → `" + v + "`");
                            } else if (k == "channel") {
                                std::string new_ch = v;
                                if (new_ch.rfind("<#", 0) == 0 && new_ch.back() == '>') {
                                    new_ch = new_ch.substr(2, new_ch.size() - 3);
                                }
                                if (new_ch != updated.channel_id && g_db->recurring_channel_occupied(new_ch)) {
                                    bot.send_message(msg.channel_id, "⚠️ Channel <#" + new_ch + "> already has a recurring message.");
                                    return;
                                }
                                updated.channel_id = new_ch;
                                change_notes.push_back("channel → <#" + new_ch + ">");
                            } else if (k == "interval") {
                                uint64_t sec = parse_duration(v);
                                if (sec < 300) {
                                    bot.send_message(msg.channel_id, "⚠️ Minimum interval is 5 minutes.");
                                    return;
                                }
                                updated.interval_seconds = sec;
                                change_notes.push_back("interval → " + format_interval(sec));
                            } else if (k == "type") {
                                if (v != "text" && v != "embed") {
                                    bot.send_message(msg.channel_id, "⚠️ Type must be `text` or `embed`.");
                                    return;
                                }
                                updated.message_type = v;
                                change_notes.push_back("type → `" + v + "`");
                            } else if (k == "content") {
                                updated.content = (v == "none" || v == "clear") ? "" : v;
                                change_notes.push_back("content updated");
                            } else if (k == "title" || k == "description" || k == "color" || k == "image") {
                                if (v == "none" || v == "null" || v == "clear") {
                                    emb.erase(k);
                                } else {
                                    emb[k] = v;
                                }
                                change_notes.push_back(k + " updated");
                            }
                        }

                        updated.embed_json = emb.dump();
                        if (g_db->update_recurring_message(updated)) {
                            std::string note;
                            for (size_t i = 0; i < change_notes.size(); ++i) {
                                if (i) note += ", ";
                                note += change_notes[i];
                            }
                            bot.send_message(msg.channel_id, "✅ Updated recurring message **\"" + updated.name + "\"**: " + note + ".");
                        } else {
                            bot.send_message(msg.channel_id, "⚠️ Failed to update recurring message.");
                        }
                        return;
                    }

                    bot.send_message(msg.channel_id,
                        "\u26a0\ufe0f Unknown subcommand. Use `add`, `remove`, `list`, `edit`, `test`, `send`, or `example`.");
                });
            },
            "Utility",
            0,
            0,
            {"add <name> <#channel> <interval>", "remove <name|id>", "list", "edit <name|id> [key:val...]", "test <name|id>", "send <name|id>", "example"}
        });

        // -----------------------------------------------------------------------
        // Background timer: fire due recurring messages every 30 seconds
        // -----------------------------------------------------------------------
        bot.start_timer([&bot](stoatpp::timer) {
            if (!g_db) return;
            auto now = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());

            auto due = g_db->get_due_recurring_messages(now);
            for (const auto& rm : due) {
                // Update last_sent immediately to prevent double-firing
                g_db->update_recurring_last_sent(rm.id, now);
                // Send in a detached thread so we don't block the timer loop
                std::thread([&bot, rm]() {
                    send_recurring(bot, rm, rm.channel_id);
                }).detach();
            }
        }, 30);
    }
};

} // namespace bronx::cogs
