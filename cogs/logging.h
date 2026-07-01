#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "../database.h"
#include <unordered_map>
#include <deque>
#include <mutex>
#include <thread>
#include <algorithm>
#include <iostream>

namespace bronx::cogs {

inline std::string format_log_placeholders(std::string text, const std::unordered_map<std::string, std::string>& placeholders) {
    auto replace_all = [](std::string& str, const std::string& from, const std::string& to) {
        size_t start_pos = 0;
        while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
            str.replace(start_pos, from.length(), to);
            start_pos += to.length();
        }
    };

    for (const auto& [key, val] : placeholders) {
        replace_all(text, "{" + key + "}", val);
    }
    return text;
}

inline void dispatch_log(stoatpp::cluster& bot, const std::string& server_id, const std::string& event_type, const std::unordered_map<std::string, std::string>& placeholders, const std::function<std::pair<std::string, nlohmann::json>()>& default_fallback) {
    if (!g_db) return;
    std::string log_chan = g_db->get_logging_channel(server_id, event_type);
    if (log_chan.empty()) return;

    auto custom_opt = g_db->get_logging_embed(server_id, event_type);
    stoatpp::models::MessagePayload payload;

    if (custom_opt) {
        auto custom = *custom_opt;
        if (custom.message_type == "embed") {
            nlohmann::json emb_json = nlohmann::json::object();
            if (!custom.embed_json.empty()) {
                try {
                    emb_json = nlohmann::json::parse(custom.embed_json);
                } catch(...) {}
            }

            if (!custom.content.empty()) {
                payload.content = format_log_placeholders(custom.content, placeholders);
            }

            nlohmann::json embed = nlohmann::json::object();
            if (emb_json.contains("title")) {
                embed["title"] = format_log_placeholders(emb_json["title"].get<std::string>(), placeholders);
            }
            if (emb_json.contains("description")) {
                embed["description"] = format_log_placeholders(emb_json["description"].get<std::string>(), placeholders);
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
                std::string img_url = emb_json["image"].get<std::string>();
                std::string desc = embed.contains("description") ? embed["description"].get<std::string>() : "";
                if (!desc.empty()) desc += "\n";
                desc += "![](" + img_url + ")";
                embed["description"] = desc;
            }
            payload.embeds.push_back(embed);
        } else {
            payload.content = format_log_placeholders(custom.content, placeholders);
        }
    } else {
        auto [content, embed] = default_fallback();
        payload.content = content;
        payload.embeds.push_back(embed);
    }

    // Run the webhook resolution and execution logic in a separate thread so it doesn't block the gateway thread
    std::string server_id_copy = server_id;
    std::string event_type_copy = event_type;
    std::thread([&bot, server_id_copy, event_type_copy, log_chan, payload]() {
        bool sent_via_webhook = false;
        
        auto wh_opt = g_db->get_logging_webhook(log_chan);
        std::string wh_id, wh_token;
        
        if (wh_opt) {
            wh_id = wh_opt->first;
            wh_token = wh_opt->second;
        } else {
            // Check if there is already a webhook named "Bronx Logger" in the channel
            auto get_res = bot.rest().get_channel_webhooks(log_chan);
            if (get_res.success()) {
                if (get_res.body.is_array() && !get_res.body.empty()) {
                    for (const auto& item : get_res.body) {
                        auto wh = stoatpp::models::Webhook::from_json(item);
                        if (wh.name == "Bronx Logger" && wh.token) {
                            wh_id = wh.id;
                            wh_token = *wh.token;
                            g_db->save_logging_webhook(log_chan, wh_id, wh_token);
                            break;
                        }
                    }
                }
            } else if (get_res.status_code == 404) {
                // Channel does not exist! Evict configuration from DB
                std::cout << "[bronx logging] Evicting deleted log channel " << log_chan << " for event " << event_type_copy << "\n";
                g_db->remove_logging_channel(server_id_copy, event_type_copy);
                return;
            }
            
            if (wh_id.empty()) {
                // No existing webhook found, create a new one
                auto create_res = bot.rest().create_webhook(log_chan, "Bronx Logger");
                if (create_res.success()) {
                    auto wh = stoatpp::models::Webhook::from_json(create_res.body);
                    if (wh.token) {
                        wh_id = wh.id;
                        wh_token = *wh.token;
                        g_db->save_logging_webhook(log_chan, wh_id, wh_token);
                    }
                } else if (create_res.status_code == 404) {
                    // Channel does not exist! Evict configuration from DB
                    std::cout << "[bronx logging] Evicting deleted log channel " << log_chan << " for event " << event_type_copy << "\n";
                    g_db->remove_logging_channel(server_id_copy, event_type_copy);
                    return;
                }
            }
        }
        
        if (!wh_id.empty() && !wh_token.empty()) {
            stoatpp::models::WebhookExecutePayload wh_payload;
            wh_payload.content = payload.content;
            wh_payload.username = "Bronx Logger";
            wh_payload.embeds = payload.embeds;
            
            auto exec_res = bot.rest().execute_webhook(wh_id, wh_token, wh_payload);
            if (exec_res.success()) {
                sent_via_webhook = true;
            } else if (exec_res.status_code == 404) {
                // Check if the channel is deleted or just the webhook
                auto ch_res = bot.rest().get_channel(log_chan);
                if (!ch_res.success() && ch_res.status_code == 404) {
                    std::cout << "[bronx logging] Evicting deleted log channel " << log_chan << " for event " << event_type_copy << "\n";
                    g_db->remove_logging_channel(server_id_copy, event_type_copy);
                    return;
                } else {
                    // Webhook was deleted on the server, evict from DB cache
                    g_db->remove_logging_webhook(log_chan);
                }
            }
        }
        
        if (!sent_via_webhook) {
            // Fallback to sending standard bot message
            bot.send_message(log_chan, payload, [&bot, server_id_copy, event_type_copy, log_chan](stoatpp::models::Message, bool success) {
                if (!success) {
                    // Check if channel is deleted
                    auto ch_res = bot.rest().get_channel(log_chan);
                    if (!ch_res.success() && ch_res.status_code == 404) {
                        std::cout << "[bronx logging] Evicting deleted log channel " << log_chan << " for event " << event_type_copy << "\n";
                        g_db->remove_logging_channel(server_id_copy, event_type_copy);
                    }
                }
            });
        }
    }).detach();
}

inline void log_moderation_action(stoatpp::cluster& bot, const std::string& server_id, const std::string& action, const std::string& mod_id, const std::string& target_id, const std::string& reason, const std::string& extra_details = "") {
    // Persist to DB so .modlogs can query history
    if (g_db && !server_id.empty()) {
        g_db->record_moderation_action(server_id, action, mod_id, target_id, reason, extra_details);
    }

    std::unordered_map<std::string, std::string> placeholders = {
        {"action", action},
        {"moderator", "<@" + mod_id + ">"},
        {"invokingUser", "<@" + mod_id + ">"},
        {"target", "<@" + target_id + ">"},
        {"affectedUser", "<@" + target_id + ">"},
        {"reason", reason.empty() ? "*No reason provided*" : reason},
        {"details", extra_details}
    };

    auto default_fallback = [action, placeholders]() {
        nlohmann::json embed = nlohmann::json::object();
        embed["title"] = "🛡️ Moderation Log: " + action;
        embed["colour"] = "#d12222"; // Premium dark red

        std::string desc = "• **Moderator:** " + placeholders.at("moderator") + "\n"
                           "• **Target:** " + placeholders.at("target") + "\n"
                           "• **Reason:** " + placeholders.at("reason") + "\n";
        std::string details = placeholders.at("details");
        if (!details.empty()) {
            desc += "• **Details:** " + details + "\n";
        }
        embed["description"] = desc;
        return std::make_pair("", embed);
    };

    dispatch_log(bot, server_id, "moderation", placeholders, default_fallback);
}

class LoggingModule : public stoatpp::bot_module {
private:
    std::unordered_map<std::string, stoatpp::events::Message> msg_cache_;
    std::deque<std::string> msg_ids_;
    std::mutex cache_mutex_;

    void cache_message(const stoatpp::events::Message& msg) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        msg_cache_[msg.id] = msg;
        msg_ids_.push_back(msg.id);
        if (msg_ids_.size() > 5000) {
            std::string oldest = msg_ids_.front();
            msg_ids_.pop_front();
            msg_cache_.erase(oldest);
        }
    }

    std::optional<stoatpp::events::Message> get_cached_message(const std::string& msg_id) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = msg_cache_.find(msg_id);
        if (it != msg_cache_.end()) return it->second;
        return std::nullopt;
    }

    void evict_message(const std::string& msg_id) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        msg_cache_.erase(msg_id);
    }

public:
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

    static std::string get_raw_event_trailing_args(const std::string& content, const std::string& prefix, const std::string& command_name, const std::string& subcommand, const std::string& event_param) {
        std::string raw_sub = get_raw_trailing_args(content, prefix, command_name, subcommand);
        if (raw_sub.empty()) return "";

        std::string lower_sub = raw_sub;
        std::transform(lower_sub.begin(), lower_sub.end(), lower_sub.begin(), ::tolower);

        std::string lower_param = event_param;
        std::transform(lower_param.begin(), lower_param.end(), lower_param.begin(), ::tolower);

        size_t pos = lower_sub.find(lower_param);
        if (pos == std::string::npos) return "";

        pos += lower_param.length();
        while (pos < raw_sub.length() && std::isspace(raw_sub[pos])) {
            pos++;
        }
        return raw_sub.substr(pos);
    }

public:
    void register_handlers(stoatpp::cluster& bot) override {
        // --- LOG COMMAND ---
        auto handle_log_cmd = [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
            std::cout << "[bronx command] handling log...\n";
            if (msg.server_id.empty()) {
                bot.send_message(msg.channel_id, "⚠️ This command can only be used in a server channel.");
                return;
            }

            if (!g_db) {
                bot.send_message(msg.channel_id, "⚠️ Database is unavailable.");
                return;
            }

            // Check permissions
            auto server_opt = bot.get_server(msg.server_id);
            if (!server_opt) {
                bot.send_message(msg.channel_id, "⚠️ Failed to resolve server details.");
                return;
            }

            std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
            if (args.empty()) {
                bot.send_message(msg.channel_id, "📋 **Logging Module Commands:**\n"
                                                 "• `" + pfx + "log aio` - Auto-setup a Logging category and channels.\n"
                                                 "• `" + pfx + "log channel <event> <#channel|none>` - Route event to channel.\n"
                                                 "• `" + pfx + "log list` - Show current logging config.\n"
                                                 "• `" + pfx + "log type <event> <text|embed>` - Set log type.\n"
                                                 "• `" + pfx + "log message <event> <text...>` - Customize log message body.\n"
                                                 "• `" + pfx + "log edit <event> <key:val...>` - Customize embed fields.\n"
                                                 "• `" + pfx + "log json <event> <raw_json>` - Directly import embed JSON.\n\n"
                                                 "**Valid Event Types:**\n"
                                                 "- `delete` / `message_delete` (Placeholders: `{author}`, `{username}`, `{channel}`, `{content}`)\n"
                                                 "- `edit` / `message_edit` (Placeholders: `{author}`, `{username}`, `{channel}`, `{before}`, `{after}`)\n"
                                                 "- `join` / `member_join` (Placeholders: `{user}`, `{username}`, `{membercount}`)\n"
                                                 "- `leave` / `member_leave` (Placeholders: `{user}`, `{username}`, `{membercount}`)\n"
                                                 "- `mod` / `moderation` (Placeholders: `{action}`, `{moderator}`, `{target}`, `{reason}`, `{details}`)\n"
                                                 "- `user` / `user_update` (Placeholders: `{user}`, `{oldUser}`, `{newUser}`)");
                return;
            }

            std::string subcommand = args[0];
            std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);

            auto execute_cmd = [this, &bot, msg, pfx, args, subcommand, server_opt]() {
                if (subcommand == "aio") {
                    std::string server_id = msg.server_id;
                    std::thread([&bot, server_id, msg]() {
                        bot.send_message(msg.channel_id, "⚙️ **All-In-One Setup Initiated**\nCreating logging channels...");
                        
                        auto res_edits = bot.rest().create_channel(server_id, "Text", "edits");
                        auto res_deletes = bot.rest().create_channel(server_id, "Text", "deletions");
                        auto res_joins = bot.rest().create_channel(server_id, "Text", "joins-leaves");
                        auto res_mod = bot.rest().create_channel(server_id, "Text", "moderation");

                        if (!res_edits.success() || !res_deletes.success() || !res_joins.success() || !res_mod.success()) {
                            bot.send_message(msg.channel_id, "⚠️ Failed to create one or more log channels. Please check my server permissions (Manage Channels).");
                            return;
                        }

                        std::string edits_id = res_edits.body["_id"].get<std::string>();
                        std::string deletes_id = res_deletes.body["_id"].get<std::string>();
                        std::string joins_id = res_joins.body["_id"].get<std::string>();
                        std::string mod_id = res_mod.body["_id"].get<std::string>();

                        g_db->save_logging_channel(server_id, "message_edit", edits_id);
                        g_db->save_logging_channel(server_id, "message_delete", deletes_id);
                        g_db->save_logging_channel(server_id, "member_join", joins_id);
                        g_db->save_logging_channel(server_id, "member_leave", joins_id);
                        g_db->save_logging_channel(server_id, "moderation", mod_id);

                        bot.send_message(msg.channel_id, "⚙️ Creating Logging category grouping...");

                        bool category_created = false;
                        auto srv_res = bot.rest().get_server(server_id);
                        if (srv_res.success()) {
                            nlohmann::json categories = nlohmann::json::array();
                            if (srv_res.body.contains("categories") && srv_res.body["categories"].is_array()) {
                                categories = srv_res.body["categories"];
                            }

                            const std::string alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
                            std::string cat_id = "";
                            for (int i = 0; i < 26; ++i) cat_id += alphabet[rand() % alphabet.length()];

                            nlohmann::json new_cat = nlohmann::json::object();
                            new_cat["id"] = cat_id;
                            new_cat["title"] = "Logging";
                            new_cat["channels"] = {edits_id, deletes_id, joins_id, mod_id};
                            categories.push_back(new_cat);

                            nlohmann::json edit_fields = nlohmann::json::object();
                            edit_fields["categories"] = categories;
                            auto edit_res = bot.rest().edit_server(server_id, edit_fields);
                            if (edit_res.success()) {
                                category_created = true;
                            }
                        }

                        std::string complete_msg = "✅ **All-In-One Setup Complete!**\n";
                        if (category_created) {
                            complete_msg += "• Created category **Logging**.\n";
                        } else {
                            complete_msg += "⚠️ *Note: I couldn't group the channels into a 'Logging' category (this action is restricted for bots on this server).* \n";
                        }
                        complete_msg += "• Created and configured log channels:\n"
                                        "  - <#" + edits_id + "> ➔ Message Edits\n"
                                        "  - <#" + deletes_id + "> ➔ Message Deletions\n"
                                        "  - <#" + joins_id + "> ➔ Joins & Leaves\n"
                                        "  - <#" + mod_id + "> ➔ Moderation Logs";
                        bot.send_message(msg.channel_id, complete_msg);
                    }).detach();
                    return;
                }

                if (subcommand == "list") {
                    auto config = g_db->get_server_logging_config(msg.server_id);
                    if (config.empty()) {
                        bot.send_message(msg.channel_id, "ℹ️ No event log channels configured for this server.");
                        return;
                    }

                    std::string out = "📋 **Server Logging Configuration:**\n";
                    for (const auto& [ev_type, chan_id] : config) {
                        out += "• `" + ev_type + "` ➔ <#" + chan_id + ">\n";
                    }
                    bot.send_message(msg.channel_id, out);
                    return;
                }

                auto send_edit_help = [&bot, msg, pfx]() {
                    bot.send_message(msg.channel_id,
                        "usage: `" + pfx + "log edit <event_type> key:value ...`\n"
                        "keys: `title`, `description`, `color`, `image`, `content`\n"
                        "Use `none` to clear a field.\n\n"
                        "**Available Tokens by Event:**\n"
                        "• **Moderation (`mod`):** `{action}`, `{moderator}`, `{target}`, `{reason}`, `{details}`\n"
                        "• **Message Delete (`delete`):** `{author}`, `{username}`, `{channel}`, `{content}`\n"
                        "• **Message Edit (`edit`):** `{author}`, `{username}`, `{channel}`, `{before}`, `{after}`\n"
                        "• **Join/Leave (`join`, `leave`):** `{user}`, `{username}`, `{membercount}`\n"
                        "• **User Update (`user`):** `{user}`, `{oldUser}`, `{newUser}`\n\n"
                        "**Examples:**\n"
                        "• `" + pfx + "log edit delete title:🗑️ Message Deleted color:#ff3333 description:**Author:** {author}\\n**Content:** {content}`\n"
                        "• `" + pfx + "log edit mod title:🛡️ {action} color:#ffa500 content:<@{target}> was moderated`"
                    );
                };

                auto send_json_help = [&bot, msg, pfx]() {
                    bot.send_message(msg.channel_id,
                        "usage: `" + pfx + "log json <event_type> <raw_json_embed>`\n"
                        "Imports a full embed JSON to use for event logging. Supported fields: `title`, `description`, `color`, `image`, `content`.\n\n"
                        "**Available Tokens by Event:**\n"
                        "• **Moderation (`mod`):** `{action}`, `{moderator}`, `{target}`, `{reason}`, `{details}`\n"
                        "• **Message Delete (`delete`):** `{author}`, `{username}`, `{channel}`, `{content}`\n"
                        "• **Message Edit (`edit`):** `{author}`, `{username}`, `{channel}`, `{before}`, `{after}`\n"
                        "• **Join/Leave (`join`, `leave`):** `{user}`, `{username}`, `{membercount}`\n"
                        "• **User Update (`user`):** `{user}`, `{oldUser}`, `{newUser}`\n\n"
                        "**Example:**\n"
                        "```json\n"
                        "{\n"
                        "  \"title\": \"🗑️ Message Deleted in {channel}\",\n"
                        "  \"description\": \"**Author:** {author} ({username})\\n**Content:**\\n{content}\",\n"
                        "  \"color\": \"#ff3333\",\n"
                        "  \"content\": \"Alert: message deleted by {author}!\"\n"
                        "}\n"
                        "```"
                    );
                };

                if (args.size() < 2) {
                    if (subcommand == "edit") {
                        send_edit_help();
                        return;
                    }
                    if (subcommand == "json") {
                        send_json_help();
                        return;
                    }
                    bot.send_message(msg.channel_id, "usage: `" + pfx + "log " + subcommand + " <event_type> [args...]`");
                    return;
                }

                std::string ev = args[1];
                std::transform(ev.begin(), ev.end(), ev.begin(), ::tolower);
                if (ev == "delete") ev = "message_delete";
                else if (ev == "edit") ev = "message_edit";
                else if (ev == "join") ev = "member_join";
                else if (ev == "leave") ev = "member_leave";
                else if (ev == "mod") ev = "moderation";
                else if (ev == "user_update" || ev == "user" || ev == "user_edit") ev = "user_update";

                if (ev != "message_delete" && ev != "message_edit" && ev != "member_join" && ev != "member_leave" && ev != "moderation" && ev != "user_update") {
                    bot.send_message(msg.channel_id, "⚠️ Invalid event type. Choose from: `delete`, `edit`, `join`, `leave`, `mod`, `user`.");
                    return;
                }

                if (subcommand == "channel") {
                    if (args.size() < 3) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "log channel <event_type> <#channel|none>`");
                        return;
                    }

                    std::string chan_resp = args[2];
                    if (chan_resp == "none" || chan_resp == "clear") {
                        if (g_db->remove_logging_channel(msg.server_id, ev)) {
                            bot.send_message(msg.channel_id, "✅ Disabled logging for `" + ev + "`.");
                        } else {
                            bot.send_message(msg.channel_id, "⚠️ Failed to update configuration.");
                        }
                        return;
                    }

                    std::string target_channel = "";
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

                    if (target_channel.empty()) {
                        bot.send_message(msg.channel_id, "⚠️ Invalid channel name or ID.");
                        return;
                    }

                    if (g_db->save_logging_channel(msg.server_id, ev, target_channel)) {
                        bot.send_message(msg.channel_id, "✅ Logging for `" + ev + "` enabled in <#" + target_channel + ">.");
                    } else {
                        bot.send_message(msg.channel_id, "⚠️ Failed to save logging configuration.");
                    }
                    return;
                }

                if (subcommand == "type") {
                    if (args.size() < 3) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "log type <event_type> <text|embed>`");
                        return;
                    }

                    std::string type = args[2];
                    std::transform(type.begin(), type.end(), type.begin(), ::tolower);
                    if (type != "text" && type != "embed") {
                        bot.send_message(msg.channel_id, "⚠️ Type must be `text` or `embed`.");
                        return;
                    }

                    auto le = g_db->get_logging_embed(msg.server_id, ev).value_or(Database::LoggingEmbed{msg.server_id, ev});
                    le.message_type = type;

                    if (g_db->save_logging_embed(le)) {
                        bot.send_message(msg.channel_id, "✅ Logging type for `" + ev + "` set to **" + type + "**.");
                    } else {
                        bot.send_message(msg.channel_id, "⚠️ Failed to save configuration.");
                    }
                    return;
                }

                if (subcommand == "message") {
                    std::string text = get_raw_event_trailing_args(msg.content, pfx, "log", "message", args[1]);
                    if (text.empty()) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "log message <event_type> <text...>`");
                        return;
                    }

                    auto le = g_db->get_logging_embed(msg.server_id, ev).value_or(Database::LoggingEmbed{msg.server_id, ev});
                    le.content = text;

                    if (g_db->save_logging_embed(le)) {
                        bot.send_message(msg.channel_id, "✅ Custom message content for `" + ev + "` updated successfully.");
                    } else {
                        bot.send_message(msg.channel_id, "⚠️ Failed to save configuration.");
                    }
                    return;
                }

                if (subcommand == "edit") {
                    std::string text = get_raw_event_trailing_args(msg.content, pfx, "log", "edit", args[1]);
                    if (text.empty()) {
                        send_edit_help();
                        return;
                    }

                    auto pairs = parse_pairs(text);
                    if (pairs.empty()) {
                        bot.send_message(msg.channel_id, "⚠️ No valid key-value pairs found. Format must be `key:value` (e.g. `title:Hello color:random`).");
                        return;
                    }

                    auto le = g_db->get_logging_embed(msg.server_id, ev).value_or(Database::LoggingEmbed{msg.server_id, ev});
                    nlohmann::json emb = nlohmann::json::object();
                    if (!le.embed_json.empty()) {
                        try {
                            emb = nlohmann::json::parse(le.embed_json);
                        } catch(...) {}
                    }

                    for (const auto& [key, val] : pairs) {
                        if (key == "content") {
                            if (val == "none" || val == "null" || val == "clear") {
                                le.content = "";
                            } else {
                                le.content = val;
                            }
                        } else if (val == "none" || val == "null" || val == "clear") {
                            emb.erase(key);
                        } else {
                            emb[key] = val;
                        }
                    }

                    le.embed_json = emb.dump();
                    if (g_db->save_logging_embed(le)) {
                        bot.send_message(msg.channel_id, "✅ Custom embed fields for `" + ev + "` updated successfully.");
                    } else {
                        bot.send_message(msg.channel_id, "⚠️ Failed to save configuration.");
                    }
                    return;
                }

                if (subcommand == "json") {
                    std::string text = get_raw_event_trailing_args(msg.content, pfx, "log", "json", args[1]);
                    if (text.empty()) {
                        send_json_help();
                        return;
                    }

                    nlohmann::json emb;
                    try {
                        emb = nlohmann::json::parse(text);
                    } catch (const std::exception& e) {
                        bot.send_message(msg.channel_id, "⚠️ Invalid JSON format: " + std::string(e.what()));
                        return;
                    }

                    auto le = g_db->get_logging_embed(msg.server_id, ev).value_or(Database::LoggingEmbed{msg.server_id, ev});

                    if (emb.contains("content") && emb["content"].is_string()) {
                        le.content = emb["content"].get<std::string>();
                        emb.erase("content");
                    }

                    if (emb.contains("colour")) {
                        emb["color"] = emb["colour"];
                        emb.erase("colour");
                    }

                    le.embed_json = emb.dump();
                    if (g_db->save_logging_embed(le)) {
                        bot.send_message(msg.channel_id, "✅ Custom embed for `" + ev + "` imported successfully.");
                    } else {
                        bot.send_message(msg.channel_id, "⚠️ Failed to save configuration.");
                    }
                    return;
                }

                bot.send_message(msg.channel_id, "⚠️ Unknown subcommand. Use `aio`, `channel`, `list`, `type`, `message`, `edit`, or `json`.");
            };

            // Check permissions
            bot.fetch_member(msg.server_id, msg.author.id, [this, &bot, msg, execute_cmd, server_opt](stoatpp::models::Member member, bool success) {
                if (!success) {
                    bot.send_message(msg.channel_id, "failed to verify your server member settings.");
                    return;
                }
                if (!bot.is_owner(msg.author.id) && !bot.has_permission(*server_opt, member, stoatpp::permissions::ManageServer)) {
                    bot.send_message(msg.channel_id, "permission error: you need the Manage Server permission to configure event logging.");
                    return;
                }
                execute_cmd();
            });
        };

        bot.register_command({
            "log",
            {"logging"},
            "configure server event log channels and templates.",
            "",
            {"<subcommand>", "[args...]"},
            handle_log_cmd,
            "Logging",
            0,
            0,
            {"aio", "channel <event> <#channel|none>", "list", "type <event> <text|embed>", "message <event> <text...>", "edit <event> <key:val...>", "json <event> <raw_json>"}
        });

        // --- GATEWAY MESSAGE CACHE ---
        bot.on_message([this](const stoatpp::events::Message& msg) {
            cache_message(msg);
        });

        // --- GATEWAY EVENT: MESSAGE DELETE ---
        bot.on_message_delete([this, &bot](const stoatpp::events::MessageDelete& e) {
            auto cached_opt = get_cached_message(e.id);
            std::string server_id = "";
            if (cached_opt) {
                server_id = cached_opt->server_id;
            } else {
                auto chan_opt = bot.get_channel(e.channel_id);
                if (chan_opt) {
                    server_id = chan_opt->server.value_or("");
                }
            }

            if (server_id.empty() || !g_db) return;

            std::string author_id = cached_opt ? cached_opt->author.id : "";
            std::string username = cached_opt ? cached_opt->author.username : "";
            std::string deleted_content = cached_opt ? cached_opt->content : "";

            std::unordered_map<std::string, std::string> placeholders = {
                {"author", "<@" + author_id + ">"},
                {"username", username},
                {"channel", "<#" + e.channel_id + ">"},
                {"content", deleted_content}
            };

            auto default_fallback = [e, author_id, username, deleted_content]() {
                nlohmann::json embed = nlohmann::json::object();
                embed["title"] = "🗑️ Message Deleted";
                embed["colour"] = "#ff4444"; // Red

                std::string desc = "• **Channel:** <#" + e.channel_id + ">\n";
                if (!author_id.empty()) {
                    desc += "• **Author:** <@" + author_id + "> (" + username + ")\n"
                            "• **Content:**\n" + deleted_content + "\n";
                } else {
                    desc += "• **Message ID:** `" + e.id + "` *(not cached)*\n";
                }
                embed["description"] = desc;
                return std::make_pair("", embed);
            };

            dispatch_log(bot, server_id, "message_delete", placeholders, default_fallback);
            evict_message(e.id);
        });

        // --- GATEWAY EVENT: MESSAGE UPDATE (EDIT) ---
        bot.on_message_update([this, &bot](const stoatpp::events::MessageUpdate& e) {
            if (!e.data.contains("content")) return;
            std::string new_content = e.data["content"].get<std::string>();

            auto cached_opt = get_cached_message(e.id);
            std::string server_id = "";
            if (cached_opt) {
                server_id = cached_opt->server_id;
            } else {
                auto chan_opt = bot.get_channel(e.channel_id);
                if (chan_opt) {
                    server_id = chan_opt->server.value_or("");
                }
            }

            if (server_id.empty() || !g_db) return;

            if (cached_opt && cached_opt->content == new_content) return;

            std::string author_id = cached_opt ? cached_opt->author.id : "";
            std::string username = cached_opt ? cached_opt->author.username : "";
            std::string old_content = cached_opt ? cached_opt->content : "";

            std::unordered_map<std::string, std::string> placeholders = {
                {"author", "<@" + author_id + ">"},
                {"username", username},
                {"channel", "<#" + e.channel_id + ">"},
                {"before", old_content},
                {"after", new_content}
            };

            auto default_fallback = [e, author_id, old_content, new_content]() {
                nlohmann::json embed = nlohmann::json::object();
                embed["title"] = "✏️ Message Edited";
                embed["colour"] = "#ffaa00"; // Orange

                std::string desc = "• **Channel:** <#" + e.channel_id + ">\n";
                if (!author_id.empty()) {
                    desc += "• **Author:** <@" + author_id + ">\n"
                            "• **Before:**\n" + old_content + "\n\n"
                            "• **After:**\n" + new_content + "\n";
                } else {
                    desc += "• **Message ID:** `" + e.id + "` *(not cached)*\n"
                            "• **After:**\n" + new_content + "\n";
                }
                embed["description"] = desc;
                return std::make_pair("", embed);
            };

            dispatch_log(bot, server_id, "message_edit", placeholders, default_fallback);

            if (cached_opt) {
                cached_opt->content = new_content;
                std::lock_guard<std::mutex> lock(cache_mutex_);
                msg_cache_[e.id] = *cached_opt;
            } else {
                stoatpp::events::Message msg;
                msg.id = e.id;
                msg.channel_id = e.channel_id;
                msg.server_id = server_id;
                msg.content = new_content;
                if (e.data.contains("author")) {
                    msg.author.id = e.data["author"].get<std::string>();
                }
                cache_message(msg);
            }
        });

        // --- GATEWAY EVENT: SERVER MEMBER JOIN ---
        bot.on_server_member_join([this, &bot](const stoatpp::events::ServerMemberJoin& e) {
            if (!g_db) return;
            bot.get_member_count(e.server_id, [this, &bot, e](int count, bool count_ok) {
                int final_count = count_ok ? count : 0;
                
                auto usr_opt = bot.get_user(e.user_id);
                std::string username = usr_opt ? usr_opt->username : "";

                std::unordered_map<std::string, std::string> placeholders = {
                    {"user", "<@" + e.user_id + ">"},
                    {"username", username},
                    {"membercount", std::to_string(final_count)}
                };

                auto default_fallback = [e, final_count]() {
                    nlohmann::json embed = nlohmann::json::object();
                    embed["title"] = "📥 Member Joined";
                    embed["colour"] = "#44ff44"; // Green

                    std::string desc = "• **User:** <@" + e.user_id + ">\n"
                                       "• **User ID:** `" + e.user_id + "`\n";
                    if (final_count > 0) {
                        desc += "• **Total Members:** " + std::to_string(final_count) + "\n";
                    }
                    embed["description"] = desc;
                    return std::make_pair("", embed);
                };

                dispatch_log(bot, e.server_id, "member_join", placeholders, default_fallback);
            });
        });

        // --- GATEWAY EVENT: SERVER MEMBER LEAVE ---
        bot.on_server_member_leave([this, &bot](const stoatpp::events::ServerMemberLeave& e) {
            if (!g_db) return;
            bot.get_member_count(e.server_id, [this, &bot, e](int count, bool count_ok) {
                int final_count = count_ok ? count : 0;

                auto usr_opt = bot.get_user(e.user_id);
                std::string username = usr_opt ? usr_opt->username : "";

                std::unordered_map<std::string, std::string> placeholders = {
                    {"user", "<@" + e.user_id + ">"},
                    {"username", username},
                    {"membercount", std::to_string(final_count)}
                };

                auto default_fallback = [e, final_count]() {
                    nlohmann::json embed = nlohmann::json::object();
                    embed["title"] = "📤 Member Left";
                    embed["colour"] = "#888888"; // Grey

                    std::string desc = "• **User ID:** `" + e.user_id + "`\n";
                    if (final_count > 0) {
                        desc += "• **Total Members:** " + std::to_string(final_count) + "\n";
                    }
                    embed["description"] = desc;
                    return std::make_pair("", embed);
                };

                dispatch_log(bot, e.server_id, "member_leave", placeholders, default_fallback);
            });
        });

        // --- GATEWAY EVENT: USER UPDATE (USERNAME CHANGE) ---
        bot.on_user_update([&bot](const stoatpp::events::UserUpdate& e) {
            if (!e.data.contains("username") || !g_db) return;
            std::string new_username = e.data["username"].get<std::string>();

            auto old_user_opt = bot.get_user(e.id);
            if (!old_user_opt) return;

            std::string old_username = old_user_opt->username;
            if (old_username == new_username) return;

            std::thread([&bot, user_id = e.id, old_username, new_username]() {
                auto cached_servers = bot.get_servers();
                for (const auto& srv : cached_servers) {
                    std::string log_chan = g_db->get_logging_channel(srv.id, "user_update");
                    if (log_chan.empty()) continue;

                    auto res = bot.rest().get_server_member(srv.id, user_id);
                    if (res.success()) {
                        std::unordered_map<std::string, std::string> placeholders = {
                            {"user", "<@" + user_id + ">"},
                            {"oldUser", old_username},
                            {"newUser", new_username}
                        };

                        auto default_fallback = [user_id, old_username, new_username]() {
                            nlohmann::json embed = nlohmann::json::object();
                            embed["title"] = "👤 User Updated";
                            embed["colour"] = "#00aaff";
                            embed["description"] = "• **User:** <@" + user_id + ">\n"
                                                   "• **Old Username:** " + old_username + "\n"
                                                   "• **New Username:** " + new_username;
                            return std::make_pair("", embed);
                        };

                        dispatch_log(bot, srv.id, "user_update", placeholders, default_fallback);
                    }
                }
            }).detach();
        });
    }
};

} // namespace bronx::cogs
