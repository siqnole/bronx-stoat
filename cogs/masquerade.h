#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include <regex>
#include <algorithm>
#include <iostream>
#include <random>
#include <sstream>
#include <iomanip>

namespace bronx::cogs {

class MasqueradeModule : public stoatpp::bot_module {
private:
    // ── helpers ──────────────────────────────────────────────────────────────

    static bool is_clear_word(const std::string& s) {
        std::string l = s;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        return l == "clear" || l == "none" || l == "reset" || l == "null";
    }

    static bool valid_colour(const std::string& val) {
        // Accept any CSS colour: named (purple, dark-slate-gray), hex (#abc, #aabbcc),
        // rgb/rgba(...), hsl/hsla(...), CSS vars, and linear/radial/conic gradients.
        std::regex pattern(
            R"(^(?:[a-z][-a-z]*|var\(--[a-z\d-]+\)|hsla?\([\d.,%/ ]+\)|rgba?\([\d.,%/ ]+\)|#[a-f0-9]{3,8}|(repeating-)?(linear|conic|radial)-gradient\((.+)\))$)",
            std::regex_constants::ECMAScript | std::regex_constants::icase
        );
        return std::regex_match(val, pattern);
    }

    static std::string random_colour() {
        static thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> dist(0, 0xFFFFFF);
        std::ostringstream oss;
        oss << "#" << std::hex << std::setfill('0') << std::setw(6) << dist(rng);
        return oss.str();
    }

    static std::string parse_colour_input(const std::string& input) {
        std::string trimmed = input;
        trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
        trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
        if (trimmed.empty()) return "";

        auto to_lower = [](std::string s) {
            for (auto& c : s) c = std::tolower(c);
            return s;
        };
        std::string lower = to_lower(trimmed);

        if (lower == "random") {
            return random_colour();
        }

        // If it's already a full gradient definition (like linear-gradient(...))
        for (const std::string& grad_func : {"linear-gradient(", "radial-gradient(", "conic-gradient(", "repeating-linear-gradient(", "repeating-radial-gradient(", "repeating-conic-gradient("}) {
            if (lower.find(grad_func) != std::string::npos) {
                return trimmed;
            }
        }

        // Tokenize
        std::vector<std::string> tokens;
        {
            std::stringstream ss(trimmed);
            std::string tok;
            while (ss >> tok) tokens.push_back(tok);
        }
        if (tokens.empty()) return "";

        std::vector<std::string> color_tokens;
        std::string angle_str = "90deg";

        for (const auto& tok : tokens) {
            std::string tok_lower = to_lower(tok);
            
            // Check if it's an angle/degree
            if ((tok_lower.size() > 3 && tok_lower.substr(tok_lower.size() - 3) == "deg") ||
                (tok_lower.size() > 3 && tok_lower.substr(tok_lower.size() - 3) == "rad") ||
                (tok_lower.size() > 4 && tok_lower.substr(tok_lower.size() - 4) == "turn")) {
                angle_str = tok_lower;
            } else if (tok_lower.rfind("deg:", 0) == 0 || tok_lower.rfind("angle:", 0) == 0 || tok_lower.rfind("degree:", 0) == 0) {
                size_t p = tok.find(':');
                angle_str = tok.substr(p + 1);
                if (angle_str.find("deg") == std::string::npos && angle_str.find("rad") == std::string::npos) {
                    angle_str += "deg";
                }
            } else if (tok_lower == "to" || tok_lower == "right" || tok_lower == "left" || tok_lower == "top" || tok_lower == "bottom") {
                if (tok_lower == "to") {
                    angle_str = "to";
                } else if (!angle_str.empty() && angle_str == "to") {
                    angle_str += " " + tok_lower;
                } else {
                    angle_str = "to " + tok_lower;
                }
            } else {
                std::string processed = tok;
                if (tok_lower == "random") {
                    processed = random_colour();
                }
                color_tokens.push_back(processed);
            }
        }

        if (angle_str == "to") {
            angle_str = "to right";
        }

        if (color_tokens.empty()) return "";
        if (color_tokens.size() == 1) {
            return color_tokens[0];
        }

        size_t n = color_tokens.size();
        std::string grad = "linear-gradient(" + angle_str;
        for (size_t i = 0; i < n; ++i) {
            int p = static_cast<int>(std::round(i * 100.0 / (n - 1)));
            grad += ", " + color_tokens[i] + " " + std::to_string(p) + "%";
        }
        grad += ")";
        return grad;
    }

    // ── personal masquerade subcommand handler ────────────────────────────────
    // Called when the user runs .masquerade [subcommand] without "server" first arg.

    static void handle_personal(
        stoatpp::cluster& bot,
        const stoatpp::events::Message& msg,
        const std::vector<std::string>& args,
        const std::string& pfx
    ) {
        if (!g_db) {
            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Database connection is not initialized.");
            return;
        }

        std::string sub = args.empty() ? "status" : args[0];
        std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

        auto current = g_db->get_user_masquerade(msg.author.id);
        Database::UserMasquerade cfg;
        cfg.user_id = msg.author.id;
        if (current) cfg = *current;

        // ── status ────────────────────────────────────────────────────────────
        if (sub == "status" || sub.empty()) {
            stoatpp::models::MessagePayload payload;
            nlohmann::json embed = nlohmann::json::object();
            embed["title"] = "🎭 Your Masquerade";
            embed["colour"] = "#9b59b6";
            std::string desc = "Your personal masquerade settings:\n\n";
            desc += "• **Name**: "   + (cfg.name   ? "`" + *cfg.name   + "`" : "*none*") + "\n";
            desc += "• **Avatar**: " + (cfg.avatar ? "[View image](" + *cfg.avatar + ")" : "*none*") + "\n";
            desc += "• **Colour**: " + (cfg.colour ? "`" + *cfg.colour + "`" : "*none*") + "\n";
            desc += "\nUse `" + pfx + "masquerade <name|avatar|colour|clear>` to customize.";
            desc += "\nAdmins can override or disable all masquerades with `" + pfx + "masquerade server`.";
            embed["description"] = desc;
            payload.embeds.push_back(embed);
            bot.send_message(msg.channel_id, payload);
            return;
        }

        // ── clear ─────────────────────────────────────────────────────────────
        if (sub == "clear") {
            if (g_db->delete_user_masquerade(msg.author.id)) {
                bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Cleared your personal masquerade.");
            } else {
                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to clear your masquerade from the database.");
            }
            return;
        }

        // ── name ──────────────────────────────────────────────────────────────
        if (sub == "name") {
            if (args.size() < 2) {
                bot.send_message(msg.channel_id, "Usage: `" + pfx + "masquerade name <new name>` or `" + pfx + "masquerade name clear`");
                return;
            }
            std::string val;
            for (size_t i = 1; i < args.size(); ++i)
                val += args[i] + (i + 1 < args.size() ? " " : "");

            if (is_clear_word(val)) {
                cfg.name = std::nullopt;
            } else {
                if (val.empty() || val.length() > 32) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Name must be between 1 and 32 characters.");
                    return;
                }
                cfg.name = val;
            }

            if (g_db->save_user_masquerade(cfg)) {
                bot.send_message(msg.channel_id, cfg.name ? "" + bronx::emj_yes + " Set name override to `" + *cfg.name + "`." : "" + bronx::emj_yes + " Cleared name override.");
            } else {
                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save setting to database.");
            }
            return;
        }

        // ── avatar ────────────────────────────────────────────────────────────
        if (sub == "avatar") {
            if (args.size() < 2 && msg.attachments.empty()) {
                bot.send_message(msg.channel_id, "Usage: `" + pfx + "masquerade avatar <url>` or upload an image attachment.");
                return;
            }

            std::string val;
            if (!msg.attachments.empty()) {
                val = bot.config().autumn_url + "/attachments/" + msg.attachments[0];
            } else {
                val = args[1];
            }

            if (is_clear_word(val)) {
                cfg.avatar = std::nullopt;
            } else {
                if (val.empty() || val.length() > 256) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Avatar URL must be between 1 and 256 characters.");
                    return;
                }
                cfg.avatar = helpers::upload_avatar(bot, val);
            }

            if (g_db->save_user_masquerade(cfg)) {
                bot.send_message(msg.channel_id, cfg.avatar ? "" + bronx::emj_yes + " Set avatar override successfully." : "" + bronx::emj_yes + " Cleared avatar override.");
            } else {
                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save setting to database.");
            }
            return;
        }

        // ── colour ────────────────────────────────────────────────────────────
        if (sub == "colour" || sub == "color") {
            if (args.size() < 2) {
                bot.send_message(msg.channel_id, "Usage: `" + pfx + "masquerade colour <CSS colour>` or `" + pfx + "masquerade colour clear`");
                return;
            }
            std::string val;
            for (size_t i = 1; i < args.size(); ++i)
                val += args[i] + (i + 1 < args.size() ? " " : "");

            if (is_clear_word(val)) {
                cfg.colour = std::nullopt;
            } else {
                std::string parsed = parse_colour_input(val);
                if (parsed.empty() || parsed.length() > 256) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Colour override must be 256 characters or fewer.");
                    return;
                }
                if (!valid_colour(parsed)) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid colour. Accepted formats: `purple`, `#ff6b6b`, `rgb(255,0,0)`, `hsl(270,60%,50%)`, `linear-gradient(...)`, or `random`.");
                    return;
                }
                cfg.colour = parsed;
            }

            if (g_db->save_user_masquerade(cfg)) {
                bot.send_message(msg.channel_id, cfg.colour ? "" + bronx::emj_yes + " Set colour override to `" + *cfg.colour + "`."
                                                            : "" + bronx::emj_yes + " Cleared colour override.");
            } else {
                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save setting to database.");
            }
            return;
        }

        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Unknown subcommand. Use `" + pfx + "masquerade <name|avatar|colour|clear|status>`.");
    }

    // ── server-level masquerade subcommand handler ────────────────────────────
    // Called when the first arg is "server". Requires ManageServer.

    static void handle_server(
        stoatpp::cluster& bot,
        const stoatpp::events::Message& msg,
        const std::vector<std::string>& args,   // args[0] == "server", args[1..] are sub-args
        const std::string& pfx,
        const stoatpp::models::Server& server,
        const stoatpp::models::Member& member
    ) {
        if (!g_db) {
            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Database connection is not initialized.");
            return;
        }

        bool has_admin = bot.is_owner(msg.author.id) ||
                         bot.is_server_owner(msg.server_id, msg.author.id) ||
                         bot.has_permission(server, member, stoatpp::permissions::ManageServer);

        if (!has_admin) {
            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Permission error: you need the Manage Server permission to configure server masquerade settings.");
            return;
        }

        // args layout: ["server", <sub>, ...rest]
        std::string sub = args.size() >= 2 ? args[1] : "status";
        std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

        auto current_opt = g_db->get_masquerade_config(msg.server_id);
        Database::MasqueradeConfig cfg;
        cfg.server_id = msg.server_id;
        if (current_opt) cfg = *current_opt;

        // ── status ────────────────────────────────────────────────────────────
        if (sub == "status") {
            stoatpp::models::MessagePayload payload;
            nlohmann::json embed = nlohmann::json::object();
            embed["title"] = "🎭 Server Masquerade Settings";
            embed["colour"] = cfg.disabled ? "#e74c3c" : "#9b59b6";
            std::string desc = "Server-wide masquerade override settings:\n\n";
            desc += "• **Server Masquerades**: " + std::string(cfg.disabled ? "🔴 **Disabled** (all masquerades are blocked)" : "🟢 **Enabled**") + "\n";
            desc += "• **User Masquerades**: "   + std::string(cfg.disabled ? "🔴 **Disabled**" : (cfg.disable_user ? "" + bronx::emj_no + " **Disabled**" : "🟢 **Enabled**")) + "\n";
            desc += "  └─ **Names**: "           + std::string(cfg.disable_user_name ? "" + bronx::emj_no + " Disabled" : "🟢 Allowed") + "\n";
            desc += "  └─ **Avatars**: "         + std::string(cfg.disable_user_avatar ? "" + bronx::emj_no + " Disabled" : "🟢 Allowed") + "\n";
            desc += "  └─ **Colours**: "         + std::string(cfg.disable_user_colour ? "" + bronx::emj_no + " Disabled" : "🟢 Allowed") + "\n\n";
            desc += "• **Name override**: "       + (cfg.name   ? "`" + *cfg.name   + "`" : "*none*") + "\n";
            desc += "• **Avatar override**: "     + (cfg.avatar ? "[View image](" + *cfg.avatar + ")" : "*none*") + "\n";
            desc += "• **Colour override**: "     + (cfg.colour ? "`" + *cfg.colour + "`" : "*none*") + "\n\n";
            desc += "Server override fields take priority over per-user masquerades (field-by-field).\n";
            desc += "Use `" + pfx + "masquerade server disable <user|name|avatar|colour>` to disable specific user masquerade fields.\n";
            desc += "Use `" + pfx + "masquerade server <disable|enable|name|avatar|colour|clear>` to manage.";
            embed["description"] = desc;
            payload.embeds.push_back(embed);
            bot.send_message(msg.channel_id, payload);
            return;
        }

        // ── disable ───────────────────────────────────────────────────────────
        if (sub == "disable" || sub == "disableuser") {
            std::string target_type = (args.size() >= 3) ? args[2] : "";
            std::transform(target_type.begin(), target_type.end(), target_type.begin(), ::tolower);

            if (target_type == "name" || target_type == "names" || target_type == "username") {
                cfg.disable_user_name = true;
                if (g_db->save_masquerade_config(cfg)) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_yes + " **User custom names** are now **disabled** for this server.");
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save setting to database.");
                }
            } else if (target_type == "avatar" || target_type == "avatars" || target_type == "pfp") {
                cfg.disable_user_avatar = true;
                if (g_db->save_masquerade_config(cfg)) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_yes + " **User custom avatars** are now **disabled** for this server.");
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save setting to database.");
                }
            } else if (target_type == "colour" || target_type == "color" || target_type == "colours" || target_type == "colors") {
                cfg.disable_user_colour = true;
                if (g_db->save_masquerade_config(cfg)) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_yes + " **User custom colours** are now **disabled** for this server.");
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save setting to database.");
                }
            } else if (sub == "disableuser" || target_type == "user" || target_type == "users" || target_type == "usermasquerade" || target_type == "usermasquerades") {
                cfg.disabled = false;
                cfg.disable_user = true;
                if (g_db->save_masquerade_config(cfg)) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_yes + " **User masquerades** are now **disabled** for this server. Server-set masquerade overrides will still apply!");
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save setting to database.");
                }
            } else {
                cfg.disabled = true;
                if (g_db->save_masquerade_config(cfg)) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_yes + " **All masquerades** are now **disabled** for this server. No user or server masquerades will be applied.");
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save setting to database.");
                }
            }
            return;
        }

        // ── enable ────────────────────────────────────────────────────────────
        if (sub == "enable" || sub == "enableuser") {
            std::string target_type = (args.size() >= 3) ? args[2] : "";
            std::transform(target_type.begin(), target_type.end(), target_type.begin(), ::tolower);

            if (target_type == "name" || target_type == "names" || target_type == "username") {
                cfg.disable_user_name = false;
                if (g_db->save_masquerade_config(cfg)) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_yes + " **User custom names** are now **enabled** for this server.");
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save setting to database.");
                }
            } else if (target_type == "avatar" || target_type == "avatars" || target_type == "pfp") {
                cfg.disable_user_avatar = false;
                if (g_db->save_masquerade_config(cfg)) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_yes + " **User custom avatars** are now **enabled** for this server.");
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save setting to database.");
                }
            } else if (target_type == "colour" || target_type == "color" || target_type == "colours" || target_type == "colors") {
                cfg.disable_user_colour = false;
                if (g_db->save_masquerade_config(cfg)) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_yes + " **User custom colours** are now **enabled** for this server.");
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save setting to database.");
                }
            } else if (sub == "enableuser" || target_type == "user" || target_type == "users" || target_type == "usermasquerade" || target_type == "usermasquerades") {
                cfg.disabled = false;
                cfg.disable_user = false;
                if (g_db->save_masquerade_config(cfg)) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_yes + " **User masquerades** are now **enabled** for this server.");
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save setting to database.");
                }
            } else {
                cfg.disabled = false;
                cfg.disable_user = false;
                cfg.disable_user_name = false;
                cfg.disable_user_avatar = false;
                cfg.disable_user_colour = false;
                if (g_db->save_masquerade_config(cfg)) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_yes + " **All masquerades** are now **enabled** for this server.");
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save setting to database.");
                }
            }
            return;
        }

        // ── clear ─────────────────────────────────────────────────────────────
        if (sub == "clear") {
            if (g_db->delete_masquerade_config(msg.server_id)) {
                bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Cleared all server masquerade override settings.");
            } else {
                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to clear masquerade settings in the database.");
            }
            return;
        }

        // ── name ──────────────────────────────────────────────────────────────
        if (sub == "name") {
            if (args.size() < 3) {
                bot.send_message(msg.channel_id, "Usage: `" + pfx + "masquerade server name <new name>` or `clear`");
                return;
            }
            std::string val;
            for (size_t i = 2; i < args.size(); ++i)
                val += args[i] + (i + 1 < args.size() ? " " : "");

            if (is_clear_word(val)) {
                cfg.name = std::nullopt;
            } else {
                if (val.empty() || val.length() > 32) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Name must be between 1 and 32 characters.");
                    return;
                }
                cfg.name = val;
            }

            if (g_db->save_masquerade_config(cfg)) {
                bot.send_message(msg.channel_id, cfg.name ? "" + bronx::emj_yes + " Set server name override to `" + *cfg.name + "`." : "" + bronx::emj_yes + " Cleared server name override.");
            } else {
                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save setting to database.");
            }
            return;
        }

        // ── avatar ────────────────────────────────────────────────────────────
        if (sub == "avatar") {
            if (args.size() < 3 && msg.attachments.empty()) {
                bot.send_message(msg.channel_id, "Usage: `" + pfx + "masquerade server avatar <url>` or upload an image attachment.");
                return;
            }

            std::string val;
            if (!msg.attachments.empty()) {
                val = bot.config().autumn_url + "/attachments/" + msg.attachments[0];
            } else {
                val = args[2];
            }

            if (is_clear_word(val)) {
                cfg.avatar = std::nullopt;
            } else {
                if (val.empty() || val.length() > 256) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Avatar URL must be between 1 and 256 characters.");
                    return;
                }
                cfg.avatar = helpers::upload_avatar(bot, val);
            }

            if (g_db->save_masquerade_config(cfg)) {
                bot.send_message(msg.channel_id, cfg.avatar ? "" + bronx::emj_yes + " Set server avatar override successfully." : "" + bronx::emj_yes + " Cleared server avatar override.");
            } else {
                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save setting to database.");
            }
            return;
        }

        // ── colour ────────────────────────────────────────────────────────────
        if (sub == "colour" || sub == "color") {
            bool has_role_perm = bot.is_owner(msg.author.id) ||
                                 bot.is_server_owner(msg.server_id, msg.author.id) ||
                                 bot.has_permission(server, member, stoatpp::permissions::ManageRole);
            if (!has_role_perm) {
                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " You need the Manage Role permission to change the server colour override.");
                return;
            }

            if (args.size() < 3) {
                bot.send_message(msg.channel_id, "Usage: `" + pfx + "masquerade server colour <CSS colour>` or `clear`");
                return;
            }
            std::string val;
            for (size_t i = 2; i < args.size(); ++i)
                val += args[i] + (i + 1 < args.size() ? " " : "");

            if (is_clear_word(val)) {
                cfg.colour = std::nullopt;
            } else {
                std::string parsed = parse_colour_input(val);
                if (parsed.empty() || parsed.length() > 256) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Colour must be 256 characters or fewer.");
                    return;
                }
                if (!valid_colour(parsed)) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid colour. Accepted formats: `purple`, `#ff6b6b`, `rgb(255,0,0)`, `hsl(270,60%,50%)`, `linear-gradient(...)`, or `random`.");
                    return;
                }
                cfg.colour = parsed;
            }

            if (g_db->save_masquerade_config(cfg)) {
                bot.send_message(msg.channel_id, cfg.colour ? "" + bronx::emj_yes + " Set server colour override to `" + *cfg.colour + "`."
                                                            : "" + bronx::emj_yes + " Cleared server colour override.");
            } else {
                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save setting to database.");
            }
            return;
        }

        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Unknown subcommand. Use `" + pfx + "masquerade server <disable|enable|name|avatar|colour|clear|status>`.");
    }

public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "masquerade",
            {"masq"},
            "set your personal masquerade (name/avatar/colour) or manage server-wide settings.",
            "",
            {"[server] <subcommand>", "[args...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " This command can only be used in a server.");
                    return;
                }

                std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;

                std::string first_arg = args.empty() ? "" : args[0];
                std::transform(first_arg.begin(), first_arg.end(), first_arg.begin(), ::tolower);

                if (first_arg == "server") {
                    auto server_opt = bot.get_server(msg.server_id);
                    if (!server_opt) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to resolve server details.");
                        return;
                    }
                    bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, server_opt, args, pfx](stoatpp::models::Member member, bool success) {
                        if (!success) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to verify your server member permissions.");
                            return;
                        }
                        handle_server(bot, msg, args, pfx, *server_opt, member);
                    });
                } else if (first_arg == "user" || first_arg == "usr") {
                    std::string second_arg = args.size() >= 2 ? args[1] : "";
                    std::transform(second_arg.begin(), second_arg.end(), second_arg.begin(), ::tolower);

                    if (second_arg == "disable" || second_arg == "enable" || second_arg == "off" || second_arg == "on") {
                        // Admin toggle for server user masquerades: .masquerade user disable [avatar|name|colour]
                        auto server_opt = bot.get_server(msg.server_id);
                        if (!server_opt) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to resolve server details.");
                            return;
                        }
                        std::string target = (args.size() >= 3) ? args[2] : "user";
                        std::vector<std::string> server_args = {"server", (second_arg == "off" ? "disable" : (second_arg == "on" ? "enable" : second_arg)), target};
                        bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, server_opt, server_args, pfx](stoatpp::models::Member member, bool success) {
                            if (!success) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to verify your server member permissions.");
                                return;
                            }
                            handle_server(bot, msg, server_args, pfx, *server_opt, member);
                        });
                    } else {
                        // Strip leading "user" keyword and treat as personal masquerade subcommand (.masquerade user clear, .masquerade user name ...)
                        std::vector<std::string> stripped_args(args.begin() + 1, args.end());
                        handle_personal(bot, msg, stripped_args, pfx);
                    }
                } else {
                    // Personal masquerade — direct subcommands (.masquerade name ..., .masquerade clear, etc.)
                    handle_personal(bot, msg, args, pfx);
                }
            },
            "Configuration",
            0,
            0,
            {
                "name <new name>",
                "avatar <url>",
                "colour <css colour>",
                "clear",
                "status",
                "user disable",
                "user enable",
                "user clear",
                "user status",
                "server disable",
                "server disable user",
                "server enable",
                "server enable user",
                "server name <new name>",
                "server avatar <url>",
                "server colour <css colour>",
                "server clear",
                "server status"
            }
        });
    }
};

} // namespace bronx::cogs
