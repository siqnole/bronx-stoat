#pragma once
// cogs/filter.h — Word/regex filter system
//
// Commands:
//   filter create <group>
//   filter delete <group>
//   filter config [group]
//   filter add <word> [group]
//   filter remove <word> [group]
//   filter toggle <group> [#channel]   (no channel = server-wide)
//   filter whitelist <word|#channel|%role>
//   filter unwhitelist <word|#channel|%role>
//
// All write commands require is_user_trusted_or_owner (via fetch_member).

#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "command_config.h"
#include <sstream>
#include <algorithm>
#include <regex>

namespace bronx::cogs {

class FilterModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {

        // ── .filter ──────────────────────────────────────────────────────────
        bot.register_command({
            "filter",
            {},
            "manage word/regex filter groups. use `.filter help` for subcommands.",
            "",
            {"<subcommand>", "[args...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (msg.server_id.empty()) { bot.send_message(msg.channel_id, "⚠️ server only."); return; }
                if (!bronx::g_db)          { bot.send_message(msg.channel_id, "⚠️ database unavailable."); return; }

                std::string sub = args.empty() ? "" : args[0];
                for (auto& c : sub) c = std::tolower((unsigned char)c);

                std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;

                // ── help ─────────────────────────────────────────────────────
                if (sub.empty() || sub == "help") {
                    stoatpp::models::MessagePayload payload;
                    nlohmann::json embed = nlohmann::json::object();
                    embed["title"]  = "🛡️ Filter Commands";
                    embed["colour"] = "#5865f2";
                    embed["description"] =
                        "**`" + pfx + "filter create <group>`** — create a new filter group\n"
                        "**`" + pfx + "filter delete <group>`** — delete a filter group\n"
                        "**`" + pfx + "filter config [group]`** — view group info or list all groups\n"
                        "**`" + pfx + "filter add <word> [group]`** — add a word/regex (default group: `default`)\n"
                        "**`" + pfx + "filter remove <word> [group]`** — remove a word/regex from a group\n"
                        "**`" + pfx + "filter toggle <group> [#channel]`** — toggle on/off (no channel = server-wide)\n"
                        "**`" + pfx + "filter whitelist <word|#channel|%role>`** — add an exemption\n"
                        "**`" + pfx + "filter unwhitelist <word|#channel|%role>`** — remove an exemption\n\n"
                        "Patterns are matched case-insensitively. Wrap in `/…/` for regex: `" + pfx + "filter add /b[a4]d/ group`";
                    payload.replies.push_back({msg.id, false});
                    payload.embeds.push_back(embed);
                    bot.send_message(msg.channel_id, payload);
                    return;
                }

                // ── config (read-only, no permission needed) ─────────────────
                if (sub == "config") {
                    std::string grp_name = args.size() > 1 ? args[1] : "";
                    auto groups = bronx::g_db->filter_list_groups(msg.server_id);

                    if (grp_name.empty()) {
                        if (groups.empty()) {
                            bot.send_message(msg.channel_id, "ℹ️ no filter groups configured. use `" + pfx + "filter create <group>` to add one.");
                            return;
                        }
                        std::string desc;
                        for (const auto& g : groups) {
                            desc += "**" + g.name + "** — " + std::to_string(g.patterns.size()) + " pattern(s)";
                            desc += g.server_wide ? " · 🌍 server-wide" : " · 🎯 channel-specific";
                            desc += "\n";
                        }
                        stoatpp::models::MessagePayload payload;
                        nlohmann::json embed = nlohmann::json::object();
                        embed["title"]       = "🛡️ Filter Groups";
                        embed["colour"]      = "#5865f2";
                        embed["description"] = desc;
                        payload.replies.push_back({msg.id, false});
                        payload.embeds.push_back(embed);
                        bot.send_message(msg.channel_id, payload);
                    } else {
                        auto opt = bronx::g_db->filter_get_group(msg.server_id, grp_name);
                        if (!opt) {
                            bot.send_message(msg.channel_id, "❌ group `" + grp_name + "` not found.");
                            return;
                        }
                        auto& g = *opt;

                        std::string pats_str = g.patterns.empty() ? "*none*" : "";
                        for (size_t i = 0; i < g.patterns.size(); ++i)
                            pats_str += "`" + g.patterns[i] + "`" + (i + 1 < g.patterns.size() ? ", " : "");

                        std::string scope_str = g.server_wide ? "🌍 server-wide" : "🎯 channel-specific";
                        if (!g.enabled_channels.empty()) {
                            scope_str += "\n**enabled in:** ";
                            for (const auto& ch : g.enabled_channels) scope_str += "<#" + ch + "> ";
                        }
                        if (!g.disabled_channels.empty()) {
                            scope_str += "\n**disabled in:** ";
                            for (const auto& ch : g.disabled_channels) scope_str += "<#" + ch + "> ";
                        }

                        stoatpp::models::MessagePayload payload;
                        nlohmann::json embed = nlohmann::json::object();
                        embed["title"]       = "🛡️ filter group: " + g.name;
                        embed["colour"]      = "#5865f2";
                        embed["description"] = "**patterns:** " + pats_str + "\n**scope:** " + scope_str;
                        payload.replies.push_back({msg.id, false});
                        payload.embeds.push_back(embed);
                        bot.send_message(msg.channel_id, payload);
                    }
                    return;
                }

                // ── all write subcommands require trusted/owner ───────────────
                bot.fetch_member(msg.server_id, msg.author.id,
                    [&bot, msg, args, sub, pfx](stoatpp::models::Member member, bool ok) {
                        if (!ok) { bot.send_message(msg.channel_id, "⚠️ could not verify permissions."); return; }
                        if (!is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, member)) {
                            bot.send_message(msg.channel_id, "⚠️ you need manage server permission (or be a trusted user) to use this."); return;
                        }

                        auto reply = [&](const std::string& text) {
                            stoatpp::models::MessagePayload p;
                            p.replies.push_back({msg.id, false});
                            p.content = text;
                            bot.send_message(msg.channel_id, p);
                        };

                        // ── create ────────────────────────────────────────────
                        if (sub == "create") {
                            std::string grp = args.size() > 1 ? args[1] : "";
                            if (grp.empty()) { reply("usage: `" + pfx + "filter create <group>`"); return; }
                            if (bronx::g_db->filter_create_group(msg.server_id, grp))
                                reply("✅ filter group `" + grp + "` created.");
                            else
                                reply("❌ failed to create group (it may already exist).");
                            return;
                        }

                        // ── delete ────────────────────────────────────────────
                        if (sub == "delete") {
                            std::string grp = args.size() > 1 ? args[1] : "";
                            if (grp.empty()) { reply("usage: `" + pfx + "filter delete <group>`"); return; }
                            if (bronx::g_db->filter_delete_group(msg.server_id, grp))
                                reply("🗑️ filter group `" + grp + "` deleted.");
                            else
                                reply("❌ group `" + grp + "` not found.");
                            return;
                        }

                        // ── add ───────────────────────────────────────────────
                        if (sub == "add") {
                            std::string word = args.size() > 1 ? args[1] : "";
                            std::string grp  = args.size() > 2 ? args[2] : "default";
                            if (word.empty()) { reply("usage: `" + pfx + "filter add <word> [group]`"); return; }
                            // Auto-create default group if needed
                            if (grp == "default") bronx::g_db->filter_create_group(msg.server_id, "default");
                            if (bronx::g_db->filter_add_pattern(msg.server_id, grp, word))
                                reply("✅ added `" + word + "` to group `" + grp + "`.");
                            else
                                reply("❌ failed to add pattern. does group `" + grp + "` exist?");
                            return;
                        }

                        // ── remove ────────────────────────────────────────────
                        if (sub == "remove") {
                            std::string word = args.size() > 1 ? args[1] : "";
                            std::string grp  = args.size() > 2 ? args[2] : "default";
                            if (word.empty()) { reply("usage: `" + pfx + "filter remove <word> [group]`"); return; }
                            if (bronx::g_db->filter_remove_pattern(msg.server_id, grp, word))
                                reply("✅ removed `" + word + "` from group `" + grp + "`.");
                            else
                                reply("❌ pattern not found in group `" + grp + "`.");
                            return;
                        }

                        // ── toggle ────────────────────────────────────────────
                        if (sub == "toggle") {
                            std::string grp      = args.size() > 1 ? args[1] : "";
                            std::string chan_arg  = args.size() > 2 ? args[2] : "";
                            if (grp.empty()) { reply("usage: `" + pfx + "filter toggle <group> [#channel]`"); return; }

                            // Resolve <#ID> channel mention
                            std::string channel_id;
                            if (!chan_arg.empty()) {
                                if (chan_arg.size() > 3 && chan_arg.front() == '<' && chan_arg[1] == '#' && chan_arg.back() == '>')
                                    channel_id = chan_arg.substr(2, chan_arg.size() - 3);
                                else
                                    channel_id = chan_arg;
                            }

                            auto opt = bronx::g_db->filter_get_group(msg.server_id, grp);
                            if (!opt) { reply("❌ group `" + grp + "` not found."); return; }

                            // Toggle: invert current state for the target scope
                            bool current  = Database::filter_group_active_in(*opt, channel_id.empty() ? msg.channel_id : channel_id);
                            bool new_state = !current;

                            if (bronx::g_db->filter_toggle(msg.server_id, grp, channel_id, new_state)) {
                                std::string scope = channel_id.empty() ? "server-wide" : "in <#" + channel_id + ">";
                                reply("✅ group `" + grp + "` is now **" + std::string(new_state ? "enabled" : "disabled") + "** " + scope + ".");
                            } else {
                                reply("❌ failed to update group `" + grp + "`.");
                            }
                            return;
                        }

                        auto resolve_whitelist_entry = [&](const std::string& input_entry) -> std::string {
                            std::string cleaned = trim_quotes(input_entry);
                            if (cleaned.size() > 3 && cleaned.front() == '<' && cleaned[1] == '#' && cleaned.back() == '>') {
                                return "#" + cleaned.substr(2, cleaned.size() - 3);
                            }
                            if (cleaned.size() > 3 && cleaned.front() == '<' && cleaned[1] == '%' && cleaned.back() == '>') {
                                return "%" + cleaned.substr(2, cleaned.size() - 3);
                            }
                            
                            std::string name_to_check = cleaned;
                            if (!name_to_check.empty() && (name_to_check[0] == '#' || name_to_check[0] == '%')) {
                                name_to_check = name_to_check.substr(1);
                            }

                            auto srv_opt = bot.get_server(msg.server_id);
                            if (srv_opt) {
                                for (const auto& ch_id : srv_opt->channels) {
                                    auto c_opt = bot.get_channel(ch_id);
                                    if (c_opt && c_opt->name && *c_opt->name == name_to_check) {
                                        return "#" + c_opt->id;
                                    }
                                }
                                for (const auto& r : srv_opt->roles) {
                                    if (r.name == name_to_check || r.id == name_to_check) {
                                        return "%" + r.id;
                                    }
                                }
                            }
                            return cleaned;
                        };

                        // ── whitelist ─────────────────────────────────────────
                        if (sub == "whitelist") {
                            std::string entry = args.size() > 1 ? args[1] : "";
                            if (entry.empty()) { reply("usage: `" + pfx + "filter whitelist <word|#channel|%role>`"); return; }
                            std::string resolved = resolve_whitelist_entry(entry);
                            if (bronx::g_db->filter_whitelist_add(msg.server_id, resolved))
                                reply("✅ added `" + entry + "` (resolved: `" + resolved + "`) to the server whitelist.");
                            else
                                reply("❌ failed to update whitelist.");
                            return;
                        }

                        // ── unwhitelist ───────────────────────────────────────
                        if (sub == "unwhitelist") {
                            std::string entry = args.size() > 1 ? args[1] : "";
                            if (entry.empty()) { reply("usage: `" + pfx + "filter unwhitelist <word|#channel|%role>`"); return; }
                            std::string resolved = resolve_whitelist_entry(entry);
                            if (bronx::g_db->filter_whitelist_remove(msg.server_id, resolved))
                                reply("✅ removed `" + entry + "` (resolved: `" + resolved + "`) from the server whitelist.");
                            else
                                reply("❌ `" + entry + "` (resolved: `" + resolved + "`) was not in the whitelist.");
                            return;
                        }

                        reply("❓ unknown subcommand `" + sub + "`. try `" + pfx + "filter help`.");
                    }
                );
            },
            "Filter",
            0,
            0,
            {"help", "create <group>", "delete <group>", "config [group]",
             "add <word> [group]", "remove <word> [group]",
             "toggle <group> [#channel]", "whitelist <entry>", "unwhitelist <entry>"}
        });
    }

    // ── Message scanner ───────────────────────────────────────────────────────
    // Call from on_message to auto-delete messages matching active filter groups.
    // Returns true if the message was deleted.
    static bool scan_message(stoatpp::cluster& bot,
                             const stoatpp::events::Message& msg) {
        if (!bronx::g_db || msg.server_id.empty() || msg.author.bot) return false;

        // Check if channel is whitelisted
        auto wl = bronx::g_db->filter_get_whitelist(msg.server_id);
        if (std::find(wl.entries.begin(), wl.entries.end(), "#" + msg.channel_id) != wl.entries.end())
            return false;

        // Check author's roles against whitelist
        auto member = bot.get_member(msg.server_id, msg.author.id);
        std::vector<std::string> roles;
        stoatpp::models::Member fetched_member;
        bool has_member = false;
        if (member) {
            fetched_member = *member;
            has_member = true;
            roles = member->roles;
        } else {
            auto res = bot.rest().get_server_member(msg.server_id, msg.author.id);
            if (res.success()) {
                fetched_member = stoatpp::models::Member::from_json(res.body);
                has_member = true;
                roles = fetched_member.roles;
                bot.cache_member(fetched_member);
            }
        }

        // Bypass for server owner or trusted users
        if (has_member && is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, fetched_member)) {
            return false;
        }

        if (has_member) {
            for (const auto& role_id : roles) {
                if (std::find(wl.entries.begin(), wl.entries.end(), "%" + role_id) != wl.entries.end())
                    return false;
            }
        }

        // Build lowercase content for matching
        std::string content_lower = msg.content;
        for (auto& c : content_lower) c = std::tolower((unsigned char)c);

        // Check whitelisted words — if any whitelist word appears in the message, skip
        for (const auto& entry : wl.entries) {
            if (entry.empty() || entry[0] == '#' || entry[0] == '%') continue;
            std::string entry_lower = entry;
            for (auto& c : entry_lower) c = std::tolower((unsigned char)c);
            if (content_lower.find(entry_lower) != std::string::npos) return false;
        }

        auto groups = bronx::g_db->filter_list_groups(msg.server_id);
        for (const auto& g : groups) {
            if (!Database::filter_group_active_in(g, msg.channel_id)) continue;
            for (const auto& pattern : g.patterns) {
                bool matched = false;
                if (pattern.size() >= 2 && pattern.front() == '/' && pattern.back() == '/') {
                    try {
                        std::regex re(pattern.substr(1, pattern.size() - 2),
                                      std::regex::icase | std::regex::ECMAScript);
                        matched = std::regex_search(content_lower, re);
                    } catch (...) {}
                } else {
                    std::string pat_lower = pattern;
                    for (auto& c : pat_lower) c = std::tolower((unsigned char)c);
                    matched = content_lower.find(pat_lower) != std::string::npos;
                }
                if (matched) {
                    bot.delete_message(msg.channel_id, msg.id);
                    return true;
                }
            }
        }
        return false;
    }
};

} // namespace bronx::cogs
