#pragma once
// cogs/customcmds.h — Custom Commands Scripting Module (.cc)

#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "command_config.h"

#include <sstream>
#include <algorithm>
#include <random>
#include <vector>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace bronx::cogs {

// ── Token substitution engine for Custom Commands ─────────────────────────────
static std::string apply_cc_tokens(std::string text, stoatpp::cluster& bot,
                                   const stoatpp::events::Message& msg,
                                   const std::vector<std::string>& cmd_args) {
    auto replace_all = [](std::string& s, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.length(), to);
            pos += to.length();
        }
    };

    // User tokens
    std::string user_name = msg.author.username;
    std::string user_mention = "<@" + msg.author.id + ">";
    std::string user_id = msg.author.id;
    std::string display_name = user_name;

    replace_all(text, "{user.mention}", user_mention);
    replace_all(text, "{user.name}", user_name);
    replace_all(text, "{user.id}", user_id);
    replace_all(text, "{user}", display_name);

    // Server tokens
    std::string server_name = "Server";
    std::string server_id = msg.server_id;
    int member_count = 0;

    if (!msg.server_id.empty()) {
        auto srv = bot.get_server(msg.server_id);
        if (srv) {
            server_name = srv->name;
            if (srv->raw.contains("member_count") && srv->raw["member_count"].is_number()) {
                member_count = srv->raw["member_count"].get<int>();
            } else {
                member_count = static_cast<int>(srv->channels.size());
            }
        }
    }

    replace_all(text, "{server.id}", server_id);
    replace_all(text, "{server}", server_name);
    replace_all(text, "{membercount}", std::to_string(member_count));
    replace_all(text, "{servercount}", std::to_string(bot.get_servers().size()));

    // Channel tokens
    std::string channel_name = msg.channel_id;
    auto chan = bot.get_channel(msg.channel_id);
    if (chan && chan->name) {
        channel_name = *chan->name;
    }
    replace_all(text, "{channel.mention}", "<#" + msg.channel_id + ">");
    replace_all(text, "{channel.id}", msg.channel_id);
    replace_all(text, "{channel}", channel_name);

    // Args tokens
    std::string full_args;
    for (size_t i = 0; i < cmd_args.size(); ++i) {
        if (i > 0) full_args += " ";
        full_args += cmd_args[i];
    }
    replace_all(text, "{args}", full_args);

    for (size_t i = 0; i < cmd_args.size(); ++i) {
        std::string token = "{args[" + std::to_string(i) + "]}";
        replace_all(text, token, cmd_args[i]);
    }
    // Clean remaining un-filled {args[N]}
    for (size_t i = cmd_args.size(); i < 10; ++i) {
        std::string token = "{args[" + std::to_string(i) + "]}";
        replace_all(text, token, "");
    }

    // Misc tokens
    std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
    replace_all(text, "{prefix}", pfx);

    uint64_t up = bot.uptime();
    uint64_t upd = up / 86400, uph = (up % 86400) / 3600, upm = (up % 3600) / 60, ups = up % 60;
    std::string upstr;
    if (upd) upstr += std::to_string(upd) + "d ";
    if (uph) upstr += std::to_string(uph) + "h ";
    if (upm) upstr += std::to_string(upm) + "m ";
    upstr += std::to_string(ups) + "s";
    replace_all(text, "{uptime}", upstr);
    replace_all(text, "{ping}", std::to_string(bot.ping_latency()) + "ms");

    // Random choice token: {random: choice1 | choice2 | choice3}
    size_t rand_pos = 0;
    while ((rand_pos = text.find("{random:", rand_pos)) != std::string::npos) {
        size_t end_pos = text.find('}', rand_pos);
        if (end_pos == std::string::npos) break;

        std::string inner = text.substr(rand_pos + 8, end_pos - (rand_pos + 8));
        std::vector<std::string> options;
        std::stringstream ss(inner);
        std::string item;
        while (std::getline(ss, item, '|')) {
            // trim whitespace
            item.erase(0, item.find_first_not_of(" \t\r\n"));
            item.erase(item.find_last_not_of(" \t\r\n") + 1);
            if (!item.empty()) options.push_back(item);
        }

        std::string chosen;
        if (!options.empty()) {
            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::uniform_int_distribution<size_t> dist(0, options.size() - 1);
            chosen = options[dist(gen)];
        }

        text.replace(rand_pos, (end_pos + 1) - rand_pos, chosen);
        rand_pos += chosen.length();
    }

    return text;
}

class CustomCmdsModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {

        // Listen to all messages for dynamic custom command triggers
        bot.on_message([&bot](const stoatpp::events::Message& msg) {
            if (msg.author.bot || msg.server_id.empty()) return;
            if (!g_db) return;

            // Determine matching prefix
            std::vector<std::string> prefixes;
            if (bot.config().prefix_resolver) {
                prefixes = bot.config().prefix_resolver(bot, msg);
            } else {
                prefixes.push_back(bot.config().command_prefix);
            }

            std::string matched_pfx;
            for (const auto& pfx : prefixes) {
                if (msg.content.rfind(pfx, 0) == 0) {
                    matched_pfx = pfx;
                    break;
                }
            }
            if (matched_pfx.empty()) return;

            // Extract command name and args
            std::string body = msg.content.substr(matched_pfx.length());
            body.erase(0, body.find_first_not_of(" \t\r\n"));
            if (body.empty()) return;

            std::stringstream ss(body);
            std::string cmd_name;
            ss >> cmd_name;
            std::transform(cmd_name.begin(), cmd_name.end(), cmd_name.begin(), ::tolower);

            std::vector<std::string> cmd_args;
            std::string arg;
            while (ss >> arg) {
                cmd_args.push_back(arg);
            }

            // Check if this matches a custom command
            auto cc_opt = g_db->get_custom_command_by_name(msg.server_id, cmd_name);
            if (!cc_opt) return;

            const auto& cc = *cc_opt;

            // Check owner_only restriction
            if (cc.owner_only && !is_bot_owner(bot, msg.author.id)) {
                return;
            }

            // Scope filter (roles, users, channels whitelist/blacklist)
            std::vector<std::string> member_roles = helpers::extract_member_roles(msg);
            Database::AutoScopeFilter filter = cc.filter;
            if (filter.allow_roles.empty() && !cc.allowed_roles.empty()) {
                std::stringstream rss(cc.allowed_roles);
                std::string r_item;
                while (std::getline(rss, r_item, ',')) {
                    r_item.erase(0, r_item.find_first_not_of(" \t\r\n"));
                    r_item.erase(r_item.find_last_not_of(" \t\r\n") + 1);
                    if (!r_item.empty()) filter.allow_roles.push_back(r_item);
                }
            }

            if (!is_bot_owner(bot, msg.author.id) && !helpers::passes_filter(filter, msg.author.id, msg.channel_id, member_roles)) {
                return;
            }

            // Prepare & send payload
            stoatpp::models::MessagePayload payload;
            stoatpp::events::Message msg_with_pfx = msg;
            msg_with_pfx.prefix = matched_pfx;

            if (cc.message_type == "embed") {
                nlohmann::json emb_json = nlohmann::json::object();
                if (!cc.embed_json.empty()) {
                    try { emb_json = nlohmann::json::parse(cc.embed_json); } catch (...) {}
                }

                if (!cc.content.empty()) {
                    payload.content = apply_cc_tokens(cc.content, bot, msg_with_pfx, cmd_args);
                }

                nlohmann::json embed = nlohmann::json::object();
                if (emb_json.contains("title") && emb_json["title"].is_string()) {
                    embed["title"] = unescape_nl(apply_cc_tokens(emb_json["title"].get<std::string>(), bot, msg_with_pfx, cmd_args));
                }
                if (emb_json.contains("description") && emb_json["description"].is_string()) {
                    embed["description"] = unescape_nl(apply_cc_tokens(emb_json["description"].get<std::string>(), bot, msg_with_pfx, cmd_args));
                }
                if (emb_json.contains("color") && emb_json["color"].is_string()) {
                    std::string col = emb_json["color"].get<std::string>();
                    if (col == "random") {
                        char h[8];
                        snprintf(h, sizeof(h), "#%06x", rand() % 0xFFFFFF);
                        col = h;
                    }
                    embed["colour"] = col;
                } else if (emb_json.contains("colour") && emb_json["colour"].is_string()) {
                    embed["colour"] = emb_json["colour"].get<std::string>();
                }
                if (emb_json.contains("image") && emb_json["image"].is_string()) {
                    std::string img_url = emb_json["image"].get<std::string>();
                    if (!img_url.empty()) {
                        std::string media_id = bot.rest().upload_from_url(img_url);
                        if (!media_id.empty()) {
                            embed["media"] = media_id;
                        }
                    }
                }
                if (!embed.empty()) payload.embeds.push_back(embed);
            } else {
                payload.content = apply_cc_tokens(cc.content, bot, msg_with_pfx, cmd_args);
            }

            bot.send_message(msg.channel_id, payload);

            if (!cc.actions.empty()) {
                execute_trigger_actions(bot, cc.actions, msg, cmd_args);
            }
        });

        // ────────────────────────────────────────────────────────────────────
        // .cc — Custom Commands Management
        // ────────────────────────────────────────────────────────────────────
        bot.register_command({
            "cc",
            {"customcommand", "customcmd", "customcmds"},
            "manage server custom scripted commands.",
            "", {"<subcommand>", "[args...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (msg.server_id.empty()) { bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Server only."); return; }
                if (!g_db) { bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Database unavailable."); return; }
                std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;

                bot.fetch_member(msg.server_id, msg.author.id,
                    [&bot, msg, args, pfx](stoatpp::models::Member member, bool ok) {
                        if (!ok) { bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Could not verify permissions."); return; }
                        if (!is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, member)) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " You need Manage Server permission."); return;
                        }

                        if (args.empty()) {
                            bot.send_message(msg.channel_id,
                                "**Custom Command subcommands:**\n"
                                "• `" + pfx + "cc add <name> [response]` — create a command (wizard if response omitted)\n"
                                "• `" + pfx + "cc remove <name|id>` — delete a command\n"
                                "• `" + pfx + "cc list` — list all server custom commands\n"
                                "• `" + pfx + "cc edit <name|id> key:val [...]` — edit fields (`content`, `description`, `type`, `title`, `color`, `image`, `roles`)\n"
                                "• `" + pfx + "cc rename <old> <new>` — rename a command\n"
                                "• `" + pfx + "cc info <name|id>` — show command details and token reference");
                            return;
                        }

                        std::string sub = args[0];
                        std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

                        // ── add ──────────────────────────────────────────────
                        if (sub == "add" || sub == "create") {
                            if (args.size() < 2) {
                                bot.send_message(msg.channel_id, "usage: `" + pfx + "cc add <name> [response]`");
                                return;
                            }
                            std::string cmd_name = args[1];
                            std::transform(cmd_name.begin(), cmd_name.end(), cmd_name.begin(), ::tolower);

                            // Validate command name
                            if (cmd_name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Command name can only contain letters, numbers, hyphens, and underscores.");
                                return;
                            }

                            // Check built-in collision
                            for (const auto& built_in : bot.get_commands()) {
                                if (built_in.name == cmd_name) {
                                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Cannot create custom command `" + cmd_name + "`: name collides with a built-in bot command.");
                                    return;
                                }
                                for (const auto& alias : built_in.aliases) {
                                    if (alias == cmd_name) {
                                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Cannot create custom command `" + cmd_name + "`: name collides with a built-in command alias.");
                                        return;
                                    }
                                }
                            }

                            // Check server limit
                            if (g_db->get_custom_commands_for_server(msg.server_id).size() >= 25) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Maximum 25 custom commands per server.");
                                return;
                            }

                            // Check duplicate custom command name
                            if (g_db->get_custom_command_by_name(msg.server_id, cmd_name)) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Custom command `" + cmd_name + "` already exists.");
                                return;
                            }

                            // If response is provided inline
                            if (args.size() >= 3) {
                                std::string response;
                                for (size_t i = 2; i < args.size(); ++i) {
                                    if (i > 2) response += " ";
                                    response += args[i];
                                }

                                Database::CustomCommand c;
                                c.server_id = msg.server_id;
                                c.name = cmd_name;
                                c.message_type = "text";
                                c.content = response;

                                if (g_db->add_custom_command(c)) {
                                    bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Custom command **`" + pfx + cmd_name + "`** created! (ID: `" + std::to_string(c.id) + "`)");
                                } else {
                                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save custom command.");
                                }
                                return;
                            }

                            // Interactive Wizard
                            bot.send_message(msg.channel_id,
                                "⚙️ **Custom Command Setup — \"" + cmd_name + "\"**\n"
                                "**Step 1/2**: Response type? Reply: `text` or `embed`");
                            bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30),
                                [&bot, msg, cmd_name, pfx](std::optional<stoatpp::events::Message> r1) {
                                    if (!r1) { bot.send_message(msg.channel_id, "⏱️ Timed out."); return; }
                                    std::string mtype = r1->content;
                                    std::transform(mtype.begin(), mtype.end(), mtype.begin(), ::tolower);
                                    if (mtype == "cancel") { bot.send_message(msg.channel_id, "" + bronx::emj_no + " Cancelled."); return; }
                                    if (mtype != "text" && mtype != "embed") mtype = "text";

                                    std::string prompt = "**Step 2/2**: ";
                                    if (mtype == "text") {
                                        prompt += "Type the response content. You can use variables like `{user.mention}`, `{server}`, `{args}`, `{random: a | b}`.";
                                    } else {
                                        prompt += "Provide embed fields: `title:...`, `description:...`, `color:#hex`, `image:url`, `content:...` (text outside embed).";
                                    }

                                    bot.send_message(msg.channel_id, prompt);
                                    bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(120),
                                        [&bot, msg, cmd_name, mtype, pfx](std::optional<stoatpp::events::Message> r2) {
                                            if (!r2) { bot.send_message(msg.channel_id, "⏱️ Timed out."); return; }
                                            if (r2->content == "cancel") { bot.send_message(msg.channel_id, "" + bronx::emj_no + " Cancelled."); return; }

                                            Database::CustomCommand c;
                                            c.server_id = msg.server_id;
                                            c.name = cmd_name;
                                            c.message_type = mtype;

                                            if (mtype == "embed") {
                                                auto pairs = parse_kv_pairs(r2->content);
                                                nlohmann::json emb = nlohmann::json::object();
                                                for (const auto& [k, v] : pairs) {
                                                    if (k == "content") c.content = v;
                                                    else emb[k] = v;
                                                }
                                                c.embed_json = emb.dump();
                                            } else {
                                                c.content = r2->content;
                                            }

                                            if (g_db->add_custom_command(c)) {
                                                bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Custom command **`" + pfx + cmd_name + "`** created! (ID: `" + std::to_string(c.id) + "`)");
                                            } else {
                                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save custom command.");
                                            }
                                        });
                                });
                            return;
                        }

                        // ── remove ───────────────────────────────────────────
                        if (sub == "remove" || sub == "delete" || sub == "del") {
                            if (args.size() < 2) { bot.send_message(msg.channel_id, "usage: `" + pfx + "cc remove <name|id>`"); return; }
                            std::string q = args[1];
                            auto opt = resolve_auto<Database::CustomCommand>(msg.server_id, q,
                                [](int id, const std::string& sid) { return g_db->get_custom_command_by_id(id, sid); },
                                [](const std::string& sid, const std::string& n) { return g_db->get_custom_command_by_name(sid, n); });
                            if (!opt) { bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Command not found: `" + q + "`"); return; }

                            if (g_db->remove_custom_command(opt->id, msg.server_id)) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Removed custom command **`" + pfx + opt->name + "`**.");
                            } else {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to remove custom command.");
                            }
                            return;
                        }

                        // ── list ─────────────────────────────────────────────
                        if (sub == "list" || sub == "ls") {
                            auto list = g_db->get_custom_commands_for_server(msg.server_id);
                            if (list.empty()) { bot.send_message(msg.channel_id, "ℹ️ No custom commands configured for this server."); return; }

                            stoatpp::models::MessagePayload payload;
                            nlohmann::json embed = nlohmann::json::object();
                            embed["title"] = "⚙️ Custom Commands (" + std::to_string(list.size()) + "/25)";
                            embed["colour"] = "#6366f1";

                            std::string desc;
                            for (const auto& c : list) {
                                desc += "**#" + std::to_string(c.id) + " — `" + pfx + c.name + "`**\n";
                                desc += "• Type: `" + c.message_type + "`";
                                if (!c.description.empty()) desc += " — " + c.description;
                                desc += "\n\n";
                            }
                            embed["description"] = desc;
                            payload.embeds.push_back(embed);
                            bot.send_message(msg.channel_id, payload);
                            return;
                        }

                        // ── info ─────────────────────────────────────────────
                        if (sub == "info") {
                            if (args.size() < 2) { bot.send_message(msg.channel_id, "usage: `" + pfx + "cc info <name|id>`"); return; }
                            std::string q = args[1];
                            auto opt = resolve_auto<Database::CustomCommand>(msg.server_id, q,
                                [](int id, const std::string& sid) { return g_db->get_custom_command_by_id(id, sid); },
                                [](const std::string& sid, const std::string& n) { return g_db->get_custom_command_by_name(sid, n); });
                            if (!opt) { bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Command not found: `" + q + "`"); return; }

                            stoatpp::models::MessagePayload payload;
                            nlohmann::json embed = nlohmann::json::object();
                            embed["title"] = "⚙️ CC #" + std::to_string(opt->id) + " — ." + opt->name;
                            embed["colour"] = "#6366f1";

                            std::string desc = "**Name:** `" + opt->name + "`\n";
                            desc += "**Type:** `" + opt->message_type + "`\n";
                            if (!opt->description.empty()) desc += "**Description:** " + opt->description + "\n";
                            if (!opt->content.empty()) desc += "**Content:**\n```\n" + opt->content.substr(0, 200) + "\n```\n";
                            if (!opt->allowed_roles.empty()) desc += "**Allowed Roles:** `" + opt->allowed_roles + "`\n";
                            if (!opt->actions.empty()) desc += "**Actions:** `" + opt->actions + "`\n";
                            if (opt->owner_only) desc += "**Restriction:** Server Owner Only\n";

                            desc += "\n**Variables:** `{user}`, `{user.mention}`, `{server}`, `{channel}`, `{args}`, `{args[0]}`, `{random: a | b}`, `{ping}`, `{uptime}`";
                            desc += "\n**Actions:** `timeout:10m`, `addrole:ROLE_ID`, `removerole:ROLE_ID`, `kick`, `ban`, `delete`";

                            embed["description"] = desc;
                            payload.embeds.push_back(embed);
                            bot.send_message(msg.channel_id, payload);
                            return;
                        }

                        // ── rename ───────────────────────────────────────────
                        if (sub == "rename") {
                            if (args.size() < 3) { bot.send_message(msg.channel_id, "usage: `" + pfx + "cc rename <old> <new>`"); return; }
                            std::string old_name = args[1];
                            std::string new_name = args[2];
                            std::transform(new_name.begin(), new_name.end(), new_name.begin(), ::tolower);

                            auto opt = resolve_auto<Database::CustomCommand>(msg.server_id, old_name,
                                [](int id, const std::string& sid) { return g_db->get_custom_command_by_id(id, sid); },
                                [](const std::string& sid, const std::string& n) { return g_db->get_custom_command_by_name(sid, n); });
                            if (!opt) { bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Command not found: `" + old_name + "`"); return; }

                            if (g_db->rename_custom_command(opt->id, msg.server_id, new_name)) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Renamed custom command **`" + pfx + opt->name + "`** → **`" + pfx + new_name + "`**.");
                            } else {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to rename command.");
                            }
                            return;
                        }

                        // ── edit ─────────────────────────────────────────────
                        if (sub == "edit") {
                            if (args.size() < 3) { bot.send_message(msg.channel_id, "usage: `" + pfx + "cc edit <name|id> key:val [...]`"); return; }
                            auto opt = resolve_auto<Database::CustomCommand>(msg.server_id, args[1],
                                [](int id, const std::string& sid) { return g_db->get_custom_command_by_id(id, sid); },
                                [](const std::string& sid, const std::string& n) { return g_db->get_custom_command_by_name(sid, n); });
                            if (!opt) { bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Command not found: `" + args[1] + "`"); return; }

                            std::string pairs_text;
                            for (size_t i = 2; i < args.size(); ++i) {
                                if (i > 2) pairs_text += " ";
                                pairs_text += args[i];
                            }
                            auto pairs = parse_kv_pairs(pairs_text);
                            if (pairs.empty()) { bot.send_message(msg.channel_id, "" + bronx::emj_warning + " No valid key:value pairs found."); return; }

                            auto split_list = [](const std::string& str) {
                                std::vector<std::string> vec;
                                std::stringstream ss(str);
                                std::string item;
                                while (std::getline(ss, item, ',')) {
                                    item.erase(0, item.find_first_not_of(" \t\r\n"));
                                    item.erase(item.find_last_not_of(" \t\r\n") + 1);
                                    if (!item.empty()) vec.push_back(item);
                                }
                                return vec;
                            };

                            Database::CustomCommand updated = *opt;
                            nlohmann::json emb = nlohmann::json::object();
                            if (!updated.embed_json.empty()) { try { emb = nlohmann::json::parse(updated.embed_json); } catch (...) {} }

                            std::vector<std::string> notes;
                            for (const auto& [k, v] : pairs) {
                                if (k == "name") { updated.name = v; notes.push_back("name→`" + v + "`"); }
                                else if (k == "description") { updated.description = v; notes.push_back("desc updated"); }
                                else if (k == "type") { updated.message_type = v; notes.push_back("type→`" + v + "`"); }
                                else if (k == "content") { updated.content = v; notes.push_back("content updated"); }
                                else if (k == "roles") { updated.allowed_roles = v; notes.push_back("roles updated"); }
                                else if (k == "action" || k == "actions") { updated.actions = v; notes.push_back("actions→`" + v + "`"); }
                                else if (k == "allowroles" || k == "allow_roles") { updated.filter.allow_roles = split_list(v); notes.push_back("allow_roles updated"); }
                                else if (k == "ignoreroles" || k == "ignore_roles") { updated.filter.ignore_roles = split_list(v); notes.push_back("ignore_roles updated"); }
                                else if (k == "allowusers" || k == "allow_users") { updated.filter.allow_users = split_list(v); notes.push_back("allow_users updated"); }
                                else if (k == "ignoreusers" || k == "ignore_users") { updated.filter.ignore_users = split_list(v); notes.push_back("ignore_users updated"); }
                                else if (k == "allowchannels" || k == "allow_channels") { updated.filter.allow_channels = split_list(v); notes.push_back("allow_channels updated"); }
                                else if (k == "ignorechannels" || k == "ignore_channels") { updated.filter.ignore_channels = split_list(v); notes.push_back("ignore_channels updated"); }
                                else if (k == "title" || k == "color" || k == "image") {
                                    if (v == "none" || v == "clear") emb.erase(k); else emb[k] = v;
                                    notes.push_back(k + " updated");
                                }
                            }
                            updated.embed_json = emb.dump();

                            if (g_db->update_custom_command(updated)) {
                                std::string n;
                                for (size_t i = 0; i < notes.size(); ++i) { if (i) n += ", "; n += notes[i]; }
                                bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Updated **`" + pfx + updated.name + "`**: " + n + ".");
                            } else {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Update failed.");
                            }
                            return;
                        }

                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Unknown subcommand. Use `add`, `remove`, `list`, `edit`, `rename`, `info`.");
                    });
            },
            "Utility", 0, 0,
            {"add <name> [response]", "remove <name|id>", "list", "edit <name|id> key:val", "rename <old> <new>", "info <name|id>"}
        });
    }
};

} // namespace bronx::cogs
