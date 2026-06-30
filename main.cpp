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
        if (bronx::g_db) {
            // 1. Check user prefixes
            std::vector<std::string> user_pfx = bronx::g_db->get_prefixes("user", msg.author.id);
            prefixes.insert(prefixes.end(), user_pfx.begin(), user_pfx.end());
            
            // 2. Check server prefixes
            if (!msg.server_id.empty()) {
                std::vector<std::string> server_pfx = bronx::g_db->get_prefixes("server", msg.server_id);
                prefixes.insert(prefixes.end(), server_pfx.begin(), server_pfx.end());
            }
        }
        
        // 3. Fallback/Default prefix
        prefixes.push_back(bot.config().command_prefix);
        
        // Remove duplicates to be clean and efficient
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

    bot.on_ready([&bot](const stoatpp::events::Ready& e) {
        std::cout << "[bronx event] ready! logged in as: " << e.user.username 
                  << " (id: " << e.user.id << ")\n";
        std::cout << "              serving: " << e.servers.size() << " servers, "
                  << e.channels.size() << " channels.\n";

        std::string status_text = "made with c++! | coming soon | " + std::to_string(e.servers.size()) + " servers";
        bot.update_status(status_text, "Online", [](bool success) {
            if (success) {
                std::cout << "[bronx] successfully set presence status.\n";
            } else {
                std::cout << "[bronx] failed to set presence status.\n";
            }
        });
    });

    bot.on_message([&bot](const stoatpp::events::Message& msg) {
        if (msg.author.bot) return;
        std::cout << "[bronx event] message in channel " << msg.channel_id
                  << " by " << msg.author.username << ": " << msg.content << "\n";

        if (bronx::g_db) {
            // 1. Message Telemetry
            bronx::g_db->increment_user_daily_messages(msg.server_id, msg.author.id, "message");

            // 2. Command Telemetry
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
                        bronx::g_db->increment_user_daily_commands(msg.server_id, msg.author.id);
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

    // autorole: assign default role on join
    bot.on_server_member_join([&bot](const stoatpp::events::ServerMemberJoin& e) {
        const std::string autorole_server = "01KW4BF8E9KY0MV8TA355372H0";
        const std::string autorole_role   = "01KW4QC7RQPKY4DE02EFF8E5NE";
        if (e.server_id != autorole_server) return;
        std::cout << "[bronx autorole] assigning role to " << e.user_id << " in " << e.server_id << "\n";
        bot.add_role_to_member(e.server_id, e.user_id, autorole_role);
    });

    bot.on_error([](const stoatpp::events::Error& e) {
        std::cerr << "[bronx event] gateway error: " << e.error_id << "\n";
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

    std::cout << "[bronx] starting cluster start() loop. press ctrl+c to terminate.\n";
    bot.start();

    return 0;
}