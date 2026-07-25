#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "command_config.h"
#include "../database.h"
#include <sstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <mutex>
#include <unordered_set>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace bronx::cogs {

struct AppSession {
    std::string server_id;
    std::string panel_name;
    std::string user_id;
    std::string current_channel_id; // DM channel ID or private channel ID
    size_t current_question_index = 0;
    std::vector<std::string> answers;
    uint64_t last_activity = 0;
    bool in_dm = true;
};

inline std::unordered_map<std::string, AppSession> g_active_apps;
inline std::mutex g_active_apps_mutex;

class ApplicationsModule : public stoatpp::bot_module {
private:
    static std::string format_placeholders(std::string text, const stoatpp::models::User& user, const stoatpp::models::Server& server, int number) {
        std::string mention = "<@" + user.id + ">";
        
        auto replace_all = [](std::string& str, const std::string& from, const std::string& to) {
            size_t start_pos = 0;
            while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
                str.replace(start_pos, from.length(), to);
                start_pos += to.length();
            }
        };

        replace_all(text, "{user}", mention);
        replace_all(text, "{username}", user.username);
        replace_all(text, "{server}", server.name);
        replace_all(text, "{number}", std::to_string(number));
        
        return text;
    }

    static nlohmann::json build_embed(const std::string& embed_json_str, const stoatpp::models::User& user, const stoatpp::models::Server& server, int number) {
        nlohmann::json emb = nlohmann::json::object();
        if (embed_json_str.empty()) return emb;

        try {
            auto parsed = nlohmann::json::parse(embed_json_str);
            if (parsed.contains("title") && parsed["title"].is_string()) {
                emb["title"] = format_placeholders(parsed["title"].get<std::string>(), user, server, number);
            }
            if (parsed.contains("description") && parsed["description"].is_string()) {
                emb["description"] = format_placeholders(parsed["description"].get<std::string>(), user, server, number);
            }
            if (parsed.contains("color") && parsed["color"].is_string()) {
                emb["colour"] = parsed["color"].get<std::string>();
            } else if (parsed.contains("colour") && parsed["colour"].is_string()) {
                emb["colour"] = parsed["colour"].get<std::string>();
            }
            if (parsed.contains("image") && parsed["image"].is_string()) {
                emb["image"] = format_placeholders(parsed["image"].get<std::string>(), user, server, number);
            }
        } catch(...) {}

        return emb;
    }

    struct ParsedViewers {
        std::vector<std::string> roles;
        std::vector<std::string> users;
        std::vector<std::string> permissions;
    };

    static ParsedViewers parse_viewers(const std::string& input, stoatpp::cluster& bot, const std::string& server_id) {
        ParsedViewers v;
        if (input.empty() || input == "none" || input == "null") return v;

        std::vector<std::string> tokens;
        {
            std::stringstream outer(input);
            std::string chunk;
            while (std::getline(outer, chunk, ',')) {
                std::stringstream inner(chunk);
                std::string tok;
                while (inner >> tok) {
                    tokens.push_back(tok);
                }
            }
        }

        for (auto token : tokens) {
            if (token.empty()) continue;

            if (token.rfind("<@", 0) == 0 && token.back() == '>') {
                std::string uid = token.substr(2, token.size() - 3);
                if (uid.rfind("!", 0) == 0) uid = uid.substr(1);
                v.users.push_back(uid);
            }
            else if (token.rfind("<%", 0) == 0 && token.back() == '>') {
                std::string rid = token.substr(2, token.size() - 3);
                v.roles.push_back(rid);
            }
            else if (parse_permission_name(token) > 0) {
                int64_t perm_mask = parse_permission_name(token);
                std::string perm_name = "";
                if (perm_mask == stoatpp::permissions::ManageServer) perm_name = "ManageServer";
                else if (perm_mask == stoatpp::permissions::ManageChannels) perm_name = "ManageChannels";
                else if (perm_mask == stoatpp::permissions::ManageRole) perm_name = "ManageRole";
                else if (perm_mask == stoatpp::permissions::ManageMessages) perm_name = "ManageMessages";
                else if (perm_mask == stoatpp::permissions::KickMembers) perm_name = "KickMembers";
                else if (perm_mask == stoatpp::permissions::BanMembers) perm_name = "BanMembers";
                else perm_name = token;
                
                v.permissions.push_back(perm_name);
            }
            else {
                auto srv_opt = bot.get_server(server_id);
                std::string matched_role_id = "";
                if (srv_opt) {
                    for (const auto& r : srv_opt->roles) {
                        if (r.id == token || r.name == token) {
                            matched_role_id = r.id;
                            break;
                        }
                    }
                }
                if (!matched_role_id.empty()) {
                    v.roles.push_back(matched_role_id);
                } else {
                    if (token.size() == 26) {
                        v.users.push_back(token);
                    }
                }
            }
        }
        return v;
    }

    static std::string join_vec(const std::vector<std::string>& v) {
        std::string s = "";
        for (size_t i = 0; i < v.size(); ++i) {
            s += v[i] + (i + 1 < v.size() ? "," : "");
        }
        return s;
    }

    static std::string strip_mention(std::string s) {
        if (s.size() >= 4 && s[0] == '<' && (s[1] == '%' || s[1] == '@') && s.back() == '>') {
            s = s.substr(2, s.size() - 3);
            if (!s.empty() && s[0] == '!') s = s.substr(1);
        }
        return s;
    }

    static std::vector<std::string> split_str(const std::string& s) {
        std::vector<std::string> v;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, ',')) {
            if (!item.empty()) v.push_back(strip_mention(item));
        }
        return v;
    }

    // Clean custom emoji string to compare
    static std::string clean_emoji(std::string raw) {
        if (raw.size() == 26 && raw.find_first_not_of("0123456789ABCDEFGHJKMNPQRSTVWXYZabcdefghjkmnpqrstvwxyz") == std::string::npos) {
            return raw;
        }
        if (raw.size() > 2 && raw.front() == ':' && raw.back() == ':') {
            return raw.substr(1, raw.size() - 2);
        }
        return raw;
    }

    static void execute_create_app_channel(stoatpp::cluster& bot, const std::string& server_id, const std::string& user_id, Database::ApplicationPanel panel, const std::vector<std::string>& answers, std::function<void(std::string)> callback = nullptr) {
        std::thread([&bot, server_id, user_id, panel, answers, callback]() mutable {
            panel.app_counter++;
            g_db->save_application_panel(panel);

            auto user_opt = bot.get_user(user_id);
            auto server_opt = bot.get_server(server_id);
            if (!user_opt || !server_opt) {
                if (callback) callback("");
                return;
            }

            std::string name_scheme = panel.naming_scheme;
            if (name_scheme.empty()) name_scheme = "app-{number}";
            
            std::string channel_name = format_placeholders(name_scheme, *user_opt, *server_opt, panel.app_counter);
            std::transform(channel_name.begin(), channel_name.end(), channel_name.begin(), ::tolower);

            auto chan_res = bot.rest().create_channel(server_id, "Text", channel_name);
            if (!chan_res.success()) {
                std::cerr << "[bronx apps] failed to create channel: " << chan_res.error_message() << "\n";
                if (callback) callback("");
                return;
            }

            std::string app_channel_id = chan_res.body["_id"].get<std::string>();

            // Create role for user permissioning
            std::string user_role_name = "app-" + user_opt->username + "-" + std::to_string(panel.app_counter);
            auto role_res = bot.rest().create_role(server_id, user_role_name);
            std::string user_role_id = "";
            if (role_res.success()) {
                if (role_res.body.contains("id") && role_res.body["id"].is_string()) {
                    user_role_id = role_res.body["id"].get<std::string>();
                } else if (role_res.body.contains("role") && role_res.body["role"].is_object() && role_res.body["role"].contains("id") && role_res.body["role"]["id"].is_string()) {
                    user_role_id = role_res.body["role"]["id"].get<std::string>();
                }
            }

            if (!user_role_id.empty()) {
                bot.add_role_to_member(server_id, user_id, user_role_id);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            int64_t creator_allow = stoatpp::permissions::ViewChannel | 
                                    stoatpp::permissions::SendMessages | 
                                    stoatpp::permissions::EmbedLinks | 
                                    stoatpp::permissions::UploadFiles | 
                                    stoatpp::permissions::AddReactions;

            if (!user_role_id.empty()) {
                bot.rest().set_channel_permission(app_channel_id, user_role_id, creator_allow, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            // Set staff view permissions
            auto roles = split_str(panel.view_roles);
            for (const auto& rid : roles) {
                bot.rest().set_channel_permission(app_channel_id, rid, creator_allow | stoatpp::permissions::ManageMessages, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            auto users = split_str(panel.view_users);
            for (const auto& uid : users) {
                if (uid == bot.current_user().id) continue;
                bot.rest().set_channel_permission(app_channel_id, uid, creator_allow | stoatpp::permissions::ManageMessages, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            // Lock channel for @default/@member
            if (!panel.member_role_id.empty()) {
                int64_t lock_deny = stoatpp::permissions::ViewChannel |
                                    stoatpp::permissions::SendMessages |
                                    stoatpp::permissions::EmbedLinks |
                                    stoatpp::permissions::UploadFiles |
                                    stoatpp::permissions::AddReactions;
                bot.rest().set_channel_permission(app_channel_id, panel.member_role_id, 0, lock_deny);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            // Send explicit override for bot roles
            {
                std::string bot_id = bot.current_user().id;
                auto bot_mbr = bot.rest().get_server_member(server_id, bot_id);
                int64_t bot_allow = creator_allow | stoatpp::permissions::ManageMessages;
                if (bot_mbr.success() && bot_mbr.body.contains("roles") && bot_mbr.body["roles"].is_array()) {
                    for (const auto& rj : bot_mbr.body["roles"]) {
                        std::string rid = rj.get<std::string>();
                        bot.rest().set_channel_permission(app_channel_id, rid, bot_allow, 0);
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                }
            }

            // Post welcome / header
            stoatpp::models::MessagePayload payload;
            std::string welcome_txt = panel.welcome_content;
            if (welcome_txt.empty()) welcome_txt = "Welcome {user}! This is your private application channel.";
            payload.content = format_placeholders(welcome_txt, *user_opt, *server_opt, panel.app_counter);

            nlohmann::json embed = nlohmann::json::object();
            embed["title"] = "📝 Application for " + panel.name;
            embed["colour"] = "#2ECC71";

            std::string desc = "This application channel has been opened for " + user_opt->username + ".\n";
            if (!answers.empty()) {
                desc += "\n**Completed Application Answers:**\n";
                for (size_t i = 0; i < answers.size() && i < panel.questions.size(); ++i) {
                    desc += "\n**" + std::to_string(i+1) + ". " + panel.questions[i] + "**\n> " + answers[i] + "\n";
                }
            } else {
                desc += "Questions will now be asked in this channel sequentially. Answer below.";
            }
            embed["description"] = desc;
            payload.embeds.push_back(embed);
            payload.set_interactions({clean_emoji(panel.close_emoji)});

            bot.send_message(app_channel_id, payload, [&bot, app_channel_id, callback](stoatpp::models::Message res, bool ok) {
                if (callback) callback(app_channel_id);
            });
        }).detach();
    }

    static void execute_close_app_channel(stoatpp::cluster& bot, const std::string& channel_id) {
        bot.send_message(channel_id, "🔒 **Closing application channel...**\nThis channel will be deleted in 5 seconds.");

        std::thread([&bot, channel_id]() {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            bot.rest().delete_channel(channel_id);
        }).detach();
    }

    static void post_submission_to_output(stoatpp::cluster& bot, const Database::ApplicationPanel& panel, Database::ApplicationSubmission sub, const std::vector<std::string>& questions, const std::vector<std::string>& answers) {
        stoatpp::models::MessagePayload payload;
        
        auto user_opt = bot.get_user(sub.user_id);
        std::string username = user_opt ? user_opt->username : "Unknown User";

        nlohmann::json embed = nlohmann::json::object();
        embed["title"] = "🗒️ Submission: " + panel.name + " (#" + std::to_string(sub.id) + ")";
        embed["colour"] = "#F1C40F"; // pending gold

        std::string desc = "**Applicant:** <@" + sub.user_id + "> (" + username + ")\n"
                           "**Status:** " + sub.status + "\n\n";

        for (size_t i = 0; i < answers.size() && i < questions.size(); ++i) {
            desc += "**Q" + std::to_string(i+1) + ": " + questions[i] + "**\n> " + answers[i] + "\n\n";
        }
        embed["description"] = desc;
        payload.embeds.push_back(embed);

        // Add easy accept / reject reactions if staff want to react
        payload.set_interactions({"✅", "❌"});

        bot.send_message(panel.output_channel_id, payload, [sub](stoatpp::models::Message res, bool ok) mutable {
            if (ok) {
                sub.output_message_id = res.id;
                g_db->save_application_submission(sub);
            }
        });
    }

public:
    void register_handlers(stoatpp::cluster& bot) override {
        // Main Application management command
        bot.register_command({
            "app",
            {"apps", "application", "applications"},
            "manage and configure the application system.",
            "",
            {"<subcommand>", "[args...]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id, "⚠️ This command can only be used in a server channel.");
                    return;
                }

                if (!g_db) {
                    bot.send_message(msg.channel_id, "⚠️ Database is unavailable.");
                    return;
                }

                std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                
                if (args.empty()) {
                    bot.send_message(msg.channel_id, "📋 **Application System Commands:**\n"
                                                     "• `" + pfx + "app setup [panel]` — Interactive wizard to create a panel.\n"
                                                     "• `" + pfx + "app list` — List all configured panels.\n"
                                                     "• `" + pfx + "app config [panel]` — Show configuration for a panel.\n"
                                                     "• `" + pfx + "app delete [panel]` — Delete a panel config.\n"
                                                     "• `" + pfx + "app edit [panel] [key:val ...]` — Edit panel settings.\n"
                                                     "• `" + pfx + "app questions [panel] list/add/remove/edit` — Manage questions.\n"
                                                     "• `" + pfx + "app panel [panel] [#channel]` — Post the trigger panel embed.\n"
                                                     "• `" + pfx + "app submissions [panel]` — Browse submissions.\n"
                                                     "• `" + pfx + "app accept <id>` — Accept a submission.\n"
                                                     "• `" + pfx + "app reject <id>` — Reject a submission.\n"
                                                     "• `" + pfx + "app close` — Close private application channel.");
                    return;
                }

                std::string subcommand = args[0];
                std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);

                if (subcommand == "close") {
                    // Try to delete channel if it is a private application channel
                    // We check if the channel name starts with "app-"
                    auto chan_opt = bot.get_channel(msg.channel_id);
                    if (chan_opt && chan_opt->name && chan_opt->name->rfind("app-", 0) == 0) {
                        execute_close_app_channel(bot, msg.channel_id);
                    } else {
                        bot.send_message(msg.channel_id, "⚠️ This is not a registered application channel.");
                    }
                    return;
                }

                bot.fetch_member(msg.server_id, msg.author.id, [this, &bot, msg, pfx, args, subcommand](stoatpp::models::Member member, bool success) {
                    if (!success) {
                        bot.send_message(msg.channel_id, "⚠️ Failed to verify permissions.");
                        return;
                    }
                    if (!is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, member)) {
                        bot.send_message(msg.channel_id, "⚠️ Permission error: you need Manage Server permission to configure the application system.");
                        return;
                    }

                    if (subcommand == "list") {
                        auto panels = g_db->get_application_panels_for_server(msg.server_id);
                        if (panels.empty()) {
                            bot.send_message(msg.channel_id, "ℹ️ No application panels configured on this server.");
                            return;
                        }
                        
                        stoatpp::models::MessagePayload payload;
                        nlohmann::json embed = nlohmann::json::object();
                        embed["title"] = "📋 Configured Application Panels";
                        embed["colour"] = "#2ECC71";

                        std::string desc = "Here are the application panels set up on this server:\n\n";
                        for (const auto& p : panels) {
                            desc += "• **" + p.name + "** — Mode: `" + p.mode + "` | Trigger: `" + (p.trigger_command.empty() ? "(none)" : p.trigger_command) + "` | Questions: `" + std::to_string(p.questions.size()) + "`\n";
                        }
                        embed["description"] = desc;
                        payload.embeds.push_back(embed);
                        bot.send_message(msg.channel_id, payload);
                        return;
                    }

                    if (subcommand == "accept" || subcommand == "reject") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "usage: `" + pfx + "app " + subcommand + " <id>`");
                            return;
                        }
                        int sub_id = 0;
                        try { sub_id = std::stoi(args[1]); } catch(...) {
                            bot.send_message(msg.channel_id, "⚠️ Invalid submission ID.");
                            return;
                        }

                        auto sub_opt = g_db->get_application_submission_by_id(sub_id);
                        if (!sub_opt || sub_opt->server_id != msg.server_id) {
                            bot.send_message(msg.channel_id, "⚠️ Submission not found.");
                            return;
                        }

                        std::string new_status = (subcommand == "accept") ? "accepted" : "rejected";
                        g_db->update_application_submission_status(sub_id, new_status);

                        bot.send_message(msg.channel_id, "✅ Submission #" + std::to_string(sub_id) + " marked as **" + new_status + "**.");

                        // Optionally update output embed if we can find it
                        auto panel_opt = g_db->get_application_panel(msg.server_id, sub_opt->panel_name);
                        if (panel_opt && !sub_opt->output_message_id.empty() && !panel_opt->output_channel_id.empty()) {
                            bot.send_message(panel_opt->output_channel_id, "📢 Update: Submission #" + std::to_string(sub_id) + " has been **" + new_status + "** by <@" + msg.author.id + ">.");
                        }

                        // Give role if accepted and configured
                        if (new_status == "accepted" && panel_opt && panel_opt->role_to_give && !panel_opt->role_to_give_id.empty()) {
                            bot.add_role_to_member(msg.server_id, sub_opt->user_id, panel_opt->role_to_give_id);
                        }

                        // Try to DM the applicant
                        stoatpp::models::MessagePayload dm_payload;
                        dm_payload.content = "📢 Your application `" + sub_opt->panel_name + "` on server `" + (bot.get_server(msg.server_id) ? bot.get_server(msg.server_id)->name : "") + "` has been **" + new_status + "**!";
                        std::thread([&bot, sub_opt, dm_payload]() {
                            auto dm_res = bot.rest().open_dm(sub_opt->user_id);
                            if (dm_res.success()) {
                                std::string dm_chan_id = dm_res.body["_id"].get<std::string>();
                                bot.send_message(dm_chan_id, dm_payload);
                            }
                        }).detach();
                        return;
                    }

                    std::string panel_name = (args.size() > 1) ? args[1] : "default";

                    if (subcommand == "delete") {
                        if (g_db->delete_application_panel(msg.server_id, panel_name)) {
                            bot.send_message(msg.channel_id, "✅ Deleted application panel `" + panel_name + "`.");
                        } else {
                            bot.send_message(msg.channel_id, "⚠️ Failed to delete panel config.");
                        }
                        return;
                    }

                    if (subcommand == "config") {
                        auto p_opt = g_db->get_application_panel(msg.server_id, panel_name);
                        if (!p_opt) {
                            bot.send_message(msg.channel_id, "ℹ️ Application panel `" + panel_name + "` is not configured.");
                            return;
                        }
                        auto p = *p_opt;

                        stoatpp::models::MessagePayload payload;
                        nlohmann::json embed = nlohmann::json::object();
                        embed["title"] = "📋 Config: `" + panel_name + "`";
                        embed["colour"] = "#2ECC71";

                        std::string desc = "• **Mode:** `" + p.mode + "`\n"
                                           "• **Trigger Command:** `" + p.trigger_command + "`\n"
                                           "• **Trigger Emoji:** " + (p.trigger_emoji.empty() ? "*None*" : p.trigger_emoji) + "\n"
                                           "• **Panel Channel:** <#" + p.modal_channel_id + ">\n"
                                           "• **Panel Message ID:** `" + p.modal_message_id + "`\n"
                                           "• **Output/Staff Channel:** <#" + p.output_channel_id + ">\n"
                                           "• **Require Role:** " + (p.require_role ? "Yes (<%" + p.require_role_id + ">)" : "No") + "\n"
                                           "• **Role on Accept:** " + (p.role_to_give ? "Yes (<%" + p.role_to_give_id + ">)" : "No") + "\n"
                                           "• **End Message:** `" + (p.end_message.empty() ? "(default)" : p.end_message) + "`\n"
                                           "• **Questions Count:** " + std::to_string(p.questions.size()) + "\n";
                        
                        if (!p.questions.empty()) {
                            desc += "\n**Questions:**\n";
                            for (size_t i = 0; i < p.questions.size(); ++i) {
                                desc += std::to_string(i+1) + ". " + p.questions[i] + "\n";
                            }
                        }

                        embed["description"] = desc;
                        payload.embeds.push_back(embed);
                        bot.send_message(msg.channel_id, payload);
                        return;
                    }

                    if (subcommand == "submissions") {
                        auto subs = g_db->get_application_submissions(msg.server_id, panel_name);
                        if (subs.empty()) {
                            bot.send_message(msg.channel_id, "ℹ️ No submissions found for panel `" + panel_name + "`.");
                            return;
                        }

                        stoatpp::models::MessagePayload payload;
                        nlohmann::json embed = nlohmann::json::object();
                        embed["title"] = "📋 Submissions: `" + panel_name + "`";
                        embed["colour"] = "#2ECC71";

                        std::string desc = "Latest submissions:\n\n";
                        for (const auto& s : subs) {
                            desc += "• **#" + std::to_string(s.id) + "** — Applicant: <@" + s.user_id + "> | Status: `" + s.status + "` | Submitted: " + helpers::format_epoch_ms(s.submitted_at) + "\n";
                        }
                        embed["description"] = desc;
                        payload.embeds.push_back(embed);
                        bot.send_message(msg.channel_id, payload);
                        return;
                    }

                    if (subcommand == "panel") {
                        auto p_opt = g_db->get_application_panel(msg.server_id, panel_name);
                        if (!p_opt) {
                            bot.send_message(msg.channel_id, "⚠️ Application panel `" + panel_name + "` is not configured.");
                            return;
                        }
                        
                        std::string target_ch = p_opt->modal_channel_id;
                        if (args.size() > 2) {
                            std::string target_arg = args[2];
                            if (target_arg.rfind("<#", 0) == 0 && target_arg.back() == '>') {
                                target_ch = target_arg.substr(2, target_arg.size() - 3);
                            } else {
                                target_ch = target_arg;
                            }
                        }

                        if (target_ch.empty()) {
                            bot.send_message(msg.channel_id, "⚠️ Please specify a target channel or configure one for the panel.");
                            return;
                        }

                        stoatpp::models::MessagePayload panel_payload;
                        panel_payload.content = p_opt->modal_content.empty() ? " " : p_opt->modal_content;

                        nlohmann::json embed = nlohmann::json::object();
                        if (!p_opt->modal_embed_json.empty()) {
                            try { embed = nlohmann::json::parse(p_opt->modal_embed_json); } catch(...) {}
                        } else {
                            embed["title"] = "📋 Submit an Application: " + p_opt->name;
                            embed["description"] = "React with " + (p_opt->trigger_emoji.empty() ? "📩" : p_opt->trigger_emoji) + " below or use command `" + pfx + (p_opt->trigger_command.empty() ? "apply" : p_opt->trigger_command) + "` to begin.";
                            embed["colour"] = "#2ECC71";
                        }
                        panel_payload.embeds.push_back(embed);

                        std::string emoji = p_opt->trigger_emoji.empty() ? "📩" : clean_emoji(p_opt->trigger_emoji);
                        panel_payload.set_interactions({emoji});

                        auto panel_conf = *p_opt;
                        bot.send_message(target_ch, panel_payload, [&bot, msg, panel_conf, target_ch](stoatpp::models::Message res, bool ok) mutable {
                            if (ok) {
                                panel_conf.modal_message_id = res.id;
                                panel_conf.modal_channel_id = target_ch;
                                g_db->save_application_panel(panel_conf);
                                bot.send_message(msg.channel_id, "✅ Panel message sent to <#" + target_ch + ">.");
                            } else {
                                bot.send_message(msg.channel_id, "⚠️ Failed to send panel embed.");
                            }
                        });
                        return;
                    }

                    if (subcommand == "questions") {
                        if (args.size() < 3) {
                            bot.send_message(msg.channel_id, "usage:\n"
                                                             "• `" + pfx + "app questions " + panel_name + " list`\n"
                                                             "• `" + pfx + "app questions " + panel_name + " add <question>`\n"
                                                             "• `" + pfx + "app questions " + panel_name + " remove <number>`\n"
                                                             "• `" + pfx + "app questions " + panel_name + " edit <number> <new question>`");
                            return;
                        }

                        auto p_opt = g_db->get_application_panel(msg.server_id, panel_name);
                        if (!p_opt) {
                            bot.send_message(msg.channel_id, "⚠️ Panel not found.");
                            return;
                        }
                        auto p = *p_opt;

                        std::string q_action = args[2];
                        std::transform(q_action.begin(), q_action.end(), q_action.begin(), ::tolower);

                        if (q_action == "list") {
                            if (p.questions.empty()) {
                                bot.send_message(msg.channel_id, "ℹ️ No questions configured for panel `" + panel_name + "`.");
                                return;
                            }
                            std::string q_txt = "📋 **Questions for panel `" + panel_name + "`:**\n\n";
                            for (size_t i = 0; i < p.questions.size(); ++i) {
                                q_txt += "**" + std::to_string(i+1) + ".** " + p.questions[i] + "\n";
                            }
                            bot.send_message(msg.channel_id, q_txt);
                            return;
                        }

                        if (q_action == "add") {
                            if (args.size() < 4) {
                                bot.send_message(msg.channel_id, "⚠️ Please provide question text.");
                                return;
                            }
                            std::string q_text;
                            for (size_t i = 3; i < args.size(); ++i) {
                                if (i > 3) q_text += " ";
                                q_text += args[i];
                            }
                            p.questions.push_back(q_text);
                            if (g_db->save_application_panel(p)) {
                                bot.send_message(msg.channel_id, "✅ Added question: `" + q_text + "`.");
                            } else {
                                bot.send_message(msg.channel_id, "⚠️ Failed to save.");
                            }
                            return;
                        }

                        if (q_action == "remove") {
                            if (args.size() < 4) {
                                bot.send_message(msg.channel_id, "usage: `app questions " + panel_name + " remove <number>`");
                                return;
                            }
                            int q_num = std::stoi(args[3]);
                            if (q_num < 1 || q_num > (int)p.questions.size()) {
                                bot.send_message(msg.channel_id, "⚠️ Question number out of bounds.");
                                return;
                            }
                            std::string removed = p.questions[q_num-1];
                            p.questions.erase(p.questions.begin() + (q_num-1));
                            if (g_db->save_application_panel(p)) {
                                bot.send_message(msg.channel_id, "✅ Removed question " + std::to_string(q_num) + ": `" + removed + "`.");
                            } else {
                                bot.send_message(msg.channel_id, "⚠️ Failed to save.");
                            }
                            return;
                        }

                        if (q_action == "edit") {
                            if (args.size() < 5) {
                                bot.send_message(msg.channel_id, "usage: `app questions " + panel_name + " edit <number> <new text>`");
                                return;
                            }
                            int q_num = std::stoi(args[3]);
                            if (q_num < 1 || q_num > (int)p.questions.size()) {
                                bot.send_message(msg.channel_id, "⚠️ Question number out of bounds.");
                                return;
                            }
                            std::string q_text;
                            for (size_t i = 4; i < args.size(); ++i) {
                                if (i > 4) q_text += " ";
                                q_text += args[i];
                            }
                            p.questions[q_num-1] = q_text;
                            if (g_db->save_application_panel(p)) {
                                bot.send_message(msg.channel_id, "✅ Updated question " + std::to_string(q_num) + " to: `" + q_text + "`.");
                            } else {
                                bot.send_message(msg.channel_id, "⚠️ Failed to save.");
                            }
                            return;
                        }

                        bot.send_message(msg.channel_id, "⚠️ Unknown questions action. Use list, add, remove, edit.");
                        return;
                    }

                    if (subcommand == "edit") {
                        size_t kv_start = 2;
                        if (args.size() <= kv_start) {
                            bot.send_message(msg.channel_id, "✏️ **Edit panel `" + panel_name + "` keys:**\n"
                                                             "• `trigger:<cmd>` (trigger command)\n"
                                                             "• `emoji:<emoji>` (trigger reaction emoji)\n"
                                                             "• `channel:<#channel>` (panel target channel)\n"
                                                             "• `output:<#channel>` (submissions channel)\n"
                                                             "• `mode:<post/ticket/channel>` (application mode)\n"
                                                             "• `viewers:<roles/users>` (staff permission roles)\n"
                                                             "• `require_role:<role_id>` (role restriction / `none` to disable)\n"
                                                             "• `role:<role_id>` (role given on accept / `none` to disable)\n"
                                                             "• `end_message:<text>` (completion message)\n"
                                                             "• `close_emoji:<emoji>`\n"
                                                             "• `welcome:<text>` (welcome message content)\n\n"
                                                             "usage: `app edit " + panel_name + " trigger:apply mode:ticket` (supports multiples)");
                            return;
                        }

                        auto p_opt = g_db->get_application_panel(msg.server_id, panel_name);
                        if (!p_opt) {
                            bot.send_message(msg.channel_id, "⚠️ Panel not found.");
                            return;
                        }
                        auto p = *p_opt;

                        std::vector<std::string> notes;
                        for (size_t i = kv_start; i < args.size(); ++i) {
                            std::string pair = args[i];
                            size_t colon = pair.find(':');
                            if (colon == std::string::npos) continue;
                            std::string k = pair.substr(0, colon);
                            std::string v = pair.substr(colon + 1);

                            if (k == "trigger") {
                                p.trigger_command = v;
                                notes.push_back("trigger→`" + v + "`");
                            } else if (k == "emoji") {
                                p.trigger_emoji = v;
                                notes.push_back("emoji→" + v);
                            } else if (k == "channel") {
                                std::string chan_id = v;
                                if (chan_id.rfind("<#", 0) == 0 && chan_id.back() == '>') {
                                    chan_id = chan_id.substr(2, chan_id.size() - 3);
                                }
                                p.modal_channel_id = chan_id;
                                notes.push_back("channel→<#" + chan_id + ">");
                            } else if (k == "output") {
                                std::string chan_id = v;
                                if (chan_id.rfind("<#", 0) == 0 && chan_id.back() == '>') {
                                    chan_id = chan_id.substr(2, chan_id.size() - 3);
                                }
                                p.output_channel_id = chan_id;
                                notes.push_back("output→<#" + chan_id + ">");
                            } else if (k == "mode") {
                                if (v == "post" || v == "ticket" || v == "channel") {
                                    p.mode = v;
                                    notes.push_back("mode→`" + v + "`");
                                }
                            } else if (k == "viewers") {
                                ParsedViewers pv = parse_viewers(v, bot, msg.server_id);
                                p.view_roles = join_vec(pv.roles);
                                p.view_users = join_vec(pv.users);
                                p.view_permissions = join_vec(pv.permissions);
                                notes.push_back("viewers updated");
                            } else if (k == "require_role") {
                                if (v == "none" || v == "clear") {
                                    p.require_role = false;
                                    p.require_role_id = "";
                                    notes.push_back("require_role disabled");
                                } else {
                                    std::string role_id = v;
                                    if (role_id.rfind("<%", 0) == 0 && role_id.back() == '>') {
                                        role_id = role_id.substr(2, role_id.size() - 3);
                                    }
                                    p.require_role = true;
                                    p.require_role_id = role_id;
                                    notes.push_back("require_role→<%" + role_id + ">");
                                }
                            } else if (k == "role") {
                                if (v == "none" || v == "clear") {
                                    p.role_to_give = false;
                                    p.role_to_give_id = "";
                                    notes.push_back("role_to_give disabled");
                                } else {
                                    std::string role_id = v;
                                    if (role_id.rfind("<%", 0) == 0 && role_id.back() == '>') {
                                        role_id = role_id.substr(2, role_id.size() - 3);
                                    } else if (role_id.rfind("<@&", 0) == 0 && role_id.back() == '>') {
                                        role_id = role_id.substr(3, role_id.size() - 4);
                                    }
                                    p.role_to_give = true;
                                    p.role_to_give_id = role_id;
                                    notes.push_back("role_to_give→<%" + role_id + ">");
                                }
                            } else if (k == "end_message" || k == "end_msg") {
                                p.end_message = v;
                                notes.push_back("end_message updated");
                            } else if (k == "close_emoji") {
                                p.close_emoji = v;
                                notes.push_back("close_emoji→" + v);
                            } else if (k == "welcome") {
                                p.welcome_content = v;
                                notes.push_back("welcome updated");
                            }
                        }

                        if (g_db->save_application_panel(p)) {
                            std::string note_str;
                            for (size_t i = 0; i < notes.size(); ++i) {
                                if (i) note_str += ", ";
                                note_str += notes[i];
                            }
                            bot.send_message(msg.channel_id, "✅ Updated panel `" + panel_name + "`: " + note_str);
                        } else {
                            bot.send_message(msg.channel_id, "⚠️ Failed to save.");
                        }
                        return;
                    }

                    if (subcommand == "setup") {
                        bot.send_message(msg.channel_id, "💬 **Application Setup Wizard** [Panel: `" + panel_name + "`]\nLet's configure this application panel.\n\n**Step 1/8**: Mention the channel where the trigger panel should be sent (e.g. #tickets), or type its ID/name. Reply `skip` to configure triggers manually later.");
                        bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(60), [this, &bot, msg, panel_name](std::optional<stoatpp::events::Message> rep1) {
                            if (!rep1 || rep1->content == "cancel") {
                                bot.send_message(msg.channel_id, "🚫 Setup canceled.");
                                return;
                            }
                            std::string target_channel = "";
                            if (rep1->content != "skip") {
                                std::string chan_resp = rep1->content;
                                if (chan_resp.rfind("<#", 0) == 0 && chan_resp.back() == '>') {
                                    target_channel = chan_resp.substr(2, chan_resp.size() - 3);
                                } else {
                                    auto server_channels_res = bot.rest().get_server_channels(msg.server_id);
                                    if (server_channels_res.success() && server_channels_res.body.is_array()) {
                                        for (const auto& c_json : server_channels_res.body) {
                                            auto c = stoatpp::models::Channel::from_json(c_json);
                                            if (c.id == chan_resp || (c.name && *c.name == chan_resp)) {
                                                target_channel = c.id;
                                                break;
                                            }
                                        }
                                    }
                                }
                                if (target_channel.empty()) {
                                    bot.send_message(msg.channel_id, "⚠️ Invalid channel. Setup canceled.");
                                    return;
                                }
                            }

                            bot.send_message(msg.channel_id, "💬 **Application Setup Wizard**\n**Step 2/8**: Type the **mode** for this panel:\n• `post` — DM wizard -> post completed application to a staff channel.\n• `ticket` — DM wizard -> open a private channel with the applicant.\n• `channel` — Open a private channel -> ask questions directly inside it.");
                            bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(60), [this, &bot, msg, panel_name, target_channel](std::optional<stoatpp::events::Message> rep2) {
                                if (!rep2 || rep2->content == "cancel") {
                                    bot.send_message(msg.channel_id, "🚫 Setup canceled.");
                                    return;
                                }
                                std::string mode = rep2->content;
                                std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
                                if (mode != "post" && mode != "ticket" && mode != "channel") {
                                    mode = "post";
                                }

                                bot.send_message(msg.channel_id, "💬 **Application Setup Wizard**\n**Step 3/8**: Type the **trigger command** name (e.g. `apply` — users can run `.apply` or `!apply` to start, or reply `skip` to disable command trigger).");
                                bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(60), [this, &bot, msg, panel_name, target_channel, mode](std::optional<stoatpp::events::Message> rep3) {
                                    if (!rep3 || rep3->content == "cancel") {
                                        bot.send_message(msg.channel_id, "🚫 Setup canceled.");
                                        return;
                                    }
                                    std::string trigger_cmd = rep3->content == "skip" ? "" : rep3->content;
                                    if (!trigger_cmd.empty() && trigger_cmd[0] == '.') trigger_cmd = trigger_cmd.substr(1);
                                    if (!trigger_cmd.empty() && trigger_cmd[0] == '!') trigger_cmd = trigger_cmd.substr(1);

                                    bot.send_message(msg.channel_id, "💬 **Application Setup Wizard**\n**Step 4/8**: Type the **trigger reaction emoji** (e.g. `📩`, or reply `skip` to disable reaction trigger).");
                                    bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(60), [this, &bot, msg, panel_name, target_channel, mode, trigger_cmd](std::optional<stoatpp::events::Message> rep4) {
                                        if (!rep4 || rep4->content == "cancel") {
                                            bot.send_message(msg.channel_id, "🚫 Setup canceled.");
                                            return;
                                        }
                                        std::string trigger_emoji = rep4->content == "skip" ? "" : rep4->content;

                                        bot.send_message(msg.channel_id, "💬 **Application Setup Wizard**\n**Step 5/8**: Mention the **output/staff channel** where completed apps or ticket logs should go (e.g. #staff-logs), or type its ID/name. Reply `skip` to use the current channel.");
                                        bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(60), [this, &bot, msg, panel_name, target_channel, mode, trigger_cmd, trigger_emoji](std::optional<stoatpp::events::Message> rep5) {
                                            if (!rep5 || rep5->content == "cancel") {
                                                bot.send_message(msg.channel_id, "🚫 Setup canceled.");
                                                return;
                                            }
                                            std::string output_channel = msg.channel_id;
                                            if (rep5->content != "skip") {
                                                std::string chan_resp = rep5->content;
                                                if (chan_resp.rfind("<#", 0) == 0 && chan_resp.back() == '>') {
                                                    output_channel = chan_resp.substr(2, chan_resp.size() - 3);
                                                } else {
                                                    auto server_channels_res = bot.rest().get_server_channels(msg.server_id);
                                                    if (server_channels_res.success() && server_channels_res.body.is_array()) {
                                                        for (const auto& c_json : server_channels_res.body) {
                                                            stoatpp::models::Channel c = stoatpp::models::Channel::from_json(c_json);
                                                            if (c.id == chan_resp || (c.name && *c.name == chan_resp)) {
                                                                output_channel = c.id;
                                                                break;
                                                            }
                                                        }
                                                    }
                                                }
                                            }

                                            bot.send_message(msg.channel_id, "💬 **Application Setup Wizard**\n**Step 6/8**: Type the **Title** of the panel embed (or reply `skip` for default: `📋 Submit an Application`).");
                                            bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(60), [this, &bot, msg, panel_name, target_channel, mode, trigger_cmd, trigger_emoji, output_channel](std::optional<stoatpp::events::Message> rep6) {
                                                if (!rep6 || rep6->content == "cancel") {
                                                    bot.send_message(msg.channel_id, "🚫 Setup canceled.");
                                                    return;
                                                }
                                                std::string emb_title = rep6->content == "skip" ? "📋 Submit an Application" : rep6->content;

                                                bot.send_message(msg.channel_id, "💬 **Application Setup Wizard**\n**Step 7/8**: Type the **Description** of the panel embed (or reply `skip` for default).");
                                                bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(60), [this, &bot, msg, panel_name, target_channel, mode, trigger_cmd, trigger_emoji, output_channel, emb_title](std::optional<stoatpp::events::Message> rep7) {
                                                    if (!rep7 || rep7->content == "cancel") {
                                                        bot.send_message(msg.channel_id, "🚫 Setup canceled.");
                                                        return;
                                                    }
                                                    std::string emb_desc = rep7->content == "skip" ? "Answer the form to submit your application." : rep7->content;

                                                    bot.send_message(msg.channel_id, "💬 **Application Setup Wizard**\n**Step 8/8**: Type the **first question** for this application, or reply `done` to finish.");
                                                    
                                                    // Use shared pointers to hold mutables across asynchronous wizard steps
                                                    auto questions = std::make_shared<std::vector<std::string>>();
                                                    
                                                    // Define a recursive capture lambda to loop questions
                                                    auto ask_loop = std::make_shared<std::function<void(std::optional<stoatpp::events::Message>)>>();
                                                    
                                                    *ask_loop = [this, &bot, msg, panel_name, target_channel, mode, trigger_cmd, trigger_emoji, output_channel, emb_title, emb_desc, questions, ask_loop](std::optional<stoatpp::events::Message> rep8) {
                                                        if (!rep8) {
                                                            bot.send_message(msg.channel_id, "⏱️ Setup timed out. Setup canceled.");
                                                            return;
                                                        }
                                                        if (rep8->content == "cancel") {
                                                            bot.send_message(msg.channel_id, "🚫 Setup canceled.");
                                                            return;
                                                        }
                                                        if (rep8->content == "done") {
                                                            // Save config!
                                                            Database::ApplicationPanel p;
                                                            p.server_id = msg.server_id;
                                                            p.name = panel_name;
                                                            p.trigger_command = trigger_cmd;
                                                            p.trigger_emoji = trigger_emoji;
                                                            p.modal_channel_id = target_channel;
                                                            p.output_channel_id = output_channel;
                                                            p.mode = mode;
                                                            p.questions = *questions;

                                                            nlohmann::json embed_obj;
                                                            embed_obj["title"] = emb_title;
                                                            embed_obj["description"] = emb_desc;
                                                            embed_obj["colour"] = "#2ECC71";
                                                            p.modal_embed_json = embed_obj.dump();

                                                            if (g_db->save_application_panel(p)) {
                                                                bot.send_message(msg.channel_id, "✅ **Setup complete!** Saved panel `" + panel_name + "` with " + std::to_string(questions->size()) + " questions.");
                                                                // Automatically send panel if channel is configured
                                                                if (!target_channel.empty()) {
                                                                    bot.send_message(msg.channel_id, "📢 Sending panel...");
                                                                    
                                                                    stoatpp::models::MessagePayload panel_payload;
                                                                    panel_payload.content = " ";
                                                                    nlohmann::json embed_panel = nlohmann::json::object();
                                                                    embed_panel["title"] = emb_title;
                                                                    embed_panel["description"] = emb_desc;
                                                                    embed_panel["colour"] = "#2ECC71";
                                                                    panel_payload.embeds.push_back(embed_panel);
                                                                    
                                                                    std::string emoji = trigger_emoji.empty() ? "📩" : clean_emoji(trigger_emoji);
                                                                    panel_payload.set_interactions({emoji});
                                                                    
                                                                    bot.send_message(target_channel, panel_payload, [&bot, msg, p, target_channel](stoatpp::models::Message res, bool ok) mutable {
                                                                        if (ok) {
                                                                            p.modal_message_id = res.id;
                                                                            p.modal_channel_id = target_channel;
                                                                            g_db->save_application_panel(p);
                                                                            bot.send_message(msg.channel_id, "✅ Panel message sent to <#" + target_channel + ">.");
                                                                        }
                                                                    });
                                                                }
                                                            } else {
                                                                bot.send_message(msg.channel_id, "⚠️ Failed to save panel.");
                                                            }
                                                            return;
                                                        }

                                                        questions->push_back(rep8->content);
                                                        bot.send_message(msg.channel_id, "💬 Added Q" + std::to_string(questions->size()) + ". Type the **next question**, or type `done` to finish.");
                                                        bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(90), *ask_loop);
                                                    };

                                                    bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(90), *ask_loop);
                                                });
                                            });
                                        });
                                    });
                                });
                            });
                        });
                        return;
                    }

                    bot.send_message(msg.channel_id, "⚠️ Unknown subcommand.");
                });
            },
            "Utility"
        });

        // Listen for trigger reaction clicks
        bot.on_message_react([&bot](const stoatpp::events::MessageReact& e) {
            if (e.user_id == bot.current_user().id) return;
            if (!g_db) return;

            std::string cleaned_emoji = clean_emoji(e.emoji_id);

            // Handle Accept/Reject reactions on submission posts in the staff channel
            if (cleaned_emoji == "✅" || cleaned_emoji == "❌") {
                auto sub_opt = g_db->get_application_submission_by_message_id(e.id);
                if (sub_opt) {
                    bot.fetch_member(sub_opt->server_id, e.user_id, [&bot, sub_opt, cleaned_emoji, e](stoatpp::models::Member member, bool success) {
                        if (success && is_user_trusted_or_owner(bot, sub_opt->server_id, e.user_id, member)) {
                            std::string new_status = (cleaned_emoji == "✅") ? "accepted" : "rejected";
                            if (sub_opt->status == new_status) return;

                            g_db->update_application_submission_status(sub_opt->id, new_status);

                            // Update staff logs channel
                            auto panel_opt = g_db->get_application_panel(sub_opt->server_id, sub_opt->panel_name);
                            if (panel_opt && !sub_opt->output_message_id.empty() && !panel_opt->output_channel_id.empty()) {
                                bot.send_message(panel_opt->output_channel_id, "📢 Update: Submission #" + std::to_string(sub_opt->id) + " has been **" + new_status + "** by <@" + e.user_id + ">.");
                            }

                            // Give role if accepted and configured
                            if (new_status == "accepted" && panel_opt && panel_opt->role_to_give && !panel_opt->role_to_give_id.empty()) {
                                bot.add_role_to_member(sub_opt->server_id, sub_opt->user_id, panel_opt->role_to_give_id);
                            }

                            // DM applicant
                            stoatpp::models::MessagePayload dm_payload;
                            dm_payload.content = "📢 Your application `" + sub_opt->panel_name + "` on server `" + (bot.get_server(sub_opt->server_id) ? bot.get_server(sub_opt->server_id)->name : "") + "` has been **" + new_status + "**!";
                            std::thread([&bot, sub_opt, dm_payload]() {
                                auto dm_res = bot.rest().open_dm(sub_opt->user_id);
                                if (dm_res.success()) {
                                    std::string dm_chan_id = dm_res.body["_id"].get<std::string>();
                                    bot.send_message(dm_chan_id, dm_payload);
                                }
                            }).detach();
                        }
                    });
                    return;
                }
            }

            // Fetch all panels for this message react
            auto panels = g_db->get_all_application_panels();
            for (const auto& panel : panels) {
                std::string target_emoji = clean_emoji(panel.trigger_emoji);
                if (e.id == panel.modal_message_id && !target_emoji.empty() && cleaned_emoji == target_emoji) {
                    bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
                    
                    {
                        std::lock_guard<std::mutex> lock(g_active_apps_mutex);
                        if (g_active_apps.find(e.user_id) != g_active_apps.end()) {
                            stoatpp::models::MessagePayload err;
                            err.content = "⚠️ You already have an active application session running! Complete or cancel that one first.";
                            err.delete_after = 5;
                            bot.send_message(e.channel_id, err);
                            return;
                        }
                    }

                    if (panel.require_role && !panel.require_role_id.empty()) {
                        auto mem_opt = bot.get_member(panel.server_id, e.user_id);
                        if (mem_opt) {
                            bool has_role = std::find(mem_opt->roles.begin(), mem_opt->roles.end(), panel.require_role_id) != mem_opt->roles.end();
                            if (!has_role) {
                                stoatpp::models::MessagePayload err;
                                err.content = "⚠️ You do not meet the role requirements to apply for `" + panel.name + "`.";
                                err.delete_after = 5;
                                bot.send_message(e.channel_id, err);
                                return;
                            }
                        }
                    }

                    start_application_flow(bot, panel.server_id, e.user_id, panel);
                    return;
                }
            }

            // Close reactions on application channels
            auto chan_opt = bot.get_channel(e.channel_id);
            if (chan_opt && chan_opt->name && chan_opt->name->rfind("app-", 0) == 0) {
                if (cleaned_emoji == "🔒") {
                    execute_close_app_channel(bot, e.channel_id);
                }
            }
        });

        // Scan all messages for trigger commands, cancel keyword, and answers
        bot.on_message([this, &bot](const stoatpp::events::Message& msg) {
            if (msg.author.bot) return;
            if (!g_db) return;

            AppSession session;
            bool found_session = false;
            {
                std::lock_guard<std::mutex> lock(g_active_apps_mutex);
                auto it = g_active_apps.find(msg.author.id);
                if (it != g_active_apps.end()) {
                    session = it->second;
                    found_session = true;
                }
            }

            if (found_session) {
                bool valid_channel = false;
                if (session.in_dm && msg.server_id.empty() && msg.channel_id == session.current_channel_id) {
                    valid_channel = true;
                } else if (!session.in_dm && msg.channel_id == session.current_channel_id) {
                    valid_channel = true;
                }

                if (valid_channel) {
                    if (msg.content == "cancel") {
                        bot.send_message(msg.channel_id, "🚫 Application canceled.");
                        {
                            std::lock_guard<std::mutex> lock(g_active_apps_mutex);
                            g_active_apps.erase(msg.author.id);
                        }
                        if (!session.in_dm) {
                            execute_close_app_channel(bot, session.current_channel_id);
                        }
                        return;
                    }

                    {
                        std::lock_guard<std::mutex> lock(g_active_apps_mutex);
                        g_active_apps[msg.author.id].answers.push_back(msg.content);
                        g_active_apps[msg.author.id].current_question_index++;
                        session = g_active_apps[msg.author.id];
                    }

                    auto p_opt = g_db->get_application_panel(session.server_id, session.panel_name);
                    if (!p_opt) {
                        bot.send_message(msg.channel_id, "⚠️ Application panel config missing. Aborting.");
                        std::lock_guard<std::mutex> lock(g_active_apps_mutex);
                        g_active_apps.erase(msg.author.id);
                        return;
                    }
                    auto panel = *p_opt;

                    if (session.current_question_index < panel.questions.size()) {
                        bot.send_message(msg.channel_id, "📋 **Question " + std::to_string(session.current_question_index + 1) + "/" + std::to_string(panel.questions.size()) + "**:\n" + panel.questions[session.current_question_index]);
                    } else {
                        {
                            std::lock_guard<std::mutex> lock(g_active_apps_mutex);
                            g_active_apps.erase(msg.author.id);
                        }

                        Database::ApplicationSubmission sub;
                        sub.server_id = session.server_id;
                        sub.panel_name = session.panel_name;
                        sub.user_id = session.user_id;
                        sub.submitted_at = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                        
                        nlohmann::json ans_json = nlohmann::json::array();
                        for (size_t i = 0; i < session.answers.size(); ++i) {
                            nlohmann::json item;
                            item["question"] = panel.questions[i];
                            item["answer"] = session.answers[i];
                            ans_json.push_back(item);
                        }
                        sub.answers_json = ans_json.dump();
                        sub.status = "pending";

                        if (panel.mode == "post") {
                            std::string end_msg = panel.end_message.empty() ? "✅ **Application submitted!** Staff will review it shortly." : panel.end_message;
                            bot.send_message(msg.channel_id, end_msg);
                            g_db->save_application_submission(sub);
                            post_submission_to_output(bot, panel, sub, panel.questions, session.answers);
                        } else if (panel.mode == "ticket") {
                            bot.send_message(msg.channel_id, "⌛ **Application completed!** Creating your private channel...");
                            execute_create_app_channel(bot, session.server_id, session.user_id, panel, session.answers, [&bot, msg, sub](std::string channel_id) mutable {
                                if (!channel_id.empty()) {
                                    sub.ticket_channel_id = channel_id;
                                    g_db->save_application_submission(sub);
                                    bot.send_message(msg.channel_id, "✅ Your private application channel has been created: <#" + channel_id + ">!");
                                } else {
                                    bot.send_message(msg.channel_id, "⚠️ Failed to create the private channel. Please contact an admin.");
                                }
                            });
                        } else if (panel.mode == "channel") {
                            std::string end_msg = panel.end_message.empty() ? "✅ **Application completed!** Staff will review it here." : panel.end_message;
                            bot.send_message(msg.channel_id, end_msg);
                            sub.ticket_channel_id = session.current_channel_id;
                            g_db->save_application_submission(sub);

                            nlohmann::json summary = nlohmann::json::object();
                            summary["title"] = "📋 Completed Application Summary";
                            summary["colour"] = "#2ECC71";
                            std::string sum_desc = "Submission #" + std::to_string(sub.id) + " is now reviewable.\n\n";
                            for (size_t i = 0; i < panel.questions.size(); ++i) {
                                sum_desc += "**" + std::to_string(i+1) + ". " + panel.questions[i] + "**\n> " + session.answers[i] + "\n\n";
                            }
                            summary["description"] = sum_desc;
                            stoatpp::models::MessagePayload sum_pl;
                            sum_pl.embeds.push_back(summary);
                            bot.send_message(msg.channel_id, sum_pl);
                        }
                    }
                    return;
                }
            }

            if (!msg.server_id.empty()) {
                auto panels = g_db->get_application_panels_for_server(msg.server_id);
                for (const auto& panel : panels) {
                    if (panel.trigger_command.empty()) continue;

                    std::string user_cmd = msg.content;
                    std::string cleaned_trig = panel.trigger_command;
                    std::transform(user_cmd.begin(), user_cmd.end(), user_cmd.begin(), ::tolower);
                    std::transform(cleaned_trig.begin(), cleaned_trig.end(), cleaned_trig.begin(), ::tolower);

                    std::string bot_pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                    std::transform(bot_pfx.begin(), bot_pfx.end(), bot_pfx.begin(), ::tolower);

                    bool matched = (user_cmd == cleaned_trig || user_cmd == (bot_pfx + cleaned_trig));

                    if (matched) {
                        bot.delete_message(msg.channel_id, msg.id);
                        {
                            std::lock_guard<std::mutex> lock(g_active_apps_mutex);
                            if (g_active_apps.find(msg.author.id) != g_active_apps.end()) {
                                stoatpp::models::MessagePayload err;
                                err.content = "⚠️ You already have an active application session running! Complete or cancel that one first.";
                                err.delete_after = 5;
                                bot.send_message(msg.channel_id, err);
                                return;
                            }
                        }

                        if (panel.require_role && !panel.require_role_id.empty()) {
                            auto mem_opt = bot.get_member(panel.server_id, msg.author.id);
                            if (mem_opt) {
                                bool has_role = std::find(mem_opt->roles.begin(), mem_opt->roles.end(), panel.require_role_id) != mem_opt->roles.end();
                                if (!has_role) {
                                    stoatpp::models::MessagePayload err;
                                    err.content = "⚠️ You do not meet the role requirements to apply for `" + panel.name + "`.";
                                    err.delete_after = 5;
                                    bot.send_message(msg.channel_id, err);
                                    return;
                                }
                            }
                        }

                        start_application_flow(bot, msg.server_id, msg.author.id, panel, msg.channel_id);
                        return;
                    }
                }
            }
        });
    }

private:
    static void start_application_flow(stoatpp::cluster& bot, const std::string& server_id, const std::string& user_id, const Database::ApplicationPanel& panel, const std::string& source_channel_id = "") {
        if (panel.questions.empty()) {
            stoatpp::models::MessagePayload pl;
            pl.content = "⚠️ This application panel doesn't have any questions configured yet.";
            pl.delete_after = 5;
            if (!source_channel_id.empty()) bot.send_message(source_channel_id, pl);
            return;
        }

        if (panel.mode == "channel") {
            execute_create_app_channel(bot, server_id, user_id, panel, {}, [&bot, user_id, panel](std::string channel_id) {
                if (!channel_id.empty()) {
                    AppSession session;
                    session.server_id = panel.server_id;
                    session.panel_name = panel.name;
                    session.user_id = user_id;
                    session.current_channel_id = channel_id;
                    session.current_question_index = 0;
                    session.last_activity = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    session.in_dm = false;

                    {
                        std::lock_guard<std::mutex> lock(g_active_apps_mutex);
                        g_active_apps[user_id] = session;
                    }

                    bot.send_message(channel_id, "📋 **Question 1/" + std::to_string(panel.questions.size()) + "**:\n" + panel.questions[0]);
                }
            });
        } else {
            std::thread([&bot, server_id, user_id, panel, source_channel_id]() {
                auto dm_res = bot.rest().open_dm(user_id);
                if (!dm_res.success()) {
                    stoatpp::models::MessagePayload pl;
                    pl.content = "⚠️ I couldn't open DMs with you! Please check if your privacy settings allow DM messages from server members.";
                    pl.delete_after = 7;
                    if (!source_channel_id.empty()) bot.send_message(source_channel_id, pl);
                    return;
                }

                std::string dm_channel_id = dm_res.body["_id"].get<std::string>();

                AppSession session;
                session.server_id = server_id;
                session.panel_name = panel.name;
                session.user_id = user_id;
                session.current_channel_id = dm_channel_id;
                session.current_question_index = 0;
                session.last_activity = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                session.in_dm = true;

                {
                    std::lock_guard<std::mutex> lock(g_active_apps_mutex);
                    g_active_apps[user_id] = session;
                }

                bot.send_message(dm_channel_id, "📋 **Application Started: " + panel.name + "**\nPlease answer the questions step-by-step. Type `cancel` at any time to cancel.\n\n**Question 1/" + std::to_string(panel.questions.size()) + "**:\n" + panel.questions[0]);
                
                if (!source_channel_id.empty()) {
                    stoatpp::models::MessagePayload success_pl;
                    success_pl.content = "📩 An application session has been started in your DMs!";
                    success_pl.delete_after = 5;
                    bot.send_message(source_channel_id, success_pl);
                }
            }).detach();
        }
    }
};

} // namespace bronx::cogs
