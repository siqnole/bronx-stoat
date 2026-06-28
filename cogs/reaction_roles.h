#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
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
                emoji = text.substr(i, close - i + 1);
                i = close + 1;
            }
        }
        
        // If not custom emoji, check if next sequence represents a unicode emoji / symbol
        if (emoji.empty()) {
            unsigned char c = text[i];
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
        
        // Strip optional prefixes like '-' or '>'
        while (!role_query.empty() && (role_query[0] == '-' || role_query[0] == '>' || std::isspace(role_query[0]))) {
            role_query = role_query.substr(1);
            role_query.erase(0, role_query.find_first_not_of(" \t\r\n"));
        }

        if (role_query.empty()) continue;
        
        auto server_opt = bot.get_server(server_id);
        if (server_opt) {
            std::string resolved_role_id = "";
            std::string resolved_role_name = "";
            
            if (role_query.rfind("<@&", 0) == 0 && role_query.back() == '>') {
                std::string rid = role_query.substr(3, role_query.size() - 4);
                for (const auto& r : server_opt->roles) {
                    if (r.id == rid) {
                        resolved_role_id = r.id;
                        resolved_role_name = r.name;
                        break;
                    }
                }
            } else {
                for (const auto& r : server_opt->roles) {
                    if (r.id == role_query || r.name == role_query) {
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
                    bot.send_message(msg.channel_id, "usage: `" + pfx + "rr <create|send|add|edit|parse|list|delete> [args...]`\n"
                                                     "examples:\n"
                                                     "• `" + pfx + "rr create verified`\n"
                                                     "• `" + pfx + "rr send verified #general`\n"
                                                     "• `" + pfx + "rr add verified :thumbsup: Member`\n"
                                                     "• `" + pfx + "rr edit verified :thumbsup: emoji :heart:`\n"
                                                     "• `" + pfx + "rr parse <message_id>`\n"
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

                // Verify Server Owner
                auto server_opt = bot.get_server(msg.server_id);
                if (!server_opt) {
                    bot.send_message(msg.channel_id, "failed to get server info.");
                    return;
                }
                if (server_opt->owner != msg.author.id) {
                    bot.send_message(msg.channel_id, "permission error: only the server owner can manage reaction roles.");
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

                    stoatpp::models::MessagePayload payload;
                    nlohmann::json embed = nlohmann::json::object();
                    embed["title"] = "reaction roles: " + modalname;
                    embed["colour"] = "#6C5CE7";
                    std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                    embed["description"] = "react below to obtain roles:\n*(no roles configured yet. use `" + pfx + "rr add " + modalname + " <emoji> <role>` to add mapping)*";
                    payload.embeds.push_back(embed);

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

                    stoatpp::models::MessagePayload payload;
                    nlohmann::json embed = nlohmann::json::object();
                    embed["title"] = "reaction roles: " + modalname;
                    embed["colour"] = "#6C5CE7";

                    std::string desc = "react below to obtain roles:\n\n";
                    if (!rr_msg_opt->rules.empty()) {
                        for (const auto& rule : rr_msg_opt->rules) {
                            desc += "• " + rule.emoji_id + " ➔ <@&" + rule.role_id + ">\n";
                        }
                    } else {
                        std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                        desc += "*(no roles configured yet. use `" + pfx + "rr add " + modalname + " <emoji> <role>` to add mapping)*";
                    }
                    embed["description"] = desc;
                    payload.embeds.push_back(embed);

                    bot.send_message(target_channel, payload, [modalname, msg, target_channel, rr_msg_opt, &bot](stoatpp::models::Message res, bool ok) {
                        if (ok) {
                            g_db->add_rr_message(res.id, target_channel, msg.server_id, modalname);
                            std::cout << "[bronx rr] sent additional reaction role modal: " << modalname << " (message: " << res.id << ")\n";
                            
                            // Copy rules to database and cache, and react
                            for (const auto& rule : rr_msg_opt->rules) {
                                g_db->add_rr_rule(res.id, rule.emoji_id, rule.role_id);
                                bot.react_to_message(target_channel, res.id, rule.emoji_id);
                                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            }
                        }
                    });
                    return;
                }

                if (subcommand == "add") {
                    if (args.size() < 4) {
                        std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "rr add <modalname> <emoji> <role>` (or list multiple)");
                        return;
                    }
                    std::string modalname = args[1];

                    std::vector<std::string> msg_ids = g_db->get_message_ids_by_modal(msg.server_id, modalname);
                    if (msg_ids.empty()) {
                        bot.send_message(msg.channel_id, "could not find any active modals named `" + modalname + "` in this server.");
                        return;
                    }

                    // Parse all mapping pairs from remainder string
                    std::string rest = "";
                    for (size_t i = 2; i < args.size(); ++i) {
                        rest += args[i] + " ";
                    }

                    std::vector<EmojiRolePair> parsed = parse_emoji_role_pairs(rest, bot, msg.server_id);
                    if (parsed.empty()) {
                        bot.send_message(msg.channel_id, "failed to parse any valid emoji-to-role mappings. check names/mentions.");
                        return;
                    }

                    // Add mapping to all instances of this modal
                    for (const auto& target_msg_id : msg_ids) {
                        auto rr_msg_opt = g_db->get_rr_message(target_msg_id);
                        if (!rr_msg_opt) continue;

                        for (const auto& pair : parsed) {
                            if (g_db->add_rr_rule(target_msg_id, pair.emoji, pair.role_id)) {
                                bot.react_to_message(rr_msg_opt->channel_id, target_msg_id, pair.emoji);
                                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            }
                        }

                        // Reload rules and rebuild embed description
                        auto updated_rr = g_db->get_rr_message(target_msg_id);
                        if (updated_rr) {
                            stoatpp::models::MessagePayload payload;
                            nlohmann::json embed = nlohmann::json::object();
                            embed["title"] = "reaction roles: " + modalname;
                            embed["colour"] = "#6C5CE7";

                            std::string desc = "react below to obtain roles:\n\n";
                            for (const auto& rule : updated_rr->rules) {
                                desc += "• " + rule.emoji_id + " ➔ <@&" + rule.role_id + ">\n";
                            }
                            embed["description"] = desc;
                            payload.embeds.push_back(embed);
                            payload.content = " ";

                            bot.edit_message(updated_rr->channel_id, target_msg_id, payload);
                        }
                    }

                    bot.send_message(msg.channel_id, "successfully added reaction roles to all instances of `" + modalname + "`.");
                    return;
                }

                if (subcommand == "edit") {
                    if (args.size() < 5) {
                        std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                        bot.send_message(msg.channel_id, "usages:\n"
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

                            g_db->remove_rr_rule(target_msg_id, old_parsed[0].emoji);
                            g_db->add_rr_rule(target_msg_id, new_parsed[0].emoji, new_parsed[0].role_id);

                            bot.unreact_from_message(rr_msg_opt->channel_id, target_msg_id, old_parsed[0].emoji, bot.current_user().id);
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            bot.react_to_message(rr_msg_opt->channel_id, target_msg_id, new_parsed[0].emoji);
                            updated = true;
                        }

                        if (updated) {
                            auto updated_rr = g_db->get_rr_message(target_msg_id);
                            if (updated_rr) {
                                stoatpp::models::MessagePayload payload;
                                nlohmann::json embed = nlohmann::json::object();
                                embed["title"] = "reaction roles: " + modalname;
                                embed["colour"] = "#6C5CE7";

                                std::string desc = "react below to obtain roles:\n\n";
                                for (const auto& rule : updated_rr->rules) {
                                    desc += "• " + rule.emoji_id + " ➔ <@&" + rule.role_id + ">\n";
                                }
                                embed["description"] = desc;
                                payload.embeds.push_back(embed);
                                payload.content = " ";

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
                            std::transform(content.begin(), content.end(), content.begin(), ::tolower);

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
                        bot.send_message(msg.channel_id, "usage: `!rr parse <message_id>`");
                        return;
                    }
                    std::string target_msg_id = args[1];

                    // Fetch the target message content
                    std::thread([&bot, msg, target_msg_id]() {
                        auto res = bot.rest().get("/channels/" + msg.channel_id + "/messages/" + target_msg_id);
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

                        if (rules.empty()) {
                            bot.send_reply(msg, "no reaction role rules detected in target message.\n"
                                                "make sure patterns write like:\n"
                                                "• `:emoji: @role`\n"
                                                "• `:emoji: - @role`\n"
                                                "• `:emoji: > @role`", true);
                            return;
                        }

                        // Send interactive confirmation
                        stoatpp::models::MessagePayload confirm_payload;
                        nlohmann::json embed = nlohmann::json::object();
                        embed["title"] = "reaction roles scanner";
                        embed["colour"] = "#00CEC9";

                        std::string desc = "i detected the following reaction roles rules in message `" + target_msg_id + "`:\n\n";
                        for (const auto& r : rules) {
                            desc += "• " + r.emoji + " ➔ <@&" + r.role_id + ">\n";
                        }
                        desc += "\nreact with ✅ below to confirm and activate, or ❌ to cancel.";
                        embed["description"] = desc;
                        confirm_payload.embeds.push_back(embed);

                        bot.send_message(msg.channel_id, confirm_payload, [target_msg_id, rules, msg, &bot](stoatpp::models::Message confirm_res, bool ok) {
                            if (ok) {
                                {
                                    std::lock_guard<std::mutex> lock(g_pending_scans_mutex);
                                    PendingScan scan;
                                    scan.target_message_id = target_msg_id;
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
            }
        });

        // Gateway event handling for role assignments
        bot.on_message_react([&bot](const stoatpp::events::MessageReact& e) {
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
                // Find matching emoji
                std::string match_role = "";
                for (const auto& r : rr_msg->rules) {
                    if (r.emoji_id == e.emoji_id) {
                        match_role = r.role_id;
                        break;
                    }
                }

                if (!match_role.empty()) {
                    std::cout << "[bronx rr] adding role " << match_role << " to user " << e.user_id << "\n";
                    bot.add_role_to_member(rr_msg->server_id, e.user_id, match_role);
                }
            }
        });

        bot.on_message_unreact([&bot](const stoatpp::events::MessageUnreact& e) {
            if (e.user_id == bot.current_user().id) return;

            auto rr_msg = g_db->get_rr_message(e.id);
            if (rr_msg) {
                std::string match_role = "";
                for (const auto& r : rr_msg->rules) {
                    if (r.emoji_id == e.emoji_id) {
                        match_role = r.role_id;
                        break;
                    }
                }

                if (!match_role.empty()) {
                    std::cout << "[bronx rr] removing role " << match_role << " from user " << e.user_id << "\n";
                    bot.remove_role_from_member(rr_msg->server_id, e.user_id, match_role);
                }
            }
        });
    }
};

} // namespace bronx::cogs
