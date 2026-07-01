#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "../database.h"
#include <algorithm>
#include <iostream>
#include <unordered_set>

namespace bronx::cogs {

inline int64_t parse_permission_name(const std::string& name) {
    std::string n = name;
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return std::tolower(c); });
    if (n == "managechannels") return stoatpp::permissions::ManageChannels;
    if (n == "manageserver") return stoatpp::permissions::ManageServer;
    if (n == "managepermissions") return stoatpp::permissions::ManagePermissions;
    if (n == "managerole" || n == "manageroles") return stoatpp::permissions::ManageRole;
    if (n == "managecustomisation") return stoatpp::permissions::ManageCustomisation;
    if (n == "kickmembers" || n == "kick") return stoatpp::permissions::KickMembers;
    if (n == "banmembers" || n == "ban") return stoatpp::permissions::BanMembers;
    if (n == "timeoutmembers" || n == "timeout") return stoatpp::permissions::TimeoutMembers;
    if (n == "assignroles") return stoatpp::permissions::AssignRoles;
    if (n == "changenickname") return stoatpp::permissions::ChangeNickname;
    if (n == "managenicknames") return stoatpp::permissions::ManageNicknames;
    if (n == "manageavatar" || n == "manageavatars") return stoatpp::permissions::ManageAvatar;
    if (n == "removeavatars") return stoatpp::permissions::RemoveAvatars;
    if (n == "viewchannel") return stoatpp::permissions::ViewChannel;
    if (n == "sendmessages") return stoatpp::permissions::SendMessages;
    if (n == "managemessages" || n == "purge") return stoatpp::permissions::ManageMessages;
    if (n == "managewebhooks") return stoatpp::permissions::ManageWebhooks;
    if (n == "createinvites") return stoatpp::permissions::CreateInvites;
    if (n == "embedlinks") return stoatpp::permissions::EmbedLinks;
    if (n == "uploadfiles") return stoatpp::permissions::UploadFiles;
    if (n == "addreactions") return stoatpp::permissions::AddReactions;
    return 0;
}

inline std::string resolve_primary_command_name(stoatpp::cluster& bot, const std::string& input_name) {
    std::string target = input_name;
    std::transform(target.begin(), target.end(), target.begin(), [](unsigned char c) { return std::tolower(c); });
    for (const auto& cmd : bot.get_commands()) {
        std::string c_name = cmd.name;
        std::transform(c_name.begin(), c_name.end(), c_name.begin(), [](unsigned char c) { return std::tolower(c); });
        if (c_name == target) return c_name;
        for (const auto& alias : cmd.aliases) {
            std::string a_name = alias;
            std::transform(a_name.begin(), a_name.end(), a_name.begin(), [](unsigned char c) { return std::tolower(c); });
            if (a_name == target) return c_name;
        }
    }
    return target;
}

inline bool is_user_trusted_or_owner(stoatpp::cluster& bot, const std::string& server_id, const std::string& user_id, const stoatpp::models::Member& member) {
    if (bot.is_owner(user_id)) return true;
    if (bot.is_server_owner(server_id, user_id)) return true;
    
    // Check ManageServer permission
    auto server_opt = bot.get_server(server_id);
    if (server_opt) {
        if (bot.has_permission(*server_opt, member, stoatpp::permissions::ManageServer)) {
            return true;
        }
    }
    
    if (g_db) {
        return g_db->is_trusted(server_id, user_id, member.roles);
    }
    return false;
}

class CommandConfigModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
        // --- TRUST COMMAND ---
        auto handle_trust = [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
            std::cout << "[bronx command] handling trust...\n";
            if (msg.server_id.empty()) {
                bot.send_message(msg.channel_id, "⚠️ This command can only be used in a server channel.");
                return;
            }
            if (!g_db) {
                bot.send_message(msg.channel_id, "⚠️ Database is unavailable.");
                return;
            }

            bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, args](stoatpp::models::Member member, bool success) {
                if (!success) {
                    bot.send_message(msg.channel_id, "⚠️ Failed to verify your permissions.");
                    return;
                }

                if (!is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, member)) {
                    bot.send_message(msg.channel_id, "permission error: you need to be trusted or have Manage Server permission to manage trust settings.");
                    return;
                }

                std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                if (args.empty()) {
                    // Show current trusted list
                    auto list = g_db->get_trusted(msg.server_id);
                    if (list.empty()) {
                        bot.send_message(msg.channel_id, "ℹ️ No trusted users or roles configured on this server. Only Server/Bot Owners can run configuration commands.");
                        return;
                    }
                    std::string out = "🛡️ **Trusted Users and Roles:**\n";
                    for (const auto& [type, id] : list) {
                        if (type == "user") {
                            out += "• User: <@" + id + "> (`" + id + "`)\n";
                        } else {
                            out += "• Role: <%" + id + "> (`" + id + "`)\n";
                        }
                    }
                    bot.send_message(msg.channel_id, out);
                    return;
                }

                std::string target = args[0];
                // 1. Resolve as User Mention
                if (target.rfind("<@", 0) == 0 && target.back() == '>' && target.find("@&") == std::string::npos) {
                    std::string user_id = target.substr(2, target.size() - 3);
                    if (user_id.rfind("!", 0) == 0) user_id = user_id.substr(1);
                    
                    bool already = g_db->is_trusted(msg.server_id, user_id, {});
                    if (already) {
                        g_db->remove_trusted(msg.server_id, "user", user_id);
                        bot.send_message(msg.channel_id, "✅ Removed trust from user <@" + user_id + ">.");
                    } else {
                        g_db->add_trusted(msg.server_id, "user", user_id);
                        bot.send_message(msg.channel_id, "✅ Granted trust to user <@" + user_id + ">.");
                    }
                    return;
                }

                // 2. Resolve as Role Mention
                if (target.rfind("<%", 0) == 0 && target.back() == '>') {
                    std::string role_id = target.substr(2, target.size() - 3);
                    bool already = g_db->is_trusted(msg.server_id, "", {role_id});
                    if (already) {
                        g_db->remove_trusted(msg.server_id, "role", role_id);
                        bot.send_message(msg.channel_id, "✅ Removed trust from role <%" + role_id + ">.");
                    } else {
                        g_db->add_trusted(msg.server_id, "role", role_id);
                        bot.send_message(msg.channel_id, "✅ Granted trust to role <%" + role_id + ">.");
                    }
                    return;
                }

                // 3. Plain ID or Text Search
                // Try looking up a role first
                std::string role_id = "";
                auto server_opt = bot.get_server(msg.server_id);
                if (server_opt) {
                    for (const auto& r : server_opt->roles) {
                        if (r.id == target || r.name == target) {
                            role_id = r.id;
                            break;
                        }
                    }
                    if (role_id.empty()) {
                        // Substring role search
                        std::string lower_target = target;
                        std::transform(lower_target.begin(), lower_target.end(), lower_target.begin(), ::tolower);
                        for (const auto& r : server_opt->roles) {
                            std::string lower_name = r.name;
                            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                            if (lower_name.find(lower_target) != std::string::npos) {
                                role_id = r.id;
                                break;
                            }
                        }
                    }
                }

                if (!role_id.empty()) {
                    bool already = g_db->is_trusted(msg.server_id, "", {role_id});
                    if (already) {
                        g_db->remove_trusted(msg.server_id, "role", role_id);
                        bot.send_message(msg.channel_id, "✅ Removed trust from role <%" + role_id + ">.");
                    } else {
                        g_db->add_trusted(msg.server_id, "role", role_id);
                        bot.send_message(msg.channel_id, "✅ Granted trust to role <%" + role_id + ">.");
                    }
                    return;
                }

                // If not role, try user ID fallback
                bool already = g_db->is_trusted(msg.server_id, target, {});
                if (already) {
                    g_db->remove_trusted(msg.server_id, "user", target);
                    bot.send_message(msg.channel_id, "✅ Removed trust from user <@" + target + ">.");
                } else {
                    g_db->add_trusted(msg.server_id, "user", target);
                    bot.send_message(msg.channel_id, "✅ Granted trust to user <@" + target + ">.");
                }
            });
        };

        bot.register_command({
            "trust",
            {},
            "toggle trusted status for a user or role.",
            "",
            {"<user|role>"},
            handle_trust,
            "Administration",
            0,
            0,
            {}
        });

        // --- COMMAND CONFIGURATION ---
        auto handle_command = [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
            std::cout << "[bronx command] handling command config...\n";
            if (msg.server_id.empty()) {
                bot.send_message(msg.channel_id, "⚠️ This command can only be used in a server channel.");
                return;
            }
            if (!g_db) {
                bot.send_message(msg.channel_id, "⚠️ Database is unavailable.");
                return;
            }

            bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, args](stoatpp::models::Member member, bool success) {
                if (!success) {
                    bot.send_message(msg.channel_id, "⚠️ Failed to verify your permissions.");
                    return;
                }

                if (!is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, member)) {
                    bot.send_message(msg.channel_id, "permission error: you need to be trusted or have Manage Server permission to configure command permissions.");
                    return;
                }

                std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                if (args.empty()) {
                    bot.send_message(msg.channel_id, "📋 **Command Permissions Commands:**\n"
                                                     "• `" + pfx + "command allow <cmd/module> <channel/role/permission>` - Whitelist channel, role, or permission.\n"
                                                     "• `" + pfx + "command restrict <cmd/module> <channel/role/permission>` - Blacklist channel, role, or permission.\n"
                                                     "• `" + pfx + "command view [cmd/module]` - View current restrictions.\n"
                                                     "• `" + pfx + "command disable [cmd/module]` - Disable command/module completely.\n"
                                                     "• `" + pfx + "command enable [cmd/module]` - Re-enable command/module.\n"
                                                     "• `" + pfx + "command disable` - List all disabled commands.");
                    return;
                }

                std::string subcommand = args[0];
                std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);

                // Check for module grouping prefix: if "module" is specified, skip it and mark target_type as "module"
                std::string target_type = "command";
                size_t offset = 1;
                if (subcommand == "module") {
                    target_type = "module";
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "command module <allow/restrict/view/disable/enable> ...`");
                        return;
                    }
                    subcommand = args[1];
                    std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);
                    offset = 2;
                }

                if (subcommand == "disable") {
                    if (args.size() <= offset) {
                        // Show all disabled
                        auto disabled = g_db->get_disabled_items(msg.server_id);
                        if (disabled.empty()) {
                            bot.send_message(msg.channel_id, "ℹ️ No commands or modules are disabled on this server.");
                            return;
                        }
                        std::string out = "🚫 **Disabled Commands and Modules:**\n";
                        for (const auto& [type, name] : disabled) {
                            out += "• " + type + ": `" + name + "`\n";
                        }
                        bot.send_message(msg.channel_id, out);
                        return;
                    }

                    std::string name = args[offset];
                    if (target_type == "command") {
                        name = resolve_primary_command_name(bot, name);
                    } else {
                        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    }

                    if (g_db->disable_item(msg.server_id, target_type, name)) {
                        bot.send_message(msg.channel_id, "✅ Completely disabled " + target_type + " `" + name + "`.");
                    } else {
                        bot.send_message(msg.channel_id, "⚠️ Failed to disable " + target_type + ".");
                    }
                    return;
                }

                if (subcommand == "enable") {
                    if (args.size() <= offset) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "command [" + target_type + "] enable <name>`");
                        return;
                    }

                    std::string name = args[offset];
                    if (target_type == "command") {
                        name = resolve_primary_command_name(bot, name);
                    } else {
                        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    }

                    if (g_db->enable_item(msg.server_id, target_type, name)) {
                        bot.send_message(msg.channel_id, "✅ Enabled " + target_type + " `" + name + "`.");
                    } else {
                        bot.send_message(msg.channel_id, "⚠️ Failed to enable " + target_type + ".");
                    }
                    return;
                }

                if (subcommand == "view") {
                    std::string name = (args.size() > offset) ? args[offset] : "";
                    if (!name.empty()) {
                        if (target_type == "command") {
                            name = resolve_primary_command_name(bot, name);
                        } else {
                            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                        }
                    }

                    if (name.empty()) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "command [" + target_type + "] view <name>`");
                        return;
                    }

                    auto rules = g_db->get_restrictions(msg.server_id, target_type, name);
                    std::string out = "📋 **Permissions for " + target_type + " `" + name + "`:**\n";
                    
                    bool has_rules = false;
                    for (const auto& re : rules) {
                        has_rules = true;
                        std::string rest_str = (re.restriction_type == "allow") ? "🟢 Whitelisted" : "🔴 Blacklisted";
                        std::string ent_str = "";
                        if (re.entity_type == "role") {
                            ent_str = "role <%" + re.entity_id + ">";
                        } else if (re.entity_type == "channel") {
                            ent_str = "channel <#" + re.entity_id + ">";
                        } else if (re.entity_type == "permission") {
                            ent_str = "permission `" + re.entity_id + "`";
                        }
                        out += "• " + rest_str + " in/for " + ent_str + "\n";
                    }
                    if (!has_rules) {
                        out += "*No channel, role, or permission restrictions configured.*";
                    }
                    bot.send_message(msg.channel_id, out);
                    return;
                }

                if (subcommand == "allow" || subcommand == "restrict") {
                    if (args.size() < offset + 2) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "command [" + target_type + "] " + subcommand + " <name> <channel/role/permission>`");
                        return;
                    }

                    std::string name = args[offset];
                    if (target_type == "command") {
                        name = resolve_primary_command_name(bot, name);
                    } else {
                        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    }

                    std::string entity = args[offset + 1];
                    std::string entity_type = "";
                    std::string entity_id = "";

                    // 1. Resolve Channel Mention
                    if (entity.rfind("<#", 0) == 0 && entity.back() == '>') {
                        entity_type = "channel";
                        entity_id = entity.substr(2, entity.size() - 3);
                    } else if (entity.rfind("<%", 0) == 0 && entity.back() == '>') {
                        entity_type = "role";
                        entity_id = entity.substr(2, entity.size() - 3);
                    } else {
                        // 2. Resolve Permission Name
                        int64_t perm_mask = parse_permission_name(entity);
                        if (perm_mask > 0) {
                            entity_type = "permission";
                            // Normalize the case of the permission name to match pretty constants
                            if (perm_mask == stoatpp::permissions::ManageChannels) entity_id = "ManageChannels";
                            else if (perm_mask == stoatpp::permissions::ManageServer) entity_id = "ManageServer";
                            else if (perm_mask == stoatpp::permissions::ManagePermissions) entity_id = "ManagePermissions";
                            else if (perm_mask == stoatpp::permissions::ManageRole) entity_id = "ManageRole";
                            else if (perm_mask == stoatpp::permissions::ManageCustomisation) entity_id = "ManageCustomisation";
                            else if (perm_mask == stoatpp::permissions::KickMembers) entity_id = "KickMembers";
                            else if (perm_mask == stoatpp::permissions::BanMembers) entity_id = "BanMembers";
                            else if (perm_mask == stoatpp::permissions::TimeoutMembers) entity_id = "TimeoutMembers";
                            else if (perm_mask == stoatpp::permissions::AssignRoles) entity_id = "AssignRoles";
                            else if (perm_mask == stoatpp::permissions::ChangeNickname) entity_id = "ChangeNickname";
                            else if (perm_mask == stoatpp::permissions::ManageNicknames) entity_id = "ManageNicknames";
                            else if (perm_mask == stoatpp::permissions::ManageAvatar) entity_id = "ManageAvatar";
                            else if (perm_mask == stoatpp::permissions::RemoveAvatars) entity_id = "RemoveAvatars";
                            else if (perm_mask == stoatpp::permissions::ViewChannel) entity_id = "ViewChannel";
                            else if (perm_mask == stoatpp::permissions::SendMessages) entity_id = "SendMessages";
                            else if (perm_mask == stoatpp::permissions::ManageMessages) entity_id = "ManageMessages";
                            else if (perm_mask == stoatpp::permissions::ManageWebhooks) entity_id = "ManageWebhooks";
                            else if (perm_mask == stoatpp::permissions::CreateInvites) entity_id = "CreateInvites";
                            else if (perm_mask == stoatpp::permissions::EmbedLinks) entity_id = "EmbedLinks";
                            else if (perm_mask == stoatpp::permissions::UploadFiles) entity_id = "UploadFiles";
                            else if (perm_mask == stoatpp::permissions::AddReactions) entity_id = "AddReactions";
                        }
                        
                        if (entity_type.empty()) {
                            // 3. Resolve Channel by Name
                            auto srv_channels = bot.rest().get_server_channels(msg.server_id);
                            if (srv_channels.success() && srv_channels.body.is_array()) {
                                for (const auto& ch_j : srv_channels.body) {
                                    auto c = stoatpp::models::Channel::from_json(ch_j);
                                    if (c.name && *c.name == entity) {
                                        entity_type = "channel";
                                        entity_id = c.id;
                                        break;
                                    }
                                }
                            }
                        }
                        
                        if (entity_type.empty()) {
                            // 4. Resolve Role by Name
                            auto srv_opt = bot.get_server(msg.server_id);
                            if (srv_opt) {
                                for (const auto& r : srv_opt->roles) {
                                    if (r.name == entity || r.id == entity) {
                                        entity_type = "role";
                                        entity_id = r.id;
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    if (entity_type.empty() || entity_id.empty()) {
                        bot.send_message(msg.channel_id, "⚠️ Could not resolve channel, role name/mention, or permission: `" + entity + "`.");
                        return;
                    }

                    // Toggling logic: if rule exists, remove it. Otherwise add it.
                    auto current = g_db->get_restrictions(msg.server_id, target_type, name);
                    bool exists = false;
                    for (const auto& re : current) {
                        if (re.restriction_type == subcommand && re.entity_type == entity_type && re.entity_id == entity_id) {
                            exists = true;
                            break;
                        }
                    }

                    std::string ent_repr = "";
                    if (entity_type == "role") ent_repr = "role <%" + entity_id + ">";
                    else if (entity_type == "channel") ent_repr = "channel <#" + entity_id + ">";
                    else if (entity_type == "permission") ent_repr = "permission `" + entity_id + "`";

                    if (exists) {
                        if (g_db->remove_restriction(msg.server_id, target_type, name, subcommand, entity_type, entity_id)) {
                            bot.send_message(msg.channel_id, "✅ Removed restriction rule: " + subcommand + " " + target_type + " `" + name + "` in/for " + ent_repr + ".");
                        } else {
                            bot.send_message(msg.channel_id, "⚠️ Failed to remove restriction.");
                        }
                    } else {
                        if (g_db->add_restriction(msg.server_id, target_type, name, subcommand, entity_type, entity_id)) {
                            bot.send_message(msg.channel_id, "✅ Configured restriction rule: " + subcommand + " " + target_type + " `" + name + "` in/for " + ent_repr + ".");
                        } else {
                            bot.send_message(msg.channel_id, "⚠️ Failed to save restriction.");
                        }
                    }
                    return;
                }

                bot.send_message(msg.channel_id, "⚠️ Unknown subcommand. Use `allow`, `restrict`, `view`, `disable`, or `enable`.");
            });
        };

        bot.register_command({
            "command",
            {"cmd"},
            "configure whitelists, blacklists, and disables for commands.",
            "",
            {"<subcommand>", "[args...]"},
            handle_command,
            "Administration",
            0,
            0,
            {"allow <name> <channel/role/permission>", "restrict <name> <channel/role/permission>", "view [name]", "disable [name]", "enable [name]", "module allow ..."}
        });
    }
};

} // namespace bronx::cogs
