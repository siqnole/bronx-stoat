#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include <regex>
#include <algorithm>
#include <iostream>

namespace bronx::cogs {

class MasqueradeModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "masquerade",
            {"masq"},
            "customize the profile photo, nickname, and role colour of the bot using masquerade overrides.",
            "",
            {"<subcommand>", "[args...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling masquerade...\n";
                
                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id, "⚠️ This command can only be used in a server.");
                    return;
                }

                if (!g_db) {
                    bot.send_message(msg.channel_id, "⚠️ Database connection is not initialized.");
                    return;
                }

                auto server_opt = bot.get_server(msg.server_id);
                if (!server_opt) {
                    bot.send_message(msg.channel_id, "⚠️ Failed to resolve server details.");
                    return;
                }

                std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;

                // Overall check: must have ManageServer permission (or server owner, or bot owner)
                bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, server_opt, args, pfx](stoatpp::models::Member member, bool success) {
                    if (!success) {
                        bot.send_message(msg.channel_id, "⚠️ Failed to verify your server member permissions.");
                        return;
                    }

                    bool has_admin = bot.is_owner(msg.author.id) || 
                                     bot.is_server_owner(msg.server_id, msg.author.id) ||
                                     bot.has_permission(*server_opt, member, stoatpp::permissions::ManageServer);
                    
                    if (!has_admin) {
                        bot.send_message(msg.channel_id, "⚠️ Permission error: you need the Manage Server permission to configure masquerade overrides.");
                        return;
                    }

                    if (args.empty() || args[0] == "status") {
                        auto current = g_db->get_masquerade_config(msg.server_id);
                        stoatpp::models::MessagePayload payload;
                        nlohmann::json embed = nlohmann::json::object();
                        embed["title"] = "🎭 Masquerade Settings";
                        embed["colour"] = "#9b59b6";

                        std::string desc = "Current masquerade settings for this server:\n\n";
                        if (current) {
                            desc += "• **Name override**: " + (current->name ? "`" + *current->name + "`" : "*none*") + "\n";
                            desc += "• **Avatar override**: " + (current->avatar ? "[View image](" + *current->avatar + ")" : "*none*") + "\n";
                            desc += "• **Colour override**: " + (current->colour ? "`" + *current->colour + "`" : "*none*") + "\n";
                        } else {
                            desc += "• **Name override**: *none*\n";
                            desc += "• **Avatar override**: *none*\n";
                            desc += "• **Colour override**: *none*\n";
                        }
                        desc += "\nUse `" + pfx + "masquerade <name|avatar|colour|clear> [value]` to customize.";
                        embed["description"] = desc;
                        payload.embeds.push_back(embed);
                        bot.send_message(msg.channel_id, payload);
                        return;
                    }

                    std::string subcommand = args[0];
                    std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);

                    auto current_cfg = g_db->get_masquerade_config(msg.server_id).value_or(Database::MasqueradeConfig{msg.server_id});

                    if (subcommand == "clear") {
                        if (g_db->delete_masquerade_config(msg.server_id)) {
                            bot.send_message(msg.channel_id, "✅ Successfully cleared all masquerade settings for this server.");
                        } else {
                            bot.send_message(msg.channel_id, "⚠️ Failed to clear masquerade settings in the database.");
                        }
                        return;
                    }

                    if (subcommand == "name") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "Usage: `" + pfx + "masquerade name <new name>` or `" + pfx + "masquerade name clear`");
                            return;
                        }

                        // Collect name string
                        std::string val = "";
                        for (size_t i = 1; i < args.size(); ++i) {
                            val += args[i] + (i + 1 < args.size() ? " " : "");
                        }

                        std::string lower_val = val;
                        std::transform(lower_val.begin(), lower_val.end(), lower_val.begin(), ::tolower);

                        if (lower_val == "clear" || lower_val == "none" || lower_val == "reset" || lower_val == "null") {
                            current_cfg.name = std::nullopt;
                            if (g_db->save_masquerade_config(current_cfg)) {
                                bot.send_message(msg.channel_id, "✅ Cleared display name override.");
                            } else {
                                bot.send_message(msg.channel_id, "⚠️ Failed to save setting to database.");
                            }
                        } else {
                            if (val.length() < 1 || val.length() > 32) {
                                bot.send_message(msg.channel_id, "⚠️ Name override must be between 1 and 32 characters in length.");
                                return;
                            }
                            current_cfg.name = val;
                            if (g_db->save_masquerade_config(current_cfg)) {
                                bot.send_message(msg.channel_id, "✅ Set display name override to `" + val + "`.");
                            } else {
                                bot.send_message(msg.channel_id, "⚠️ Failed to save setting to database.");
                            }
                        }
                        return;
                    }

                    if (subcommand == "avatar") {
                        if (args.size() < 2 && msg.attachments.empty()) {
                            bot.send_message(msg.channel_id, "Usage: `" + pfx + "masquerade avatar <url>` or upload an image attachment.");
                            return;
                        }

                        std::string val = "";
                        if (!msg.attachments.empty()) {
                            val = bot.config().autumn_url + "/attachments/" + msg.attachments[0];
                        } else {
                            val = args[1];
                        }

                        std::string lower_val = val;
                        std::transform(lower_val.begin(), lower_val.end(), lower_val.begin(), ::tolower);

                        if (lower_val == "clear" || lower_val == "none" || lower_val == "reset" || lower_val == "null") {
                            current_cfg.avatar = std::nullopt;
                            if (g_db->save_masquerade_config(current_cfg)) {
                                bot.send_message(msg.channel_id, "✅ Cleared avatar override.");
                            } else {
                                bot.send_message(msg.channel_id, "⚠️ Failed to save setting to database.");
                            }
                        } else {
                            if (val.length() < 1 || val.length() > 256) {
                                bot.send_message(msg.channel_id, "⚠️ Avatar URL must be between 1 and 256 characters in length.");
                                return;
                            }
                            std::string uploaded_url = helpers::upload_avatar(bot, val);
                            current_cfg.avatar = uploaded_url;
                            if (g_db->save_masquerade_config(current_cfg)) {
                                bot.send_message(msg.channel_id, "✅ Set avatar override URL successfully (uploaded to Autumn).");
                            } else {
                                bot.send_message(msg.channel_id, "⚠️ Failed to save setting to database.");
                            }
                        }
                        return;
                    }

                    if (subcommand == "colour" || subcommand == "color") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "Usage: `" + pfx + "masquerade colour <CSS colour>` or `" + pfx + "masquerade colour clear`");
                            return;
                        }

                        // Additional permission check: must have ManageRole permission (or server owner, or bot owner)
                        bool has_role_perm = bot.is_owner(msg.author.id) ||
                                            bot.is_server_owner(msg.server_id, msg.author.id) ||
                                            bot.has_permission(*server_opt, member, stoatpp::permissions::ManageRole);
                        
                        if (!has_role_perm) {
                            bot.send_message(msg.channel_id, "⚠️ Permission error: you need the Manage Role permission to modify the masquerade colour.");
                            return;
                        }

                        std::string val = "";
                        for (size_t i = 1; i < args.size(); ++i) {
                            val += args[i] + (i + 1 < args.size() ? " " : "");
                        }

                        std::string lower_val = val;
                        std::transform(lower_val.begin(), lower_val.end(), lower_val.begin(), ::tolower);

                        if (lower_val == "clear" || lower_val == "none" || lower_val == "reset" || lower_val == "null") {
                            current_cfg.colour = std::nullopt;
                            if (g_db->save_masquerade_config(current_cfg)) {
                                bot.send_message(msg.channel_id, "✅ Cleared colour override.");
                            } else {
                                bot.send_message(msg.channel_id, "⚠️ Failed to save setting to database.");
                            }
                        } else {
                            if (val.length() < 1 || val.length() > 128) {
                                bot.send_message(msg.channel_id, "⚠️ Colour override must be between 1 and 128 characters in length.");
                                return;
                            }

                            // Regular expression validation
                            std::regex pattern(
                                R"(^(?:[a-z ]+|var\(--[a-z\d-]+\)|rgba?\([\d, ]+\)|#[a-f0-9]+|(repeating-)?(linear|conic|radial)-gradient\(([a-z ]+|var\(--[a-z\d-]+\)|rgba?\([\d, ]+\)|#[a-f0-9]+|\d+deg)([ ]+(\d{1,3}%|0))?(,[ ]*([a-z ]+|var\(--[a-z\d-]+\)|rgba?\([\d, ]+\)|#[a-f0-9]+)([ ]+(\d{1,3}%|0))?)+\))$)",
                                std::regex_constants::ECMAScript | std::regex_constants::icase
                            );

                            if (!std::regex_match(val, pattern)) {
                                bot.send_message(msg.channel_id, "⚠️ Invalid color/gradient pattern. Please specify a valid hex code, rgb/rgba, CSS name, or CSS gradient.");
                                return;
                            }

                            current_cfg.colour = val;
                            if (g_db->save_masquerade_config(current_cfg)) {
                                bot.send_message(msg.channel_id, "✅ Set display role colour override to `" + val + "`.");
                            } else {
                                bot.send_message(msg.channel_id, "⚠️ Failed to save setting to database.");
                            }
                        }
                        return;
                    }

                    bot.send_message(msg.channel_id, "⚠️ Unknown subcommand. Use `" + pfx + "masquerade <name|avatar|colour|clear|status>`.");
                });
            },
            "Configuration",
            0,
            0,
            {"name <new name>", "avatar <url>", "colour <css colour>", "clear", "status"}
        });
    }
};

} // namespace bronx::cogs
