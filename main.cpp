#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <algorithm>
#include <sstream>
#include "stoatpp/cluster.h"
#include "stoatpp/bot_module.h"
#include "stoatpp/utils/ulid.h"
#include <nlohmann/json.hpp>

// Include our Cogs
#include "cogs/general.h"
#include "cogs/moderation.h"
#include "cogs/roles.h"
#include "cogs/users.h"
#include "cogs/prefix.h"
#include "cogs/reaction_roles.h"
#include "cogs/emoji.h"
#include "cogs/help.h"
#include "cogs/stats.h"
#include "cogs/economy.h"
#include "cogs/blackjack.h"
#include "cogs/welcome_leave.h"
#include "cogs/logging.h"
#include "cogs/command_config.h"
#include "cogs/media.h"
#include "cogs/reminders.h"
#include "cogs/antiraid.h"

// Simple self-contained helper to load environment variables from a .env file
void load_env(const std::string& filepath = ".env") {
    std::ifstream file(filepath);
    std::string path_used = filepath;
    if (!file.is_open()) {
        path_used = "../" + filepath;
        file.open(path_used);
    }
    if (!file.is_open()) {
        std::cout << "[bronx env] warning: could not open " << filepath << " or parent directory file. falling back to system environment variables.\n";
        return;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        key.erase(0, key.find_first_not_of(" \t\r\n"));
        key.erase(key.find_last_not_of(" \t\r\n") + 1);
        value.erase(0, value.find_first_not_of(" \t\r\n"));
        value.erase(value.find_last_not_of(" \t\r\n") + 1);

        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }

        setenv(key.c_str(), value.c_str(), 1);
    }
    std::cout << "[bronx env] successfully loaded variables from " << path_used << "\n";
}

int main(int argc, char* argv[]) {
    load_env();

    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        }
    }

    const char* token_env = std::getenv("BOT_TOKEN");
    std::string token = token_env ? token_env : "";
    if (token.empty() || token == "token") {
        std::cerr << "[bronx error] bot_token is empty or not set. please update your .env file.\n";
        return 1;
    }

    // DB configuration
    const char* db_host_env = std::getenv("DB_HOST");
    std::string db_host = db_host_env ? db_host_env : "127.0.0.1";
    
    const char* db_user_env = std::getenv("DB_USER");
    std::string db_user = db_user_env ? db_user_env : "root";
    
    const char* db_pass_env = std::getenv("DB_PASSWORD");
    std::string db_pass = db_pass_env ? db_pass_env : "";
    
    const char* db_name_env = std::getenv("DB_DATABASE");
    std::string db_name = db_name_env ? db_name_env : "bronx";
    
    int db_port = 3306;
    if (const char* db_port_env = std::getenv("DB_PORT")) {
        try {
            db_port = std::stoi(db_port_env);
        } catch(...) {}
    }

    std::cout << "[bronx db] connecting to MariaDB on " << db_host << ":" << db_port << "...\n";
    bronx::g_db = std::make_shared<bronx::Database>();
    if (!bronx::g_db->init(db_host, db_user, db_pass, db_name, db_port)) {
        std::cerr << "[bronx error] failed to initialize database connection. exiting.\n";
        return 1;
    }

    stoatpp::ClientConfig cfg;
    cfg.log_level = verbose ? stoatpp::LogLevel::TRACE : stoatpp::LogLevel::INFO;
    cfg.enable_default_help = false;
    cfg.dispatch_commands_on_edit = true;
    cfg.case_insensitive_commands = true;
    
    cfg.command_prefix = "!";
    if (const char* pfx = std::getenv("COMMAND_PREFIX")) {
        cfg.command_prefix = pfx;
    }

    if (const char* api_url = std::getenv("API_BASE_URL")) {
        cfg.api_base_url = api_url;
    }
    if (const char* ws_url = std::getenv("WS_URL")) {
        cfg.ws_url = ws_url;
    }

    cfg.prefix_resolver = [](stoatpp::cluster& bot, const stoatpp::events::Message& msg) -> std::vector<std::string> {
        std::vector<std::string> prefixes;
        bool server_has_custom = false;

        if (bronx::g_db) {
            // 1. User prefixes always apply
            std::vector<std::string> user_pfx = bronx::g_db->get_prefixes("user", msg.author.id);
            prefixes.insert(prefixes.end(), user_pfx.begin(), user_pfx.end());

            // 2. Server custom prefixes
            if (!msg.server_id.empty()) {
                std::vector<std::string> server_pfx = bronx::g_db->get_prefixes("server", msg.server_id);
                if (!server_pfx.empty()) {
                    server_has_custom = true;
                    prefixes.insert(prefixes.end(), server_pfx.begin(), server_pfx.end());
                }
            }
        }

        // 3. Default prefix — skip if the server has custom prefixes AND default is disabled
        bool add_default = true;
        if (server_has_custom && !msg.server_id.empty() && bronx::g_db) {
            add_default = bronx::g_db->get_prefix_default_enabled(msg.server_id);
        }
        if (add_default) {
            prefixes.push_back(bot.config().command_prefix);
        }

        // Deduplicate
        std::vector<std::string> unique_prefixes;
        for (const auto& p : prefixes) {
            if (std::find(unique_prefixes.begin(), unique_prefixes.end(), p) == unique_prefixes.end()) {
                unique_prefixes.push_back(p);
            }
        }
        return unique_prefixes;
    };

    std::cout << "[bronx] initialising stoatpp cluster...\n";
    std::cout << "        api base: " << cfg.api_base_url << "\n";
    std::cout << "        ws gateway: " << cfg.ws_url << "\n";
    std::cout << "        command prefix: " << cfg.command_prefix << "\n";

    stoatpp::cluster bot(token, cfg);

    // Register our Cogs/Modules
    bot.use(std::make_unique<bronx::cogs::HelpModule>());
    bot.use(std::make_unique<bronx::cogs::GeneralModule>());
    bot.use(std::make_unique<bronx::cogs::ModerationModule>());
    bot.use(std::make_unique<bronx::cogs::RolesModule>());
    bot.use(std::make_unique<bronx::cogs::UsersModule>());
    bot.use(std::make_unique<bronx::cogs::PrefixModule>());
    bot.use(std::make_unique<bronx::cogs::ReactionRolesModule>());
    bot.use(std::make_unique<bronx::cogs::EmojiModule>());
    bot.use(std::make_unique<bronx::cogs::StatsModule>());
    bot.use(std::make_unique<bronx::cogs::EconomyModule>());
    bot.use(std::make_unique<bronx::cogs::BlackjackModule>());
    bot.use(std::make_unique<bronx::cogs::WelcomeLeaveModule>());
    bot.use(std::make_unique<bronx::cogs::LoggingModule>());
    bot.use(std::make_unique<bronx::cogs::CommandConfigModule>());
    bot.use(std::make_unique<bronx::cogs::MediaModule>());
    bot.use(std::make_unique<bronx::cogs::RemindersModule>());
    bot.use(std::make_unique<bronx::cogs::AntiRaidModule>());

    bot.on_ready([&bot](const stoatpp::events::Ready& e) {
        std::cout << "[bronx event] ready! logged in as: " << e.user.username 
                  << " (id: " << e.user.id << ")\n";
        std::cout << "              serving: " << e.servers.size() << " servers, "
                  << e.channels.size() << " channels.\n";
        std::cout << "              users in ready payload: " << e.users.size() << "\n";

        std::string status_text = "made with c++! | coming soon | " + std::to_string(e.servers.size()) + " servers";
        bot.update_status(status_text, "Online", [](bool success) {
            if (success) {
                std::cout << "[bronx] successfully set presence status.\n";
            } else {
                std::cout << "[bronx] failed to set presence status.\n";
            }
        });

        // Fix historical DB rows where bot users were mis-tagged as is_bot=FALSE.
        // Revolt only sends "bot": true reliably in the Ready payload; message payloads
        // omit it. We run this once per startup on a background thread.
        if (bronx::g_db) {
            // Collect bot user IDs: all users in Ready with bot=true, plus self
            std::vector<std::string> bot_ids;
            for (const auto& u : e.users) {
                if (u.bot && !u.id.empty()) {
                    bot_ids.push_back(u.id);
                    std::cout << "[bronx] ready: detected bot user " << u.username
                              << " (" << u.id << ")\n";
                }
            }
            // bronx itself is always a bot
            if (!e.user.id.empty()) {
                bot_ids.push_back(e.user.id);
            }

            std::thread([bot_ids]() {
                for (const auto& id : bot_ids) {
                    bronx::g_db->fix_bot_flags(id);
                }
            }).detach();
        }
    });

    bot.on_message([&bot](const stoatpp::events::Message& msg) {
        std::cout << "[bronx event] message in channel " << msg.channel_id
                  << " by " << msg.author.username << ": " << msg.content << "\n";

        if (bronx::g_db) {
            // Resolve bot flag — Revolt sometimes omits "bot" in message payloads,
            // so fall back to the user cache if the field isn't set on this message.
            bool is_bot = msg.author.bot;
            if (!is_bot) {
                auto cached = bot.get_user(msg.author.id);
                if (cached && cached->bot) is_bot = true;
            }

            // 1. Message Telemetry
            bronx::g_db->increment_user_daily_messages(msg.server_id, msg.author.id, is_bot, "message");

            // 2. Channel Telemetry
            bronx::g_db->increment_channel_daily_messages(msg.server_id, msg.channel_id);

            // Skip command telemetry tracking for bots
            if (is_bot) return;

            // 3. Command Telemetry
            std::vector<std::string> prefixes;
            std::vector<std::string> user_pfx = bronx::g_db->get_prefixes("user", msg.author.id);
            prefixes.insert(prefixes.end(), user_pfx.begin(), user_pfx.end());
            
            if (!msg.server_id.empty()) {
                std::vector<std::string> server_pfx = bronx::g_db->get_prefixes("server", msg.server_id);
                prefixes.insert(prefixes.end(), server_pfx.begin(), server_pfx.end());
            }
            prefixes.push_back(bot.config().command_prefix);

            std::vector<std::string> unique_prefixes;
            for (const auto& p : prefixes) {
                if (std::find(unique_prefixes.begin(), unique_prefixes.end(), p) == unique_prefixes.end()) {
                    unique_prefixes.push_back(p);
                }
            }

            std::string matched_prefix;
            for (const auto& p : unique_prefixes) {
                if (!p.empty() && msg.content.rfind(p, 0) == 0) {
                    matched_prefix = p;
                    break;
                }
            }

            if (!matched_prefix.empty()) {
                std::stringstream ss(msg.content.substr(matched_prefix.size()));
                std::string cmd_name;
                ss >> cmd_name;

                if (!cmd_name.empty()) {
                    bool cmd_exists = false;
                    std::string normalized_name = cmd_name;
                    for (const auto& cmd : bot.get_commands()) {
                        if (cmd.name == cmd_name || std::find(cmd.aliases.begin(), cmd.aliases.end(), cmd_name) != cmd.aliases.end()) {
                            cmd_exists = true;
                            normalized_name = cmd.name;
                            break;
                        }
                    }

                    if (cmd_exists) {
                        bronx::g_db->increment_command_usage(msg.server_id, normalized_name, msg.channel_id);
                        bronx::g_db->increment_user_daily_commands(msg.server_id, msg.author.id, msg.author.bot);
                    }
                }
            }
        }
    });

    bot.on_message_update([](const stoatpp::events::MessageUpdate& e) {
        std::cout << "[bronx event] message updated! id: " << e.id << " in channel " << e.channel_id << "\n";
    });

    bot.on_message_delete([](const stoatpp::events::MessageDelete& e) {
        std::cout << "[bronx event] message deleted! id: " << e.id << " in channel " << e.channel_id << "\n";
    });

    bot.on_message_react([](const stoatpp::events::MessageReact& e) {
        std::cout << "[bronx event] react: user " << e.user_id 
                  << " added emoji " << e.emoji_id << " to message " << e.id << "\n";
    });

    bot.on_message_unreact([](const stoatpp::events::MessageUnreact& e) {
        std::cout << "[bronx event] unreact: user " << e.user_id 
                  << " removed emoji " << e.emoji_id << " from message " << e.id << "\n";
    });

    bot.on_channel_create([](const stoatpp::events::ChannelCreate& e) {
        std::cout << "[bronx event] channel created: " << e.channel.name.value_or("(unnamed)") 
                  << " (id: " << e.channel.id << ")\n";
    });

    bot.on_channel_delete([](const stoatpp::events::ChannelDelete& e) {
        std::cout << "[bronx event] channel deleted: " << e.id << "\n";
    });

    bot.on_server_create([](const stoatpp::events::ServerCreate& e) {
        std::cout << "[bronx event] server added: " << e.server.name << " (id: " << e.server.id << ")\n";
    });



    bot.on_error([](const stoatpp::events::Error& e) {
        std::cerr << "[bronx event] gateway error: " << e.error_id << "\n";
    });

    bot.on_rest_error([](const std::string& method, const std::string& path, int status_code, const std::string& error_msg) {
        std::cerr << "[bronx rest error] " << method << " " << path << " -> " << status_code << ": " << error_msg << "\n";
    });

    bot.on_logout([](const stoatpp::events::Logout&) {
        std::cout << "[bronx event] logout detected\n";
    });

    if (verbose) {
        bot.on_raw_event([](const std::string& type, const nlohmann::json& data) {
            std::cout << "[bronx audit raw] " << type << " payload size: " << data.dump().size() << "\n";
        });
    }

    bot.on_command_cooldown([](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const stoatpp::Command& cmd, int64_t remaining) {
        if (cmd.category == "Economy") {
            std::string time_str = "";
            if (remaining >= 3600) {
                int64_t hours = remaining / 3600;
                int64_t mins = (remaining % 3600) / 60;
                time_str = std::to_string(hours) + "h " + std::to_string(mins) + "m";
            } else if (remaining >= 60) {
                time_str = std::to_string(remaining / 60) + "m " + std::to_string(remaining % 60) + "s";
            } else {
                time_str = std::to_string(remaining) + "s";
            }
            bot.send_message(msg.channel_id, "you are on cooldown. please wait " + time_str + ".");
        } else {
            auto now_unix = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            std::string ts_tag = stoatpp::utils::format_timestamp(now_unix + remaining, "R");
            bot.send_message(msg.channel_id, "you are on cooldown. please wait " + ts_tag + ".");
        }
    });

    bot.on_command_error([](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const stoatpp::Command& cmd, const std::string& error_type) {
        if (error_type == "member_fetch_failed") {
            bot.send_message(msg.channel_id, "failed to verify your server member details.");
        } else if (error_type == "server_resolve_failed") {
            bot.send_message(msg.channel_id, "failed to verify server configuration.");
        } else if (error_type == "permission_denied") {
            bot.send_message(msg.channel_id, "permission error: you do not have the required permissions to use this command.");
        } else if (error_type == "server_only") {
            bot.send_message(msg.channel_id, "this command can only be used in a server.");
        }
    });

    bot.on_command_run([](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const stoatpp::Command& cmd, const std::optional<stoatpp::models::Member>& mem_opt, const std::vector<std::string>& args) {
        if (msg.server_id.empty()) return true; 
        if (!bronx::g_db) return true;

        std::string cmd_name = cmd.name;
        std::string mod_name = cmd.category;
        std::transform(cmd_name.begin(), cmd_name.end(), cmd_name.begin(), ::tolower);
        std::transform(mod_name.begin(), mod_name.end(), mod_name.begin(), ::tolower);

        // 1. Completely Disabled Commands/Modules Check (ignores invocation completely)
        if (bronx::g_db->is_disabled(msg.server_id, cmd_name, mod_name)) {
            // Bypass for Server Owner or Bot Owner on command config commands to avoid locked out state
            if (bot.is_owner(msg.author.id) || bot.is_server_owner(msg.server_id, msg.author.id)) {
                if (cmd_name == "command" || cmd_name == "trust" || cmd_name == "cmd") {
                    return true;
                }
            }
            return false;
        }

        // Emergency Override: Bot Owner always bypasses. Server Owner and ManageServer users only bypass config commands
        bool bypass = bot.is_owner(msg.author.id);
        if (!bypass) {
            bool is_admin = bot.is_server_owner(msg.server_id, msg.author.id);
            if (!is_admin && mem_opt) {
                auto server_opt = bot.get_server(msg.server_id);
                if (server_opt && bot.has_permission(*server_opt, *mem_opt, stoatpp::permissions::ManageServer)) {
                    is_admin = true;
                }
            }
            if (is_admin && (cmd_name == "command" || cmd_name == "trust" || cmd_name == "cmd")) {
                bypass = true;
            }
        }

        // Whitelist/blacklist restrictions check
        if (!bypass) {
            auto rules = bronx::g_db->get_restrictions(msg.server_id, "command", cmd_name);
            auto mod_rules = bronx::g_db->get_restrictions(msg.server_id, "module", mod_name);
            rules.insert(rules.end(), mod_rules.begin(), mod_rules.end());

            if (!rules.empty()) {
                std::vector<std::string> whitelist_roles;
                std::vector<std::string> whitelist_channels;
                std::vector<int64_t> whitelist_permissions;

                std::vector<std::string> blacklist_roles;
                std::vector<std::string> blacklist_channels;
                std::vector<int64_t> blacklist_permissions;

                for (const auto& re : rules) {
                    if (re.restriction_type == "allow") {
                        if (re.entity_type == "role") whitelist_roles.push_back(re.entity_id);
                        else if (re.entity_type == "channel") whitelist_channels.push_back(re.entity_id);
                        else if (re.entity_type == "permission") {
                            whitelist_permissions.push_back(bronx::cogs::parse_permission_name(re.entity_id));
                        }
                    } else if (re.restriction_type == "restrict") {
                        if (re.entity_type == "role") blacklist_roles.push_back(re.entity_id);
                        else if (re.entity_type == "channel") blacklist_channels.push_back(re.entity_id);
                        else if (re.entity_type == "permission") {
                            blacklist_permissions.push_back(bronx::cogs::parse_permission_name(re.entity_id));
                        }
                    }
                }

                bool has_channel_whitelist = !whitelist_channels.empty();
                bool has_member_whitelist = !whitelist_roles.empty() || !whitelist_permissions.empty();

                if (has_channel_whitelist) {
                    bool channel_ok = (std::find(whitelist_channels.begin(), whitelist_channels.end(), msg.channel_id) != whitelist_channels.end());
                    if (!channel_ok) {
                        bot.send_message(msg.channel_id, "⚠️ This command/module is allowed only in specific channels.");
                        return false;
                    }
                }

                if (has_member_whitelist) {
                    bool member_ok = false;
                    if (mem_opt) {
                        for (const auto& r : mem_opt->roles) {
                            if (std::find(whitelist_roles.begin(), whitelist_roles.end(), r) != whitelist_roles.end()) {
                                member_ok = true;
                                break;
                            }
                        }
                        auto server_opt = bot.get_server(msg.server_id);
                        if (server_opt && !member_ok) {
                            for (auto mask : whitelist_permissions) {
                                if (bot.has_permission(*server_opt, *mem_opt, mask)) {
                                    member_ok = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (!member_ok) {
                        bot.send_message(msg.channel_id, "⚠️ This command/module is whitelisted and not accessible by your current roles or permissions.");
                        return false;
                    }
                }

                // Check blacklist
                bool matches_blacklist = false;
                if (std::find(blacklist_channels.begin(), blacklist_channels.end(), msg.channel_id) != blacklist_channels.end()) {
                    matches_blacklist = true;
                }
                if (mem_opt) {
                    for (const auto& r : mem_opt->roles) {
                        if (std::find(blacklist_roles.begin(), blacklist_roles.end(), r) != blacklist_roles.end()) {
                            matches_blacklist = true;
                            break;
                        }
                    }
                    // Check permissions
                    auto server_opt = bot.get_server(msg.server_id);
                    if (server_opt) {
                        for (auto mask : blacklist_permissions) {
                            if (bot.has_permission(*server_opt, *mem_opt, mask)) {
                                matches_blacklist = true;
                                break;
                            }
                        }
                    }
                }

                if (matches_blacklist) {
                    bot.send_message(msg.channel_id, "⚠️ This command/module has been blacklisted for your current roles, permissions, or channel.");
                    return false;
                }
            }
        }

        // 3. Config commands protection (trust, command)
        if (cmd_name == "trust" || cmd_name == "command" || cmd_name == "cmd") {
            if (mem_opt) {
                if (!bronx::cogs::is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, *mem_opt)) {
                    bot.send_message(msg.channel_id, "permission error: you need to be trusted or have Manage Server permission to configure command permissions.");
                    return false;
                }
            }
        }

        return true;
    });

    std::cout << "[bronx] starting cluster start() loop. press ctrl+c to terminate.\n";
    bot.start();

    return 0;
}