#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"

namespace bronx::cogs {

inline std::string clean_role_query(const std::string& rq) {
    std::string s = rq;
    s.erase(0, s.find_first_not_of(" \t\r\n,"));
    s.erase(s.find_last_not_of(" \t\r\n,") + 1);
    if (s.rfind("<%", 0) == 0 && s.back() == '>') {
        s = s.substr(2, s.size() - 3);
    } else if (s.rfind("<@&", 0) == 0 && s.back() == '>') {
        s = s.substr(3, s.size() - 4);
    } else if (s.rfind("<@", 0) == 0 && s.back() == '>') {
        s = s.substr(2, s.size() - 3);
    }
    return s;
}

inline std::pair<std::string, std::string> parse_role_spec_item(const std::string& item) {
    std::string trimmed = item;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
    trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
    if (trimmed.empty()) return {"", ""};

    auto to_lower = [](std::string s) {
        for (auto& c : s) c = std::tolower(c);
        return s;
    };
    std::string lower = to_lower(trimmed);

    for (const std::string& grad_func : {"linear-gradient(", "radial-gradient(", "conic-gradient(", "repeating-linear-gradient(", "repeating-radial-gradient(", "repeating-conic-gradient("}) {
        size_t pos = lower.find(grad_func);
        if (pos != std::string::npos && pos > 0) {
            std::string name = trimmed.substr(0, pos);
            name.erase(name.find_last_not_of(" \t\r\n") + 1);
            std::string col = trimmed.substr(pos);
            return {name, col};
        }
    }

    std::vector<std::string> tokens;
    {
        std::stringstream ss(trimmed);
        std::string tok;
        while (ss >> tok) tokens.push_back(tok);
    }
    if (tokens.empty()) return {"", ""};

    std::vector<std::string> color_tokens;
    std::string angle_str = "90deg";
    while (!tokens.empty()) {
        std::string last = tokens.back();
        std::string last_lower = to_lower(last);
        if (last_lower[0] == '#' || last_lower.rfind("rgb(", 0) == 0 || last_lower.rfind("rgba(", 0) == 0 || last_lower.rfind("var(", 0) == 0) {
            color_tokens.push_back(last);
            tokens.pop_back();
        } else if ((last_lower.size() > 3 && last_lower.substr(last_lower.size() - 3) == "deg") ||
                   (last_lower.size() > 3 && last_lower.substr(last_lower.size() - 3) == "rad") ||
                   (last_lower.size() > 4 && last_lower.substr(last_lower.size() - 4) == "turn")) {
            angle_str = last_lower;
            tokens.pop_back();
        } else if (last_lower.rfind("deg:", 0) == 0 || last_lower.rfind("angle:", 0) == 0 || last_lower.rfind("degree:", 0) == 0) {
            size_t p = last.find(':');
            angle_str = last.substr(p + 1);
            if (angle_str.find("deg") == std::string::npos && angle_str.find("rad") == std::string::npos) angle_str += "deg";
            tokens.pop_back();
        } else if (tokens.size() >= 2 && to_lower(tokens[tokens.size() - 2]) == "to" &&
                   (last_lower == "right" || last_lower == "left" || last_lower == "top" || last_lower == "bottom")) {
            angle_str = "to " + last_lower;
            tokens.pop_back();
            tokens.pop_back();
        } else {
            break;
        }
    }

    std::reverse(color_tokens.begin(), color_tokens.end());

    std::string role_name = "";
    for (size_t i = 0; i < tokens.size(); ++i) {
        role_name += tokens[i] + (i + 1 < tokens.size() ? " " : "");
    }

    if (role_name.empty()) return {trimmed, ""};
    if (color_tokens.empty()) return {role_name, ""};
    if (color_tokens.size() == 1) return {role_name, color_tokens[0]};

    size_t n = color_tokens.size();
    std::string grad = "linear-gradient(" + angle_str;
    for (size_t i = 0; i < n; ++i) {
        int p = static_cast<int>(std::round(i * 100.0 / (n - 1)));
        grad += ", " + color_tokens[i] + " " + std::to_string(p) + "%";
    }
    grad += ")";
    return {role_name, grad};
}

class RolesModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "role",
            {},
            "manage roles or batch toggle multiple roles on members.",
            "",
            {"<subcommand/users> [args...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling role...\n";
                if (args.empty()) {
                    stoatpp::models::MessagePayload payload;
                    nlohmann::json embed = nlohmann::json::object();
                    embed["title"] = "🛡️ Role Management & Batch Assignment Guide";
                    embed["colour"] = "#4eb3ec";
                    embed["description"] =
                        "**Subcommands:**\n"
                        "• **Create Roles:** `.role create <role1 #hex, role2 #hex1 #hex2...>`\n"
                        "• **Edit Roles:** `.role edit <role> <options / colors / degree>`\n"
                        "• **Batch Assignment:** `.role <user1,user2...> <role1,role2...>`\n\n"
                        "Type `.role create` or `.role edit` without arguments to see detailed examples!";
                    payload.embeds.push_back(embed);
                    bot.send_message(msg.channel_id, payload);
                    return;
                }

                auto to_lower = [](std::string s) {
                    for (auto& c : s) c = std::tolower(c);
                    return s;
                };

                std::string sub = to_lower(args[0]);
                if (sub == "create" || sub == "add") {
                    if (args.size() < 2) {
                        stoatpp::models::MessagePayload payload;
                        nlohmann::json embed = nlohmann::json::object();
                        embed["title"] = "✨ Role Creation Guide";
                        embed["colour"] = "#4eb3ec";
                        embed["description"] =
                            "Create one or multiple roles at once with custom solid colors or linear gradients!\n\n"
                            "**Usage:**\n"
                            "`.role create <role1 [color/gradient], role2 [color/gradient]...>`\n\n"
                            "**Examples:**\n"
                            "• **Single Role with Solid Color:**\n"
                            "  `.role create VIP #ff0000`\n"
                            "• **Multiple Roles in Batch:**\n"
                            "  `.role create VIP #ff0000, Member #00ff00, Bot #0000ff`\n"
                            "• **Custom Gradient with Degree:**\n"
                            "  `.role create Legend 135deg #ff0000 #ffff00 #00ff00`\n"
                            "• **Batch Creation with Mixed Gradients:**\n"
                            "  `.role create Pro 45deg #ff00ff #00ffff, Rookie #808080`";
                        payload.embeds.push_back(embed);
                        bot.send_message(msg.channel_id, payload);
                        return;
                    }
                    std::string spec_str = "";
                    for (size_t i = 1; i < args.size(); ++i) {
                        spec_str += args[i] + (i + 1 < args.size() ? " " : "");
                    }
                    std::vector<std::string> items;
                    {
                        std::stringstream ss(spec_str);
                        std::string item;
                        while (std::getline(ss, item, ',')) {
                            item.erase(0, item.find_first_not_of(" \t\r\n"));
                            item.erase(item.find_last_not_of(" \t\r\n") + 1);
                            if (!item.empty()) items.push_back(item);
                        }
                    }
                    if (items.empty()) {
                        bot.send_message(msg.channel_id, "no roles specified to create.");
                        return;
                    }
                    bot.send_message(msg.channel_id, "creating **" + std::to_string(items.size()) + "** role(s)... please wait.");
                    std::thread([&bot, msg, items]() {
                        std::string summary = "";
                        for (const auto& item : items) {
                            auto [name, color] = parse_role_spec_item(item);
                            if (name.empty()) continue;
                            auto res = bot.rest().create_role(msg.server_id, name);
                            if (res.success()) {
                                std::string role_id = "";
                                if (res.body.contains("id") && res.body["id"].is_string()) {
                                    role_id = res.body["id"].get<std::string>();
                                } else if (res.body.contains("role") && res.body["role"].is_object() && res.body["role"].contains("id") && res.body["role"]["id"].is_string()) {
                                    role_id = res.body["role"]["id"].get<std::string>();
                                }
                                if (!color.empty() && !role_id.empty()) {
                                    nlohmann::json edit_fields;
                                    edit_fields["colour"] = color;
                                    auto edit_res = bot.rest().edit_role(msg.server_id, role_id, edit_fields);
                                    if (!edit_res.success()) {
                                        summary += "• Created role **" + name + "** (`" + role_id + "`) but failed to set color: " + edit_res.error_message() + "\n";
                                        continue;
                                    }
                                }
                                summary += "• Created role **" + name + "**" + (!role_id.empty() ? " (`" + role_id + "`)" : "") + (!color.empty() ? " with color `" + color + "`" : "") + "\n";
                            } else {
                                summary += "• ❌ Failed to create role **" + name + "**: " + res.error_message() + "\n";
                            }
                        }
                        auto fresh_res = bot.rest().get_server(msg.server_id);
                        if (fresh_res.success()) {
                            bot.cache_server(stoatpp::models::Server::from_json(fresh_res.body));
                        }
                        stoatpp::models::MessagePayload payload;
                        nlohmann::json embed = nlohmann::json::object();
                        embed["title"] = "✨ Role Creation Summary";
                        embed["colour"] = "#4eb3ec";
                        embed["description"] = summary.empty() ? "No roles created." : summary;
                        payload.embeds.push_back(embed);
                        bot.send_message(msg.channel_id, payload);
                    }).detach();
                    return;
                }

                if (sub == "edit") {
                    if (args.size() < 3) {
                        stoatpp::models::MessagePayload payload;
                        nlohmann::json embed = nlohmann::json::object();
                        embed["title"] = "🎨 Role Editing Guide";
                        embed["colour"] = "#4eb3ec";
                        embed["description"] =
                            "Edit role names, solid colors, gradients, or gradient angles on the fly!\n\n"
                            "**Usage:**\n"
                            "`.role edit <role> <options / colors / degree>`\n\n"
                            "**Examples:**\n"
                            "• **Set Solid Color or Linear Gradient:**\n"
                            "  `.role edit VIP #ff0000 #0000ff`\n"
                            "• **Update Only Gradient Angle (Preserves Colors):**\n"
                            "  `.role edit VIP 180deg` *(or `deg:45`, `to right`)*\n"
                            "• **Set Gradient with Custom Angle:**\n"
                            "  `.role edit VIP 135deg #ff0000 #00ff00 #0000ff`\n"
                            "• **Update Multiple Properties via Key-Value Flags:**\n"
                            "  `.role edit VIP name:SuperVIP colour:#ff00ff rank:5`";
                        payload.embeds.push_back(embed);
                        bot.send_message(msg.channel_id, payload);
                        return;
                    }
                    auto server_opt = bot.get_server(msg.server_id);
                    if (!server_opt) {
                        bot.send_message(msg.channel_id, "failed to retrieve server settings.");
                        return;
                    }
                    std::string rem_str = "";
                    for (size_t i = 1; i < args.size(); ++i) {
                        rem_str += args[i] + (i + 1 < args.size() ? " " : "");
                    }
                    auto sorted_roles = server_opt->roles;
                    std::sort(sorted_roles.begin(), sorted_roles.end(), [](const auto& a, const auto& b) {
                        return a.name.size() > b.name.size();
                    });

                    stoatpp::models::Role matched;
                    bool matched_found = false;
                    std::string updates_str = rem_str;

                    for (const auto& r : sorted_roles) {
                        std::string lower_rem = to_lower(rem_str);
                        std::string lower_r = to_lower(r.name);
                        if (lower_rem.rfind(lower_r + " ", 0) == 0) {
                            matched = r;
                            matched_found = true;
                            updates_str = rem_str.substr(r.name.size());
                            break;
                        }
                        if (to_lower(rem_str) == lower_r) {
                            matched = r;
                            matched_found = true;
                            updates_str = "";
                            break;
                        }
                        if (rem_str.rfind(r.id + " ", 0) == 0) {
                            matched = r;
                            matched_found = true;
                            updates_str = rem_str.substr(r.id.size());
                            break;
                        }
                        std::string tag = "<%" + r.id + ">";
                        if (rem_str.rfind(tag + " ", 0) == 0) {
                            matched = r;
                            matched_found = true;
                            updates_str = rem_str.substr(tag.size());
                            break;
                        }
                    }

                    if (!matched_found) {
                        auto srv_res = bot.rest().get_server(msg.server_id);
                        if (srv_res.success()) {
                            auto fresh_srv = stoatpp::models::Server::from_json(srv_res.body);
                            bot.cache_server(fresh_srv);
                            for (const auto& r : fresh_srv.roles) {
                                std::string lower_rem = to_lower(rem_str);
                                std::string lower_r = to_lower(r.name);
                                if (lower_rem.rfind(lower_r + " ", 0) == 0 || to_lower(rem_str) == lower_r || rem_str.rfind(r.id + " ", 0) == 0 || rem_str.rfind("<%" + r.id + "> ", 0) == 0) {
                                    matched = r;
                                    matched_found = true;
                                    if (to_lower(rem_str) == lower_r) updates_str = "";
                                    else if (lower_rem.rfind(lower_r + " ", 0) == 0) updates_str = rem_str.substr(r.name.size());
                                    else if (rem_str.rfind(r.id + " ", 0) == 0) updates_str = rem_str.substr(r.id.size());
                                    else updates_str = rem_str.substr(std::string("<%" + r.id + ">").size());
                                    break;
                                }
                            }
                        }
                    }

                    if (!matched_found) {
                        bot.send_message(msg.channel_id, "could not find the specified role to edit.");
                        return;
                    }

                    updates_str.erase(0, updates_str.find_first_not_of(" \t\r\n"));
                    updates_str.erase(updates_str.find_last_not_of(" \t\r\n") + 1);

                    if (updates_str.empty()) {
                        bot.send_message(msg.channel_id, "no updates provided for role **" + matched.name + "**.");
                        return;
                    }

                    nlohmann::json edit_fields;
                    std::string angle_update = "";

                    if (updates_str.find("name:") != std::string::npos || updates_str.find("colour:") != std::string::npos || updates_str.find("color:") != std::string::npos || updates_str.find("rank:") != std::string::npos || updates_str.find("deg:") != std::string::npos || updates_str.find("degree:") != std::string::npos || updates_str.find("angle:") != std::string::npos) {
                        std::stringstream ss(updates_str);
                        std::string token;
                        while (ss >> token) {
                            if (token.rfind("name:", 0) == 0) edit_fields["name"] = token.substr(5);
                            else if (token.rfind("colour:", 0) == 0) edit_fields["colour"] = token.substr(7);
                            else if (token.rfind("color:", 0) == 0) edit_fields["colour"] = token.substr(6);
                            else if (token.rfind("deg:", 0) == 0) angle_update = token.substr(4);
                            else if (token.rfind("degree:", 0) == 0) angle_update = token.substr(7);
                            else if (token.rfind("angle:", 0) == 0) angle_update = token.substr(6);
                            else if (token.rfind("rank:", 0) == 0) {
                                try { edit_fields["rank"] = std::stoi(token.substr(5)); } catch (...) {}
                            }
                        }
                    } else {
                        auto [parsed_name, parsed_color] = parse_role_spec_item("dummy_role " + updates_str);
                        if (!parsed_color.empty() && parsed_name == "dummy_role") {
                            edit_fields["colour"] = parsed_color;
                        } else {
                            std::string lower_up = to_lower(updates_str);
                            if ((lower_up.size() > 3 && lower_up.substr(lower_up.size() - 3) == "deg") ||
                                (lower_up.size() > 3 && lower_up.substr(lower_up.size() - 3) == "rad") ||
                                (lower_up.size() > 4 && lower_up.substr(lower_up.size() - 4) == "turn") ||
                                lower_up.rfind("to ", 0) == 0) {
                                angle_update = lower_up;
                            } else {
                                edit_fields["name"] = updates_str;
                            }
                        }
                    }

                    if (!angle_update.empty()) {
                        if (angle_update.find("deg") == std::string::npos && angle_update.find("rad") == std::string::npos && angle_update.find("turn") == std::string::npos && angle_update.rfind("to ", 0) != 0) {
                            angle_update += "deg";
                        }
                        if (edit_fields.contains("colour")) {
                            std::string col_val = edit_fields["colour"].get<std::string>();
                            if (col_val.rfind("linear-gradient(", 0) != 0 && col_val.find(' ') != std::string::npos) {
                                auto [pname, pcol] = parse_role_spec_item("dummy_role " + angle_update + " " + col_val);
                                if (!pcol.empty() && pname == "dummy_role") edit_fields["colour"] = pcol;
                            }
                        } else if (matched.colour.has_value() && !matched.colour->empty()) {
                            std::string old_col = *matched.colour;
                            if (old_col.rfind("linear-gradient(", 0) == 0) {
                                size_t p1 = old_col.find('(');
                                size_t p2 = old_col.find(',', p1);
                                if (p1 != std::string::npos && p2 != std::string::npos) {
                                    std::string first_arg = old_col.substr(p1 + 1, p2 - p1 - 1);
                                    std::string lower_first = to_lower(first_arg);
                                    if (lower_first.find("deg") != std::string::npos || lower_first.find("rad") != std::string::npos || lower_first.find("turn") != std::string::npos || lower_first.find("to ") != std::string::npos) {
                                        old_col.replace(p1 + 1, p2 - p1 - 1, angle_update);
                                    } else {
                                        old_col.insert(p1 + 1, angle_update + ", ");
                                    }
                                    edit_fields["colour"] = old_col;
                                }
                            } else {
                                bot.send_message(msg.channel_id, "⚠️ Role **" + matched.name + "** currently has a solid color (`" + old_col + "`), not a gradient. Please specify multiple hex colors along with the angle, e.g., `!role edit " + matched.name + " " + angle_update + " #ff0000 #0000ff`.");
                                return;
                            }
                        } else {
                            bot.send_message(msg.channel_id, "⚠️ Role **" + matched.name + "** does not currently have a color. Please specify colors along with the angle, e.g., `!role edit " + matched.name + " " + angle_update + " #ff0000 #0000ff`.");
                            return;
                        }
                    }

                    if (edit_fields.empty()) {
                        bot.send_message(msg.channel_id, "no valid fields parsed to update.");
                        return;
                    }

                    std::thread([&bot, msg, matched, edit_fields]() {
                        auto res = bot.rest().edit_role(msg.server_id, matched.id, edit_fields);
                        if (res.success()) {
                            bot.send_message(msg.channel_id, "✅ Successfully updated role **" + matched.name + "**.");
                        } else {
                            bot.send_message(msg.channel_id, "❌ Failed to update role: " + res.error_message());
                        }
                    }).detach();
                    return;
                }

                if (args.size() < 2) {
                    bot.send_message(msg.channel_id, "usage: `!role <user1,user2,...> <role1,role2,...>`");
                    return;
                }

                size_t user_args_end = 0;
                while (user_args_end < args.size()) {
                    if (user_args_end == 0 || args[user_args_end - 1].back() == ',') {
                        user_args_end++;
                    } else {
                        break;
                    }
                }
                if (user_args_end >= args.size()) user_args_end = 1;

                std::string user_str = "";
                for (size_t i = 0; i < user_args_end; ++i) {
                    user_str += args[i] + (i + 1 < user_args_end ? " " : "");
                }

                std::string role_str = "";
                for (size_t i = user_args_end; i < args.size(); ++i) {
                    role_str += args[i] + (i + 1 < args.size() ? " " : "");
                }

                std::vector<std::string> user_queries;
                {
                    std::stringstream ss(user_str);
                    std::string item;
                    while (std::getline(ss, item, ',')) {
                        item.erase(0, item.find_first_not_of(" \t\r\n"));
                        item.erase(item.find_last_not_of(" \t\r\n") + 1);
                        if (!item.empty()) user_queries.push_back(item);
                    }
                }

                std::vector<std::string> role_queries;
                {
                    std::stringstream ss(role_str);
                    std::string item;
                    while (std::getline(ss, item, ',')) {
                        std::string cleaned = clean_role_query(item);
                        if (!cleaned.empty()) role_queries.push_back(cleaned);
                    }
                }

                auto server_opt = bot.get_server(msg.server_id);
                if (!server_opt) {
                    bot.send_message(msg.channel_id, "failed to retrieve server settings.");
                    return;
                }

                std::vector<stoatpp::models::Role> resolved_roles;
                std::vector<std::string> missing_roles;
                for (const auto& rq : role_queries) {
                    bool found = false;
                    for (const auto& r : server_opt->roles) {
                        if (r.id == rq || to_lower(r.name) == to_lower(rq)) {
                            resolved_roles.push_back(r);
                            found = true;
                            break;
                        }
                    }
                    if (!found) missing_roles.push_back(rq);
                }

                if (!missing_roles.empty()) {
                    auto srv_res = bot.rest().get_server(msg.server_id);
                    if (srv_res.success()) {
                        auto fresh_srv = stoatpp::models::Server::from_json(srv_res.body);
                        bot.cache_server(fresh_srv);
                        server_opt = fresh_srv;
                        std::vector<std::string> still_missing;
                        for (const auto& rq : missing_roles) {
                            bool found = false;
                            for (const auto& r : server_opt->roles) {
                                if (r.id == rq || to_lower(r.name) == to_lower(rq)) {
                                    resolved_roles.push_back(r);
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) still_missing.push_back(rq);
                        }
                        missing_roles = still_missing;
                    }
                }

                if (resolved_roles.empty()) {
                    bot.send_message(msg.channel_id, "could not find any of the specified roles.");
                    return;
                }

                if (args[0] == "*") {
                    bool force_remove = false;
                    if (args.size() >= 3) {
                        std::string action = args[2];
                        std::transform(action.begin(), action.end(), action.begin(), ::tolower);
                        if (action == "remove" || action == "rem" || action == "del") {
                            force_remove = true;
                        }
                    }

                    std::thread([&bot, msg, resolved_roles, force_remove, missing_roles]() {
                        auto res = bot.rest().get_server_members(msg.server_id);
                        if (!res.success()) {
                            bot.send_message(msg.channel_id, "failed to fetch server members.");
                            return;
                        }

                        std::vector<stoatpp::models::Member> members;
                        if (res.body.is_object() && res.body.contains("members") && res.body["members"].is_array()) {
                            for (const auto& item : res.body["members"]) {
                                members.push_back(stoatpp::models::Member::from_json(item));
                            }
                        }

                        stoatpp::models::Member exec_member;
                        bool exec_found = false;
                        for (const auto& m : members) {
                            if (m.id == msg.author.id) {
                                exec_member = m;
                                exec_found = true;
                                break;
                            }
                        }

                        if (!exec_found) {
                            bot.send_message(msg.channel_id, "failed to verify executor member settings.");
                            return;
                        }

                        stoatpp::models::Member bot_member;
                        for (const auto& m : members) {
                            if (m.id == bot.current_user().id) {
                                bot_member = m;
                                break;
                            }
                        }

                        struct AllAction {
                            std::string user_id;
                            std::string role_id;
                            bool add;
                        };
                        std::vector<AllAction> actions;

                        int exec_highest = helpers::get_highest_rank(bot, msg.server_id, exec_member);
                        int bot_highest = helpers::get_highest_rank(bot, msg.server_id, bot_member);

                        for (const auto& m : members) {
                            int target_highest = helpers::get_highest_rank(bot, msg.server_id, m);
                            if (exec_highest >= target_highest && exec_highest != -1) {
                                continue;
                            }

                            for (const auto& role : resolved_roles) {
                                int role_rank = helpers::get_role_rank(bot, msg.server_id, role.id);
                                if (exec_highest >= role_rank && exec_highest != -1) {
                                    continue;
                                }
                                if (bot_highest >= role_rank && bot_highest != -1) {
                                    continue;
                                }

                                bool has_role = false;
                                for (const auto& r_id : m.roles) {
                                    if (r_id == role.id) {
                                        has_role = true;
                                        break;
                                    }
                                }

                                if (force_remove) {
                                    if (has_role) {
                                        actions.push_back({m.id, role.id, false});
                                    }
                                } else {
                                    if (!has_role) {
                                        actions.push_back({m.id, role.id, true});
                                    }
                                }
                            }
                        }

                        if (actions.empty()) {
                            bot.send_message(msg.channel_id, "no changes needed for any members.");
                            return;
                        }

                        bot.send_message(msg.channel_id, "applying changes to **" + std::to_string(actions.size()) + "** roles across members... please wait.");

                        auto completed = std::make_shared<std::atomic<size_t>>(0);
                        size_t total = actions.size();

                        for (const auto& act : actions) {
                            auto cb = [&bot, msg, completed, total](bool ok) {
                                size_t comp = ++(*completed);
                                if (comp == total) {
                                    auto srv_res = bot.rest().get_server(msg.server_id);
                                    if (srv_res.success()) bot.cache_server(stoatpp::models::Server::from_json(srv_res.body));
                                    bot.send_message(msg.channel_id, "successfully finished updating roles for all members.");
                                }
                            };
                            if (act.add) {
                                bot.add_role_to_member(msg.server_id, act.user_id, act.role_id, cb);
                            } else {
                                bot.remove_role_from_member(msg.server_id, act.user_id, act.role_id, cb);
                            }
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        }
                    }).detach();
                    return;
                }

                bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, user_queries, resolved_roles, missing_roles, args](stoatpp::models::Member exec_member, bool exec_ok) {
                    if (!exec_ok) {
                        bot.send_message(msg.channel_id, "failed to verify executor member settings.");
                        return;
                    }

                    struct ResolvedTarget {
                        stoatpp::models::User user;
                        stoatpp::models::Member member;
                        bool success = false;
                    };
                    
                    auto resolved_ptr = std::make_shared<std::vector<ResolvedTarget>>();
                    
                    struct UserResolver {
                        static void resolve(stoatpp::cluster& bot, const std::string& server_id,
                                            const std::vector<std::string>& queries, size_t index,
                                            std::shared_ptr<std::vector<ResolvedTarget>> resolved,
                                            std::function<void()> callback) {
                            if (index >= queries.size()) {
                                callback();
                                return;
                            }
                            helpers::resolve_user(bot, server_id, queries[index], [&bot, server_id, queries, index, resolved, callback](stoatpp::models::User u, stoatpp::models::Member m, bool ok) {
                                ResolvedTarget target;
                                target.user = u;
                                target.member = m;
                                target.success = ok;
                                resolved->push_back(target);
                                resolve(bot, server_id, queries, index + 1, resolved, callback);
                            });
                        }
                    };

                    UserResolver::resolve(bot, msg.server_id, user_queries, 0, resolved_ptr, [&bot, msg, resolved_ptr, resolved_roles, exec_member, missing_roles, user_queries, args]() {
                        struct TargetResult {
                            std::string id;
                            std::string username;
                            std::string user_mention;
                            std::string query;
                            std::string status_err = "";
                            std::vector<std::string> added_roles;
                            std::vector<std::string> removed_roles;
                        };

                        auto results = std::make_shared<std::vector<TargetResult>>();
                        results->resize(resolved_ptr->size());

                        struct ActionItem {
                            size_t target_index;
                            std::string role_id;
                            std::string role_name;
                            bool add;
                        };

                        auto actions = std::make_shared<std::vector<ActionItem>>();

                        for (size_t i = 0; i < resolved_ptr->size(); ++i) {
                            const auto& target = (*resolved_ptr)[i];
                            (*results)[i].id = target.success ? target.user.id : "";
                            (*results)[i].username = target.success ? target.user.username : "";
                            (*results)[i].user_mention = target.success ? "<@" + target.user.id + ">" : "";
                            (*results)[i].query = user_queries[i];

                            if (!target.success) {
                                (*results)[i].status_err = "failed to resolve user";
                                continue;
                            }

                            int exec_highest = helpers::get_highest_rank(bot, msg.server_id, exec_member);
                            int target_highest = helpers::get_highest_rank(bot, msg.server_id, target.member);
                            if (exec_highest >= target_highest && exec_highest != -1) {
                                (*results)[i].status_err = "hierarchy error: you cannot modify this user";
                                continue;
                            }

                            for (const auto& role : resolved_roles) {
                                int role_rank = helpers::get_role_rank(bot, msg.server_id, role.id);
                                if (exec_highest >= role_rank && exec_highest != -1) {
                                    continue;
                                }

                                bool has_role = false;
                                for (const auto& r_id : target.member.roles) {
                                    if (r_id == role.id) {
                                        has_role = true;
                                        break;
                                    }
                                }

                                bool force_remove = false;
                                if (args.size() >= 3) {
                                    std::string action = args[2];
                                    std::transform(action.begin(), action.end(), action.begin(), ::tolower);
                                    if (action == "remove" || action == "rem" || action == "del") {
                                        force_remove = true;
                                    }
                                }

                                ActionItem act;
                                act.target_index = i;
                                act.role_id = role.id;
                                act.role_name = role.name;
                                if (force_remove) {
                                    if (has_role) {
                                        act.add = false;
                                        actions->push_back(act);
                                    }
                                } else {
                                    act.add = !has_role;
                                    actions->push_back(act);
                                }
                            }
                        }

                        if (actions->empty()) {
                            stoatpp::models::MessagePayload payload;
                            nlohmann::json embed = nlohmann::json::object();
                            embed["colour"] = "#4eb3ec";
                            
                            std::string desc = "";
                            for (const auto& res : *results) {
                                std::string target_label = res.user_mention.empty() ? "`" + res.query + "`" : res.user_mention;
                                desc += target_label + " ,";
                                if (!res.status_err.empty()) {
                                    desc += " ⚠️ " + res.status_err + "\n";
                                } else {
                                    desc += " no changes applied.\n";
                                }
                            }
                            if (!missing_roles.empty()) {
                                desc += "missing/invalid roles ignored: ";
                                for (size_t i = 0; i < missing_roles.size(); ++i) {
                                    desc += "`" + missing_roles[i] + "`" + (i + 1 < missing_roles.size() ? ", " : "");
                                }
                                desc += "\n";
                            }
                            embed["description"] = desc;
                            payload.embeds.push_back(embed);
                            bot.send_message(msg.channel_id, payload);
                            return;
                        }

                        auto actions_completed = std::make_shared<std::atomic<size_t>>(0);
                        size_t total_actions = actions->size();

                        for (const auto& act : *actions) {
                            auto target_id = (*results)[act.target_index].id;
                            auto cb = [&bot, act, actions_completed, total_actions, results, missing_roles, msg](bool action_ok) {
                                if (action_ok) {
                                    if (act.add) {
                                        (*results)[act.target_index].added_roles.push_back(act.role_name);
                                    } else {
                                        (*results)[act.target_index].removed_roles.push_back(act.role_name);
                                    }
                                }

                                size_t completed = ++(*actions_completed);
                                if (completed == total_actions) {
                                    stoatpp::models::MessagePayload payload;
                                    nlohmann::json embed = nlohmann::json::object();
                                    embed["colour"] = "#4eb3ec";

                                    std::string desc = "";
                                    for (const auto& res : *results) {
                                        std::string target_label = res.user_mention.empty() ? "`" + res.query + "`" : res.user_mention;
                                        desc += target_label + " ,";
                                        if (!res.status_err.empty()) {
                                            desc += " ⚠️ " + res.status_err + "\n";
                                        } else {
                                            bool changed = false;
                                            for (const auto& r_name : res.removed_roles) {
                                                desc += " -" + r_name;
                                                changed = true;
                                            }
                                            for (const auto& r_name : res.added_roles) {
                                                desc += " +" + r_name;
                                                changed = true;
                                            }
                                            if (!changed) {
                                                desc += " no changes applied.";
                                            }
                                            desc += "\n";
                                        }
                                    }
                                    if (!missing_roles.empty()) {
                                        desc += "missing/invalid roles ignored: ";
                                        for (size_t i = 0; i < missing_roles.size(); ++i) {
                                            desc += "`" + missing_roles[i] + "`" + (i + 1 < missing_roles.size() ? ", " : "");
                                        }
                                        desc += "\n";
                                    }
                                    embed["description"] = desc;
                                    payload.embeds.push_back(embed);
                                    bot.send_message(msg.channel_id, payload);
                                }
                            };

                            if (act.add) {
                                bot.add_role_to_member(msg.server_id, target_id, act.role_id, cb);
                            } else {
                                bot.remove_role_from_member(msg.server_id, target_id, act.role_id, cb);
                            }
                        }
                    });
                });
            },
            "Roles",
            stoatpp::permissions::ManageRole
        });

        bot.register_command({
            "roleinfo",
            {},
            "display role metadata and role hierarchy details.",
            "",
            {},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling roleinfo...\n";
                auto server_opt = bot.get_server(msg.server_id);
                if (!server_opt) {
                    bot.send_message(msg.channel_id, "failed to get server info.");
                    return;
                }

                auto sorted_roles = server_opt->roles;
                std::sort(sorted_roles.begin(), sorted_roles.end(), [](const auto& a, const auto& b) {
                    return a.rank < b.rank;
                });

                std::string out = "roles for **" + server_opt->name + "** (sorted by hierarchy, highest first):\n";
                for (const auto& r : sorted_roles) {
                    out += "• **" + r.name + "** (rank: " + std::to_string(r.rank) + ", id: `" + r.id + "`";
                    if (r.colour) {
                        out += ", color: " + *r.colour;
                    }
                    out += ")\n";
                }

                bot.send_message(msg.channel_id, out);
            },
            "Roles",
            stoatpp::permissions::ManageRole
        });
    }
};

} // namespace bronx::cogs
