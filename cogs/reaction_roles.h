#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "roles.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace bronx::cogs {

struct EmojiRolePair {
    std::string emoji;
    std::string role_id;
    std::string role_name;
};

struct PendingScan {
    std::string target_message_id;
    std::string target_channel_id;
    std::string target_server_id;
    std::vector<EmojiRolePair> rules;
    std::string author_id;
};

// Global pending scans state
inline std::unordered_map<std::string, PendingScan> g_pending_scans;
inline std::mutex g_pending_scans_mutex;

inline std::string clean_emoji(const std::string& emoji_arg) {
    if (emoji_arg.size() >= 28 && emoji_arg.front() == ':' && emoji_arg.back() == ':') {
        std::string inner = emoji_arg.substr(1, emoji_arg.size() - 2);
        if (inner.size() == 26 && inner.find_first_not_of("0123456789ABCDEFGHJKMNPQRSTVWXYZabcdefghjkmnpqrstvwxyz") == std::string::npos) {
            return inner;
        }
    }
    return emoji_arg;
}

inline std::string display_emoji(const std::string& raw_emoji) {
    if (raw_emoji.size() == 26 && raw_emoji.find_first_not_of("0123456789ABCDEFGHJKMNPQRSTVWXYZabcdefghjkmnpqrstvwxyz") == std::string::npos) {
        return ":" + raw_emoji + ":";
    }
    return raw_emoji;
}

inline std::string format_rr_description(const std::vector<bronx::Database::RRRule>& rules) {
    if (rules.empty()) return "";
    
    // Group rules by emoji while preserving the order of first appearance
    std::vector<std::string> ordered_emojis;
    struct RuleDetail {
        std::string role_id;
        std::string rule_type;
    };
    std::unordered_map<std::string, std::vector<RuleDetail>> emoji_roles;
    
    for (const auto& rule : rules) {
        if (emoji_roles.find(rule.emoji_id) == emoji_roles.end()) {
            ordered_emojis.push_back(rule.emoji_id);
        }
        emoji_roles[rule.emoji_id].push_back({rule.role_id, rule.rule_type});
    }

    std::string desc;
    for (const auto& emoji : ordered_emojis) {
        desc += "• " + display_emoji(emoji) + " ➔ ";
        const auto& details = emoji_roles[emoji];
        for (size_t i = 0; i < details.size(); ++i) {
            if (i > 0) desc += " ";
            if (details[i].rule_type == "remove") {
                desc += "[Remove] <%" + details[i].role_id + ">";
            } else {
                desc += "<%" + details[i].role_id + ">";
            }
        }
        desc += "\n";
    }
    return desc;
}

inline std::vector<EmojiRolePair> parse_emoji_role_pairs(const std::string& text, stoatpp::cluster& bot, const std::string& server_id) {
    std::vector<EmojiRolePair> pairs;
    size_t i = 0;
    while (i < text.size()) {
        // Skip whitespace/commas
        while (i < text.size() && (std::isspace(text[i]) || text[i] == ',')) {
            i++;
        }
        if (i >= text.size()) break;
        
        std::string emoji = "";
        // Check custom emoji starting with ':'
        if (text[i] == ':') {
            size_t close = text.find(':', i + 1);
            if (close != std::string::npos && close - i >= 2) {
                std::string inner = text.substr(i + 1, close - i - 1);
                // If it's a 26-character ULID, strip the colons
                if (inner.size() == 26 && inner.find_first_not_of("0123456789ABCDEFGHJKMNPQRSTVWXYZabcdefghjkmnpqrstvwxyz") == std::string::npos) {
                    emoji = inner;
                } else {
                    emoji = text.substr(i, close - i + 1);
                }
                i = close + 1;
            }
        }
        
        // If not custom emoji, check if next sequence represents a unicode emoji / symbol
        if (emoji.empty()) {
            unsigned char c = text[i];
            
            // Skip PUA characters (U+E000 to U+F8FF)
            if (c == 0xEE || (c == 0xEF && i + 1 < text.size() && static_cast<unsigned char>(text[i+1]) < 0xB8)) {
                size_t len = 1;
                if ((c & 0xE0) == 0xC0) len = 2;
                else if ((c & 0xF0) == 0xE0) len = 3;
                else if ((c & 0xF8) == 0xF0) len = 4;
                i += len;
                continue;
            }

            // Match UTF-8 lead bytes and common symbol characters
            if (c >= 0x80 || (c < 128 && !std::isalnum(c) && c != '<' && c != '@' && c != '&' && !std::isspace(c) && c != ',')) {
                size_t len = 1;
                if ((c & 0xE0) == 0xC0) len = 2;
                else if ((c & 0xF0) == 0xE0) len = 3;
                else if ((c & 0xF8) == 0xF0) len = 4;
                
                if (i + len <= text.size()) {
                    emoji = text.substr(i, len);
                    i += len;
                }
            }
        }
        
        // Consume variation selector (U+FE0E or U+FE0F) if present immediately after the unicode emoji
        if (!emoji.empty() && i + 3 <= text.size()) {
            if (static_cast<unsigned char>(text[i]) == 0xEF && 
                static_cast<unsigned char>(text[i+1]) == 0xB8 && 
                (static_cast<unsigned char>(text[i+2]) == 0x8F || static_cast<unsigned char>(text[i+2]) == 0x8E)) {
                emoji += text.substr(i, 3);
                i += 3;
            }
        }
        
        if (emoji.empty()) {
            // Skip to next word
            while (i < text.size() && !std::isspace(text[i])) {
                i++;
            }
            continue;
        }
        
        // Read role query following the emoji
        size_t role_start = i;
        while (i < text.size()) {
            if (text[i] == ',') {
                break;
            }
            if (text[i] == ':') {
                size_t next_close = text.find(':', i + 1);
                if (next_close != std::string::npos && next_close - i >= 2) {
                    break;
                }
            }
            unsigned char next_c = text[i];
            if (next_c >= 0x80) {
                break;
            }
            i++;
        }
        
        std::string role_query = text.substr(role_start, i - role_start);
        role_query.erase(0, role_query.find_first_not_of(" \t\r\n"));
        role_query.erase(role_query.find_last_not_of(" \t\r\n") + 1);
        
        // Strip optional prefixes like '-', '>', '@' or spaces
        while (!role_query.empty() && (role_query[0] == '-' || role_query[0] == '>' || role_query[0] == '@' || std::isspace(role_query[0]))) {
            role_query = role_query.substr(1);
            role_query.erase(0, role_query.find_first_not_of(" \t\r\n"));
        }

        if (role_query.empty()) continue;
        
        auto server_opt = bot.get_server(server_id);
        if (server_opt) {
            std::string resolved_role_id = "";
            std::string resolved_role_name = "";
            
            std::string cleaned_query = clean_role_query(role_query);
            for (const auto& r : server_opt->roles) {
                if (r.id == cleaned_query || r.name == cleaned_query) {
                    resolved_role_id = r.id;
                    resolved_role_name = r.name;
                    break;
                }
            }
            
            // Fallback: Case-insensitive, space-stripped comparison
            if (resolved_role_id.empty()) {
                std::string clean_query = cleaned_query;
                clean_query.erase(std::remove_if(clean_query.begin(), clean_query.end(), [](unsigned char c) { return std::isspace(c); }), clean_query.end());
                std::transform(clean_query.begin(), clean_query.end(), clean_query.begin(), [](unsigned char c) { return std::tolower(c); });

                for (const auto& r : server_opt->roles) {
                    std::string clean_name = r.name;
                    clean_name.erase(std::remove_if(clean_name.begin(), clean_name.end(), [](unsigned char c) { return std::isspace(c); }), clean_name.end());
                    std::transform(clean_name.begin(), clean_name.end(), clean_name.begin(), [](unsigned char c) { return std::tolower(c); });

                    if (clean_name == clean_query) {
                        resolved_role_id = r.id;
                        resolved_role_name = r.name;
                        break;
                    }
                }
            }

            if (!resolved_role_id.empty()) {
                pairs.push_back({emoji, resolved_role_id, resolved_role_name});
            }
        }
    }
    return pairs;
}


inline stoatpp::models::MessagePayload build_rr_payload(const bronx::Database::RRMessage& rr_msg) {
    stoatpp::models::MessagePayload payload;
    nlohmann::json embed = nlohmann::json::object();
    
    if (!rr_msg.embed_json.empty()) {
        try {
            embed = nlohmann::json::parse(rr_msg.embed_json);
        } catch(...) {}
    }

    if (!embed.contains("title")) {
        embed["title"] = "reaction roles: " + (rr_msg.modal_name.empty() ? rr_msg.message_id : rr_msg.modal_name);
    }
    if (!embed.contains("colour") && !embed.contains("color")) {
        embed["colour"] = "#6C5CE7";
    } else if (embed.contains("color")) {
        embed["colour"] = embed["color"];
        embed.erase("color");
    }

    std::string desc = "";
    if (embed.contains("description")) {
        desc = embed["description"].get<std::string>() + "\n\n";
    } else {
        desc = "react below to obtain roles:\n\n";
    }
    
    if (!rr_msg.rules.empty()) {
        desc += format_rr_description(rr_msg.rules);
        std::vector<std::string> rx;
        for (const auto& rule : rr_msg.rules) {
            rx.push_back(rule.emoji_id);
        }
        payload.set_interactions(rx);
    } else {
        desc += "*(no roles configured yet)*";
    }
    embed["description"] = desc;
    payload.embeds.push_back(embed);
    payload.content = " ";
    return payload;
}

inline bool is_role_dangerous(const stoatpp::models::Role& r) {
    constexpr int64_t DANGEROUS_PERMISSIONS = 
        stoatpp::permissions::ManageServer |
        stoatpp::permissions::ManagePermissions |
        stoatpp::permissions::ManageRole |
        stoatpp::permissions::KickMembers |
        stoatpp::permissions::BanMembers |
        stoatpp::permissions::TimeoutMembers;

    int64_t allowed = r.allowed_permissions();
    return (allowed & DANGEROUS_PERMISSIONS) != 0;
}

class ReactionRolesModule : public stoatpp::bot_module {

public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "rr",
            {"reactionrole", "reactionroles"},
            "manage reaction roles configs (subcommands: create, add, edit, parse, list, delete).",
            "",
            {"<subcommand>", "[args...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling rr...\n";
                if (args.empty()) {
                    std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                    bot.send_message(msg.channel_id, "usage: `" + pfx + "rr <create|send|add|addremove|type|edit|parse|list|delete> [args...]`\n"
                                                     "examples:\n"
                                                     "• `" + pfx + "rr create verified`\n"
                                                     "• `" + pfx + "rr send verified #general`\n"
                                                     "• `" + pfx + "rr add verified :thumbsup: Member`\n"
                                                     "• `" + pfx + "rr addremove verified :check: Unverified` (or `" + pfx + "rr add verified :check: Unverified remove`)\n"
                                                     "• `" + pfx + "rr type verified verify` (types: standard, panel, verify)\n"
                                                     "• `" + pfx + "rr edit verified :thumbsup: emoji :heart:`\n"
                                                     "• `" + pfx + "rr parse <modal> <message_id>` — import rules into a new modal\n"
                                                     "• `" + pfx + "rr parse <message_id>` — activate rules on the scanned message\n"
                                                     "• `" + pfx + "rr list`\n"
                                                     "• `" + pfx + "rr delete verified`");
                    return;
                }

                if (!g_db) {
                    bot.send_message(msg.channel_id, "database connection not initialized.");
                    return;
                }

                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id, "reaction roles can only be set up within server channels.");
                    return;
                }

                // Verify ManageRole permission
                auto server_opt = bot.get_server(msg.server_id);
                if (!server_opt) {
                    bot.send_message(msg.channel_id, "failed to get server info.");
                    return;
                }

                auto member_res = bot.rest().get_server_member(msg.server_id, msg.author.id);
                if (!member_res.success()) {
                    bot.send_message(msg.channel_id, "failed to verify your server member details.");
                    return;
                }
                auto member = stoatpp::models::Member::from_json(member_res.body);

                bool allowed = (server_opt->owner == msg.author.id) ||
                               bot.is_owner(msg.author.id) ||
                               bot.has_permission(*server_opt, member, stoatpp::permissions::ManageRole);
                if (!allowed) {
                    bot.send_message(msg.channel_id, "permission error: you need the Manage Roles permission to manage reaction roles.");
                    return;
                }

                std::string subcommand = args[0];

                if (subcommand == "create") {
                    if (args.size() < 2) {
                        std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "rr create <modalname>`");
                        return;
                    }
                    std::string modalname = args[1];

                    // Check if modal name already exists in this server
                    std::string existing_msg_id = g_db->get_message_id_by_modal_name(msg.server_id, modalname);
                    if (!existing_msg_id.empty()) {
                        bot.send_message(msg.channel_id, "a modal named `" + modalname + "` already exists in this server.");
                        return;
                    }

                    bronx::Database::RRMessage tmp_msg;
                    tmp_msg.modal_name = modalname;
                    stoatpp::models::MessagePayload payload = build_rr_payload(tmp_msg);

                    bot.send_message(msg.channel_id, payload, [modalname, msg](stoatpp::models::Message res, bool ok) {
                        if (ok) {
                            g_db->add_rr_message(res.id, res.channel, msg.server_id, modalname);
                            std::cout << "[bronx rr] registered reaction role modal: " << modalname << " (message: " << res.id << ")\n";
                        }
                    });
                    return;
                }

                if (subcommand == "send") {
                    if (args.size() < 2) {
                        std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "rr send <modalname> [channel]`");
                        return;
                    }
                    std::string modalname = args[1];

                    std::string template_msg_id = g_db->get_message_id_by_modal_name(msg.server_id, modalname);
                    if (template_msg_id.empty()) {
                        bot.send_message(msg.channel_id, "could not find modal named `" + modalname + "` in this server. please run create first.");
                        return;
                    }

                    auto rr_msg_opt = g_db->get_rr_message(template_msg_id);
                    if (!rr_msg_opt) {
                        bot.send_message(msg.channel_id, "failed to load reaction roles details.");
                        return;
                    }

                    std::string target_channel = msg.channel_id;
                    if (args.size() >= 3) {
                        std::string chan_arg = args[2];
                        if (chan_arg.rfind("<#", 0) == 0 && chan_arg.back() == '>') {
                            target_channel = chan_arg.substr(2, chan_arg.size() - 3);
                        } else {
                            target_channel = chan_arg;
                        }
                    }

                    stoatpp::models::MessagePayload payload = build_rr_payload(*rr_msg_opt);

                    bot.send_message(target_channel, payload, [modalname, msg, target_channel, rr_msg_opt, &bot](stoatpp::models::Message res, bool ok) {
                        if (ok) {
                            g_db->add_rr_message(res.id, target_channel, msg.server_id, modalname);
                            std::cout << "[bronx rr] sent additional reaction role modal: " << modalname << " (message: " << res.id << ")\n";
                            
                            // Copy rules to database and cache
                            for (const auto& rule : rr_msg_opt->rules) {
                                g_db->add_rr_rule(res.id, rule.emoji_id, rule.role_id);
                            }
                        }
                    });
                    return;
                }

                if (subcommand == "add" || subcommand == "addremove") {
                    std::string target = "";
                    std::string raw_emoji = "";
                    std::string role_query = "";
                    bool resolved_target = false;

                    // Case A: User is replying to a message
                    if (!msg.replies.empty() && args.size() >= 3) {
                        if (args[1] == "^") {
                            target = msg.replies[0];
                            size_t arg_idx = 2;
                            if (arg_idx < args.size() && args[arg_idx].rfind("<#", 0) == 0 && args[arg_idx].back() == '>') arg_idx++;
                            if (arg_idx < args.size()) raw_emoji = args[arg_idx++];
                            for (size_t i = arg_idx; i < args.size(); ++i) {
                                if (i > arg_idx) role_query += " ";
                                role_query += args[i];
                            }
                            resolved_target = true;
                        } else {
                            bool arg1_is_target = false;
                            if (args[1].size() == 26 && args[1].find_first_not_of("0123456789ABCDEFGHJKMNPQRSTVWXYZabcdefghjkmnpqrstvwxyz") == std::string::npos) {
                                arg1_is_target = true;
                            } else {
                                std::vector<std::string> msg_ids = g_db->get_message_ids_by_modal(msg.server_id, args[1]);
                                if (!msg_ids.empty()) {
                                    arg1_is_target = true;
                                }
                            }

                            if (!arg1_is_target) {
                                target = msg.replies[0];
                                size_t arg_idx = 1;
                                if (arg_idx < args.size() && args[arg_idx].rfind("<#", 0) == 0 && args[arg_idx].back() == '>') arg_idx++;
                                if (arg_idx < args.size()) raw_emoji = args[arg_idx++];
                                for (size_t i = arg_idx; i < args.size(); ++i) {
                                    if (i > arg_idx) role_query += " ";
                                    role_query += args[i];
                                }
                                resolved_target = true;
                            }
                        }
                    }

                    // Case B: Not resolved yet
                    if (!resolved_target) {
                        if (args.size() < 4) {
                            std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                            bot.send_message(msg.channel_id, "usage:\n"
                                                             "• `" + pfx + "rr add <modalname> <emoji> <role> [add|remove]`\n"
                                                             "• `" + pfx + "rr addremove <modalname> <emoji> <role>`\n"
                                                             "• `" + pfx + "rr add <messageID> <emoji> <role> [add|remove]`\n"
                                                             "• `" + pfx + "rr add ^ <emoji> <role> [add|remove]` (use message above, or reply target)");
                            return;
                        }
                        if (args[1] == "^") {
                            // Fetch the message immediately above the command message
                            stoatpp::models::MessageQuery query;
                            query.before = msg.id;
                            query.limit = 1;
                            auto res = bot.rest().get_messages(msg.channel_id, query);
                            std::string found_id = "";
                            if (res.success()) {
                                if (res.body.is_array() && !res.body.empty()) {
                                    auto m = stoatpp::models::Message::from_json(res.body[0]);
                                    found_id = m.id;
                                } else if (res.body.is_object() && res.body.contains("messages") && res.body["messages"].is_array() && !res.body["messages"].empty()) {
                                    auto m = stoatpp::models::Message::from_json(res.body["messages"][0]);
                                    found_id = m.id;
                                }
                            }

                            if (found_id.empty()) {
                                bot.send_message(msg.channel_id, "failed to find the message above.");
                                return;
                            }
                            target = found_id;
                            size_t arg_idx = 2;
                            if (arg_idx < args.size() && args[arg_idx].rfind("<#", 0) == 0 && args[arg_idx].back() == '>') arg_idx++;
                            if (arg_idx < args.size()) raw_emoji = args[arg_idx++];
                            for (size_t i = arg_idx; i < args.size(); ++i) {
                                if (i > arg_idx) role_query += " ";
                                role_query += args[i];
                            }
                        } else {
                            target = args[1];
                            size_t arg_idx = 2;
                            if (arg_idx < args.size() && args[arg_idx].rfind("<#", 0) == 0 && args[arg_idx].back() == '>') arg_idx++;
                            if (arg_idx < args.size()) raw_emoji = args[arg_idx++];
                            for (size_t i = arg_idx; i < args.size(); ++i) {
                                if (i > arg_idx) role_query += " ";
                                role_query += args[i];
                            }
                        }
                    }
                    std::string emoji = clean_emoji(raw_emoji);

                    // Parse optional trailing "add" or "remove" specifier
                    std::string rule_type = "add";
                    if (subcommand == "addremove") {
                        rule_type = "remove";
                    } else {
                        size_t last_space = role_query.find_last_of(" ");
                        if (last_space != std::string::npos) {
                            std::string last_word = role_query.substr(last_space + 1);
                            std::transform(last_word.begin(), last_word.end(), last_word.begin(), ::tolower);
                            if (last_word == "remove") {
                                rule_type = "remove";
                                role_query = role_query.substr(0, last_space);
                            } else if (last_word == "add") {
                                rule_type = "add";
                                role_query = role_query.substr(0, last_space);
                            }
                        }
                    }

                    std::vector<stoatpp::models::Role> matching_roles;
                    std::string lower_query = role_query;
                    std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), [](unsigned char c) { return std::tolower(c); });

                    if (role_query.rfind("<%", 0) == 0 && role_query.back() == '>') {
                        std::string rid = role_query.substr(2, role_query.size() - 3);
                        for (const auto& r : server_opt->roles) {
                            if (r.id == rid) {
                                matching_roles.push_back(r);
                                break;
                            }
                        }
                    } else {
                        // 1. Search for exact case-insensitive match on name or ID
                        for (const auto& r : server_opt->roles) {
                            std::string lower_name = r.name;
                            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), [](unsigned char c) { return std::tolower(c); });
                            if (r.id == role_query || lower_name == lower_query) {
                                matching_roles.push_back(r);
                            }
                        }
                        
                        // 2. If no exact matches, search for substring matches
                        if (matching_roles.empty()) {
                            for (const auto& r : server_opt->roles) {
                                std::string lower_name = r.name;
                                std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), [](unsigned char c) { return std::tolower(c); });
                                if (lower_name.find(lower_query) != std::string::npos) {
                                    matching_roles.push_back(r);
                                }
                            }
                        }
                    }

                    bool is_message_id = (target.size() == 26 && 
                                          target.find_first_not_of("0123456789ABCDEFGHJKMNPQRSTVWXYZabcdefghjkmnpqrstvwxyz") == std::string::npos);

                    auto execute_add = [&bot, msg, target, emoji, is_message_id, member, server_opt, rule_type](stoatpp::models::Role selected_role) {
                        // Hierarchy check: role rank <= member rank (remember: lower is higher power)
                        bool is_owner_or_bypass = (server_opt->owner == msg.author.id) || bot.is_owner(msg.author.id);
                        if (!is_owner_or_bypass) {
                            int member_rank = server_opt->get_highest_rank(member);
                            if (selected_role.rank <= member_rank) {
                                bot.send_message(msg.channel_id, "permission error: you cannot configure the role **" + selected_role.name + "** because it is at or above your highest role in the server hierarchy.");
                                return;
                            }
                        }

                        auto run_db_add = [&bot, msg, target, emoji, is_message_id, selected_role, rule_type]() {
                            if (is_message_id) {
                                std::thread([&bot, msg, target, emoji, selected_role, rule_type]() {
                                    auto existing_rr = g_db->get_rr_message(target);
                                    std::string channel_id = msg.channel_id;

                                    if (!existing_rr) {
                                        auto res = bot.rest().get("/channels/" + msg.channel_id + "/messages/" + target);
                                        if (!res.success()) {
                                            bot.send_message(msg.channel_id, "failed to verify message ID `" + target + "` in this channel.");
                                            return;
                                        }
                                        if (!g_db->add_rr_message(target, msg.channel_id, msg.server_id, "")) {
                                            bot.send_message(msg.channel_id, "failed to register message `" + target + "` in database.");
                                            return;
                                        }
                                    } else {
                                        channel_id = existing_rr->channel_id;
                                    }

                                    if (g_db->add_rr_rule(target, emoji, selected_role.id, rule_type)) {
                                        bot.react_to_message(channel_id, target, emoji);
                                        bot.send_message(msg.channel_id, "successfully added reaction role mapping: " + display_emoji(emoji) + " ➔ " + (rule_type == "remove" ? "[Remove] " : "") + "**" + selected_role.name + "** on message `" + target + "`.");
                                    } else {
                                        bot.send_message(msg.channel_id, "failed to add reaction role mapping.");
                                    }
                                }).detach();
                            } else {
                                std::vector<std::string> msg_ids = g_db->get_message_ids_by_modal(msg.server_id, target);
                                if (msg_ids.empty()) {
                                    bot.send_message(msg.channel_id, "could not find any active modals named `" + target + "` in this server.");
                                    return;
                                }

                                std::thread([&bot, msg, target, emoji, selected_role, msg_ids, rule_type]() {
                                    for (const auto& target_msg_id : msg_ids) {
                                        auto rr_msg_opt = g_db->get_rr_message(target_msg_id);
                                        if (!rr_msg_opt) continue;

                                        bool is_duplicate = false;
                                        for (const auto& rule : rr_msg_opt->rules) {
                                            if (rule.emoji_id == emoji && rule.role_id == selected_role.id && rule.rule_type == rule_type) {
                                                is_duplicate = true;
                                                break;
                                            }
                                        }
                                        if (is_duplicate) {
                                            bot.send_message(msg.channel_id, "this emoji is already mapped to that exact role!");
                                            return;
                                        }

                                        if (g_db->add_rr_rule(target_msg_id, emoji, selected_role.id, rule_type)) {
                                            bot.react_to_message(rr_msg_opt->channel_id, target_msg_id, emoji);
                                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                                        }

                                        auto updated_rr = g_db->get_rr_message(target_msg_id);
                                        if (updated_rr) {
                                            stoatpp::models::MessagePayload payload = build_rr_payload(*updated_rr);

                                            bot.edit_message(updated_rr->channel_id, target_msg_id, payload);
                                        }
                                    }

                                    bot.send_message(msg.channel_id, "successfully added reaction role mapping: " + display_emoji(emoji) + " ➔ " + (rule_type == "remove" ? "[Remove] " : "") + "**" + selected_role.name + "** on modal `" + target + "`.");
                                }).detach();
                            }
                        };

                        // Dangerous permission check
                        if (is_role_dangerous(selected_role)) {
                            stoatpp::models::MessagePayload payload;
                            nlohmann::json embed = nlohmann::json::object();
                            embed["title"] = "⚠️ Warning: Dangerous Role Permissions";
                            embed["colour"] = "#F1C40F";
                            embed["description"] = "The role **" + selected_role.name + "** contains administrative or moderator permissions (e.g. Manage Roles, Ban, Kick, etc.).\n"
                                                  "Are you sure you want to add this role as a reaction role? Anyone will be able to get these permissions by reacting!\n\n"
                                                  "Type **yes** to confirm and activate, or **no** to cancel.";
                            payload.embeds.push_back(embed);

                            bot.send_message(msg.channel_id, payload, [&bot, msg, run_db_add](stoatpp::models::Message prompt, bool ok) {
                                if (!ok) return;
                                bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30), [&bot, msg, prompt, run_db_add](std::optional<stoatpp::events::Message> reply) {
                                    bot.delete_message(prompt.channel, prompt.id);
                                    if (reply) bot.delete_message(reply->channel_id, reply->id);

                                    if (reply && (reply->content == "yes" || reply->content == "YES" || reply->content == "confirm")) {
                                        run_db_add();
                                    } else {
                                        bot.send_reply(msg, "cancelled adding dangerous role.", true);
                                    }
                                });
                            });
                        } else {
                            run_db_add();
                        }
                    };

                    if (matching_roles.empty()) {
                        bot.send_message(msg.channel_id, "could not find any role matching \"" + role_query + "\".");
                        return;
                    }

                    if (matching_roles.size() == 1) {
                        execute_add(matching_roles[0]);
                    } else {
                        stoatpp::models::MessagePayload payload;
                        nlohmann::json embed = nlohmann::json::object();
                        embed["title"] = "i found some conflicts";
                        embed["colour"] = "#F1C40F";

                        std::string desc = "";
                        for (size_t i = 0; i < matching_roles.size(); ++i) {
                            desc += std::to_string(i + 1) + ". " + matching_roles[i].name + "\n";
                        }
                        desc += "\nyou only asked for \"" + role_query + "\"... which should I choose?\n*please reply with the option number within 30 seconds!*";
                        embed["description"] = desc;
                        payload.embeds.push_back(embed);

                        bot.send_message(msg.channel_id, payload, [&bot, msg, execute_add, matching_roles](stoatpp::models::Message prompt, bool ok) {
                            if (!ok) return;

                            bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30), [&bot, msg, prompt, execute_add, matching_roles](std::optional<stoatpp::events::Message> reply) {
                                // Clean up prompt and user reply
                                bot.delete_message(prompt.channel, prompt.id);
                                if (reply) {
                                    bot.delete_message(reply->channel_id, reply->id);
                                }

                                if (!reply) {
                                    bot.send_reply(msg, "timed out waiting for selection. cancelled.", true);
                                    return;
                                }

                                int choice = 0;
                                try {
                                    choice = std::stoi(reply->content);
                                } catch (...) {}

                                if (choice < 1 || choice > static_cast<int>(matching_roles.size())) {
                                    bot.send_reply(msg, "invalid selection. cancelled.", true);
                                    return;
                                }

                                execute_add(matching_roles[choice - 1]);
                            });
                        });
                    }
                    return;
                }
                if (subcommand == "type") {
                    if (args.size() < 3) {
                        std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "rr type <modalname|messageID> <standard|panel|verify>`");
                        return;
                    }
                    std::string target = args[1];
                    std::string type = args[2];
                    std::transform(type.begin(), type.end(), type.begin(), ::tolower);

                    if (type != "standard" && type != "panel" && type != "verify") {
                        bot.send_message(msg.channel_id, "invalid type. available types: `standard`, `panel`, `verify`.");
                        return;
                    }

                    bool is_message_id = (target.size() == 26 && 
                                          target.find_first_not_of("0123456789ABCDEFGHJKMNPQRSTVWXYZabcdefghjkmnpqrstvwxyz") == std::string::npos);

                    bool updated = false;
                    if (is_message_id) {
                        auto existing_rr = g_db->get_rr_message(target);
                        if (existing_rr) {
                            if (g_db->update_rr_message_type(target, type)) {
                                updated = true;
                                auto updated_rr = g_db->get_rr_message(target);
                                if (updated_rr) {
                                    stoatpp::models::MessagePayload payload = build_rr_payload(*updated_rr);
                                    bot.edit_message(updated_rr->channel_id, target, payload);
                                }
                            }
                        }
                    } else {
                        std::vector<std::string> msg_ids = g_db->get_message_ids_by_modal(msg.server_id, target);
                        for (const auto& msg_id : msg_ids) {
                            if (g_db->update_rr_message_type(msg_id, type)) {
                                updated = true;
                                auto updated_rr = g_db->get_rr_message(msg_id);
                                if (updated_rr) {
                                    stoatpp::models::MessagePayload payload = build_rr_payload(*updated_rr);
                                    bot.edit_message(updated_rr->channel_id, msg_id, payload);
                                }
                            }
                        }
                    }

                    if (updated) {
                        bot.send_message(msg.channel_id, "✅ successfully updated reaction role type for target `" + target + "` to `" + type + "`.");
                    } else {
                        bot.send_message(msg.channel_id, "⚠️ could not find any active modals or reaction role messages matching `" + target + "`.");
                    }
                    return;
                }

                if (subcommand == "remove") {
                    std::string target = "";
                    std::string raw_emoji = "";
                    std::string role_query = "";
                    bool resolved_target = false;

                    if (!msg.replies.empty() && args.size() >= 3) {
                        if (args[1] == "^") {
                            target = msg.replies[0];
                            size_t arg_idx = 2;
                            if (arg_idx < args.size() && args[arg_idx].rfind("<#", 0) == 0 && args[arg_idx].back() == '>') arg_idx++;
                            if (arg_idx < args.size()) raw_emoji = args[arg_idx++];
                            for (size_t i = arg_idx; i < args.size(); ++i) {
                                if (i > arg_idx) role_query += " ";
                                role_query += args[i];
                            }
                            resolved_target = true;
                        } else {
                            bool arg1_is_target = false;
                            if (args[1].size() == 26 && args[1].find_first_not_of("0123456789ABCDEFGHJKMNPQRSTVWXYZabcdefghjkmnpqrstvwxyz") == std::string::npos) {
                                arg1_is_target = true;
                            } else {
                                std::vector<std::string> msg_ids = g_db->get_message_ids_by_modal(msg.server_id, args[1]);
                                if (!msg_ids.empty()) arg1_is_target = true;
                            }
                            if (!arg1_is_target) {
                                target = msg.replies[0];
                                size_t arg_idx = 1;
                                if (arg_idx < args.size() && args[arg_idx].rfind("<#", 0) == 0 && args[arg_idx].back() == '>') arg_idx++;
                                if (arg_idx < args.size()) raw_emoji = args[arg_idx++];
                                for (size_t i = arg_idx; i < args.size(); ++i) {
                                    if (i > arg_idx) role_query += " ";
                                    role_query += args[i];
                                }
                                resolved_target = true;
                            }
                        }
                    }

                    if (!resolved_target) {
                        if (args.size() < 3) {
                            std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                            bot.send_message(msg.channel_id, "usage:\n"
                                                             "• `" + pfx + "rr remove <modalname> <emoji> [role]`\n"
                                                             "• `" + pfx + "rr remove <messageID> <emoji> [role]`\n"
                                                             "• `" + pfx + "rr remove ^ <emoji> [role]`");
                            return;
                        }
                        if (args[1] == "^") {
                            stoatpp::models::MessageQuery query;
                            query.before = msg.id;
                            query.limit = 1;
                            auto res = bot.rest().get_messages(msg.channel_id, query);
                            std::string found_id = "";
                            if (res.success()) {
                                if (res.body.is_array() && !res.body.empty()) {
                                    auto m = stoatpp::models::Message::from_json(res.body[0]);
                                    found_id = m.id;
                                } else if (res.body.is_object() && res.body.contains("messages") && res.body["messages"].is_array() && !res.body["messages"].empty()) {
                                    auto m = stoatpp::models::Message::from_json(res.body["messages"][0]);
                                    found_id = m.id;
                                }
                            }
                            if (found_id.empty()) {
                                bot.send_message(msg.channel_id, "failed to find the message above.");
                                return;
                            }
                            target = found_id;
                            size_t arg_idx = 2;
                            if (arg_idx < args.size() && args[arg_idx].rfind("<#", 0) == 0 && args[arg_idx].back() == '>') arg_idx++;
                            if (arg_idx < args.size()) raw_emoji = args[arg_idx++];
                            for (size_t i = arg_idx; i < args.size(); ++i) {
                                if (i > arg_idx) role_query += " ";
                                role_query += args[i];
                            }
                        } else {
                            target = args[1];
                            size_t arg_idx = 2;
                            if (arg_idx < args.size() && args[arg_idx].rfind("<#", 0) == 0 && args[arg_idx].back() == '>') arg_idx++;
                            if (arg_idx < args.size()) raw_emoji = args[arg_idx++];
                            for (size_t i = arg_idx; i < args.size(); ++i) {
                                if (i > arg_idx) role_query += " ";
                                role_query += args[i];
                            }
                        }
                    }
                    std::string emoji = clean_emoji(raw_emoji);

                    auto server_opt = bot.get_server(msg.server_id);
                    if (!server_opt) {
                        bot.send_message(msg.channel_id, "could not fetch server info");
                        return;
                    }
                    
                    std::vector<std::string> target_msg_ids;
                    if (target.size() == 26 && target.find_first_not_of("0123456789ABCDEFGHJKMNPQRSTVWXYZabcdefghjkmnpqrstvwxyz") == std::string::npos) {
                        target_msg_ids.push_back(target);
                    } else {
                        target_msg_ids = g_db->get_message_ids_by_modal(msg.server_id, target);
                    }

                    if (target_msg_ids.empty()) {
                        bot.send_message(msg.channel_id, "could not resolve target modal.");
                        return;
                    }

                    std::string selected_role_id = "";
                    if (!role_query.empty()) {
                        std::string lower_query = role_query;
                        std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), [](unsigned char c) { return std::tolower(c); });

                        if (role_query.rfind("<%", 0) == 0 && role_query.back() == '>') {
                            selected_role_id = role_query.substr(2, role_query.size() - 3);
                        } else {
                            for (const auto& r : server_opt->roles) {
                                std::string lower_name = r.name;
                                std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), [](unsigned char c) { return std::tolower(c); });
                                if (r.id == role_query || lower_name == lower_query) {
                                    selected_role_id = r.id;
                                    break;
                                }
                            }
                            if (selected_role_id.empty()) {
                                bot.send_message(msg.channel_id, "could not find any role matching `" + role_query + "`.");
                                return;
                            }
                        }
                    }

                    for (const auto& target_msg_id : target_msg_ids) {
                        auto rr_msg_opt = g_db->get_rr_message(target_msg_id);
                        if (!rr_msg_opt) {
                            continue;
                        }

                        if (g_db->remove_rr_rule(target_msg_id, emoji, selected_role_id)) {
                            auto updated_rr = g_db->get_rr_message(target_msg_id);
                            if (updated_rr) {
                                bool still_has_emoji = false;
                                for (const auto& r : updated_rr->rules) {
                                    if (r.emoji_id == emoji) {
                                        still_has_emoji = true;
                                        break;
                                    }
                                }
                                if (!still_has_emoji) {
                                    bot.unreact_from_message(updated_rr->channel_id, target_msg_id, emoji, bot.current_user().id);
                                }

                                if (!updated_rr->modal_name.empty()) {
                                    auto res = bot.rest().get_message(updated_rr->channel_id, target_msg_id);
                                    if (res.success()) {
                                        auto m = stoatpp::models::Message::from_json(res.body);
                                        if (m.author == bot.current_user().id) {
                                            stoatpp::models::MessagePayload payload = build_rr_payload(*updated_rr);
                                            
                                            bot.edit_message(updated_rr->channel_id, target_msg_id, payload, [](auto, bool){});
                                        }
                                    }
                                }
                            }
                        }
                    }
                    bot.send_message(msg.channel_id, "✅ removed mapping.");
                    return;
                }

                if (subcommand == "edit") {
                    if (args.size() >= 4 && args[2] == "embed") {
                        std::string modalname = args[1];
                        std::vector<std::string> msg_ids = g_db->get_message_ids_by_modal(msg.server_id, modalname);
                        if (msg_ids.empty()) {
                            bot.send_message(msg.channel_id, "could not find any active modals named `" + modalname + "` in this server.");
                            return;
                        }
                        
                        std::string json_str = args[3];
                        for (size_t i = 4; i < args.size(); ++i) json_str += " " + args[i];
                        
                        try {
                            auto parsed = nlohmann::json::parse(json_str);
                            if (!parsed.is_object()) throw std::runtime_error("not object");
                        } catch (...) {
                            bot.send_message(msg.channel_id, "invalid json format.");
                            return;
                        }
                        
                        for (const auto& target_msg_id : msg_ids) {
                            g_db->update_rr_embed_json(target_msg_id, json_str);
                            auto updated_rr = g_db->get_rr_message(target_msg_id);
                            if (updated_rr) {
                                stoatpp::models::MessagePayload payload = build_rr_payload(*updated_rr);
                                bot.edit_message(updated_rr->channel_id, target_msg_id, payload);
                            }
                        }
                        bot.send_message(msg.channel_id, "successfully updated embed for modal `" + modalname + "`.");
                        return;
                    }

                    if (args.size() < 5) {
                        std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                        bot.send_message(msg.channel_id, "usages:\n"
                                                         "• `" + pfx + "rr edit <modalname> embed <json...>`\n"
                                                         "• `" + pfx + "rr edit <modalname> <emoji> role <new_role>`\n"
                                                         "• `" + pfx + "rr edit <modalname> <old_emoji> emoji <new_emoji>`\n"
                                                         "• `" + pfx + "rr edit <modalname> <old_emoji> <new_emoji> <old_role> <new_role>`");
                        return;
                    }
                    std::string modalname = args[1];

                    std::vector<std::string> msg_ids = g_db->get_message_ids_by_modal(msg.server_id, modalname);
                    if (msg_ids.empty()) {
                        bot.send_message(msg.channel_id, "could not find any active modals named `" + modalname + "` in this server.");
                        return;
                    }

                    std::string old_emoji_query = args[2];
                    bool updated = false;

                    if (args.size() == 5) {
                        std::string edit_type = args[3];
                        std::string edit_value = args[4];
                        if (edit_type == "role") {
                            std::vector<EmojiRolePair> parsed = parse_emoji_role_pairs(old_emoji_query + " " + edit_value, bot, msg.server_id);
                            if (parsed.empty()) {
                                bot.send_message(msg.channel_id, "failed to parse/resolve the new role.");
                                return;
                            }
                            std::string rid = parsed[0].role_id;
                            stoatpp::models::Role selected_role;
                            bool found = false;
                            for (const auto& r : server_opt->roles) {
                                if (r.id == rid) { selected_role = r; found = true; break; }
                            }
                            if (found) {
                                // Hierarchy check
                                bool is_owner_or_bypass = (server_opt->owner == msg.author.id) || bot.is_owner(msg.author.id);
                                if (!is_owner_or_bypass) {
                                    int member_rank = server_opt->get_highest_rank(member);
                                    if (selected_role.rank <= member_rank) {
                                        bot.send_message(msg.channel_id, "permission error: you cannot configure the role **" + selected_role.name + "** because it is at or above your highest role in the server hierarchy.");
                                        return;
                                    }
                                }
                                // Dangerous check
                                if (is_role_dangerous(selected_role)) {
                                    stoatpp::models::MessagePayload payload;
                                    nlohmann::json embed = nlohmann::json::object();
                                    embed["title"] = "⚠️ Warning: Dangerous Role Permissions";
                                    embed["colour"] = "#F1C40F";
                                    embed["description"] = "The role **" + selected_role.name + "** contains administrative or moderator permissions.\n"
                                                          "Are you sure you want to map this role? Anyone will be able to get these permissions by reacting!\n\n"
                                                          "Type **yes** to confirm and activate, or **no** to cancel.";
                                    payload.embeds.push_back(embed);

                                    bot.send_message(msg.channel_id, payload, [&bot, msg, msg_ids, old_emoji_query, rid](stoatpp::models::Message prompt, bool ok) {
                                        if (!ok) return;
                                        bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30), [&bot, msg, prompt, msg_ids, old_emoji_query, rid](std::optional<stoatpp::events::Message> reply) {
                                            bot.delete_message(prompt.channel, prompt.id);
                                            if (reply) bot.delete_message(reply->channel_id, reply->id);

                                            if (reply && (reply->content == "yes" || reply->content == "YES" || reply->content == "confirm")) {
                                                for (const auto& target_msg_id : msg_ids) {
                                                    g_db->remove_rr_rule(target_msg_id, old_emoji_query);
                                                    g_db->add_rr_rule(target_msg_id, old_emoji_query, rid);
                                                    auto updated_rr = g_db->get_rr_message(target_msg_id);
                                                    if (updated_rr) {
                                                        stoatpp::models::MessagePayload payload = build_rr_payload(*updated_rr);
                                                        bot.edit_message(updated_rr->channel_id, target_msg_id, payload);
                                                    }
                                                }
                                                bot.send_reply(msg, "successfully edited role mapping for all instances.", true);
                                            } else {
                                                bot.send_reply(msg, "cancelled editing dangerous role.", true);
                                            }
                                        });
                                    });
                                    return;
                                }
                            }
                        }
                    } else if (args.size() >= 6) {
                        std::string new_emoji = args[3];
                        std::string old_role_q = args[4];
                        std::string new_role_q = args[5];
                        std::vector<EmojiRolePair> old_parsed = parse_emoji_role_pairs(old_emoji_query + " " + old_role_q, bot, msg.server_id);
                        std::vector<EmojiRolePair> new_parsed = parse_emoji_role_pairs(new_emoji + " " + new_role_q, bot, msg.server_id);
                        if (old_parsed.empty() || new_parsed.empty()) {
                            bot.send_message(msg.channel_id, "failed to parse/resolve the roles.");
                            return;
                        }
                        
                        std::string rid = new_parsed[0].role_id;
                        stoatpp::models::Role selected_role;
                        bool found = false;
                        for (const auto& r : server_opt->roles) {
                            if (r.id == rid) { selected_role = r; found = true; break; }
                        }
                        if (found) {
                            // Hierarchy check
                            bool is_owner_or_bypass = (server_opt->owner == msg.author.id) || bot.is_owner(msg.author.id);
                            if (!is_owner_or_bypass) {
                                int member_rank = server_opt->get_highest_rank(member);
                                if (selected_role.rank <= member_rank) {
                                    bot.send_message(msg.channel_id, "permission error: you cannot configure the role **" + selected_role.name + "** because it is at or above your highest role in the server hierarchy.");
                                    return;
                                }
                            }
                            
                            // Dangerous check
                            if (is_role_dangerous(selected_role)) {
                                stoatpp::models::MessagePayload payload;
                                nlohmann::json embed = nlohmann::json::object();
                                embed["title"] = "⚠️ Warning: Dangerous Role Permissions";
                                embed["colour"] = "#F1C40F";
                                embed["description"] = "The role **" + selected_role.name + "** contains administrative or moderator permissions.\n"
                                                      "Are you sure you want to map this role? Anyone will be able to get these permissions by reacting!\n\n"
                                                      "Type **yes** to confirm and activate, or **no** to cancel.";
                                payload.embeds.push_back(embed);

                                bot.send_message(msg.channel_id, payload, [&bot, msg, msg_ids, old_parsed, new_parsed](stoatpp::models::Message prompt, bool ok) {
                                    if (!ok) return;
                                    bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30), [&bot, msg, prompt, msg_ids, old_parsed, new_parsed](std::optional<stoatpp::events::Message> reply) {
                                        bot.delete_message(prompt.channel, prompt.id);
                                        if (reply) bot.delete_message(reply->channel_id, reply->id);

                                        if (reply && (reply->content == "yes" || reply->content == "YES" || reply->content == "confirm")) {
                                            for (const auto& target_msg_id : msg_ids) {
                                                auto rr_msg_opt = g_db->get_rr_message(target_msg_id);
                                                if (!rr_msg_opt) continue;
                                                
                                                g_db->remove_rr_rule(target_msg_id, old_parsed[0].emoji, old_parsed[0].role_id);
                                                g_db->add_rr_rule(target_msg_id, new_parsed[0].emoji, new_parsed[0].role_id);

                                                auto updated_rr_temp = g_db->get_rr_message(target_msg_id);
                                                if (updated_rr_temp) {
                                                    bool still_has_old_emoji = false;
                                                    for (const auto& r : updated_rr_temp->rules) {
                                                        if (r.emoji_id == old_parsed[0].emoji) {
                                                            still_has_old_emoji = true;
                                                            break;
                                                        }
                                                    }
                                                    if (!still_has_old_emoji) {
                                                        bot.unreact_from_message(rr_msg_opt->channel_id, target_msg_id, old_parsed[0].emoji, bot.current_user().id);
                                                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                                                    }
                                                }
                                                
                                                bot.react_to_message(rr_msg_opt->channel_id, target_msg_id, new_parsed[0].emoji);
                                                
                                                auto updated_rr = g_db->get_rr_message(target_msg_id);
                                                if (updated_rr) {
                                                    stoatpp::models::MessagePayload payload = build_rr_payload(*updated_rr);
                                                    bot.edit_message(updated_rr->channel_id, target_msg_id, payload);
                                                }
                                            }
                                            bot.send_reply(msg, "successfully edited role mapping for all instances.", true);
                                        } else {
                                            bot.send_reply(msg, "cancelled editing dangerous role.", true);
                                        }
                                    });
                                });
                                return;
                            }
                        }
                    }

                    for (const auto& target_msg_id : msg_ids) {
                        auto rr_msg_opt = g_db->get_rr_message(target_msg_id);
                        if (!rr_msg_opt) continue;

                        if (args.size() == 5) {
                            std::string edit_type = args[3];
                            std::string edit_value = args[4];

                            if (edit_type == "emoji") {
                                std::string matched_role_id = "";
                                for (const auto& r : rr_msg_opt->rules) {
                                    if (r.emoji_id == old_emoji_query) {
                                        matched_role_id = r.role_id;
                                        break;
                                    }
                                }

                                if (matched_role_id.empty()) continue;

                                g_db->remove_rr_rule(target_msg_id, old_emoji_query);
                                g_db->add_rr_rule(target_msg_id, edit_value, matched_role_id);
                                
                                bot.unreact_from_message(rr_msg_opt->channel_id, target_msg_id, old_emoji_query, bot.current_user().id);
                                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                                bot.react_to_message(rr_msg_opt->channel_id, target_msg_id, edit_value);
                                updated = true;
                            } 
                            else if (edit_type == "role") {
                                std::vector<EmojiRolePair> parsed = parse_emoji_role_pairs(old_emoji_query + " " + edit_value, bot, msg.server_id);
                                if (parsed.empty()) continue;

                                g_db->remove_rr_rule(target_msg_id, old_emoji_query);
                                g_db->add_rr_rule(target_msg_id, old_emoji_query, parsed[0].role_id);
                                updated = true;
                            }
                        }
                        else if (args.size() >= 6) {
                            std::string new_emoji = args[3];
                            std::string old_role_q = args[4];
                            std::string new_role_q = args[5];

                            std::vector<EmojiRolePair> old_parsed = parse_emoji_role_pairs(old_emoji_query + " " + old_role_q, bot, msg.server_id);
                            std::vector<EmojiRolePair> new_parsed = parse_emoji_role_pairs(new_emoji + " " + new_role_q, bot, msg.server_id);

                            if (old_parsed.empty() || new_parsed.empty()) continue;

                            g_db->remove_rr_rule(target_msg_id, old_parsed[0].emoji, old_parsed[0].role_id);
                            g_db->add_rr_rule(target_msg_id, new_parsed[0].emoji, new_parsed[0].role_id);

                            auto updated_rr_temp = g_db->get_rr_message(target_msg_id);
                            if (updated_rr_temp) {
                                bool still_has_old_emoji = false;
                                for (const auto& r : updated_rr_temp->rules) {
                                    if (r.emoji_id == old_parsed[0].emoji) {
                                        still_has_old_emoji = true;
                                        break;
                                    }
                                }
                                if (!still_has_old_emoji) {
                                    bot.unreact_from_message(rr_msg_opt->channel_id, target_msg_id, old_parsed[0].emoji, bot.current_user().id);
                                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                                }
                            }
                            
                            bot.react_to_message(rr_msg_opt->channel_id, target_msg_id, new_parsed[0].emoji);
                            updated = true;
                        }

                        if (updated) {
                            auto updated_rr = g_db->get_rr_message(target_msg_id);
                            if (updated_rr) {
                                stoatpp::models::MessagePayload payload = build_rr_payload(*updated_rr);

                                bot.edit_message(updated_rr->channel_id, target_msg_id, payload);
                            }
                        }
                    }

                    if (updated) {
                        bot.send_message(msg.channel_id, "successfully updated reaction role rules for all instances of `" + modalname + "`.");
                    } else {
                        bot.send_message(msg.channel_id, "failed to update/match any rules.");
                    }
                    return;
                }

                if (subcommand == "list") {
                    auto modals = g_db->get_server_modals(msg.server_id);
                    if (modals.empty()) {
                        bot.send_message(msg.channel_id, "there are no reaction role modals configured in this server.");
                        return;
                    }

                    std::sort(modals.begin(), modals.end(), [](const auto& a, const auto& b) {
                        return a.name < b.name;
                    });

                    int max_pages = (modals.size() + 4) / 5;
                    stoatpp::models::MessagePayload payload;
                    nlohmann::json embed = nlohmann::json::object();
                    embed["title"] = "reaction role modals list";
                    embed["colour"] = "#6C5CE7";

                    std::string desc = "here are all configured modals in this server:\n\n";
                    for (size_t i = 0; i < 5 && i < modals.size(); ++i) {
                        desc += "• **" + modals[i].name + "**: " + std::to_string(modals[i].rule_count) + 
                                " rules mapped across " + std::to_string(modals[i].message_count) + " active messages.\n";
                    }
                    if (max_pages > 1) {
                        desc += "\n(page 1 of " + std::to_string(max_pages) + ")";
                    }
                    embed["description"] = desc;
                    payload.embeds.push_back(embed);

                    bot.send_message(msg.channel_id, payload, [&bot, msg, max_pages](stoatpp::models::Message res, bool ok) {
                        if (ok && max_pages > 1) {
                            {
                                std::lock_guard<std::mutex> lock(helpers::g_pages_mutex);
                                helpers::g_pages[res.id] = {0, max_pages, msg.author.id, "rr_list"};
                            }
                            bot.react_to_message(res.channel, res.id, "⬅");
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            bot.react_to_message(res.channel, res.id, "➡");
                        }
                    });
                    return;
                }

                if (subcommand == "delete") {
                    if (args.size() < 2) {
                        std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "rr delete <messageID|modal>`");
                        return;
                    }

                    std::string target = args[1];
                    std::vector<std::string> msg_ids = g_db->get_message_ids_by_modal(msg.server_id, target);
                    std::string single_msg_id = "";
                    bool is_modal = false;

                    if (!msg_ids.empty()) {
                        is_modal = true;
                    } else {
                        auto rr_msg = g_db->get_rr_message(target);
                        if (rr_msg && rr_msg->server_id == msg.server_id) {
                            single_msg_id = target;
                        }
                    }

                    if (!is_modal && single_msg_id.empty()) {
                        bot.send_message(msg.channel_id, "could not find active modal or message ID `" + target + "` in this server.");
                        return;
                    }

                    std::string confirm_text;
                    if (is_modal) {
                        confirm_text = "are you sure you want to delete modal **" + target + "** and all of its " + 
                                       std::to_string(msg_ids.size()) + " active message instances? reply with **yes** or **no** within 15 seconds.";
                    } else {
                        confirm_text = "are you sure you want to delete reaction roles for message ID **" + target + "**? reply with **yes** or **no** within 15 seconds.";
                    }

                    bot.send_reply(msg, confirm_text, true, [&bot, msg, msg_ids, single_msg_id, target, is_modal](stoatpp::models::Message confirm_res, bool ok) {
                        if (!ok) return;

                        bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(15), [&bot, msg, msg_ids, single_msg_id, target, is_modal](std::optional<stoatpp::events::Message> reply) {
                            if (!reply) {
                                bot.send_reply(msg, "delete confirmation timed out. cancelled.", true);
                                return;
                            }

                            std::string content = reply->content;
                            std::transform(content.begin(), content.end(), content.begin(), [](unsigned char c) { return std::tolower(c); });

                            if (content == "yes" || content == "y" || content == "confirm") {
                                if (is_modal) {
                                    int deleted_count = 0;
                                    for (const auto& mid : msg_ids) {
                                        auto details = g_db->get_rr_message(mid);
                                        if (details) {
                                            bot.delete_message(details->channel_id, mid);
                                        }
                                        if (g_db->delete_rr_message(mid)) {
                                            deleted_count++;
                                        }
                                    }
                                    bot.send_reply(msg, "successfully deleted modal **" + target + "** and all " + std::to_string(deleted_count) + " active messages.", true);
                                } else {
                                    auto details = g_db->get_rr_message(single_msg_id);
                                    if (details) {
                                        bot.delete_message(details->channel_id, single_msg_id);
                                    }
                                    if (g_db->delete_rr_message(single_msg_id)) {
                                        bot.send_reply(msg, "successfully deleted reaction roles configuration for message ID **" + single_msg_id + "**.", true);
                                    } else {
                                        bot.send_reply(msg, "failed to delete reaction roles from database.", true);
                                    }
                                }
                            } else {
                                bot.send_reply(msg, "cancelled delete operation.", true);
                            }
                        });
                    });
                    return;
                }

                if (subcommand == "parse" || subcommand == "scan") {
                    if (args.size() < 2) {
                        std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                        bot.send_message(msg.channel_id, "usage:\n"
                            "• `" + pfx + "rr parse <modal> <message_id>` — import rules into a new modal\n"
                            "• `" + pfx + "rr parse <message_id>` — activate rules directly on the scanned message");
                        return;
                    }

                    // Determine if the user passed a modal name: .rr parse <modal> <msg>
                    std::string parse_modal_name = "";
                    std::string target_msg_id;
                    if (args.size() >= 3) {
                        parse_modal_name = args[1];
                        target_msg_id    = args[2];
                        // Reject immediately if modal already exists
                        std::string existing_modal_id = g_db->get_message_id_by_modal_name(msg.server_id, parse_modal_name);
                        if (!existing_modal_id.empty()) {
                            std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                            bot.send_message(msg.channel_id, "a modal named `" + parse_modal_name + "` already exists. "
                                "use `" + pfx + "rr edit` to change its rules, or `" + pfx + "rr delete` it first.");
                            return;
                        }
                    } else {
                        target_msg_id = args[1];
                    }

                    // Fetch the target message content
                    std::thread([&bot, msg, target_msg_id, parse_modal_name, member, server_opt]() {
                        std::string actual_msg_id = target_msg_id;
                        if (actual_msg_id == "^") {
                            stoatpp::models::MessageQuery query;
                            query.before = msg.id;
                            query.limit = 1;
                            auto history_res = bot.rest().get_messages(msg.channel_id, query);
                            if (history_res.success()) {
                                if (history_res.body.is_array() && !history_res.body.empty()) {
                                    auto m = stoatpp::models::Message::from_json(history_res.body[0]);
                                    actual_msg_id = m.id;
                                } else if (history_res.body.is_object() && history_res.body.contains("messages") && history_res.body["messages"].is_array() && !history_res.body["messages"].empty()) {
                                    auto m = stoatpp::models::Message::from_json(history_res.body["messages"][0]);
                                    actual_msg_id = m.id;
                                }
                            }
                            if (actual_msg_id == "^") {
                                bot.send_reply(msg, "failed to find the message above.", true);
                                return;
                            }
                        }

                        auto res = bot.rest().get("/channels/" + msg.channel_id + "/messages/" + actual_msg_id);
                        if (!res.success()) {
                            bot.send_reply(msg, "failed to download target message. make sure the message ID is correct and in this channel.", true);
                            return;
                        }

                        auto target_msg = stoatpp::models::Message::from_json(res.body);
                        if (!target_msg.content || target_msg.content->empty()) {
                            bot.send_reply(msg, "target message has no text content to parse.", true);
                            return;
                        }

                        // Split by lines and parse
                        std::vector<EmojiRolePair> rules;
                        std::stringstream ss(*target_msg.content);
                        std::string line;
                        while (std::getline(ss, line)) {
                            auto parsed_line = parse_emoji_role_pairs(line, bot, msg.server_id);
                            for (const auto& p : parsed_line) {
                                rules.push_back(p);
                            }
                        }

                        // Hierarchy check: role rank <= member rank (remember: lower is higher power)
                        bool is_owner_or_bypass = (server_opt->owner == msg.author.id) || bot.is_owner(msg.author.id);
                        if (!is_owner_or_bypass) {
                            int member_rank = server_opt->get_highest_rank(member);
                            std::vector<EmojiRolePair> permitted_rules;
                            std::vector<std::string> blocked_role_names;
                            for (const auto& r : rules) {
                                int role_rank = 999999;
                                for (const auto& sr : server_opt->roles) {
                                    if (sr.id == r.role_id) {
                                        role_rank = sr.rank;
                                        break;
                                    }
                                }
                                if (role_rank <= member_rank) {
                                    blocked_role_names.push_back(r.role_name);
                                } else {
                                    permitted_rules.push_back(r);
                                }
                            }
                            if (!blocked_role_names.empty()) {
                                std::string blocked_msg = "⚠️ permission warning: skipped the following roles because they are at or above your highest role in the server hierarchy: ";
                                for (size_t i = 0; i < blocked_role_names.size(); ++i) {
                                    blocked_msg += "**" + blocked_role_names[i] + "**" + (i + 1 < blocked_role_names.size() ? ", " : "");
                                }
                                bot.send_message(msg.channel_id, blocked_msg);
                            }
                            rules = permitted_rules;
                        }

                        std::vector<std::string> physical_reactions;
                        if (target_msg.raw.contains("reactions") && target_msg.raw["reactions"].is_object()) {
                            for (auto& element : target_msg.raw["reactions"].items()) {
                                physical_reactions.push_back(element.key());
                            }
                        }

                        if (rules.empty()) {
                            std::string reply_msg = "no reaction role rules detected in target message.\n"
                                                    "make sure patterns write like:\n"
                                                    "• `:emoji: @role`\n"
                                                    "• `:emoji: - @role`\n"
                                                    "• `:emoji: > @role`";
                            
                            auto existing_rr = g_db->get_rr_message(actual_msg_id);
                            if (existing_rr && !existing_rr->rules.empty()) {
                                reply_msg += "\n\n⚙️ **Note**: This message already has reaction roles configured in my database:\n";
                                for (const auto& r : existing_rr->rules) {
                                    reply_msg += display_emoji(r.emoji_id) + " ➔ <@&" + r.role_id + ">\n";
                                }
                            } else if (!physical_reactions.empty()) {
                                reply_msg += "\n\n⚠️ **Note**: This message has the following existing reactions on it:\n";
                                for (const auto& r : physical_reactions) {
                                    reply_msg += display_emoji(r) + " ";
                                }
                            }
                            bot.send_reply(msg, reply_msg, true);
                            return;
                        }

                        // Check if these rules are already active in the database
                        auto existing_rr = g_db->get_rr_message(actual_msg_id);
                        if (existing_rr && !existing_rr->rules.empty()) {
                            // Build a set of emoji->role from the DB
                            std::map<std::string, std::string> db_rules;
                            for (const auto& r : existing_rr->rules) {
                                db_rules[r.emoji_id] = r.role_id;
                            }
                            // Build a set from what we just parsed
                            std::map<std::string, std::string> parsed_rules;
                            for (const auto& r : rules) {
                                parsed_rules[r.emoji] = r.role_id;
                            }
                            if (db_rules == parsed_rules) {
                                stoatpp::models::MessagePayload already_payload;
                                nlohmann::json embed = nlohmann::json::object();
                                embed["title"] = "reaction roles scanner";
                                embed["colour"] = "#00B894";
                                std::string desc = "these reaction role rules are already active on message `" + actual_msg_id + "`:\n\n";
                                std::vector<bronx::Database::RRRule> converted_rules;
                                for (const auto& r : rules) {
                                    converted_rules.push_back({r.emoji, r.role_id});
                                }
                                desc += format_rr_description(converted_rules);
                                desc += "\n\nno changes needed.";
                                embed["description"] = desc;
                                already_payload.embeds.push_back(embed);
                                bot.send_message(msg.channel_id, already_payload);
                                return;
                            }
                        }

                        // --- MODAL IMPORT FLOW (.rr parse <modal> <msg>) ---
                        if (!parse_modal_name.empty()) {
                            // Build the modal message payload
                            bronx::Database::RRMessage tmp_rr;
                            tmp_rr.modal_name = parse_modal_name;
                            for (const auto& r : rules) {
                                tmp_rr.rules.push_back({r.emoji, r.role_id});
                            }
                            stoatpp::models::MessagePayload modal_payload = build_rr_payload(tmp_rr);

                            bot.send_message(msg.channel_id, modal_payload, [parse_modal_name, rules, msg, &bot](stoatpp::models::Message res, bool ok) {
                                if (!ok) {
                                    bot.send_message(msg.channel_id, "failed to create the modal message.");
                                    return;
                                }
                                g_db->add_rr_message(res.id, res.channel, msg.server_id, parse_modal_name);
                                for (const auto& r : rules) {
                                    g_db->add_rr_rule(res.id, r.emoji, r.role_id);
                                }
                                std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                                bot.send_message(msg.channel_id, "✅ created modal **" + parse_modal_name + "** with " +
                                    std::to_string(rules.size()) + " rule" + (rules.size() == 1 ? "" : "s") + ". "
                                    "use `" + pfx + "rr send " + parse_modal_name + " #channel` to deploy it.");
                            });
                            return;
                        }

                        // --- ACTIVATE-ON-MESSAGE FLOW (.rr parse <msg>) ---

                        // Check for dangerous permissions
                        std::vector<std::string> dangerous_roles;
                        for (const auto& r : rules) {
                            for (const auto& sr : server_opt->roles) {
                                if (sr.id == r.role_id) {
                                    if (is_role_dangerous(sr)) {
                                        dangerous_roles.push_back(sr.name);
                                    }
                                    break;
                                }
                            }
                        }

                        // Send interactive confirmation
                        stoatpp::models::MessagePayload confirm_payload;
                        nlohmann::json embed = nlohmann::json::object();
                        embed["title"] = "reaction roles scanner";
                        embed["colour"] = dangerous_roles.empty() ? "#00CEC9" : "#D63031";

                        std::string desc = "i detected the following reaction roles rules in message `" + actual_msg_id + "`:\n\n";
                        std::vector<bronx::Database::RRRule> converted_rules;
                        for (const auto& r : rules) {
                            converted_rules.push_back({r.emoji, r.role_id});
                        }
                        desc += format_rr_description(converted_rules);

                        if (!dangerous_roles.empty()) {
                            desc += "\n\n⚠️ **WARNING: The following roles have dangerous administrative/moderator permissions (e.g. Manage Roles, Ban, Kick):**\n";
                            for (const auto& dr : dangerous_roles) {
                                desc += "• **" + dr + "**\n";
                            }
                            desc += "\n*Reacting to confirm will allow anyone to obtain these dangerous permissions!*";
                        }

                        desc += "\n\nreact with ✅ below to confirm and activate, or ❌ to cancel.";
                        embed["description"] = desc;
                        confirm_payload.embeds.push_back(embed);

                        bot.send_message(msg.channel_id, confirm_payload, [actual_msg_id, rules, msg, &bot](stoatpp::models::Message confirm_res, bool ok) {
                            if (ok) {
                                {
                                    std::lock_guard<std::mutex> lock(g_pending_scans_mutex);
                                    PendingScan scan;
                                    scan.target_message_id = actual_msg_id;
                                    scan.target_channel_id = confirm_res.channel;
                                    scan.target_server_id = msg.server_id;
                                    scan.rules = rules;
                                    scan.author_id = msg.author.id;
                                    g_pending_scans[confirm_res.id] = scan;
                                }
                                bot.react_to_message(confirm_res.channel, confirm_res.id, "✅");
                                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                                bot.react_to_message(confirm_res.channel, confirm_res.id, "❌");
                            }
                        });
                    }).detach();
                    return;
                }

                bot.send_message(msg.channel_id, "unknown subcommand. use create, add, edit, or parse.");
            },
            "Reaction Roles",
            0,
            0,
            {"create <name>", "send <name> <#channel>", "add <name|message_id> <emoji> <role>", "edit <name> <emoji> <emoji|role> <new_val>", "parse <modal> <message_id>", "parse <message_id>", "list", "delete <name>"}
        });

        // Gateway event handling for role assignments
        bot.on_message_react([&bot](const stoatpp::events::MessageReact& e) {
            try {
                if (e.user_id == bot.current_user().id) return;

            // Check if pagination turn for rr_list
            {
                std::unique_lock<std::mutex> lock(helpers::g_pages_mutex);
                auto it = helpers::g_pages.find(e.id);
                if (it != helpers::g_pages.end() && it->second.type == "rr_list") {
                    auto& session = it->second;
                    if (e.user_id != session.user_id) {
                        lock.unlock();
                        bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
                        return;
                    }

                    bool page_changed = false;
                    if (e.emoji_id == "⬅️" || e.emoji_id == "◀" || e.emoji_id == "⬅") {
                        if (session.current_page > 0) {
                            session.current_page--;
                        } else {
                            session.current_page = session.max_pages - 1;
                        }
                        page_changed = true;
                    } else if (e.emoji_id == "➡️" || e.emoji_id == "▶" || e.emoji_id == "➡") {
                        if (session.current_page < session.max_pages - 1) {
                            session.current_page++;
                        } else {
                            session.current_page = 0;
                        }
                        page_changed = true;
                    }

                    if (page_changed) {
                        int page = session.current_page;
                        int max_pages = session.max_pages;
                        lock.unlock();

                        auto rr_msg = g_db->get_rr_message(e.id);
                        std::string server_id = rr_msg ? rr_msg->server_id : "";
                        
                        // Fallback using active channels if server_id is empty
                        if (server_id.empty()) {
                            auto chan = bot.get_channel(e.channel_id);
                            if (chan && chan->server) {
                                server_id = *chan->server;
                            }
                        }

                        auto modals = g_db->get_server_modals(server_id);
                        std::sort(modals.begin(), modals.end(), [](const auto& a, const auto& b) {
                            return a.name < b.name;
                        });

                        stoatpp::models::MessagePayload payload;
                        nlohmann::json embed = nlohmann::json::object();
                        embed["title"] = "reaction role modals list";
                        embed["colour"] = "#6C5CE7";

                        std::string desc = "here are all configured modals in this server:\n\n";
                        size_t start_idx = page * 5;
                        for (size_t i = start_idx; i < start_idx + 5 && i < modals.size(); ++i) {
                            desc += "• **" + modals[i].name + "**: " + std::to_string(modals[i].rule_count) + 
                                    " rules mapped across " + std::to_string(modals[i].message_count) + " active messages.\n";
                        }
                        desc += "\n(page " + std::to_string(page + 1) + " of " + std::to_string(max_pages) + ")";
                        embed["description"] = desc;
                        payload.embeds.push_back(embed);
                        payload.content = " ";

                        bot.edit_message(e.channel_id, e.id, payload, [&bot, e](auto, bool) {
                            bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
                        });
                    } else {
                        lock.unlock();
                        bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
                    }
                    return;
                }
            }

            // 1. Check if pending scanner confirmation
            {
                std::unique_lock<std::mutex> lock(g_pending_scans_mutex);
                auto it = g_pending_scans.find(e.id);
                if (it != g_pending_scans.end()) {
                    auto scan = it->second;
                    if (e.user_id != scan.author_id) {
                        lock.unlock();
                        bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
                        return;
                    }

                    lock.unlock();
                    if (e.emoji_id == "✅") {
                        // Confirm: save rules in database
                        g_db->add_rr_message(scan.target_message_id, scan.target_channel_id, scan.target_server_id, "");
                        for (const auto& r : scan.rules) {
                            g_db->add_rr_rule(scan.target_message_id, r.emoji, r.role_id);
                            bot.react_to_message(scan.target_channel_id, scan.target_message_id, r.emoji);
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        }

                        stoatpp::models::MessagePayload payload;
                        nlohmann::json embed = nlohmann::json::object();
                        embed["title"] = "reaction roles scanner";
                        embed["colour"] = "#2ECC71";
                        embed["description"] = "✅ configuration successfully confirmed! mappings activated on target message `" + scan.target_message_id + "`.";
                        payload.embeds.push_back(embed);
                        payload.content = " ";
                        bot.edit_message(e.channel_id, e.id, payload, [&bot, e](auto, bool) {
                            bot.delete_message(e.channel_id, e.id); // clean up prompt
                        });
                    } 
                    else if (e.emoji_id == "❌") {
                        // Cancel
                        stoatpp::models::MessagePayload payload;
                        nlohmann::json embed = nlohmann::json::object();
                        embed["title"] = "reaction roles scanner";
                        embed["colour"] = "#E74C3C";
                        embed["description"] = "❌ configuration cancelled.";
                        payload.embeds.push_back(embed);
                        payload.content = " ";
                        bot.edit_message(e.channel_id, e.id, payload, [&bot, e](auto, bool) {
                            std::this_thread::sleep_for(std::chrono::seconds(3));
                            bot.delete_message(e.channel_id, e.id);
                        });
                    }

                    std::lock_guard<std::mutex> lock2(g_pending_scans_mutex);
                    g_pending_scans.erase(e.id);
                    return;
                }
            }

            // 2. Check if active reaction roles message
            auto rr_msg = g_db->get_rr_message(e.id);
            if (rr_msg) {
                std::vector<Database::RRRule> match_rules;
                for (const auto& r : rr_msg->rules) {
                    if (r.emoji_id == e.emoji_id) {
                        match_rules.push_back(r);
                    }
                }

                for (const auto& rule : match_rules) {
                    std::string role_name = rule.role_id;
                    auto server_opt = bot.get_server(rr_msg->server_id);
                    if (server_opt) {
                        for (const auto& r : server_opt->roles) {
                            if (r.id == rule.role_id) {
                                role_name = r.name;
                                break;
                            }
                        }
                    }

                    if (rule.rule_type == "remove") {
                        std::cout << "[bronx rr] removing role " << rule.role_id << " from user " << e.user_id << "\n";
                        bot.remove_role_from_member(rr_msg->server_id, e.user_id, rule.role_id, [&bot, e, rule, role_name, server_opt, rr_msg](bool success, std::string err) {
                            std::cout << "[bronx rr] role remove " << (success ? "succeeded" : "failed") 
                                      << " for user " << e.user_id << " (role: " << rule.role_id << ") error: " << err << "\n";
                            if (!success) {
                                bot.send_message(e.channel_id, "⚠️ failed to remove role **" + role_name + "**. make sure i have the **Manage Roles** permission and my role is positioned above it.");
                            }
                        });
                    } else {
                        std::cout << "[bronx rr] adding role " << rule.role_id << " to user " << e.user_id << "\n";
                        bot.add_role_to_member(rr_msg->server_id, e.user_id, rule.role_id, [&bot, e, rule, role_name, server_opt, rr_msg](bool success, std::string err) {
                            std::cout << "[bronx rr] role add " << (success ? "succeeded" : "failed") 
                                      << " for user " << e.user_id << " (role: " << rule.role_id << ") error: " << err << "\n";
                            if (!success) {
                                if (err == "NotElevated" || err == "HierarchyError") {
                                    bot.send_message(e.channel_id, "⚠️ hierarchy error: i couldn't give you the **" + role_name + "** role in `" + (server_opt ? server_opt->name : "server") + "`. my role needs to be placed *above* the roles I am giving out. please contact an admin.");
                                } else {
                                    bot.send_message(e.channel_id, "⚠️ failed to assign role **" + role_name + "**. make sure i have the **Manage Roles** permission and my role is positioned above it in the server settings.");
                                }
                            }
                        });
                    }
                }
            }
            } catch (const std::exception& err) {
                std::cout << "[bronx rr error] Exception in on_message_react: " << err.what() << "\n";
            }
        });

        bot.on_message_unreact([&bot](const stoatpp::events::MessageUnreact& e) {
            try {
                if (e.user_id == bot.current_user().id) return;

            auto rr_msg = g_db->get_rr_message(e.id);
            if (rr_msg) {
                // If it is panel or verify type, we do not remove/add back anything when they unreact
                if (rr_msg->type == "panel" || rr_msg->type == "verify") {
                    return;
                }

                std::vector<Database::RRRule> match_rules;
                for (const auto& r : rr_msg->rules) {
                    if (r.emoji_id == e.emoji_id) {
                        match_rules.push_back(r);
                    }
                }

                for (const auto& rule : match_rules) {
                    std::string role_name = rule.role_id;
                    auto server_opt = bot.get_server(rr_msg->server_id);
                    if (server_opt) {
                        for (const auto& r : server_opt->roles) {
                            if (r.id == rule.role_id) {
                                role_name = r.name;
                                break;
                            }
                        }
                    }

                    if (rule.rule_type == "remove") {
                        // The opposite of remove is ADD
                        std::cout << "[bronx rr] adding role back " << rule.role_id << " to user " << e.user_id << "\n";
                        bot.add_role_to_member(rr_msg->server_id, e.user_id, rule.role_id, [&bot, e, rule, role_name, server_opt, rr_msg](bool success, std::string err) {
                            std::cout << "[bronx rr] role add " << (success ? "succeeded" : "failed") 
                                      << " for user " << e.user_id << " (role: " << rule.role_id << ") error: " << err << "\n";
                            if (!success) {
                                bot.send_message(e.channel_id, "⚠️ failed to assign role **" + role_name + "**. make sure i have the **Manage Roles** permission and my role is positioned above it.");
                            }
                        });
                    } else {
                        // The opposite of add is REMOVE
                        std::cout << "[bronx rr] removing role " << rule.role_id << " from user " << e.user_id << "\n";
                        bot.remove_role_from_member(rr_msg->server_id, e.user_id, rule.role_id, [&bot, e, rule, role_name, server_opt, rr_msg](bool success, std::string err) {
                            std::cout << "[bronx rr] role remove " << (success ? "succeeded" : "failed") 
                                      << " for user " << e.user_id << " (role: " << rule.role_id << ") error: " << err << "\n";
                            if (!success) {
                                if (err == "NotElevated" || err == "HierarchyError") {
                                    bot.send_message(e.channel_id, "⚠️ hierarchy error: i couldn't remove the **" + role_name + "** role from you in `" + (server_opt ? server_opt->name : "server") + "`. my role needs to be placed *above* the roles I am giving out. please contact an admin.");
                                } else {
                                    bot.send_message(e.channel_id, "⚠️ failed to remove role **" + role_name + "**. make sure i have the **Manage Roles** permission and my role is positioned above it in the server settings.");
                                }
                            }
                        });
                    }
                }
            }
            } catch (const std::exception& err) {
                std::cout << "[bronx rr error] Exception in on_message_unreact: " << err.what() << "\n";
            }
        });
    }
};

} // namespace bronx::cogs
