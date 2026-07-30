#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "logging.h"
#include <chrono>

namespace bronx::cogs {

inline void send_action_response(stoatpp::cluster& bot, const std::string& server_id, const std::string& channel_id,
                                 const std::string& action_type, const std::unordered_map<std::string, std::string>& placeholders,
                                 const std::string& default_msg, int delete_after = 0) {
    if (server_id.empty() || !g_db) {
        stoatpp::models::MessagePayload payload;
        payload.content = default_msg;
        if (delete_after > 0) payload.delete_after = delete_after;
        bot.send_message(channel_id, payload);
        return;
    }

    auto cfg_opt = g_db->get_action_message(server_id, action_type);
    if (!cfg_opt || (cfg_opt->content.empty() && cfg_opt->embed_json.empty())) {
        stoatpp::models::MessagePayload payload;
        payload.content = default_msg;
        if (delete_after > 0) payload.delete_after = delete_after;
        bot.send_message(channel_id, payload);
        return;
    }

    auto replace_tokens = [&placeholders](std::string text) {
        for (const auto& [token, val] : placeholders) {
            size_t pos = 0;
            while ((pos = text.find(token, pos)) != std::string::npos) {
                text.replace(pos, token.length(), val);
                pos += val.length();
            }
        }
        return text;
    };

    stoatpp::models::MessagePayload payload;
    if (delete_after > 0) payload.delete_after = delete_after;

    if (cfg_opt->message_type == "text" || cfg_opt->embed_json.empty()) {
        std::string text = cfg_opt->content.empty() ? default_msg : cfg_opt->content;
        payload.content = replace_tokens(text);
    } else {
        if (!cfg_opt->content.empty()) {
            payload.content = replace_tokens(cfg_opt->content);
        }
        try {
            nlohmann::json emb = nlohmann::json::parse(cfg_opt->embed_json);
            if (emb.contains("title") && emb["title"].is_string()) {
                emb["title"] = replace_tokens(emb["title"].get<std::string>());
            }
            if (emb.contains("description") && emb["description"].is_string()) {
                emb["description"] = replace_tokens(emb["description"].get<std::string>());
            }
            if (emb.contains("content") && emb["content"].is_string()) {
                std::string c = replace_tokens(emb["content"].get<std::string>());
                if (payload.content.empty()) {
                    payload.content = c;
                }
                emb.erase("content");
            }
            payload.embeds.push_back(emb);
        } catch (...) {
            payload.content = replace_tokens(cfg_opt->content.empty() ? default_msg : cfg_opt->content);
        }
    }
    bot.send_message(channel_id, payload);
}

class ModerationModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "ban",
            {},
            "ban a member from the server.",
            "",
            {"<@user|id>", "[reason]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling ban...\n";
                if (args.empty()) {
                    bot.send_message(msg.channel_id, "usage: `!ban <@user|id> [reason]`");
                    return;
                }

                std::string query = args[0];
                std::string reason = "no reason provided";
                if (args.size() > 1) {
                    reason = "";
                    for (size_t i = 1; i < args.size(); ++i) {
                        reason += args[i] + (i + 1 < args.size() ? " " : "");
                    }
                }

                helpers::resolve_user(bot, msg.server_id, query, [&bot, msg, reason](stoatpp::models::User target_user, stoatpp::models::Member target_member, bool success) {
                    if (!success) {
                        bot.send_message(msg.channel_id, "could not resolve target user for ban.");
                        return;
                    }

                    bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, target_member, reason, target_user](stoatpp::models::Member exec_member, bool exec_ok) {
                        if (!exec_ok) {
                            bot.send_message(msg.channel_id, "failed to verify executor member settings.");
                            return;
                        }

                        bot.fetch_member(msg.server_id, bot.current_user().id, [&bot, msg, target_member, reason, target_user, exec_member](stoatpp::models::Member bot_member, bool bot_ok) {
                            if (!bot_ok) {
                                bot.send_message(msg.channel_id, "failed to verify bot member settings.");
                                  return;
                            }

                            if (!helpers::check_hierarchy(bot, msg.server_id, exec_member, target_member)) {
                                bot.send_message(msg.channel_id, "hierarchy error: you cannot moderate this user.");
                                return;
                            }

                            if (!helpers::check_hierarchy(bot, msg.server_id, bot_member, target_member)) {
                                bot.send_message(msg.channel_id, "hierarchy error: i do not have high enough position to moderate this user.");
                                return;
                            }

                            bot.ban_user(msg.server_id, target_user.id, reason, [&bot, msg, target_user, reason](bool action_success) {
                                if (action_success) {
                                    std::unordered_map<std::string, std::string> tokens = {
                                        {"{user}", "<@" + target_user.id + ">"},
                                        {"{username}", target_user.username},
                                        {"{reason}", reason},
                                        {"{moderator}", "<@" + msg.author.id + ">"},
                                        {"{modname}", msg.author.username}
                                    };
                                    send_action_response(bot, msg.server_id, msg.channel_id, "ban", tokens, "successfully banned **" + target_user.username + "**.");
                                    log_moderation_action(bot, msg.server_id, "Ban", msg.author.id, target_user.id, reason, "", msg.channel_id);
                                } else {
                                    bot.send_message(msg.channel_id, "failed to ban user. do i have target permissions?");
                                }
                            });
                        });
                    });
                });
            },
            "Moderation",
            stoatpp::permissions::BanMembers
        });

        bot.register_command({
            "kick",
            {},
            "kick a member from the server.",
            "",
            {"<@user|id>", "[reason]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling kick...\n";
                if (args.empty()) {
                    bot.send_message(msg.channel_id, "usage: `!kick <@user|id> [reason]`");
                    return;
                }

                std::string query = args[0];
                std::string reason = "no reason provided";
                if (args.size() > 1) {
                    reason = "";
                    for (size_t i = 1; i < args.size(); ++i) {
                        reason += args[i] + (i + 1 < args.size() ? " " : "");
                    }
                }

                helpers::resolve_user(bot, msg.server_id, query, [&bot, msg, reason](stoatpp::models::User target_user, stoatpp::models::Member target_member, bool success) {
                    if (!success) {
                        bot.send_message(msg.channel_id, "could not resolve target user for kick.");
                        return;
                    }

                    bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, target_member, target_user, reason](stoatpp::models::Member exec_member, bool exec_ok) {
                        if (!exec_ok) {
                            bot.send_message(msg.channel_id, "failed to verify executor member settings.");
                            return;
                        }

                        bot.fetch_member(msg.server_id, bot.current_user().id, [&bot, msg, target_member, target_user, exec_member, reason](stoatpp::models::Member bot_member, bool bot_ok) {
                            if (!bot_ok) {
                                bot.send_message(msg.channel_id, "failed to verify bot member settings.");
                                return;
                            }

                            if (!helpers::check_hierarchy(bot, msg.server_id, exec_member, target_member)) {
                                bot.send_message(msg.channel_id, "hierarchy error: you cannot moderate this user.");
                                return;
                            }

                            if (!helpers::check_hierarchy(bot, msg.server_id, bot_member, target_member)) {
                                bot.send_message(msg.channel_id, "hierarchy error: i do not have high enough position to moderate this user.");
                                return;
                            }

                            bot.kick_member(msg.server_id, target_user.id, [&bot, msg, target_user, reason](bool action_success) {
                                if (action_success) {
                                    std::unordered_map<std::string, std::string> tokens = {
                                        {"{user}", "<@" + target_user.id + ">"},
                                        {"{username}", target_user.username},
                                        {"{reason}", reason},
                                        {"{moderator}", "<@" + msg.author.id + ">"},
                                        {"{modname}", msg.author.username}
                                    };
                                    send_action_response(bot, msg.server_id, msg.channel_id, "kick", tokens, "successfully kicked **" + target_user.username + "**.");
                                    log_moderation_action(bot, msg.server_id, "Kick", msg.author.id, target_user.id, reason, "", msg.channel_id);
                                } else {
                                    bot.send_message(msg.channel_id, "failed to kick user.");
                                }
                            });
                        });
                    });
                });
            },
            "Moderation",
            stoatpp::permissions::KickMembers
        });

        bot.register_command({
            "timeout",
            {},
            "timeout a member (mute/restrict).",
            "",
            {"<@user|id>", "<duration>", "[reason]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling timeout...\n";
                if (args.size() < 2) {
                    bot.send_message(msg.channel_id, "usage: `!timeout <@user|id> <duration> [reason]`");
                    return;
                }

                std::string query = args[0];
                uint64_t seconds = 0;
                size_t consumed = 0;
                if (!helpers::parse_duration_from_args(args, 1, seconds, consumed, true)) {
                    bot.send_message(msg.channel_id, "invalid duration. please specify a duration (e.g. `5m`, `1d`, `24h`, `5h 3s` or raw seconds).");
                    return;
                }

                std::string reason = "no reason provided";
                if (args.size() > 1 + consumed) {
                    reason = "";
                    for (size_t i = 1 + consumed; i < args.size(); ++i) {
                        reason += args[i] + (i + 1 < args.size() ? " " : "");
                    }
                }

                helpers::resolve_user(bot, msg.server_id, query, [&bot, msg, seconds, reason](stoatpp::models::User target_user, stoatpp::models::Member target_member, bool success) {
                    if (!success) {
                        bot.send_message(msg.channel_id, "could not resolve target user.");
                        return;
                    }

                    bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, target_member, target_user, seconds, reason](stoatpp::models::Member exec_member, bool exec_ok) {
                        if (!exec_ok) {
                            bot.send_message(msg.channel_id, "failed to verify executor member settings.");
                            return;
                        }

                        bot.fetch_member(msg.server_id, bot.current_user().id, [&bot, msg, target_member, target_user, exec_member, seconds, reason](stoatpp::models::Member bot_member, bool bot_ok) {
                            if (!bot_ok) {
                                bot.send_message(msg.channel_id, "failed to verify bot member settings.");
                                return;
                            }

                            if (!helpers::check_hierarchy(bot, msg.server_id, exec_member, target_member)) {
                                bot.send_message(msg.channel_id, "hierarchy error: you cannot moderate this user.");
                                return;
                            }

                            if (!helpers::check_hierarchy(bot, msg.server_id, bot_member, target_member)) {
                                bot.send_message(msg.channel_id, "hierarchy error: i do not have high enough position to moderate this user.");
                                return;
                            }

                             std::string iso_duration = helpers::get_future_iso_timestamp(static_cast<int>(seconds));
                             bot.timeout_member(msg.server_id, target_user.id, iso_duration, [&bot, msg, target_user, seconds, reason](bool action_success) {
                                 if (action_success) {
                                     auto now_unix = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                                     auto expires_unix = now_unix + seconds;
                                     std::unordered_map<std::string, std::string> tokens = {
                                         {"{user}", "<@" + target_user.id + ">"},
                                         {"{username}", target_user.username},
                                         {"{reason}", reason},
                                         {"{duration}", helpers::format_duration(seconds)},
                                         {"{expires}", stoatpp::utils::format_timestamp(expires_unix, "R")},
                                         {"{moderator}", "<@" + msg.author.id + ">"},
                                         {"{modname}", msg.author.username}
                                     };
                                     send_action_response(bot, msg.server_id, msg.channel_id, "timeout", tokens, "successfully timed out **" + target_user.username + "** until " + stoatpp::utils::format_timestamp(expires_unix, "R") + ".");
                                     log_moderation_action(bot, msg.server_id, "Timeout", msg.author.id, target_user.id, reason, "Duration: " + helpers::format_duration(seconds), msg.channel_id);
                                 } else {
                                     bot.send_message(msg.channel_id, "failed to set timeout.");
                                 }
                             });
                        });
                    });
                });
            },
            "Moderation",
            stoatpp::permissions::TimeoutMembers
        });

        bot.register_command({
            "warn",
            {},
            "warn a member from the server.",
            "",
            {"<@user|id>", "[reason]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling warn...\n";
                if (args.empty()) {
                    bot.send_message(msg.channel_id, "usage: `!warn <@user|id> [reason]`");
                    return;
                }

                std::string query = args[0];
                std::string reason = "no reason provided";
                if (args.size() > 1) {
                    reason = "";
                    for (size_t i = 1; i < args.size(); ++i) {
                        reason += args[i] + (i + 1 < args.size() ? " " : "");
                    }
                }

                helpers::resolve_user(bot, msg.server_id, query, [&bot, msg, reason](stoatpp::models::User target_user, stoatpp::models::Member target_member, bool success) {
                    if (!success) {
                        bot.send_message(msg.channel_id, "could not resolve target user for warn.");
                        return;
                    }

                    bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, target_member, reason, target_user](stoatpp::models::Member exec_member, bool exec_ok) {
                        if (!exec_ok) {
                            bot.send_message(msg.channel_id, "failed to verify executor member settings.");
                            return;
                        }

                        if (!helpers::check_hierarchy(bot, msg.server_id, exec_member, target_member)) {
                            bot.send_message(msg.channel_id, "hierarchy error: you cannot moderate this user.");
                            return;
                        }

                        std::unordered_map<std::string, std::string> tokens = {
                            {"{user}", "<@" + target_user.id + ">"},
                            {"{username}", target_user.username},
                            {"{reason}", reason},
                            {"{moderator}", "<@" + msg.author.id + ">"},
                            {"{modname}", msg.author.username}
                        };
                        send_action_response(bot, msg.server_id, msg.channel_id, "warn", tokens, "successfully warned **" + target_user.username + "**.");
                        log_moderation_action(bot, msg.server_id, "Warn", msg.author.id, target_user.id, reason, "", msg.channel_id);
                    });
                });
            },
            "Moderation",
            stoatpp::permissions::KickMembers
        });

        bot.register_command({
            "jail",
            {},
            "jail a member or manage jail configuration.",
            "",
            {"<@user|id/setup/role/channel>", "[args...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling jail...\n";
                if (args.empty()) {
                    bot.send_message(msg.channel_id, 
                        "⚖️ **Jail System Commands:**\n"
                        "• `.jail setup` — Automatically configure jail role and jail channel overrides.\n"
                        "• `.jail role <@role|role_id>` — Manually set jail role.\n"
                        "• `.jail channel <#channel|channel_id>` — Manually set jail channel.\n"
                        "• `.jail <@user|id> [duration] [reason]` — Jail a member.");
                    return;
                }

                std::string cmd_arg = args[0];
                if (cmd_arg == "setup") {
                    std::thread([&bot, msg]() {
                        bot.send_message(msg.channel_id, "⚙️ Setting up jail role and channel...");
                        
                        // 1. Create role
                        auto role_res = bot.rest().create_role(msg.server_id, "Jailed");
                        if (!role_res.success()) {
                            bot.send_message(msg.channel_id, "⚠️ Failed to create Jailed role: " + std::to_string(role_res.status_code) + " — " + role_res.error_message());
                            return;
                        }
                        
                        std::string jail_role_id = "";
                        if (role_res.body.contains("id") && role_res.body["id"].is_string()) {
                            jail_role_id = role_res.body["id"].get<std::string>();
                        } else if (role_res.body.contains("role") && role_res.body["role"].is_object() && role_res.body["role"].contains("id") && role_res.body["role"]["id"].is_string()) {
                            jail_role_id = role_res.body["role"]["id"].get<std::string>();
                        }

                        if (jail_role_id.empty()) {
                            bot.send_message(msg.channel_id, "⚠️ Failed to parse Jailed role ID.");
                            return;
                        }

                        // 2. Create channel
                        auto chan_res = bot.rest().create_channel(msg.server_id, "Text", "jail");
                        if (!chan_res.success()) {
                            bot.send_message(msg.channel_id, "⚠️ Failed to create jail channel: " + std::to_string(chan_res.status_code) + " — " + chan_res.error_message());
                            return;
                        }

                        std::string jail_channel_id = chan_res.body["_id"].get<std::string>();

                        // 3. Set overrides
                        auto srv_chans_res = bot.rest().get_server_channels(msg.server_id);
                        if (srv_chans_res.success() && srv_chans_res.body.is_array()) {
                            for (const auto& ch_json : srv_chans_res.body) {
                                auto c = stoatpp::models::Channel::from_json(ch_json);
                                if (c.channel_type == "TextChannel" || c.channel_type == "VoiceChannel") {
                                    if (c.id == jail_channel_id) {
                                        bot.rest().set_channel_permission(c.id, jail_role_id, 
                                            stoatpp::permissions::ViewChannel | stoatpp::permissions::SendMessages | stoatpp::permissions::ReadMessageHistory, 0);
                                    } else {
                                        bot.rest().set_channel_permission(c.id, jail_role_id, 0, stoatpp::permissions::ViewChannel);
                                    }
                                }
                            }
                        }

                        // 4. Save config
                        Database::JailConfig cfg;
                        cfg.server_id = msg.server_id;
                        cfg.jail_role_id = jail_role_id;
                        cfg.jail_channel_id = jail_channel_id;
                        g_db->save_jail_config(cfg);

                        // Clear server cache so new role is fetched
                        auto fresh_res = bot.rest().get_server(msg.server_id);
                        if (fresh_res.success()) {
                            bot.cache_server(stoatpp::models::Server::from_json(fresh_res.body));
                        }

                        bot.send_message(msg.channel_id, "✅ Setup complete! Created Jailed role and <#" + jail_channel_id + "> channel with proper overrides.");
                    }).detach();
                    return;
                }

                if (cmd_arg == "role") {
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "usage: `.jail role <%role|role_id>`");
                        return;
                    }
                    std::string role_query = args[1];
                    if (role_query.rfind("<%", 0) == 0 && role_query.back() == '>') {
                        role_query = role_query.substr(2, role_query.size() - 3);
                    } else if (role_query.rfind("<@&", 0) == 0 && role_query.back() == '>') {
                        role_query = role_query.substr(3, role_query.size() - 4);
                    }

                    auto server_opt = bot.get_server(msg.server_id);
                    if (!server_opt) {
                        bot.send_message(msg.channel_id, "failed to retrieve server settings.");
                        return;
                    }

                    std::string matched_role_id = "";
                    for (const auto& r : server_opt->roles) {
                        if (r.id == role_query || r.name == role_query) {
                            matched_role_id = r.id;
                            break;
                        }
                    }

                    if (matched_role_id.empty()) {
                        bot.send_message(msg.channel_id, "⚠️ Role not found on this server.");
                        return;
                    }

                    auto cfg = g_db->get_jail_config(msg.server_id).value_or(Database::JailConfig{msg.server_id, "", ""});
                    cfg.jail_role_id = matched_role_id;
                    g_db->save_jail_config(cfg);
                    bot.send_message(msg.channel_id, "✅ Jail role set to **" + role_query + "**.");
                    return;
                }

                if (cmd_arg == "channel") {
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "usage: `.jail channel <#channel|channel_id>`");
                        return;
                    }
                    std::string chan_query = args[1];
                    if (chan_query.rfind("<#", 0) == 0 && chan_query.back() == '>') {
                        chan_query = chan_query.substr(2, chan_query.size() - 3);
                    }

                    auto cfg = g_db->get_jail_config(msg.server_id).value_or(Database::JailConfig{msg.server_id, "", ""});
                    cfg.jail_channel_id = chan_query;
                    g_db->save_jail_config(cfg);
                    bot.send_message(msg.channel_id, "✅ Jail channel set to <#" + chan_query + ">.");
                    return;
                }

                // Jailing member
                auto jail_cfg_opt = g_db->get_jail_config(msg.server_id);
                if (!jail_cfg_opt || jail_cfg_opt->jail_role_id.empty() || jail_cfg_opt->jail_channel_id.empty()) {
                    bot.send_message(msg.channel_id, "⚠️ Jail environment is not set up on this server. Run `!jail setup` first.");
                    return;
                }

                std::string target_query = cmd_arg;
                uint64_t duration = 0;
                std::string reason = "no reason provided";
                bool is_timed = false;

                if (args.size() > 1) {
                    uint64_t d = helpers::parse_duration(args[1]);
                    if (d > 0) {
                        duration = d;
                        is_timed = true;
                        if (args.size() > 2) {
                            reason = "";
                            for (size_t i = 2; i < args.size(); ++i) {
                                reason += args[i] + (i + 1 < args.size() ? " " : "");
                            }
                        }
                    } else {
                        reason = "";
                        for (size_t i = 1; i < args.size(); ++i) {
                            reason += args[i] + (i + 1 < args.size() ? " " : "");
                        }
                    }
                }

                helpers::resolve_user(bot, msg.server_id, target_query, [&bot, msg, reason, duration, is_timed, jail_cfg_opt](stoatpp::models::User target_user, stoatpp::models::Member target_member, bool success) {
                    if (!success) {
                        bot.send_message(msg.channel_id, "could not resolve target user.");
                        return;
                    }

                    bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, target_member, reason, duration, is_timed, jail_cfg_opt, target_user](stoatpp::models::Member exec_member, bool exec_ok) {
                        if (!exec_ok) {
                            bot.send_message(msg.channel_id, "failed to verify executor member settings.");
                            return;
                        }

                        if (!helpers::check_hierarchy(bot, msg.server_id, exec_member, target_member)) {
                            bot.send_message(msg.channel_id, "hierarchy error: you cannot moderate this user.");
                            return;
                        }

                        auto existing_jail = g_db->get_active_jail(msg.server_id, target_user.id);
                        if (existing_jail) {
                            bot.send_message(msg.channel_id, "user is already jailed.");
                            return;
                        }

                        // Save original roles as comma-separated string
                        std::string original_roles_str = "";
                        for (size_t i = 0; i < target_member.roles.size(); ++i) {
                            original_roles_str += target_member.roles[i] + (i + 1 < target_member.roles.size() ? "," : "");
                        }

                        std::thread([&bot, msg, target_user, jail_cfg_opt, original_roles_str, duration, is_timed, reason]() {
                            nlohmann::json edit_fields;
                            edit_fields["roles"] = { jail_cfg_opt->jail_role_id };
                            auto edit_res = bot.rest().edit_member(msg.server_id, target_user.id, edit_fields);
                            if (!edit_res.success()) {
                                bot.send_message(msg.channel_id, "failed to restrict user roles: " + edit_res.error_message());
                                return;
                            }

                            auto now_unix = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                            auto expires_unix = is_timed ? (now_unix + duration) : 0;

                            Database::ActiveJail jail;
                            jail.server_id = msg.server_id;
                            jail.user_id = target_user.id;
                            jail.roles = original_roles_str;
                            jail.jailed_at = now_unix;
                            jail.expires_at = expires_unix;
                            g_db->add_active_jail(jail);

                            std::string duration_str = is_timed ? (std::to_string(duration) + " seconds") : "indefinitely";
                            std::string duration_msg = is_timed ? ("for " + std::to_string(duration) + " seconds") : "indefinitely";

                            std::unordered_map<std::string, std::string> tokens = {
                                {"{user}", "<@" + target_user.id + ">"},
                                {"{username}", target_user.username},
                                {"{reason}", reason},
                                {"{duration}", duration_str},
                                {"{duration_msg}", duration_msg},
                                {"{moderator}", "<@" + msg.author.id + ">"},
                                {"{modname}", msg.author.username}
                            };

                            // Send customized jail message to jail channel
                            std::string default_jail_msg = "<@" + target_user.id + "> you have been jailed for " + reason + ", " + duration_msg + ". The staff will speak to you soon.";
                            send_action_response(bot, msg.server_id, jail_cfg_opt->jail_channel_id, "jail", tokens, default_jail_msg);

                            // Send customized invoke confirmation to command channel
                            send_action_response(bot, msg.server_id, msg.channel_id, "jail_invoke", tokens, "successfully jailed **" + target_user.username + "**.");

                            // Log mod log
                            log_moderation_action(bot, msg.server_id, "Jail", msg.author.id, target_user.id, reason, "Duration: " + duration_str, msg.channel_id);
                        }).detach();
                    });
                });
            },
            "Moderation",
            stoatpp::permissions::KickMembers
        });

        bot.register_command({
            "unjail",
            {},
            "unjail a member, restoring their original roles.",
            "",
            {"<@user|id>", "[reason]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling unjail...\n";
                if (args.empty()) {
                    bot.send_message(msg.channel_id, "usage: `!unjail <@user|id> [reason]`");
                    return;
                }

                std::string target_query = args[0];
                std::string reason = "no reason provided";
                if (args.size() > 1) {
                    reason = "";
                    for (size_t i = 1; i < args.size(); ++i) {
                        reason += args[i] + (i + 1 < args.size() ? " " : "");
                    }
                }

                helpers::resolve_user(bot, msg.server_id, target_query, [&bot, msg, reason](stoatpp::models::User target_user, stoatpp::models::Member target_member, bool success) {
                    if (!success) {
                        bot.send_message(msg.channel_id, "could not resolve target user.");
                        return;
                    }

                    auto active_jail_opt = g_db->get_active_jail(msg.server_id, target_user.id);
                    if (!active_jail_opt) {
                        bot.send_message(msg.channel_id, "user is not jailed.");
                        return;
                    }

                    std::thread([&bot, msg, target_user, active_jail_opt, reason]() {
                        // 1. Restore original roles
                        std::vector<std::string> roles_to_restore;
                        std::stringstream ss(active_jail_opt->roles);
                        std::string r_id;
                        while (std::getline(ss, r_id, ',')) {
                            if (!r_id.empty()) roles_to_restore.push_back(r_id);
                        }

                        nlohmann::json edit_fields;
                        edit_fields["roles"] = roles_to_restore;
                        auto edit_res = bot.rest().edit_member(msg.server_id, target_user.id, edit_fields);
                        if (!edit_res.success()) {
                            bot.send_message(msg.channel_id, "failed to restore user roles: " + edit_res.error_message());
                            return;
                        }

                        // 2. Remove active jail record
                        g_db->remove_active_jail(msg.server_id, target_user.id);

                        // 3. Send unjail customized response
                        std::unordered_map<std::string, std::string> tokens = {
                            {"{user}", "<@" + target_user.id + ">"},
                            {"{username}", target_user.username},
                            {"{reason}", reason},
                            {"{moderator}", "<@" + msg.author.id + ">"},
                            {"{modname}", msg.author.username}
                        };
                        send_action_response(bot, msg.server_id, msg.channel_id, "unjail", tokens, "successfully unjailed **" + target_user.username + "**.");

                        // 4. Log to modlogs
                        log_moderation_action(bot, msg.server_id, "Unjail", msg.author.id, target_user.id, reason, "", msg.channel_id);
                    }).detach();
                });
            },
            "Moderation",
            stoatpp::permissions::KickMembers
        });

        bot.register_command({
            "purge",
            {},
            "bulk delete messages in the channel.",
            "",
            {"[amount]", "[@user]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling purge...\n";
                int amount = 10;
                std::string user_query = "";
                
                if (!args.empty()) {
                    try {
                        size_t pos;
                        int val = std::stoi(args[0], &pos);
                        if (pos == args[0].size()) {
                            amount = val;
                            if (args.size() > 1) {
                                user_query = args[1];
                            }
                        } else {
                            user_query = args[0];
                            if (args.size() > 1) {
                                try {
                                    amount = std::stoi(args[1]);
                                } catch (...) {}
                            }
                        }
                    } catch (...) {
                        user_query = args[0];
                        if (args.size() > 1) {
                            try {
                                amount = std::stoi(args[1]);
                            } catch (...) {}
                        }
                    }
                }

                if (amount < 1 || amount > 100) {
                    bot.send_message(msg.channel_id, "amount must be between 1 and 100.");
                    return;
                }

                auto proceed_purge = [&bot, msg, amount](const std::string& target_user_id) {
                    stoatpp::models::MessageQuery query;
                    query.limit = target_user_id.empty() ? amount : 100;

                    std::thread([&bot, msg, amount, target_user_id, query]() {
                        auto res = bot.rest().get_messages(msg.channel_id, query);
                        if (!res.success() || !res.body.is_array()) {
                            bot.send_message(msg.channel_id, "failed to fetch messages for purge.");
                            return;
                        }

                        std::vector<std::string> ids;
                        for (const auto& item : res.body) {
                            auto m = stoatpp::models::Message::from_json(item);
                            if (m.raw.value("pinned", false)) {
                                continue;
                            }
                            if (target_user_id.empty() || m.author == target_user_id) {
                                ids.push_back(m.id);
                                if (!target_user_id.empty() && ids.size() >= static_cast<size_t>(amount)) {
                                    break;
                                }
                            }
                        }

                        if (ids.empty()) {
                            bot.send_message(msg.channel_id, "no messages found to delete.");
                            return;
                        }

                        auto del_res = bot.rest().delete_messages_bulk(msg.channel_id, ids);
                        if (del_res.success()) {
                            std::cout << "[bronx command] successfully purged " << ids.size() << " messages.\n";
                            std::unordered_map<std::string, std::string> tokens = {
                                {"{amount}", std::to_string(ids.size())},
                                {"{moderator}", "<@" + msg.author.id + ">"},
                                {"{modname}", msg.author.username}
                            };
                            send_action_response(bot, msg.server_id, msg.channel_id, "purge", tokens, "successfully purged " + std::to_string(ids.size()) + " messages.", 5);
                        } else {
                            bot.send_message(msg.channel_id, "failed to bulk delete messages: " + del_res.error_message());
                        }
                    }).detach();
                };

                if (!user_query.empty()) {
                    helpers::resolve_user(bot, msg.server_id, user_query, [proceed_purge, &bot, msg](stoatpp::models::User user, stoatpp::models::Member member, bool success) {
                        if (!success) {
                            bot.send_message(msg.channel_id, "could not resolve target user for purge.");
                            return;
                        }
                        proceed_purge(user.id);
                    });
                } else {
                    proceed_purge("");
                }
            },
            "Moderation",
            stoatpp::permissions::ManageMessages
        });
        bot.register_command({
            "pin",
            {},
            "pin a message in this channel. reply to a message or provide its id.",
            "",
            {"[message_id]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling pin...\n";

                // Prefer a message ID supplied as an argument, otherwise use the replied-to message
                std::string target_id;
                if (!args.empty()) {
                    target_id = args[0];
                } else if (!msg.replies.empty()) {
                    target_id = msg.replies[0];
                }

                if (target_id.empty()) {
                    bot.send_message(msg.channel_id, "usage: reply to a message or provide a message id: `pin <message_id>`");
                    return;
                }

                bot.pin_message(msg.channel_id, target_id, [&bot, msg, target_id](bool success) {
                    if (success) {
                        bot.send_message(msg.channel_id, "📌 message pinned.");
                    } else {
                        bot.send_message(msg.channel_id, "failed to pin message. do i have the manage messages permission?");
                    }
                });
            },
            "Moderation",
            stoatpp::permissions::ManageMessages
        });

        bot.register_command({
            "unpin",
            {},
            "unpin a message in this channel. reply to a message or provide its id.",
            "",
            {"[message_id]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling unpin...\n";

                std::string target_id;
                if (!args.empty()) {
                    target_id = args[0];
                } else if (!msg.replies.empty()) {
                    target_id = msg.replies[0];
                }

                if (target_id.empty()) {
                    bot.send_message(msg.channel_id, "usage: reply to a message or provide a message id: `unpin <message_id>`");
                    return;
                }

                bot.unpin_message(msg.channel_id, target_id, [&bot, msg, target_id](bool success) {
                    if (success) {
                        bot.send_message(msg.channel_id, "📌 message unpinned.");
                    } else {
                        bot.send_message(msg.channel_id, "failed to unpin message. do i have the manage messages permission?");
                    }
                });
            },
            "Moderation",
            stoatpp::permissions::ManageMessages
        });

        bot.register_command({
            "cleanup",
            {"cu"},
            "delete bot messages and command invocations.",
            "",
            {"[amount]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling cleanup...\n";

                int amount = 100;
                bool has_limit = false;
                if (!args.empty()) {
                    try {
                        amount = std::stoi(args[0]);
                        has_limit = true;
                    } catch (...) {
                        bot.send_message(msg.channel_id, "amount must be a valid number.");
                        return;
                    }
                }

                if (amount < 1 || amount > 100) {
                    bot.send_message(msg.channel_id, "amount must be between 1 and 100.");
                    return;
                }

                std::vector<std::string> prefixes = { bot.config().command_prefix };
                if (bot.config().prefix_resolver) {
                    prefixes = bot.config().prefix_resolver(bot, msg);
                }

                auto starts_with_prefix = [](const std::string& content, const std::vector<std::string>& pfxs) {
                    for (const auto& p : pfxs) {
                        if (content.rfind(p, 0) == 0) return true;
                    }
                    return false;
                };

                stoatpp::models::MessageQuery query;
                query.limit = 100;

                std::thread([&bot, msg, amount, has_limit, prefixes, starts_with_prefix, query]() {
                    auto res = bot.rest().get_messages(msg.channel_id, query);
                    if (!res.success() || !res.body.is_array()) {
                        bot.send_message(msg.channel_id, "failed to fetch messages for cleanup.");
                        return;
                    }

                    std::vector<stoatpp::models::Message> messages;
                    for (const auto& item : res.body) {
                        messages.push_back(stoatpp::models::Message::from_json(item));
                    }

                    std::vector<std::string> ids_to_delete;
                    ids_to_delete.push_back(msg.id); // delete the cleanup command itself

                    int bot_msgs_deleted = 0;
                    for (size_t i = 0; i < messages.size(); ++i) {
                        const auto& m = messages[i];
                        if (m.id == msg.id) continue;

                        if (m.author == bot.current_user().id) {
                            if (has_limit && bot_msgs_deleted >= amount) {
                                continue;
                            }

                            ids_to_delete.push_back(m.id);
                            bot_msgs_deleted++;

                            // Check replies first
                            if (!m.replies.empty()) {
                                ids_to_delete.push_back(m.replies[0]);
                            } else if (i + 1 < messages.size()) {
                                const auto& prev = messages[i + 1];
                                if (prev.content && starts_with_prefix(*prev.content, prefixes)) {
                                    ids_to_delete.push_back(prev.id);
                                }
                            }
                        }
                    }

                    // Deduplicate IDs
                    std::sort(ids_to_delete.begin(), ids_to_delete.end());
                    ids_to_delete.erase(std::unique(ids_to_delete.begin(), ids_to_delete.end()), ids_to_delete.end());

                    if (ids_to_delete.empty()) {
                        bot.send_message(msg.channel_id, "no messages found to clean up.");
                        return;
                    }

                    auto del_res = bot.rest().delete_messages_bulk(msg.channel_id, ids_to_delete);
                    if (del_res.success()) {
                        std::cout << "[bronx command] successfully cleaned up " << ids_to_delete.size() << " messages.\n";
                        stoatpp::models::MessagePayload confirm_payload;
                        confirm_payload.content = "successfully cleaned up " + std::to_string(ids_to_delete.size()) + " messages.";
                        confirm_payload.delete_after = 5;
                        bot.send_message(msg.channel_id, confirm_payload);
                    } else {
                        bot.send_message(msg.channel_id, "failed to delete messages: " + del_res.error_message());
                    }
                }).detach();
            },
            "Moderation",
            stoatpp::permissions::ManageMessages
        });

        bot.register_command({
            "message",
            {"actionmsg", "actionmessage"},
            "configure customized responses for moderation actions (ban, kick, timeout, purge).",
            "",
            {"<subcommand|action>", "[args...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling message config...\n";
                std::vector<std::string> prefixes = { bot.config().command_prefix };
                if (bot.config().prefix_resolver) {
                    prefixes = bot.config().prefix_resolver(bot, msg);
                }
                std::string pfx = bot.config().command_prefix;
                for (const auto& p : prefixes) {
                    if (msg.content.rfind(p, 0) == 0) { pfx = p; break; }
                }

                auto send_help = [&bot, msg, pfx]() {
                    bot.send_message(msg.channel_id,
                        "usage: `" + pfx + "message <subcommand|action> [args...]`\n"
                        "Shortcut: `" + pfx + "message <action> <custom text...>` (sets text response directly)\n\n"
                        "**Subcommands:**\n"
                        "• `type <action> <text|embed>` — Set response format to plain text or embed.\n"
                        "• `text <action> <message...>` — Set custom plain text response (aliases: `message`, `set`).\n"
                        "• `edit <action> key:value...` — Customize embed properties (`title`, `description`, `color`, `image`, `content`).\n"
                        "• `json <action> <raw_json>` — Import full JSON embed.\n"
                        "• `reset <action>` — Revert an action response to the bot default (aliases: `clear`).\n"
                        "• `list` — List all customized action responses.\n\n"
                        "**Supported Actions & Tokens:**\n"
                        "• **Ban (`ban`):** `{user}`, `{username}`, `{reason}`, `{moderator}`, `{modname}`\n"
                        "• **Kick (`kick`):** `{user}`, `{username}`, `{reason}`, `{moderator}`, `{modname}`\n"
                        "• **Timeout (`timeout`):** `{user}`, `{username}`, `{reason}`, `{duration}`, `{expires}`, `{moderator}`, `{modname}`\n"
                        "• **Purge (`purge`):** `{amount}`, `{moderator}`, `{modname}`\n"
                        "• **Warn (`warn`):** `{user}`, `{username}`, `{reason}`, `{moderator}`, `{modname}`\n"
                        "• **Jail (`jail`):** `{user}`, `{username}`, `{reason}`, `{duration}`, `{moderator}`, `{modname}`\n"
                        "• **Jail Invoke (`jail_invoke`):** `{user}`, `{username}`, `{reason}`, `{duration}`, `{moderator}`, `{modname}`\n"
                        "• **Unjail (`unjail`):** `{user}`, `{username}`, `{reason}`, `{moderator}`, `{modname}`\n\n"
                        "**Examples:**\n"
                        "• `" + pfx + "message ban {user} was sent into the stratosphere! Reason: {reason}`\n"
                        "• `" + pfx + "message edit ban title:🔨 Banned! color:#ff0000 description:{user} was banned by {moderator}`"
                    );
                };

                if (args.empty()) {
                    send_help();
                    return;
                }

                std::string sub = args[0];
                std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

                auto normalize_action = [](std::string act) -> std::string {
                    std::transform(act.begin(), act.end(), act.begin(), ::tolower);
                    if (act == "bans") return "ban";
                    if (act == "kicks") return "kick";
                    if (act == "timeouts" || act == "mute") return "timeout";
                    if (act == "purges" || act == "clear") return "purge";
                    if (act == "warns") return "warn";
                    if (act == "jails") return "jail";
                    if (act == "unjails") return "unjail";
                    if (act == "jail_invokes") return "jail_invoke";
                    return act;
                };

                auto is_valid_action = [](const std::string& act) {
                    return act == "ban" || act == "kick" || act == "timeout" || act == "purge" ||
                           act == "warn" || act == "jail" || act == "unjail" || act == "jail_invoke";
                };

                if (sub == "list") {
                    auto cfgs = g_db->list_action_messages(msg.server_id);
                    if (cfgs.empty()) {
                        bot.send_message(msg.channel_id, "ℹ️ No custom action messages configured for this server.");
                        return;
                    }
                    std::string out = "**Custom Action Responses:**\n";
                    for (const auto& c : cfgs) {
                        out += "• **" + c.action_type + "** (`" + c.message_type + "`): " + (c.content.empty() ? "(Embed configured)" : c.content) + "\n";
                    }
                    bot.send_message(msg.channel_id, out);
                    return;
                }

                // Check shortcut: !message <action> <text...>
                std::string norm_sub = normalize_action(sub);
                if (is_valid_action(norm_sub)) {
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "message " + norm_sub + " <custom text...>` or `" + pfx + "message reset " + norm_sub + "`");
                        return;
                    }
                    size_t pos = msg.content.find(args[0]);
                    if (pos != std::string::npos) {
                        pos += args[0].size();
                        while (pos < msg.content.size() && msg.content[pos] == ' ') pos++;
                    }
                    std::string text = (pos != std::string::npos && pos < msg.content.size()) ? msg.content.substr(pos) : "";
                    if (text.empty()) {
                        bot.send_message(msg.channel_id, "⚠️ Please provide message text.");
                        return;
                    }
                    auto cfg = g_db->get_action_message(msg.server_id, norm_sub).value_or(Database::ActionMessageConfig{msg.server_id, norm_sub});
                    cfg.message_type = "text";
                    cfg.content = text;
                    if (g_db->save_action_message(cfg)) {
                        bot.send_message(msg.channel_id, "✅ Custom response for `" + norm_sub + "` set successfully!");
                    } else {
                        bot.send_message(msg.channel_id, "⚠️ Failed to save configuration.");
                    }
                    return;
                }

                if (sub == "reset" || sub == "clear" || sub == "delete" || sub == "remove") {
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "message reset <ban|kick|timeout|purge>`");
                        return;
                    }
                    std::string act = normalize_action(args[1]);
                    if (!is_valid_action(act)) {
                        bot.send_message(msg.channel_id, "⚠️ Invalid action type. Choose from: `ban`, `kick`, `timeout`, `purge`.");
                        return;
                    }
                    if (g_db->remove_action_message(msg.server_id, act)) {
                        bot.send_message(msg.channel_id, "✅ Reset response message for `" + act + "` to default.");
                    } else {
                        bot.send_message(msg.channel_id, "⚠️ Failed to reset configuration.");
                    }
                    return;
                }

                if (args.size() < 2) {
                    send_help();
                    return;
                }

                std::string act = normalize_action(args[1]);
                if (!is_valid_action(act)) {
                    bot.send_message(msg.channel_id, "⚠️ Invalid action type. Choose from: `ban`, `kick`, `timeout`, `purge`.");
                    return;
                }

                if (sub == "type") {
                    if (args.size() < 3) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "message type <action> <text|embed>`");
                        return;
                    }
                    std::string type = args[2];
                    std::transform(type.begin(), type.end(), type.begin(), ::tolower);
                    if (type != "text" && type != "embed") {
                        bot.send_message(msg.channel_id, "⚠️ Type must be `text` or `embed`.");
                        return;
                    }
                    auto cfg = g_db->get_action_message(msg.server_id, act).value_or(Database::ActionMessageConfig{msg.server_id, act});
                    cfg.message_type = type;
                    if (g_db->save_action_message(cfg)) {
                        bot.send_message(msg.channel_id, "✅ Response type for `" + act + "` set to **" + type + "**.");
                    } else {
                        bot.send_message(msg.channel_id, "⚠️ Failed to save configuration.");
                    }
                    return;
                }

                if (sub == "text" || sub == "set" || sub == "message" || sub == "content") {
                    size_t pos = msg.content.find(args[1]);
                    if (pos != std::string::npos) {
                        pos += args[1].size();
                        while (pos < msg.content.size() && msg.content[pos] == ' ') pos++;
                    }
                    std::string text = (pos != std::string::npos && pos < msg.content.size()) ? msg.content.substr(pos) : "";
                    if (text.empty()) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "message text <action> <text...>`");
                        return;
                    }
                    auto cfg = g_db->get_action_message(msg.server_id, act).value_or(Database::ActionMessageConfig{msg.server_id, act});
                    cfg.content = text;
                    if (g_db->save_action_message(cfg)) {
                        bot.send_message(msg.channel_id, "✅ Custom text content for `" + act + "` updated successfully.");
                    } else {
                        bot.send_message(msg.channel_id, "⚠️ Failed to save configuration.");
                    }
                    return;
                }

                if (sub == "edit") {
                    size_t pos = msg.content.find(args[1]);
                    if (pos != std::string::npos) {
                        pos += args[1].size();
                        while (pos < msg.content.size() && msg.content[pos] == ' ') pos++;
                    }
                    std::string text = (pos != std::string::npos && pos < msg.content.size()) ? msg.content.substr(pos) : "";
                    if (text.empty()) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "message edit <action> key:value ...`\nkeys: `title`, `description`, `color`, `image`, `content`");
                        return;
                    }

                    auto pairs = LoggingModule::parse_pairs(text);
                    if (pairs.empty()) {
                        bot.send_message(msg.channel_id, "⚠️ No valid key-value pairs found. Format must be `key:value`.");
                        return;
                    }

                    auto cfg = g_db->get_action_message(msg.server_id, act).value_or(Database::ActionMessageConfig{msg.server_id, act});
                    nlohmann::json emb = nlohmann::json::object();
                    if (!cfg.embed_json.empty()) {
                        try { emb = nlohmann::json::parse(cfg.embed_json); } catch (...) {}
                    }

                    for (const auto& [key, val] : pairs) {
                        if (key == "content") {
                            if (val == "none" || val == "null" || val == "clear") cfg.content = "";
                            else cfg.content = val;
                        } else if (val == "none" || val == "null" || val == "clear") {
                            emb.erase(key);
                        } else {
                            emb[key] = val;
                        }
                    }

                    cfg.embed_json = emb.dump();
                    if (g_db->save_action_message(cfg)) {
                        bot.send_message(msg.channel_id, "✅ Custom embed fields for `" + act + "` updated successfully.");
                    } else {
                        bot.send_message(msg.channel_id, "⚠️ Failed to save configuration.");
                    }
                    return;
                }

                if (sub == "json") {
                    size_t pos = msg.content.find(args[1]);
                    if (pos != std::string::npos) {
                        pos += args[1].size();
                        while (pos < msg.content.size() && msg.content[pos] == ' ') pos++;
                    }
                    std::string text = (pos != std::string::npos && pos < msg.content.size()) ? msg.content.substr(pos) : "";
                    if (text.empty()) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "message json <action> <raw_json_embed>`");
                        return;
                    }

                    nlohmann::json emb;
                    try {
                        emb = nlohmann::json::parse(text);
                    } catch (const std::exception& e) {
                        bot.send_message(msg.channel_id, "⚠️ Invalid JSON format: " + std::string(e.what()));
                        return;
                    }

                    auto cfg = g_db->get_action_message(msg.server_id, act).value_or(Database::ActionMessageConfig{msg.server_id, act});
                    if (emb.contains("content") && emb["content"].is_string()) {
                        cfg.content = emb["content"].get<std::string>();
                        emb.erase("content");
                    }
                    if (emb.contains("colour")) {
                        emb["color"] = emb["colour"];
                        emb.erase("colour");
                    }

                    cfg.embed_json = emb.dump();
                    if (g_db->save_action_message(cfg)) {
                        bot.send_message(msg.channel_id, "✅ Custom embed for `" + act + "` imported successfully.");
                    } else {
                        bot.send_message(msg.channel_id, "⚠️ Failed to save configuration.");
                    }
                    return;
                }

                send_help();
            },
            "Moderation",
            stoatpp::permissions::ManageServer
        });

        bot.register_command({
            "modlogs",
            {"ml"},
            "view or manage moderation history for a user.",
            "modlogs <@user|id/remove> [actions/id]",
            {"<@user|id>", "[actions]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id, "this command can only be used inside a server.");
                    return;
                }
                if (!g_db) {
                    bot.send_message(msg.channel_id, "database unavailable.");
                    return;
                }
                if (args.empty()) {
                    bot.send_message(msg.channel_id,
                        "**modlogs** — view/manage moderation history\n"
                        "`.modlogs <@user|id>` — history of actions taken against a user\n"
                        "`.modlogs <@user|id> actions` — actions a user has invoked on others\n"
                        "`.modlogs remove <id>` — remove a modlog entry");
                    return;
                }

                std::string query = args[0];
                if (query == "remove" || query == "delete" || query == "rm") {
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "usage: `.modlogs remove <id>`");
                        return;
                    }
                    int id = 0;
                    try {
                        id = std::stoi(args[1]);
                    } catch (...) {
                        bot.send_message(msg.channel_id, "invalid entry ID.");
                        return;
                    }
                    if (g_db->delete_moderation_action(msg.server_id, id)) {
                        bot.send_message(msg.channel_id, "✅ successfully removed modlog entry `#" + std::to_string(id) + "`.");
                    } else {
                        bot.send_message(msg.channel_id, "failed to remove modlog entry.");
                    }
                    return;
                }

                bool show_actions = args.size() > 1 && (args[1] == "actions" || args[1] == "action" || args[1] == "a");

                helpers::resolve_user(bot, msg.server_id, query,
                    [&bot, msg, show_actions](stoatpp::models::User target_user, stoatpp::models::Member, bool success) {
                        if (!success) {
                            bot.send_message(msg.channel_id, "could not resolve user.");
                            return;
                        }

                        stoatpp::models::MessagePayload payload;
                        payload.content = " ";
                        stoatpp::models::Embed embed;
                        embed.set_colour(0x2f3136);

                        std::string display = target_user.username.empty()
                            ? ("<@" + target_user.id + ">")
                            : target_user.username;

                        // Action emoji map
                        auto action_emoji = [](const std::string& a) -> std::string {
                            if (a == "Ban")     return "🔨";
                            if (a == "Kick")    return "👢";
                            if (a == "Timeout") return "🔇";
                            if (a == "Warn")    return "⚠️";
                            if (a == "Unban")   return "✅";
                            if (a == "Jail")    return "⛓️";
                            if (a == "Unjail")  return "🔓";
                            return "🛡️";
                        };

                        if (!show_actions) {
                            // Moderation history FOR the user (victim view)
                            auto entries = g_db->get_mod_logs(msg.server_id, target_user.id, 10);
                            embed.set_title("📋 modlogs — " + display);

                            if (entries.empty()) {
                                embed.set_description("*no moderation history for this user.*");
                            } else {
                                std::string desc;
                                for (const auto& e : entries) {
                                    desc += action_emoji(e.action) + " **" + e.action + "**";
                                    desc += " `#" + std::to_string(e.id) + "`\n";
                                    desc += "  **time:** " + e.created_at + "\n";
                                    desc += "  **invoker:** <@" + e.moderator_id + ">\n";
                                    if (!e.reason.empty())
                                        desc += "  **reason:** " + e.reason + "\n";
                                    if (!e.extra_details.empty())
                                        desc += "  **details:** " + e.extra_details + "\n";
                                    desc += "\n";
                                }
                                embed.set_description(desc);
                                desc += "-# showing latest " + std::to_string(entries.size()) + " entries";
                                embed.set_description(desc);
                            }
                        } else {
                            // Moderation actions BY the user (moderator view)
                            auto entries = g_db->get_mod_actions(msg.server_id, target_user.id, 10);
                            embed.set_title("⚖️ mod actions — " + display);

                            if (entries.empty()) {
                                embed.set_description("*no moderation actions recorded for this user.*");
                            } else {
                                std::string desc;
                                for (const auto& e : entries) {
                                    desc += action_emoji(e.action) + " **" + e.action + "**";
                                    desc += " `#" + std::to_string(e.id) + "`\n";
                                    desc += "  **time:** " + e.created_at + "\n";
                                    desc += "  **target:** <@" + e.target_id + ">\n";
                                    if (!e.reason.empty())
                                        desc += "  **reason:** " + e.reason + "\n";
                                    if (!e.extra_details.empty())
                                        desc += "  **details:** " + e.extra_details + "\n";
                                    desc += "\n";
                                }
                                desc += "-# showing latest " + std::to_string(entries.size()) + " entries";
                                embed.set_description(desc);
                            }
                        }

                        payload.add_embed(embed);
                        bot.send_message(msg.channel_id, payload);
                    });
            },
            "Moderation",
            stoatpp::permissions::KickMembers
        });

        bot.register_command({
            "slowmode",
            {},
            "set channel slowmode (0 to disable).",
            "",
            {"<seconds>", "[channel]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (args.empty()) {
                    bot.send_message(msg.channel_id, "usage: `.slowmode <seconds> [channel]`");
                    return;
                }


                int seconds = 0;
                try {
                    seconds = std::stoi(args[0]);
                    if (seconds < 0) {
                        bot.send_message(msg.channel_id, "duration cannot be negative.");
                        return;
                    }
                } catch (...) {
                    bot.send_message(msg.channel_id, "invalid duration. please specify duration in seconds.");
                    return;
                }

                std::string target_channel = msg.channel_id;
                if (args.size() > 1) {
                    target_channel = args[1];
                    if (target_channel.rfind("<#", 0) == 0 && target_channel.back() == '>') {
                        target_channel = target_channel.substr(2, target_channel.size() - 3);
                    }
                }

                nlohmann::json patch = nlohmann::json::object();
                if (seconds == 0) {
                    patch["remove"] = nlohmann::json::array({"Slowmode"});
                } else {
                    patch["slowmode"] = seconds;
                }

                auto res = bot.rest().edit_channel(target_channel, patch);
                if (res.success()) {
                    if (seconds == 0) {
                        bot.send_message(msg.channel_id, "✅ slowmode disabled in <#" + target_channel + ">.");
                    } else {
                        bot.send_message(msg.channel_id, "✅ slowmode in <#" + target_channel + "> set to **" + std::to_string(seconds) + "s**.");
                    }
                } else {
                    bot.send_message(msg.channel_id, "⚠️ failed to set slowmode. make sure I have manage channels permission.");
                }
            },
            "Moderation",
            stoatpp::permissions::ManageChannels
        });

        // Background thread to check for expired jails
        std::thread([&bot]() {
            while (true) {
                if (g_db) {
                    auto now_unix = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    auto expired_jails = g_db->get_expired_jails(now_unix);
                    for (const auto& j : expired_jails) {
                        // Unjail target user
                        auto s_opt = bot.get_server(j.server_id);
                        if (s_opt) {
                            auto j_cfg_opt = g_db->get_jail_config(j.server_id);
                            
                            // Restore roles
                            std::vector<std::string> roles_to_restore;
                            std::stringstream ss(j.roles);
                            std::string r_id;
                            while (std::getline(ss, r_id, ',')) {
                                if (!r_id.empty()) roles_to_restore.push_back(r_id);
                            }
                            nlohmann::json fields;
                            fields["roles"] = roles_to_restore;
                            bot.rest().edit_member(j.server_id, j.user_id, fields);
                        }
                        g_db->remove_active_jail(j.server_id, j.user_id);
                        
                        // Log unjail to moderation logs
                        log_moderation_action(bot, j.server_id, "Unjail", bot.current_user().id, j.user_id, "Jail duration expired");
                    }
                }
                std::this_thread::sleep_for(std::chrono::seconds(10));
            }
        }).detach();

    }
};

} // namespace bronx::cogs
