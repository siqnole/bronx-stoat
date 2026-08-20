#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "command_config.h"
#include <sstream>
#include <algorithm>
#include <random>

namespace bronx::cogs {

class WelcomeLeaveModule : public stoatpp::bot_module {
private:
    static void send_verbose_test_message(stoatpp::cluster& bot, const std::string& target_channel, const stoatpp::models::MessagePayload& payload, const std::string& cmd_channel, const std::string& success_msg, const std::string& error_prefix) {
        std::thread([&bot, target_channel, payload, cmd_channel, success_msg, error_prefix]() {
            auto res = bot.rest().create_message(target_channel, payload);
            if (res.success()) {
                bot.send_message(cmd_channel, success_msg);
            } else {
                std::string err_msg = "" + bronx::emj_warning + " " + error_prefix;
                if (res.status_code == 403 || res.status_code == 404) {
                    err_msg += "\n*Details: Make sure the bot is in that channel and has View Channel and Send Messages permissions (HTTP " + std::to_string(res.status_code) + ").*";
                } else if (res.body.is_object() && res.body.contains("error") && res.body["error"].is_string()) {
                    err_msg += "\n*Details: " + res.body["error"].get<std::string>() + "*";
                } else if (!res.body.is_null()) {
                    err_msg += "\n*Details: " + res.body.dump() + "*";
                } else {
                    err_msg += " (HTTP " + std::to_string(res.status_code) + ")";
                }
                bot.send_message(cmd_channel, err_msg);
            }
        }).detach();
    }

    static std::string format_placeholders(std::string text, const stoatpp::models::User& user, const stoatpp::models::Server& server, int64_t member_count) {
        std::string mention = "<@" + user.id + ">";
        
        auto replace_all = [](std::string& str, const std::string& from, const std::string& to) {
            size_t start_pos = 0;
            while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
                str.replace(start_pos, from.length(), to);
                start_pos += to.length();
            }
        };

        replace_all(text, "{user}", mention);
        replace_all(text, "{member}", mention);
        replace_all(text, "{username}", user.username);
        replace_all(text, "{server}", server.name);
        replace_all(text, "{membercount}", std::to_string(member_count));
        
        return text;
    }

    static std::unordered_map<std::string, std::string> parse_pairs(const std::string& text) {
        std::unordered_map<std::string, std::string> pairs;
        std::vector<std::string> keys = {"title:", "description:", "desc:", "colour:", "color:", "image:", "content:"};
        
        struct KeyPos {
            std::string key;
            size_t pos;
        };
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
        
        std::sort(occurrences.begin(), occurrences.end(), [](const KeyPos& a, const KeyPos& b) {
            return a.pos < b.pos;
        });
        
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

    static std::string get_raw_trailing_args(const std::string& content, const std::string& prefix, const std::string& command_name, const std::string& subcommand) {
        std::string search1 = prefix + command_name;
        std::string lower_content = content;
        std::transform(lower_content.begin(), lower_content.end(), lower_content.begin(), ::tolower);
        
        std::string lower_search1 = search1;
        std::transform(lower_search1.begin(), lower_search1.end(), lower_search1.begin(), ::tolower);
        
        size_t pos = lower_content.find(lower_search1);
        if (pos == std::string::npos) return "";
        
        pos += lower_search1.length();
        while (pos < content.length() && std::isspace(content[pos])) {
            pos++;
        }
        
        std::string lower_sub = subcommand;
        std::transform(lower_sub.begin(), lower_sub.end(), lower_sub.begin(), ::tolower);
        
        if (pos + lower_sub.length() <= content.length() && lower_content.compare(pos, lower_sub.length(), lower_sub) == 0) {
            pos += lower_sub.length();
            if (pos < content.length() && std::isspace(content[pos])) {
                pos++;
            }
            return content.substr(pos);
        }
        
        return "";
    }

public:
    void register_handlers(stoatpp::cluster& bot) override {
        // --- WELCOME COMMAND ---
        bot.register_command({
            "welcome",
            {},
            "manage welcome messages for new members.",
            "",
            {"<subcommand>", "[args...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling welcome...\n";
                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " This command can only be used in a server channel.");
                    return;
                }

                if (!g_db) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Database is unavailable.");
                    return;
                }

                // Check Manage Server permission
                bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, args](stoatpp::models::Member member, bool success) {
                    if (!success) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to verify your permissions.");
                        return;
                    }
                    if (!is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, member)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Permission error: you need Manage Server permission to configure welcome messages.");
                        return;
                    }

                std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                if (args.empty()) {
                    bot.send_message(msg.channel_id, "📋 **Welcome Configuration Subcommands:**\n"
                                                     "• `" + pfx + "welcome setup` - Step-by-step interactive setup wizard.\n"
                                                     "• `" + pfx + "welcome channel <#channel|none>` - Configure welcomer channel.\n"
                                                     "• `" + pfx + "welcome role <role>` - Set custom autorole to assign on join.\n"
                                                     "• `" + pfx + "welcome type <text|embed>` - Set type.\n"
                                                     "• `" + pfx + "welcome message <text...>` - Set greeting message body.\n"
                                                     "• `" + pfx + "welcome edit <title:val desc:val color:val image:val>` - Customize embed fields.\n"
                                                     "• `" + pfx + "welcome json <raw_json>` - Import full embed JSON.\n"
                                                     "• `" + pfx + "welcome test` - Send a simulated greet test.\n"
                                                     "• `" + pfx + "welcome dm <text|embed|none>` - Configure welcome DM type.\n"
                                                     "• `" + pfx + "welcome dmmessage <text...>` - Set welcome DM message body.\n"
                                                     "• `" + pfx + "welcome dmedit <key:val...>` - Customize welcome DM embed fields.\n"
                                                     "• `" + pfx + "welcome dmjson <raw_json>` - Import welcome DM embed JSON.\n"
                                                     "• `" + pfx + "welcome dmtest` - Send a simulated welcome DM test to yourself.");
                    return;
                }

                std::string subcommand = args[0];

                if (subcommand == "role") {
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "welcome role <roleName|roleID|none>`");
                        return;
                    }

                    std::string target = "";
                    for (size_t i = 1; i < args.size(); ++i) {
                        if (i > 1) target += " ";
                        target += args[i];
                    }

                    // Strip <%roleId> mention wrapper if present (Stoat role mention format)
                    if (target.rfind("<%", 0) == 0 && target.back() == '>') {
                        target = target.substr(2, target.size() - 3);
                    }

                    auto config = g_db->get_welcome_leave_config(msg.server_id).value_or(Database::WelcomeLeaveConfig{msg.server_id});

                    if (target == "none" || target == "clear" || target == "null") {
                        config.autorole_role_id = "";
                        if (g_db->save_welcome_leave_config(config)) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Autorole disabled for this server.");
                        } else {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save configuration.");
                        }
                        return;
                    }

                    // Search for role
                    auto server_roles_res = bot.rest().get_server(msg.server_id);
                    if (!server_roles_res.success()) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to fetch server roles.");
                        return;
                    }

                    auto server_obj = server_roles_res.body;
                    std::vector<std::pair<std::string, std::string>> matching_roles; // ID -> Name
                    if (server_obj.contains("roles") && server_obj["roles"].is_object()) {
                        for (auto& [role_id, role_json] : server_obj["roles"].items()) {
                            std::string role_name = role_json.contains("name") ? role_json["name"].get<std::string>() : "";
                            
                            std::string norm_target = target;
                            std::string norm_name = role_name;
                            std::string norm_id = role_id;
                            std::transform(norm_target.begin(), norm_target.end(), norm_target.begin(), ::tolower);
                            std::transform(norm_name.begin(), norm_name.end(), norm_name.begin(), ::tolower);
                            std::transform(norm_id.begin(), norm_id.end(), norm_id.begin(), ::tolower);

                            if (norm_id == norm_target || norm_name == norm_target) {
                                matching_roles.clear();
                                matching_roles.push_back({role_id, role_name});
                                break;
                            }

                            if (norm_name.find(norm_target) != std::string::npos) {
                                matching_roles.push_back({role_id, role_name});
                            }
                        }
                    }

                    if (matching_roles.empty()) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " No roles matching `" + target + "` found.");
                        return;
                    }

                    auto execute_save = [&bot, msg, config](std::string role_id, std::string role_name) {
                        auto save_cfg = config;
                        save_cfg.autorole_role_id = role_id;
                        if (g_db->save_welcome_leave_config(save_cfg)) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Welcome autorole successfully set to **" + role_name + "** (" + role_id + ").");
                        } else {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save configuration.");
                        }
                    };

                    if (matching_roles.size() == 1) {
                        execute_save(matching_roles[0].first, matching_roles[0].second);
                        return;
                    }

                    // Conflict resolution
                    stoatpp::models::MessagePayload payload;
                    nlohmann::json embed = nlohmann::json::object();
                    embed["title"] = "I found some conflicts";
                    embed["colour"] = "#ff5555";
                    
                    std::string desc = "";
                    for (size_t i = 0; i < matching_roles.size(); ++i) {
                        desc += std::to_string(i + 1) + ". " + matching_roles[i].second + "\n";
                    }
                    desc += "\nyou only asked for \"" + target + "\"... which should I choose?\n*please reply with a number within 30 seconds!*";
                    embed["description"] = desc;
                    payload.embeds.push_back(embed);

                    bot.send_message(msg.channel_id, payload, [&bot, msg, matching_roles, execute_save](stoatpp::models::Message prompt_msg, bool ok) {
                        if (!ok) return;

                        bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30), [&bot, msg, prompt_msg, matching_roles, execute_save](std::optional<stoatpp::events::Message> reply) {
                            bot.delete_message(msg.channel_id, prompt_msg.id);
                            if (reply) {
                                bot.delete_message(msg.channel_id, reply->id);
                                try {
                                    int index = std::stoi(reply->content) - 1;
                                    if (index >= 0 && index < (int)matching_roles.size()) {
                                        execute_save(matching_roles[index].first, matching_roles[index].second);
                                        return;
                                    }
                                } catch (...) {}
                            }
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid selection or timed out.");
                        });
                    });
                    return;
                }

                if (subcommand == "channel") {
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "welcome channel <#channel|channelID|none>`");
                        return;
                    }

                    std::string target_channel = "";
                    if (args[1] != "none") {
                        if (args[1].rfind("<#", 0) == 0 && args[1].back() == '>') {
                            target_channel = args[1].substr(2, args[1].size() - 3);
                        } else {
                            target_channel = args[1];
                        }
                    }

                    auto config = g_db->get_welcome_leave_config(msg.server_id).value_or(Database::WelcomeLeaveConfig{msg.server_id});
                    config.welcome_channel_id = target_channel;

                    if (g_db->save_welcome_leave_config(config)) {
                        if (target_channel.empty()) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Welcome messages have been disabled.");
                        } else {
                            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Welcome messages channel set to <#" + target_channel + ">.");
                        }
                    } else {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save configuration to database.");
                    }
                    return;
                }

                if (subcommand == "type") {
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "welcome type <text|embed>`");
                        return;
                    }

                    std::string type = args[1];
                    if (type != "text" && type != "embed") {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Type must be `text` or `embed`.");
                        return;
                    }

                    auto config = g_db->get_welcome_leave_config(msg.server_id).value_or(Database::WelcomeLeaveConfig{msg.server_id});
                    config.welcome_type = type;

                    if (g_db->save_welcome_leave_config(config)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Welcome message type set to **" + type + "**.");
                    } else {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save configuration to database.");
                    }
                    return;
                }

                if (subcommand == "message") {
                    std::string text = get_raw_trailing_args(msg.content, pfx, "welcome", "message");
                    if (text.empty()) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "welcome message <text...>`");
                        return;
                    }

                    auto config = g_db->get_welcome_leave_config(msg.server_id).value_or(Database::WelcomeLeaveConfig{msg.server_id});
                    config.welcome_content = text;

                    if (g_db->save_welcome_leave_config(config)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Welcome message content updated successfully.");
                    } else {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save configuration to database.");
                    }
                    return;
                }

                if (subcommand == "edit") {
                    std::string text = get_raw_trailing_args(msg.content, pfx, "welcome", "edit");
                    if (text.empty()) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "welcome edit key:value ...`\nkeys: `title`, `description`, `color`, `image`, `content`\nUse `none` to clear a field.\nExample:\n• `" + pfx + "welcome edit title:Welcome {user}! color:#5865f2`\n• `" + pfx + "welcome edit content:{user} just joined! title:none`");
                        return;
                    }

                    auto pairs = parse_pairs(text);
                    if (pairs.empty()) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " No valid key-value pairs found. Format must be `key:value` (e.g. `title:Hello color:random`).");
                        return;
                    }

                    auto config = g_db->get_welcome_leave_config(msg.server_id).value_or(Database::WelcomeLeaveConfig{msg.server_id});
                    nlohmann::json emb = nlohmann::json::object();
                    if (!config.welcome_embed_json.empty()) {
                        try {
                            emb = nlohmann::json::parse(config.welcome_embed_json);
                        } catch(...) {}
                    }

                    for (const auto& [key, val] : pairs) {
                        if (key == "content") {
                            // content lives in welcome_content, not the embed JSON
                            if (val == "none" || val == "null" || val == "clear") {
                                config.welcome_content = "";
                            } else {
                                config.welcome_content = val;
                            }
                        } else if (val == "none" || val == "null" || val == "clear") {
                            emb.erase(key);
                        } else {
                            emb[key] = val;
                        }
                    }

                    config.welcome_embed_json = emb.dump();
                    if (g_db->save_welcome_leave_config(config)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Welcome embed configuration updated successfully.");
                    } else {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save configuration to database.");
                    }
                    return;
                }

                if (subcommand == "json") {
                    std::string text = get_raw_trailing_args(msg.content, pfx, "welcome", "json");
                    if (text.empty()) {
                        bot.send_message(msg.channel_id,
                            "usage: `" + pfx + "welcome json <raw_json_embed>`\n"
                            "Imports a full embed JSON to use as the welcome message. Supported fields: `title`, `description`, `color`, `image`.\n"
                            "Available tokens: `{user}` `{username}` `{server}` `{membercount}`\n\n"
                            "**Example:**\n"
                            "```json\n"
                            "{\n"
                            "  \"title\": \"Welcome {username}!\",\n"
                            "  \"description\": \"Hey {user}, welcome to {server}! You are member #{membercount}.\",\n"
                            "  \"color\": \"#5865f2\",\n"
                            "  \"image\": \"https://example.com/banner.png\",\n"
                            "  \"content\": \"{user} just joined!\"\n"
                            "}\n"
                            "```"
                        );
                        return;
                    }

                    nlohmann::json emb;
                    try {
                        emb = nlohmann::json::parse(text);
                    } catch (const std::exception& e) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid JSON format: " + std::string(e.what()));
                        return;
                    }

                    auto config = g_db->get_welcome_leave_config(msg.server_id).value_or(Database::WelcomeLeaveConfig{msg.server_id});

                    // Extract "content" from the JSON (it goes to welcome_content, not the embed)
                    if (emb.contains("content") && emb["content"].is_string()) {
                        config.welcome_content = emb["content"].get<std::string>();
                        emb.erase("content");
                    }

                    if (emb.contains("colour")) {
                        emb["color"] = emb["colour"];
                        emb.erase("colour");
                    }

                    config.welcome_embed_json = emb.dump();
                    if (g_db->save_welcome_leave_config(config)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Welcome embed configuration imported successfully.");
                    } else {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save configuration to database.");
                    }
                    return;
                }

                if (subcommand == "test") {
                    auto config_opt = g_db->get_welcome_leave_config(msg.server_id);
                    if (!config_opt || (config_opt->welcome_channel_id.empty() && config_opt->autorole_role_id.empty())) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Welcomer and autorole features are not set up or are disabled on this server. Use `" + pfx + "welcome setup` or `" + pfx + "welcome role` first.");
                        return;
                    }

                    auto config = *config_opt;

                    // 1. Assign welcome autorole as a test
                    if (!config.autorole_role_id.empty()) {
                        bot.add_role_to_member(msg.server_id, msg.author.id, config.autorole_role_id, [&bot, msg](bool role_ok, std::string err) {
                            if (role_ok) {
                                bot.send_message(msg.channel_id, "ℹ️ [Test] Assigned welcome autorole to you.");
                            } else {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " [Test] Failed to assign welcome autorole: " + err);
                            }
                        });
                    }

                    // 2. Dispatch greeting message
                    if (!config.welcome_channel_id.empty()) {
                        bot.get_member_count(msg.server_id, [&bot, msg, config](int count, bool ok) {
                            int final_count = ok ? count : 1;
                            auto server_opt = bot.get_server(msg.server_id);
                            stoatpp::models::Server server;
                            if (server_opt) {
                                server = *server_opt;
                            } else {
                                server.name = "Test Server";
                            }

                            if (config.welcome_type == "embed") {
                                nlohmann::json emb_json = nlohmann::json::object();
                                if (!config.welcome_embed_json.empty()) {
                                    try {
                                        emb_json = nlohmann::json::parse(config.welcome_embed_json);
                                    } catch(...) {}
                                }

                                std::thread([&bot, config, msg, server, final_count, emb_json]() {
                                    stoatpp::models::MessagePayload payload;
                                    std::string content = "";
                                    if (!config.welcome_content.empty()) {
                                        content = format_placeholders(config.welcome_content, msg.author, server, final_count);
                                    }
                                    payload.content = content;

                                    nlohmann::json embed = nlohmann::json::object();
                                    if (emb_json.contains("title")) {
                                        embed["title"] = format_placeholders(emb_json["title"].get<std::string>(), msg.author, server, final_count);
                                    }
                                    if (emb_json.contains("description")) {
                                        embed["description"] = format_placeholders(emb_json["description"].get<std::string>(), msg.author, server, final_count);
                                    }
                                    if (emb_json.contains("color")) {
                                        std::string col = emb_json["color"].get<std::string>();
                                        if (col == "random") {
                                            char hex[8];
                                            snprintf(hex, sizeof(hex), "#%06x", rand() % 0xFFFFFF);
                                            col = hex;
                                        }
                                        embed["colour"] = col;
                                    }
                                    if (emb_json.contains("image")) {
                                        std::string img_url = format_placeholders(emb_json["image"].get<std::string>(), msg.author, server, final_count);
                                        std::string media_id = bot.rest().upload_from_url(img_url);
                                        if (!media_id.empty()) {
                                            embed["media"] = media_id;
                                        } else {
                                            std::string desc = embed.contains("description") ? embed["description"].get<std::string>() : "";
                                            if (!desc.empty()) desc += "\n";
                                            desc += "![](" + img_url + ")";
                                            embed["description"] = desc;
                                        }
                                    }

                                    if (!embed.empty()) {
                                        payload.embeds.push_back(embed);
                                    }

                                    send_verbose_test_message(bot, config.welcome_channel_id, payload, msg.channel_id,
                                         "" + bronx::emj_yes + " Test welcome message successfully sent to the configured channel.",
                                         "Failed to send test welcome message. Make sure the bot has permission to post in that channel.");
                                }).detach();
                            } else {
                                stoatpp::models::MessagePayload payload;
                                std::string text_body = config.welcome_content.empty() ? "Welcome {name} to {server}!" : config.welcome_content;
                                payload.content = format_placeholders(text_body, msg.author, server, final_count);
                                send_verbose_test_message(bot, config.welcome_channel_id, payload, msg.channel_id,
                                     "" + bronx::emj_yes + " Test welcome message successfully sent to the configured channel.",
                                     "Failed to send test welcome message. Make sure the bot has permission to post in that channel.");
                            }
                        });
                    }
                    return;
                }

                if (subcommand == "dm") {
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "welcome dm <text|embed|none>`");
                        return;
                    }

                    std::string dm_type = args[1];
                    std::transform(dm_type.begin(), dm_type.end(), dm_type.begin(), ::tolower);
                    if (dm_type != "text" && dm_type != "embed" && dm_type != "none") {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " DM welcome type must be `text`, `embed`, or `none`.");
                        return;
                    }

                    auto config = g_db->get_welcome_leave_config(msg.server_id).value_or(Database::WelcomeLeaveConfig{msg.server_id});
                    config.welcome_dm_type = dm_type;

                    if (g_db->save_welcome_leave_config(config)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Welcome DM type set to **" + dm_type + "**.");
                    } else {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save configuration to database.");
                    }
                    return;
                }

                if (subcommand == "dmmessage") {
                    std::string text = get_raw_trailing_args(msg.content, pfx, "welcome", "dmmessage");
                    if (text.empty()) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "welcome dmmessage <text...>`");
                        return;
                    }

                    auto config = g_db->get_welcome_leave_config(msg.server_id).value_or(Database::WelcomeLeaveConfig{msg.server_id});
                    config.welcome_dm_content = text;

                    if (g_db->save_welcome_leave_config(config)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Welcome DM message content updated successfully.");
                    } else {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save configuration to database.");
                    }
                    return;
                }

                if (subcommand == "dmedit") {
                    std::string text = get_raw_trailing_args(msg.content, pfx, "welcome", "dmedit");
                    if (text.empty()) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "welcome dmedit key:value ...`\nkeys: `title`, `description`, `color`, `image`, `content`\nUse `none` to clear a field.");
                        return;
                    }

                    auto pairs = parse_pairs(text);
                    if (pairs.empty()) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " No valid key-value pairs found.");
                        return;
                    }

                    auto config = g_db->get_welcome_leave_config(msg.server_id).value_or(Database::WelcomeLeaveConfig{msg.server_id});
                    nlohmann::json emb = nlohmann::json::object();
                    if (!config.welcome_dm_embed_json.empty()) {
                        try {
                            emb = nlohmann::json::parse(config.welcome_dm_embed_json);
                        } catch(...) {}
                    }

                    for (const auto& [key, val] : pairs) {
                        if (key == "content") {
                            if (val == "none" || val == "null" || val == "clear") {
                                config.welcome_dm_content = "";
                            } else {
                                config.welcome_dm_content = val;
                            }
                        } else if (val == "none" || val == "null" || val == "clear") {
                            emb.erase(key);
                        } else {
                            emb[key] = val;
                        }
                    }

                    config.welcome_dm_embed_json = emb.dump();
                    if (g_db->save_welcome_leave_config(config)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Welcome DM embed configuration updated successfully.");
                    } else {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save configuration to database.");
                    }
                    return;
                }

                if (subcommand == "dmjson") {
                    std::string text = get_raw_trailing_args(msg.content, pfx, "welcome", "dmjson");
                    if (text.empty()) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "welcome dmjson <raw_json_embed>`");
                        return;
                    }

                    nlohmann::json emb;
                    try {
                        emb = nlohmann::json::parse(text);
                    } catch (const std::exception& e) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid JSON format: " + std::string(e.what()));
                        return;
                    }

                    auto config = g_db->get_welcome_leave_config(msg.server_id).value_or(Database::WelcomeLeaveConfig{msg.server_id});

                    if (emb.contains("content") && emb["content"].is_string()) {
                        config.welcome_dm_content = emb["content"].get<std::string>();
                        emb.erase("content");
                    }

                    if (emb.contains("colour")) {
                        emb["color"] = emb["colour"];
                        emb.erase("colour");
                    }

                    config.welcome_dm_embed_json = emb.dump();
                    if (g_db->save_welcome_leave_config(config)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Welcome DM embed configuration imported successfully.");
                    } else {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save configuration to database.");
                    }
                    return;
                }

                if (subcommand == "dmtest") {
                    auto config_opt = g_db->get_welcome_leave_config(msg.server_id);
                    if (!config_opt || config_opt->welcome_dm_type == "none") {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " DM welcome is not enabled or set up. Use `" + pfx + "welcome dm <text|embed>` first.");
                        return;
                    }

                    auto config = *config_opt;
                    bot.get_member_count(msg.server_id, [&bot, msg, config](int count, bool ok) {
                        int final_count = ok ? count : 1;
                        auto server_opt = bot.get_server(msg.server_id);
                        stoatpp::models::Server server;
                        if (server_opt) {
                            server = *server_opt;
                        } else {
                            server.name = "Test Server";
                        }

                        std::thread([&bot, config, msg, server, final_count]() {
                            auto dm_res = bot.rest().open_dm(msg.author.id);
                            if (!dm_res.success() || !dm_res.body.is_object() || !dm_res.body.contains("_id")) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to open DM with you.");
                                return;
                            }
                            std::string dm_channel_id = dm_res.body["_id"].get<std::string>();

                            stoatpp::models::MessagePayload payload;
                            if (config.welcome_dm_type == "embed") {
                                nlohmann::json emb_json = nlohmann::json::object();
                                if (!config.welcome_dm_embed_json.empty()) {
                                    try { emb_json = nlohmann::json::parse(config.welcome_dm_embed_json); } catch(...) {}
                                }

                                std::string content = "";
                                if (!config.welcome_dm_content.empty()) {
                                    content = format_placeholders(config.welcome_dm_content, msg.author, server, final_count);
                                }
                                payload.content = content;

                                nlohmann::json embed = nlohmann::json::object();
                                if (emb_json.contains("title")) {
                                    embed["title"] = format_placeholders(emb_json["title"].get<std::string>(), msg.author, server, final_count);
                                }
                                if (emb_json.contains("description")) {
                                    embed["description"] = format_placeholders(emb_json["description"].get<std::string>(), msg.author, server, final_count);
                                }
                                if (emb_json.contains("color")) {
                                    std::string col = emb_json["color"].get<std::string>();
                                    if (col == "random") {
                                        char hex[8];
                                        snprintf(hex, sizeof(hex), "#%06x", rand() % 0xFFFFFF);
                                        col = hex;
                                    }
                                    embed["colour"] = col;
                                }
                                if (emb_json.contains("image")) {
                                    std::string img_url = format_placeholders(emb_json["image"].get<std::string>(), msg.author, server, final_count);
                                    std::string media_id = bot.rest().upload_from_url(img_url);
                                    if (!media_id.empty()) {
                                        embed["media"] = media_id;
                                    } else {
                                        std::string desc = embed.contains("description") ? embed["description"].get<std::string>() : "";
                                        if (!desc.empty()) desc += "\n";
                                        desc += "![](" + img_url + ")";
                                        embed["description"] = desc;
                                    }
                                }

                                if (!embed.empty()) {
                                    payload.embeds.push_back(embed);
                                }
                            } else {
                                payload.content = format_placeholders(config.welcome_dm_content, msg.author, server, final_count);
                            }

                            send_verbose_test_message(bot, dm_channel_id, payload, msg.channel_id,
                                "" + bronx::emj_yes + " Test welcome DM successfully sent to you.",
                                "Failed to send test welcome DM.");
                        }).detach();
                    });
                    return;
                }

                if (subcommand == "setup") {
                    bot.send_message(msg.channel_id, "💬 **Welcome Setup Wizard**\nLet's configure welcome messages for this server.\n\n**Step 1/7**: Mention a channel (e.g. #general), type its name/ID, or reply `none` to disable.");
                    bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30), [&bot, msg](std::optional<stoatpp::events::Message> rep1) {
                        if (!rep1) {
                            bot.send_message(msg.channel_id, "⏱️ Setup timed out. Welcome setup canceled.");
                            return;
                        }
                        
                        std::string chan_resp = rep1->content;
                        if (chan_resp == "cancel") {
                            bot.send_message(msg.channel_id, "" + bronx::emj_no + " Welcome setup canceled.");
                            return;
                        }
                        std::string target_channel = "";
                        if (chan_resp != "none") {
                            if (chan_resp.rfind("<#", 0) == 0 && chan_resp.back() == '>') {
                                target_channel = chan_resp.substr(2, chan_resp.size() - 3);
                            } else {
                                auto server_channels_res = bot.rest().get_server_channels(msg.server_id);
                                if (server_channels_res.success() && server_channels_res.body.is_array()) {
                                    for (const auto& c_json : server_channels_res.body) {
                                        auto c = stoatpp::models::Channel::from_json(c_json);
                                        if (c.id == chan_resp || c.name == chan_resp) {
                                            target_channel = c.id;
                                            break;
                                        }
                                    }
                                }
                                if (target_channel.empty()) {
                                    target_channel = chan_resp;
                                }
                            }
                        }

                        if (target_channel.empty() && chan_resp != "none") {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid channel. Welcome setup canceled.");
                            return;
                        }

                        if (chan_resp == "none") {
                            auto config = g_db->get_welcome_leave_config(msg.server_id).value_or(Database::WelcomeLeaveConfig{msg.server_id});
                            config.welcome_channel_id = "";
                            g_db->save_welcome_leave_config(config);
                            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Welcome messages have been disabled.");
                            return;
                        }

                        bot.send_message(msg.channel_id, "💬 **Welcome Setup Wizard**\n**Step 2/7**: Choose the message type. Reply with `text` or `embed`.");
                        bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30), [&bot, msg, target_channel](std::optional<stoatpp::events::Message> rep2) {
                            if (!rep2) {
                                bot.send_message(msg.channel_id, "⏱️ Setup timed out. Welcome setup canceled.");
                                return;
                            }

                            std::string type_resp = rep2->content;
                            std::transform(type_resp.begin(), type_resp.end(), type_resp.begin(), ::tolower);
                            if (type_resp == "cancel") {
                                bot.send_message(msg.channel_id, "" + bronx::emj_no + " Welcome setup canceled.");
                                return;
                            }
                            if (type_resp != "text" && type_resp != "embed") {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid type (must be `text` or `embed`). Welcome setup canceled.");
                                return;
                            }

                            if (type_resp == "text") {
                                bot.send_message(msg.channel_id, "💬 **Welcome Setup Wizard**\n**Step 3/3**: Type the welcome message/content.\n*(You can use tokens like `{user}`, `{username}`, `{server}`, and `{membercount}`)*");
                                bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(45), [&bot, msg, target_channel, type_resp](std::optional<stoatpp::events::Message> rep3) {
                                    if (!rep3) {
                                        bot.send_message(msg.channel_id, "⏱️ Setup timed out. Welcome setup canceled.");
                                        return;
                                    }

                                    std::string content_resp = rep3->content;
                                    if (content_resp == "cancel") {
                                        bot.send_message(msg.channel_id, "" + bronx::emj_no + " Welcome setup canceled.");
                                        return;
                                    }
                                    auto config = g_db->get_welcome_leave_config(msg.server_id).value_or(Database::WelcomeLeaveConfig{msg.server_id});
                                    config.welcome_channel_id = target_channel;
                                    config.welcome_type = type_resp;
                                    config.welcome_content = content_resp;

                                    g_db->save_welcome_leave_config(config);
                                    bot.send_message(msg.channel_id, "" + bronx::emj_yes + " **Welcome Setup Complete!**\nWelcome messages will be sent to <#" + target_channel + "> in text format.");
                                });
                                return;
                            }

                            // Embed Setup flow:
                            bot.send_message(msg.channel_id, "💬 **Welcome Setup Wizard**\n**Step 3/7**: Type the embed **title** (or reply `skip`).\n*Available tokens:*\n```\n{user} {username} {server} {membercount}\n```");
                            bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(60), [&bot, msg, target_channel, type_resp](std::optional<stoatpp::events::Message> rep3) {
                                if (!rep3) {
                                    bot.send_message(msg.channel_id, "⏱️ Setup timed out. Welcome setup canceled.");
                                    return;
                                }

                                nlohmann::json emb = nlohmann::json::object();
                                std::string title_resp = rep3->content;
                                if (title_resp == "cancel") {
                                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Welcome setup canceled.");
                                    return;
                                }
                                if (title_resp != "skip") {
                                    emb["title"] = title_resp;
                                }

                                bot.send_message(msg.channel_id, "💬 **Welcome Setup Wizard**\n**Step 4/7**: Type the embed **description** (or reply `skip`).\n*Available tokens:*\n```\n{user} {username} {server} {membercount}\n```");
                                bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(90), [&bot, msg, emb, target_channel, type_resp](std::optional<stoatpp::events::Message> rep4) {
                                    if (!rep4) {
                                        bot.send_message(msg.channel_id, "⏱️ Setup timed out. Welcome setup canceled.");
                                        return;
                                    }

                                    auto current_emb = emb;
                                    std::string desc_resp = rep4->content;
                                    if (desc_resp == "cancel") {
                                        bot.send_message(msg.channel_id, "" + bronx::emj_no + " Welcome setup canceled.");
                                        return;
                                    }
                                    if (desc_resp != "skip") {
                                        current_emb["description"] = desc_resp;
                                    }

                                    bot.send_message(msg.channel_id, "💬 **Welcome Setup Wizard**\n**Step 5/7**: Type the embed **color hex** (e.g. `#5865f2` or `random`, or reply `skip`).");
                                    bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30), [&bot, msg, current_emb, target_channel, type_resp](std::optional<stoatpp::events::Message> rep5) {
                                        if (!rep5) {
                                            bot.send_message(msg.channel_id, "⏱️ Setup timed out. Welcome setup canceled.");
                                            return;
                                        }

                                        auto final_emb = current_emb;
                                        std::string color_resp = rep5->content;
                                        if (color_resp == "cancel") {
                                            bot.send_message(msg.channel_id, "" + bronx::emj_no + " Welcome setup canceled.");
                                            return;
                                        }
                                        if (color_resp != "skip") {
                                            final_emb["color"] = color_resp;
                                        }

                                        bot.send_message(msg.channel_id, "💬 **Welcome Setup Wizard**\n**Step 6/7**: Type the embed **image URL** (or reply `skip`).");
                                        bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(45), [&bot, msg, final_emb, target_channel, type_resp](std::optional<stoatpp::events::Message> rep6) {
                                            if (!rep6) {
                                                bot.send_message(msg.channel_id, "⏱️ Setup timed out. Welcome setup canceled.");
                                                return;
                                            }

                                            auto save_emb = final_emb;
                                            std::string img_resp = rep6->content;
                                            if (img_resp == "cancel") {
                                                bot.send_message(msg.channel_id, "" + bronx::emj_no + " Welcome setup canceled.");
                                                return;
                                            }
                                            if (img_resp != "skip") {
                                                save_emb["image"] = img_resp;
                                            }

                                            bot.send_message(msg.channel_id, "💬 **Welcome Setup Wizard**\n**Step 7/7**: Type the **message content/text** to show outside the embed (or reply `skip`).\n*(You can use tokens like `{user}`, `{username}`, `{server}`, and `{membercount}`)*");
                                            bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(45), [&bot, msg, save_emb, target_channel, type_resp](std::optional<stoatpp::events::Message> rep7) {
                                                if (!rep7) {
                                                    bot.send_message(msg.channel_id, "⏱️ Setup timed out. Welcome setup canceled.");
                                                    return;
                                                }

                                                std::string content_resp = rep7->content;
                                                if (content_resp == "cancel") {
                                                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Welcome setup canceled.");
                                                    return;
                                                }
                                                auto config = g_db->get_welcome_leave_config(msg.server_id).value_or(Database::WelcomeLeaveConfig{msg.server_id});
                                                config.welcome_channel_id = target_channel;
                                                config.welcome_type = type_resp;
                                                config.welcome_embed_json = save_emb.dump();
                                                if (content_resp != "skip") {
                                                    config.welcome_content = content_resp;
                                                } else {
                                                    config.welcome_content = "";
                                                }

                                                g_db->save_welcome_leave_config(config);
                                                bot.send_message(msg.channel_id, "" + bronx::emj_yes + " **Welcome Setup Complete!**\nWelcome messages will be sent to <#" + target_channel + "> as a rich embed.");
                                            });
                                        });
                                    });
                                });
                            });
                        });
                    });
                    return;
                }

                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Unknown subcommand. Use setup, channel, role, type, message, edit, json, test, dm, dmmessage, dmedit, dmjson, or dmtest.");
                });
            },
            "Welcome",
            0,
            0,
            {"setup", "channel <#channel|none>", "role <role>", "type <text|embed>", "message <text...>", "edit <key:val...>", "json <raw_json>", "test", "dm <text|embed|none>", "dmmessage <text...>", "dmedit <key:val...>", "dmjson <raw_json>", "dmtest"}
        });

        // --- LEAVE COMMAND ---
        bot.register_command({
            "leave",
            {},
            "manage leave messages for members who depart.",
            "",
            {"<subcommand>", "[args...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling leave...\n";
                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " This command can only be used in a server channel.");
                    return;
                }

                if (!g_db) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Database is unavailable.");
                    return;
                }

                // Check Manage Server permission
                bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, args](stoatpp::models::Member member, bool success) {
                    if (!success) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to verify your permissions.");
                        return;
                    }
                    if (!is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, member)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Permission error: you need Manage Server permission to configure leave messages.");
                        return;
                    }

                std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                if (args.empty()) {
                    bot.send_message(msg.channel_id, "📋 **Leave Configuration Subcommands:**\n"
                                                     "• `" + pfx + "leave setup` - Step-by-step interactive setup wizard.\n"
                                                     "• `" + pfx + "leave channel <#channel|none>` - Configure leave channel.\n"
                                                     "• `" + pfx + "leave type <text|embed>` - Set type.\n"
                                                     "• `" + pfx + "leave message <text...>` - Set departure message body.\n"
                                                     "• `" + pfx + "leave edit <title:val desc:val color:val image:val>` - Customize embed fields.\n"
                                                     "• `" + pfx + "leave json <raw_json>` - Import full embed JSON.\n"
                                                     "• `" + pfx + "leave test` - Send a simulated depart test.");
                    return;
                }

                std::string subcommand = args[0];

                if (subcommand == "channel") {
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "leave channel <#channel|channelID|none>`");
                        return;
                    }

                    std::string target_channel = "";
                    if (args[1] != "none") {
                        if (args[1].rfind("<#", 0) == 0 && args[1].back() == '>') {
                            target_channel = args[1].substr(2, args[1].size() - 3);
                        } else {
                            target_channel = args[1];
                        }
                    }

                    auto config = g_db->get_welcome_leave_config(msg.server_id).value_or(Database::WelcomeLeaveConfig{msg.server_id});
                    config.leave_channel_id = target_channel;

                    if (g_db->save_welcome_leave_config(config)) {
                        if (target_channel.empty()) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Leave messages have been disabled.");
                        } else {
                            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Leave messages channel set to <#" + target_channel + ">.");
                        }
                    } else {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save configuration to database.");
                    }
                    return;
                }

                if (subcommand == "type") {
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "leave type <text|embed>`");
                        return;
                    }

                    std::string type = args[1];
                    if (type != "text" && type != "embed") {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Type must be `text` or `embed`.");
                        return;
                    }

                    auto config = g_db->get_welcome_leave_config(msg.server_id).value_or(Database::WelcomeLeaveConfig{msg.server_id});
                    config.leave_type = type;

                    if (g_db->save_welcome_leave_config(config)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Leave message type set to **" + type + "**.");
                    } else {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save configuration to database.");
                    }
                    return;
                }

                if (subcommand == "message") {
                    std::string text = get_raw_trailing_args(msg.content, pfx, "leave", "message");
                    if (text.empty()) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "leave message <text...>`");
                        return;
                    }

                    auto config = g_db->get_welcome_leave_config(msg.server_id).value_or(Database::WelcomeLeaveConfig{msg.server_id});
                    config.leave_content = text;

                    if (g_db->save_welcome_leave_config(config)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Leave message content updated successfully.");
                    } else {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save configuration to database.");
                    }
                    return;
                }

                if (subcommand == "edit") {
                    std::string text = get_raw_trailing_args(msg.content, pfx, "leave", "edit");
                    if (text.empty()) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "leave edit key:value ...`\nkeys: `title`, `description`, `color`, `image`, `content`\nUse `none` to clear a field.\nExample:\n• `" + pfx + "leave edit title:Goodbye {username}! color:#ff5555`\n• `" + pfx + "leave edit content:{user} has left. title:none`");
                        return;
                    }

                    auto pairs = parse_pairs(text);
                    if (pairs.empty()) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " No valid key-value pairs found. Format must be `key:value` (e.g. `title:Bye color:random`).");
                        return;
                    }

                    auto config = g_db->get_welcome_leave_config(msg.server_id).value_or(Database::WelcomeLeaveConfig{msg.server_id});
                    nlohmann::json emb = nlohmann::json::object();
                    if (!config.leave_embed_json.empty()) {
                        try {
                            emb = nlohmann::json::parse(config.leave_embed_json);
                        } catch(...) {}
                    }

                    for (const auto& [key, val] : pairs) {
                        if (key == "content") {
                            // content lives in leave_content, not the embed JSON
                            if (val == "none" || val == "null" || val == "clear") {
                                config.leave_content = "";
                            } else {
                                config.leave_content = val;
                            }
                        } else if (val == "none" || val == "null" || val == "clear") {
                            emb.erase(key);
                        } else {
                            emb[key] = val;
                        }
                    }

                    config.leave_embed_json = emb.dump();
                    if (g_db->save_welcome_leave_config(config)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Leave embed configuration updated successfully.");
                    } else {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save configuration to database.");
                    }
                    return;
                }

                if (subcommand == "json") {
                    std::string text = get_raw_trailing_args(msg.content, pfx, "leave", "json");
                    if (text.empty()) {
                        bot.send_message(msg.channel_id,
                            "usage: `" + pfx + "leave json <raw_json_embed>`\n"
                            "Imports a full embed JSON to use as the departure message. Supported fields: `title`, `description`, `color`, `image`.\n"
                            "Available tokens: `{user}` `{username}` `{server}` `{membercount}`\n\n"
                            "**Example:**\n"
                            "```json\n"
                            "{\n"
                            "  \"title\": \"Goodbye {username}!\",\n"
                            "  \"description\": \"{user} has left {server}. We now have {membercount} members.\",\n"
                            "  \"color\": \"#ff5555\",\n"
                            "  \"image\": \"https://example.com/banner.png\",\n"
                            "  \"content\": \"{user} has left.\"\n"
                            "}\n"
                            "```"
                        );
                        return;
                    }

                    nlohmann::json emb;
                    try {
                        emb = nlohmann::json::parse(text);
                    } catch (const std::exception& e) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid JSON format: " + std::string(e.what()));
                        return;
                    }

                    auto config = g_db->get_welcome_leave_config(msg.server_id).value_or(Database::WelcomeLeaveConfig{msg.server_id});

                    // Extract "content" from the JSON (it goes to leave_content, not the embed)
                    if (emb.contains("content") && emb["content"].is_string()) {
                        config.leave_content = emb["content"].get<std::string>();
                        emb.erase("content");
                    }

                    if (emb.contains("colour")) {
                        emb["color"] = emb["colour"];
                        emb.erase("colour");
                    }

                    config.leave_embed_json = emb.dump();
                    if (g_db->save_welcome_leave_config(config)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Leave embed configuration imported successfully.");
                    } else {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save configuration to database.");
                    }
                    return;
                }

                if (subcommand == "test") {
                    auto config_opt = g_db->get_welcome_leave_config(msg.server_id);
                    if (!config_opt || config_opt->leave_channel_id.empty()) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Leave messages are not set up or are disabled on this server. Use `" + pfx + "leave setup` first.");
                        return;
                    }

                    auto config = *config_opt;
                    bot.get_member_count(msg.server_id, [&bot, msg, config](int count, bool ok) {
                        int final_count = ok ? count : 1;
                        auto server_opt = bot.get_server(msg.server_id);
                        stoatpp::models::Server server;
                        if (server_opt) {
                            server = *server_opt;
                        } else {
                            server.name = "Test Server";
                        }

                            if (config.leave_type == "embed") {
                                nlohmann::json emb_json = nlohmann::json::object();
                                if (!config.leave_embed_json.empty()) {
                                    try {
                                        emb_json = nlohmann::json::parse(config.leave_embed_json);
                                    } catch(...) {}
                                }

                                std::thread([&bot, config, msg, server, final_count, emb_json]() {
                                    stoatpp::models::MessagePayload payload;
                                    std::string content = "";
                                    if (!config.leave_content.empty()) {
                                        content = format_placeholders(config.leave_content, msg.author, server, final_count);
                                    }
                                    payload.content = content;

                                    nlohmann::json embed = nlohmann::json::object();
                                    if (emb_json.contains("title")) {
                                        embed["title"] = format_placeholders(emb_json["title"].get<std::string>(), msg.author, server, final_count);
                                    }
                                    if (emb_json.contains("description")) {
                                        embed["description"] = format_placeholders(emb_json["description"].get<std::string>(), msg.author, server, final_count);
                                    }
                                    if (emb_json.contains("color")) {
                                        std::string col = emb_json["color"].get<std::string>();
                                        if (col == "random") {
                                            char hex[8];
                                            snprintf(hex, sizeof(hex), "#%06x", rand() % 0xFFFFFF);
                                            col = hex;
                                        }
                                        embed["colour"] = col;
                                    }
                                    if (emb_json.contains("image")) {
                                        std::string img_url = format_placeholders(emb_json["image"].get<std::string>(), msg.author, server, final_count);
                                        std::string media_id = bot.rest().upload_from_url(img_url);
                                        if (!media_id.empty()) {
                                            embed["media"] = media_id;
                                        } else {
                                            std::string desc = embed.contains("description") ? embed["description"].get<std::string>() : "";
                                            if (!desc.empty()) desc += "\n";
                                            desc += "![](" + img_url + ")";
                                            embed["description"] = desc;
                                        }
                                    }

                                    if (!embed.empty()) {
                                        payload.embeds.push_back(embed);
                                    }

                                    send_verbose_test_message(bot, config.leave_channel_id, payload, msg.channel_id,
                                         "" + bronx::emj_yes + " Test leave message successfully sent to the configured channel.",
                                         "Failed to send test leave message. Make sure the bot has permission to post in that channel.");
                                }).detach();
                            } else {
                                stoatpp::models::MessagePayload payload;
                                std::string text_body = config.leave_content.empty() ? "{name} left {server}." : config.leave_content;
                                payload.content = format_placeholders(text_body, msg.author, server, final_count);
                                send_verbose_test_message(bot, config.leave_channel_id, payload, msg.channel_id,
                                     "" + bronx::emj_yes + " Test leave message successfully sent to the configured channel.",
                                     "Failed to send test leave message. Make sure the bot has permission to post in that channel.");
                            }
                    });
                    return;
                }

                if (subcommand == "setup") {
                    bot.send_message(msg.channel_id, "💬 **Leave Setup Wizard**\nLet's configure departure messages for this server.\n\n**Step 1/7**: Mention a channel (e.g. #departures), type its name/ID, or reply `none` to disable.");
                    bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30), [&bot, msg](std::optional<stoatpp::events::Message> rep1) {
                        if (!rep1) {
                            bot.send_message(msg.channel_id, "⏱️ Setup timed out. Leave setup canceled.");
                            return;
                        }
                        
                        std::string chan_resp = rep1->content;
                        std::string target_channel = "";
                        if (chan_resp != "none") {
                            if (chan_resp.rfind("<#", 0) == 0 && chan_resp.back() == '>') {
                                target_channel = chan_resp.substr(2, chan_resp.size() - 3);
                            } else {
                                auto server_channels_res = bot.rest().get_server_channels(msg.server_id);
                                if (server_channels_res.success() && server_channels_res.body.is_array()) {
                                    for (const auto& c_json : server_channels_res.body) {
                                        auto c = stoatpp::models::Channel::from_json(c_json);
                                        if (c.id == chan_resp || c.name == chan_resp) {
                                            target_channel = c.id;
                                            break;
                                        }
                                    }
                                }
                                if (target_channel.empty()) {
                                    target_channel = chan_resp;
                                }
                            }
                        }

                        if (target_channel.empty() && chan_resp != "none") {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid channel. Leave setup canceled.");
                            return;
                        }

                        if (chan_resp == "none") {
                            auto config = g_db->get_welcome_leave_config(msg.server_id).value_or(Database::WelcomeLeaveConfig{msg.server_id});
                            config.leave_channel_id = "";
                            g_db->save_welcome_leave_config(config);
                            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Leave messages have been disabled.");
                            return;
                        }

                        bot.send_message(msg.channel_id, "💬 **Leave Setup Wizard**\n**Step 2/7**: Choose the message type. Reply with `text` or `embed`.");
                        bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30), [&bot, msg, target_channel](std::optional<stoatpp::events::Message> rep2) {
                            if (!rep2) {
                                bot.send_message(msg.channel_id, "⏱️ Setup timed out. Leave setup canceled.");
                                return;
                            }

                            std::string type_resp = rep2->content;
                            std::transform(type_resp.begin(), type_resp.end(), type_resp.begin(), ::tolower);
                            if (type_resp != "text" && type_resp != "embed") {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid type (must be `text` or `embed`). Leave setup canceled.");
                                return;
                            }

                            if (type_resp == "text") {
                                bot.send_message(msg.channel_id, "💬 **Leave Setup Wizard**\n**Step 3/3**: Type the departure message/content.\n*(You can use tokens like `{user}`, `{username}`, `{server}`, and `{membercount}`)*");
                                bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(45), [&bot, msg, target_channel, type_resp](std::optional<stoatpp::events::Message> rep3) {
                                    if (!rep3) {
                                        bot.send_message(msg.channel_id, "⏱️ Setup timed out. Leave setup canceled.");
                                        return;
                                    }

                                    std::string content_resp = rep3->content;
                                    auto config = g_db->get_welcome_leave_config(msg.server_id).value_or(Database::WelcomeLeaveConfig{msg.server_id});
                                    config.leave_channel_id = target_channel;
                                    config.leave_type = type_resp;
                                    config.leave_content = content_resp;

                                    g_db->save_welcome_leave_config(config);
                                    bot.send_message(msg.channel_id, "" + bronx::emj_yes + " **Leave Setup Complete!**\nDeparture messages will be sent to <#" + target_channel + "> in text format.");
                                });
                                return;
                            }

                            // Embed Setup flow:
                            bot.send_message(msg.channel_id, "💬 **Leave Setup Wizard**\n**Step 3/7**: Type the embed **title** (or reply `skip`).\n*Available tokens:*\n```\n{user} {username} {server} {membercount}\n```");
                            bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(60), [&bot, msg, target_channel, type_resp](std::optional<stoatpp::events::Message> rep3) {
                                if (!rep3) {
                                    bot.send_message(msg.channel_id, "⏱️ Setup timed out. Leave setup canceled.");
                                    return;
                                }

                                nlohmann::json emb = nlohmann::json::object();
                                std::string title_resp = rep3->content;
                                if (title_resp != "skip") {
                                    emb["title"] = title_resp;
                                }

                                bot.send_message(msg.channel_id, "💬 **Leave Setup Wizard**\n**Step 4/7**: Type the embed **description** (or reply `skip`).\n*Available tokens:*\n```\n{user} {username} {server} {membercount}\n```");
                                bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(90), [&bot, msg, emb, target_channel, type_resp](std::optional<stoatpp::events::Message> rep4) {
                                    if (!rep4) {
                                        bot.send_message(msg.channel_id, "⏱️ Setup timed out. Leave setup canceled.");
                                        return;
                                    }

                                    auto current_emb = emb;
                                    std::string desc_resp = rep4->content;
                                    if (desc_resp != "skip") {
                                        current_emb["description"] = desc_resp;
                                    }

                                    bot.send_message(msg.channel_id, "💬 **Leave Setup Wizard**\n**Step 5/7**: Type the embed **color hex** (e.g. `#ff5555` or `random`, or reply `skip`).");
                                    bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30), [&bot, msg, current_emb, target_channel, type_resp](std::optional<stoatpp::events::Message> rep5) {
                                        if (!rep5) {
                                            bot.send_message(msg.channel_id, "⏱️ Setup timed out. Leave setup canceled.");
                                            return;
                                        }

                                        auto final_emb = current_emb;
                                        std::string color_resp = rep5->content;
                                        if (color_resp != "skip") {
                                            final_emb["color"] = color_resp;
                                        }

                                        bot.send_message(msg.channel_id, "💬 **Leave Setup Wizard**\n**Step 6/7**: Type the embed **image URL** (or reply `skip`).");
                                        bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(45), [&bot, msg, final_emb, target_channel, type_resp](std::optional<stoatpp::events::Message> rep6) {
                                            if (!rep6) {
                                                bot.send_message(msg.channel_id, "⏱️ Setup timed out. Leave setup canceled.");
                                                return;
                                            }

                                            auto save_emb = final_emb;
                                            std::string img_resp = rep6->content;
                                            if (img_resp != "skip") {
                                                save_emb["image"] = img_resp;
                                            }

                                            bot.send_message(msg.channel_id, "💬 **Leave Setup Wizard**\n**Step 7/7**: Type the **message content/text** to show outside the embed (or reply `skip`).\n*(You can use tokens like `{user}`, `{username}`, `{server}`, and `{membercount}`)*");
                                            bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(45), [&bot, msg, save_emb, target_channel, type_resp](std::optional<stoatpp::events::Message> rep7) {
                                                if (!rep7) {
                                                    bot.send_message(msg.channel_id, "⏱️ Setup timed out. Leave setup canceled.");
                                                    return;
                                                }

                                                std::string content_resp = rep7->content;
                                                auto config = g_db->get_welcome_leave_config(msg.server_id).value_or(Database::WelcomeLeaveConfig{msg.server_id});
                                                config.leave_channel_id = target_channel;
                                                config.leave_type = type_resp;
                                                config.leave_embed_json = save_emb.dump();
                                                if (content_resp != "skip") {
                                                    config.leave_content = content_resp;
                                                } else {
                                                    config.leave_content = "";
                                                }

                                                g_db->save_welcome_leave_config(config);
                                                bot.send_message(msg.channel_id, "" + bronx::emj_yes + " **Leave Setup Complete!**\nDeparture messages will be sent to <#" + target_channel + "> as a rich embed.");
                                            });
                                        });
                                    });
                                });
                            });
                        });
                    });
                    return;
                }

                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Unknown subcommand. Use setup, channel, type, message, edit, json, or test.");
                });
            },
            "Leave",
            0,
            0,
            {"setup", "channel <#channel|none>", "type <text|embed>", "message <text...>", "edit <key:val...>", "json <raw_json>", "test"}
        });

        // --- GATEWAY MEMBER JOIN HOOK ---
        bot.on_server_member_join([&bot](const stoatpp::events::ServerMemberJoin& e) {
            if (e.user_id == bot.current_user().id) return;
            if (g_db) g_db->increment_guild_member_joins(e.server_id);

            auto config_opt = g_db->get_welcome_leave_config(e.server_id);
            if (!config_opt) return;

            auto config = *config_opt;

            // 1. Assign custom Autorole if configured
            if (!config.autorole_role_id.empty()) {
                std::cout << "[bronx autorole] assigning role " << config.autorole_role_id << " to member " << e.user_id << " in " << e.server_id << "\n";
                bot.add_role_to_member(e.server_id, e.user_id, config.autorole_role_id, [e, config](bool ok, std::string err) {
                    if (!ok) {
                        std::cerr << "[bronx autorole] failed to assign role " << config.autorole_role_id << " to member " << e.user_id << " in " << e.server_id << ": " << err << "\n";
                    }
                });
            }

            // 2. Dispatch Welcome Message if configured
            if (!config.welcome_channel_id.empty()) {
                bot.get_member_count(e.server_id, [&bot, e, config](int count, bool ok) {
                    int final_count = ok ? count : 1;
                    auto server_opt = bot.get_server(e.server_id);
                    stoatpp::models::Server server;
                    if (server_opt) {
                        server = *server_opt;
                    } else {
                        server.name = "our server";
                    }

                    bot.fetch_user(e.user_id, [&bot, e, config, server, final_count](stoatpp::models::User user, bool user_ok) {
                        if (!user_ok) return;

                        if (config.welcome_type == "embed") {
                            nlohmann::json emb_json = nlohmann::json::object();
                            if (!config.welcome_embed_json.empty()) {
                                try {
                                    emb_json = nlohmann::json::parse(config.welcome_embed_json);
                                } catch(...) {}
                            }

                            std::thread([&bot, config, user, server, final_count, emb_json]() {
                                stoatpp::models::MessagePayload payload;
                                std::string content = "";
                                if (!config.welcome_content.empty()) {
                                    content = format_placeholders(config.welcome_content, user, server, final_count);
                                }
                                payload.content = content;

                                nlohmann::json embed = nlohmann::json::object();
                                if (emb_json.contains("title")) {
                                    embed["title"] = format_placeholders(emb_json["title"].get<std::string>(), user, server, final_count);
                                }
                                if (emb_json.contains("description")) {
                                    embed["description"] = format_placeholders(emb_json["description"].get<std::string>(), user, server, final_count);
                                }
                                if (emb_json.contains("color")) {
                                    std::string col = emb_json["color"].get<std::string>();
                                    if (col == "random") {
                                        char hex[8];
                                        snprintf(hex, sizeof(hex), "#%06x", rand() % 0xFFFFFF);
                                        col = hex;
                                    }
                                    embed["colour"] = col;
                                }
                                if (emb_json.contains("image")) {
                                    std::string img_url = format_placeholders(emb_json["image"].get<std::string>(), user, server, final_count);
                                    std::string media_id = bot.rest().upload_from_url(img_url);
                                    if (!media_id.empty()) {
                                        embed["media"] = media_id;
                                    } else {
                                        std::string desc = embed.contains("description") ? embed["description"].get<std::string>() : "";
                                        if (!desc.empty()) desc += "\n";
                                        desc += "![](" + img_url + ")";
                                        embed["description"] = desc;
                                    }
                                }

                                if (!embed.empty()) {
                                    payload.embeds.push_back(embed);
                                }

                                bot.send_message(config.welcome_channel_id, payload);
                            }).detach();
                        } else {
                            stoatpp::models::MessagePayload payload;
                            std::string text_body = config.welcome_content.empty() ? "Welcome {name} to {server}!" : config.welcome_content;
                            payload.content = format_placeholders(text_body, user, server, final_count);
                            bot.send_message(config.welcome_channel_id, payload);
                        }
                    });
                });
            }

            // 3. Dispatch DM Welcome if configured
            if (config.welcome_dm_type == "text" || config.welcome_dm_type == "embed") {
                bot.get_member_count(e.server_id, [&bot, e, config](int count, bool ok) {
                    int final_count = ok ? count : 1;
                    auto server_opt = bot.get_server(e.server_id);
                    stoatpp::models::Server server;
                    if (server_opt) {
                        server = *server_opt;
                    } else {
                        server.name = "our server";
                    }

                    bot.fetch_user(e.user_id, [&bot, e, config, server, final_count](stoatpp::models::User user, bool user_ok) {
                        if (!user_ok) return;

                        std::thread([&bot, config, user, server, final_count]() {
                            auto dm_res = bot.rest().open_dm(user.id);
                            if (!dm_res.success() || !dm_res.body.is_object() || !dm_res.body.contains("_id")) {
                                return;
                            }
                            std::string dm_channel_id = dm_res.body["_id"].get<std::string>();

                            stoatpp::models::MessagePayload payload;
                            if (config.welcome_dm_type == "embed") {
                                nlohmann::json emb_json = nlohmann::json::object();
                                if (!config.welcome_dm_embed_json.empty()) {
                                    try {
                                        emb_json = nlohmann::json::parse(config.welcome_dm_embed_json);
                                    } catch(...) {}
                                }

                                std::string content = "";
                                if (!config.welcome_dm_content.empty()) {
                                    content = format_placeholders(config.welcome_dm_content, user, server, final_count);
                                }
                                payload.content = content;

                                nlohmann::json embed = nlohmann::json::object();
                                if (emb_json.contains("title")) {
                                    embed["title"] = format_placeholders(emb_json["title"].get<std::string>(), user, server, final_count);
                                }
                                if (emb_json.contains("description")) {
                                    embed["description"] = format_placeholders(emb_json["description"].get<std::string>(), user, server, final_count);
                                }
                                if (emb_json.contains("color")) {
                                    std::string col = emb_json["color"].get<std::string>();
                                    if (col == "random") {
                                        char hex[8];
                                        snprintf(hex, sizeof(hex), "#%06x", rand() % 0xFFFFFF);
                                        col = hex;
                                    }
                                    embed["colour"] = col;
                                }
                                if (emb_json.contains("image")) {
                                    std::string img_url = format_placeholders(emb_json["image"].get<std::string>(), user, server, final_count);
                                    std::string media_id = bot.rest().upload_from_url(img_url);
                                    if (!media_id.empty()) {
                                        embed["media"] = media_id;
                                    } else {
                                        std::string desc = embed.contains("description") ? embed["description"].get<std::string>() : "";
                                        if (!desc.empty()) desc += "\n";
                                        desc += "![](" + img_url + ")";
                                        embed["description"] = desc;
                                    }
                                }

                                if (!embed.empty()) {
                                    payload.embeds.push_back(embed);
                                }
                            } else {
                                payload.content = format_placeholders(config.welcome_dm_content, user, server, final_count);
                            }

                            bot.send_message(dm_channel_id, payload);
                        }).detach();
                    });
                });
            }
        });

        // --- GATEWAY MEMBER LEAVE HOOK ---
        bot.on_server_member_leave([&bot](const stoatpp::events::ServerMemberLeave& e) {
            if (e.user_id == bot.current_user().id) return;
            if (g_db) g_db->increment_guild_member_leaves(e.server_id);

            auto config_opt = g_db->get_welcome_leave_config(e.server_id);
            if (!config_opt || config_opt->leave_channel_id.empty()) return;

            auto config = *config_opt;
            bot.get_member_count(e.server_id, [&bot, e, config](int count, bool ok) {
                int final_count = ok ? count : 1;
                auto server_opt = bot.get_server(e.server_id);
                stoatpp::models::Server server;
                if (server_opt) {
                    server = *server_opt;
                } else {
                    server.name = "our server";
                }

                bot.fetch_user(e.user_id, [&bot, e, config, server, final_count](stoatpp::models::User user, bool user_ok) {
                    if (!user_ok) return;

                    if (config.leave_type == "embed") {
                        nlohmann::json emb_json = nlohmann::json::object();
                        if (!config.leave_embed_json.empty()) {
                            try {
                                emb_json = nlohmann::json::parse(config.leave_embed_json);
                            } catch(...) {}
                        }

                        std::thread([&bot, config, user, server, final_count, emb_json]() {
                            stoatpp::models::MessagePayload payload;
                            std::string content = "";
                            if (!config.leave_content.empty()) {
                                content = format_placeholders(config.leave_content, user, server, final_count);
                            }
                            payload.content = content;

                            nlohmann::json embed = nlohmann::json::object();
                            if (emb_json.contains("title")) {
                                embed["title"] = format_placeholders(emb_json["title"].get<std::string>(), user, server, final_count);
                            }
                            if (emb_json.contains("description")) {
                                embed["description"] = format_placeholders(emb_json["description"].get<std::string>(), user, server, final_count);
                            }
                            if (emb_json.contains("color")) {
                                std::string col = emb_json["color"].get<std::string>();
                                if (col == "random") {
                                    char hex[8];
                                    snprintf(hex, sizeof(hex), "#%06x", rand() % 0xFFFFFF);
                                    col = hex;
                                }
                                embed["colour"] = col;
                            }
                            if (emb_json.contains("image")) {
                                std::string img_url = format_placeholders(emb_json["image"].get<std::string>(), user, server, final_count);
                                std::string media_id = bot.rest().upload_from_url(img_url);
                                if (!media_id.empty()) {
                                    embed["media"] = media_id;
                                } else {
                                    std::string desc = embed.contains("description") ? embed["description"].get<std::string>() : "";
                                    if (!desc.empty()) desc += "\n";
                                    desc += "![](" + img_url + ")";
                                    embed["description"] = desc;
                                }
                            }

                            if (!embed.empty()) {
                                payload.embeds.push_back(embed);
                            }

                            bot.send_message(config.leave_channel_id, payload);
                        }).detach();
                    } else {
                        stoatpp::models::MessagePayload payload;
                        std::string text_body = config.leave_content.empty() ? "{name} left {server}." : config.leave_content;
                        payload.content = format_placeholders(text_body, user, server, final_count);
                        bot.send_message(config.leave_channel_id, payload);
                    }
                });
            });
        });
    }
};

} // namespace bronx::cogs
