#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <iostream>
#include <optional>
#include <mysql.h>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace bronx {

class Database {
public:
    struct RestrictionEntry {
        std::string target_type; // "command" or "module"
        std::string target_name;
        std::string restriction_type; // "allow" or "restrict"
        std::string entity_type; // "role" or "channel"
        std::string entity_id;
    };
    struct RRRule {
        std::string emoji_id;
        std::string role_id;
    };

    struct RRMessage {
        std::string message_id;
        std::string channel_id;
        std::string server_id;
        std::string modal_name; // empty if scanned from existing msg
        std::string embed_json;
        std::vector<RRRule> rules;
    };

    struct ModalInfo {
        std::string name;
        int rule_count = 0;
        int message_count = 0;
    };

    struct Reminder {
        int id;
        std::string user_id;
        std::string channel_id;
        std::string message;
        uint64_t timestamp_set;
        uint64_t timestamp_due;
    };

    struct WelcomeLeaveConfig {
        std::string server_id;
        std::string welcome_channel_id; // empty/NULL if disabled
        std::string welcome_type = "text"; // "text" or "embed"
        std::string welcome_content; // markdown message text
        std::string welcome_embed_json; // JSON object storing builder fields
        std::string leave_channel_id; // empty/NULL if disabled
        std::string leave_type = "text"; // "text" or "embed"
        std::string leave_content;
        std::string leave_embed_json;
        std::string autorole_role_id; // empty/NULL if disabled
    };

    struct LoggingEmbed {
        std::string server_id;
        std::string event_type;
        std::string message_type = "embed"; // "text" or "embed"
        std::string content;
        std::string embed_json;
    };

    struct ActionMessageConfig {
        std::string server_id;
        std::string action_type; // "ban", "kick", "timeout", "purge"
        std::string message_type = "text"; // "text" or "embed"
        std::string content;
        std::string embed_json;
    };

    struct AntiRaidConfig {
        std::string server_id;
        int joins_threshold_count = 5;
        int joins_threshold_seconds = 60;
        std::string joins_action = "none";
        int spam_threshold_count = 5;
        int spam_threshold_seconds = 2;
        std::string spam_action = "none";
        std::string default_profile_action = "none";
    };

private:
    MYSQL* mysql_ = nullptr;
    std::unordered_map<std::string, std::vector<std::string>> cache_;
    std::shared_mutex cache_mutex_;
    std::mutex db_mutex_;

    // Message ID -> RRMessage
    std::unordered_map<std::string, RRMessage> rr_cache_;
    std::shared_mutex rr_cache_mutex_;

    // Server ID -> WelcomeLeaveConfig
    std::unordered_map<std::string, WelcomeLeaveConfig> wl_cache_;
    std::shared_mutex wl_cache_mutex_;

    // Server ID -> AntiRaidConfig
    std::unordered_map<std::string, AntiRaidConfig> ar_cache_;
    std::shared_mutex ar_cache_mutex_;

    // Server ID -> {Event Type -> Channel ID}
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> log_cache_;
    std::shared_mutex log_cache_mutex_;

    // Server ID -> {Event Type -> LoggingEmbed}
    std::unordered_map<std::string, std::unordered_map<std::string, LoggingEmbed>> log_embed_cache_;
    std::shared_mutex log_embed_cache_mutex_;

    // Server ID -> {Action Type -> ActionMessageConfig}
    std::unordered_map<std::string, std::unordered_map<std::string, ActionMessageConfig>> action_msg_cache_;
    std::shared_mutex action_msg_cache_mutex_;

    // Channel ID -> {Webhook ID, Webhook Token}
    struct WebhookCacheEntry {
        std::string id;
        std::string token;
    };
    std::unordered_map<std::string, WebhookCacheEntry> webhook_cache_;
    std::shared_mutex webhook_cache_mutex_;

    // Server ID -> Set of "Type:ID"
    std::unordered_map<std::string, std::unordered_set<std::string>> trusted_cache_;
    std::shared_mutex trusted_cache_mutex_;

    // Server ID -> List of restrictions
    std::unordered_map<std::string, std::vector<RestrictionEntry>> restrictions_cache_;
    std::shared_mutex restrictions_cache_mutex_;

    // Server ID -> Set of "Type:Name"
    std::unordered_map<std::string, std::unordered_set<std::string>> disabled_cache_;
    std::shared_mutex disabled_cache_mutex_;

    // Server ID -> default prefix enabled (true = default prefix active)
    std::unordered_map<std::string, bool> pfx_default_cache_;
    std::shared_mutex pfx_default_cache_mutex_;

    std::string escape_string(const std::string& input) {
        if (!mysql_) return "";
        std::vector<char> buffer(input.size() * 2 + 1);
        mysql_real_escape_string(mysql_, buffer.data(), input.c_str(), input.size());
        return std::string(buffer.data());
    }

public:
    Database() = default;
    
    ~Database() {
        if (mysql_) {
            mysql_close(mysql_);
        }
    }

    bool init(const std::string& host, const std::string& user, const std::string& password,
              const std::string& dbname, unsigned int port) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);

        mysql_ = mysql_init(nullptr);
        if (!mysql_) {
            std::cerr << "[bronx db] mysql_init failed\n";
            return false;
        }

        // Connect to server (without DB name first, to create it if it doesn't exist)
        if (!mysql_real_connect(mysql_, host.c_str(), user.c_str(), password.c_str(), nullptr, port, nullptr, 0)) {
            std::cerr << "[bronx db] connection failed: " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Create Database if it doesn't exist
        std::string create_db = "CREATE DATABASE IF NOT EXISTS " + dbname;
        if (mysql_query(mysql_, create_db.c_str())) {
            std::cerr << "[bronx db] failed to create database '" << dbname << "': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Select the Database
        if (mysql_select_db(mysql_, dbname.c_str())) {
            std::cerr << "[bronx db] failed to select database '" << dbname << "': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Create Table prefixes: JSON column is native in MariaDB and MySQL
        std::string create_table = 
            "CREATE TABLE IF NOT EXISTS prefixes ("
            "  target_type VARCHAR(10) NOT NULL,"
            "  target_id VARCHAR(30) NOT NULL,"
            "  prefixes JSON NOT NULL,"
            "  PRIMARY KEY (target_type, target_id)"
            ")";
        if (mysql_query(mysql_, create_table.c_str())) {
            std::cerr << "[bronx db] failed to create table 'prefixes': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Migration: add default_enabled column if it doesn't exist yet
        mysql_query(mysql_,
            "ALTER TABLE prefixes ADD COLUMN IF NOT EXISTS "
            "default_enabled TINYINT(1) NOT NULL DEFAULT 1");

        // Create Table rr_messages
        std::string create_rr_msgs = 
            "CREATE TABLE IF NOT EXISTS rr_messages ("
            "  message_id VARCHAR(30) NOT NULL,"
            "  channel_id VARCHAR(30) NOT NULL,"
            "  server_id VARCHAR(30) NOT NULL,"
            "  modal_name VARCHAR(50) DEFAULT NULL,"
            "  PRIMARY KEY (message_id)"
            ")";
        if (mysql_query(mysql_, create_rr_msgs.c_str())) {
            std::cerr << "[bronx db] failed to create table 'rr_messages': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Migration: Ensure embed_json column exists
        std::string alter_rr_msg = "ALTER TABLE rr_messages ADD COLUMN IF NOT EXISTS embed_json TEXT DEFAULT NULL";
        if (mysql_query(mysql_, alter_rr_msg.c_str())) {
            std::string alter_rr_msg_fallback = "ALTER TABLE rr_messages ADD COLUMN embed_json TEXT DEFAULT NULL";
            mysql_query(mysql_, alter_rr_msg_fallback.c_str());
        }

        // Create Table rr_rules
        std::string create_rr_rules = 
            "CREATE TABLE IF NOT EXISTS rr_rules ("
            "  message_id VARCHAR(30) NOT NULL,"
            "  emoji_id VARCHAR(100) NOT NULL,"
            "  role_id VARCHAR(30) NOT NULL,"
            "  PRIMARY KEY (message_id, emoji_id, role_id),"
            "  FOREIGN KEY (message_id) REFERENCES rr_messages(message_id) ON DELETE CASCADE"
            ")";
        if (mysql_query(mysql_, create_rr_rules.c_str())) {
            std::cerr << "[bronx db] failed to create table 'rr_rules': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Migration: Update primary key to allow multiple roles per emoji
        std::string alter_rr_rules = "ALTER TABLE rr_rules DROP PRIMARY KEY, ADD PRIMARY KEY(message_id, emoji_id, role_id)";
        mysql_query(mysql_, alter_rr_rules.c_str()); // Ignore error if it fails (e.g. if it already has the correct PK)

        // Create Table guild_command_usage
        std::string create_cmd_usage = 
            "CREATE TABLE IF NOT EXISTS guild_command_usage ("
            "  guild_id VARCHAR(30) NOT NULL,"
            "  command_name VARCHAR(64) NOT NULL,"
            "  channel_id VARCHAR(30) NOT NULL,"
            "  usage_date DATE NOT NULL,"
            "  use_count INT UNSIGNED NOT NULL DEFAULT 1,"
            "  PRIMARY KEY (guild_id, command_name, channel_id, usage_date)"
            ")";
        if (mysql_query(mysql_, create_cmd_usage.c_str())) {
            std::cerr << "[bronx db] failed to create table 'guild_command_usage': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Create Table guild_user_activity_daily
        std::string create_user_activity = 
            "CREATE TABLE IF NOT EXISTS guild_user_activity_daily ("
            "  guild_id VARCHAR(30) NOT NULL,"
            "  user_id VARCHAR(30) NOT NULL,"
            "  stat_date DATE NOT NULL,"
            "  messages INT UNSIGNED NOT NULL DEFAULT 0,"
            "  edits INT UNSIGNED NOT NULL DEFAULT 0,"
            "  deletes INT UNSIGNED NOT NULL DEFAULT 0,"
            "  commands_used INT UNSIGNED NOT NULL DEFAULT 0,"
            "  is_bot BOOLEAN NOT NULL DEFAULT FALSE,"
            "  PRIMARY KEY (guild_id, user_id, stat_date)"
            ")";
        if (mysql_query(mysql_, create_user_activity.c_str())) {
            std::cerr << "[bronx db] failed to create table 'guild_user_activity_daily': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Add is_bot column if not exists in case of existing tables
        std::string alter_user_activity = "ALTER TABLE guild_user_activity_daily ADD COLUMN IF NOT EXISTS is_bot BOOLEAN NOT NULL DEFAULT FALSE";
        mysql_query(mysql_, alter_user_activity.c_str());

        // Create Table guild_channel_activity_daily
        std::string create_channel_activity = 
            "CREATE TABLE IF NOT EXISTS guild_channel_activity_daily ("
            "  guild_id VARCHAR(30) NOT NULL,"
            "  channel_id VARCHAR(30) NOT NULL,"
            "  stat_date DATE NOT NULL,"
            "  messages INT UNSIGNED NOT NULL DEFAULT 0,"
            "  PRIMARY KEY (guild_id, channel_id, stat_date)"
            ")";
        if (mysql_query(mysql_, create_channel_activity.c_str())) {
            std::cerr << "[bronx db] failed to create table 'guild_channel_activity_daily': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Create Table users for economy
        std::string create_users = 
            "CREATE TABLE IF NOT EXISTS users ("
            "  user_id VARCHAR(30) NOT NULL PRIMARY KEY,"
            "  wallet BIGINT NOT NULL DEFAULT 0,"
            "  bank BIGINT NOT NULL DEFAULT 0,"
            "  bank_limit BIGINT NOT NULL DEFAULT 10000,"
            "  prestige INT NOT NULL DEFAULT 0,"
            "  last_beg TIMESTAMP NULL DEFAULT NULL,"
            "  last_work TIMESTAMP NULL DEFAULT NULL,"
            "  last_rob TIMESTAMP NULL DEFAULT NULL,"
            "  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP"
            ")";
        if (mysql_query(mysql_, create_users.c_str())) {
            std::cerr << "[bronx db] failed to create table 'users': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Create Table welcome_leave_configs
        std::string create_wl = 
            "CREATE TABLE IF NOT EXISTS welcome_leave_configs ("
            "  server_id VARCHAR(30) NOT NULL PRIMARY KEY,"
            "  welcome_channel_id VARCHAR(30) DEFAULT NULL,"
            "  welcome_type VARCHAR(10) NOT NULL DEFAULT 'text',"
            "  welcome_content TEXT DEFAULT NULL,"
            "  welcome_embed JSON DEFAULT NULL,"
            "  leave_channel_id VARCHAR(30) DEFAULT NULL,"
            "  leave_type VARCHAR(10) NOT NULL DEFAULT 'text',"
            "  leave_content TEXT DEFAULT NULL,"
            "  leave_embed JSON DEFAULT NULL,"
            "  autorole_role_id VARCHAR(30) DEFAULT NULL"
            ")";
        if (mysql_query(mysql_, create_wl.c_str())) {
            std::cerr << "[bronx db] failed to create table 'welcome_leave_configs': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Migration: Ensure autorole_role_id column exists
        std::string alter_wl = "ALTER TABLE welcome_leave_configs ADD COLUMN IF NOT EXISTS autorole_role_id VARCHAR(30) DEFAULT NULL";
        if (mysql_query(mysql_, alter_wl.c_str())) {
            std::string alter_wl_fallback = "ALTER TABLE welcome_leave_configs ADD COLUMN autorole_role_id VARCHAR(30) DEFAULT NULL";
            mysql_query(mysql_, alter_wl_fallback.c_str()); 
        }

        // Create Table logging_configs
        std::string create_log_configs = 
            "CREATE TABLE IF NOT EXISTS logging_configs ("
            "  server_id VARCHAR(30) NOT NULL,"
            "  event_type VARCHAR(30) NOT NULL,"
            "  channel_id VARCHAR(30) NOT NULL,"
            "  PRIMARY KEY (server_id, event_type)"
            ")";
        if (mysql_query(mysql_, create_log_configs.c_str())) {
            std::cerr << "[bronx db] failed to create table 'logging_configs': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Create Table logging_embeds
        std::string create_log_embeds = 
            "CREATE TABLE IF NOT EXISTS logging_embeds ("
            "  server_id VARCHAR(30) NOT NULL,"
            "  event_type VARCHAR(30) NOT NULL,"
            "  message_type VARCHAR(10) NOT NULL DEFAULT 'embed',"
            "  content TEXT DEFAULT NULL,"
            "  embed_json JSON DEFAULT NULL,"
            "  PRIMARY KEY (server_id, event_type)"
            ")";
        if (mysql_query(mysql_, create_log_embeds.c_str())) {
            std::cerr << "[bronx db] failed to create table 'logging_embeds': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Create Table logging_webhooks
        std::string create_log_webhooks = 
            "CREATE TABLE IF NOT EXISTS logging_webhooks ("
            "  channel_id VARCHAR(30) NOT NULL,"
            "  webhook_id VARCHAR(30) NOT NULL,"
            "  webhook_token VARCHAR(100) NOT NULL,"
            "  PRIMARY KEY (channel_id)"
            ")";
        if (mysql_query(mysql_, create_log_webhooks.c_str())) {
            std::cerr << "[bronx db] failed to create table 'logging_webhooks': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Create Table trusted_entities
        std::string create_trusted_entities = 
            "CREATE TABLE IF NOT EXISTS trusted_entities ("
            "  server_id VARCHAR(30) NOT NULL,"
            "  entity_type VARCHAR(10) NOT NULL,"
            "  entity_id VARCHAR(30) NOT NULL,"
            "  PRIMARY KEY (server_id, entity_type, entity_id)"
            ")";
        if (mysql_query(mysql_, create_trusted_entities.c_str())) {
            std::cerr << "[bronx db] failed to create table 'trusted_entities': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Create Table command_restrictions
        std::string create_command_restrictions = 
            "CREATE TABLE IF NOT EXISTS command_restrictions ("
            "  server_id VARCHAR(30) NOT NULL,"
            "  target_type VARCHAR(10) NOT NULL,"
            "  target_name VARCHAR(50) NOT NULL,"
            "  restriction_type VARCHAR(10) NOT NULL,"
            "  entity_type VARCHAR(10) NOT NULL,"
            "  entity_id VARCHAR(30) NOT NULL,"
            "  PRIMARY KEY (server_id, target_type, target_name, restriction_type, entity_type, entity_id)"
            ")";
        if (mysql_query(mysql_, create_command_restrictions.c_str())) {
            std::cerr << "[bronx db] failed to create table 'command_restrictions': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Create Table disabled_commands
        std::string create_disabled_commands = 
            "CREATE TABLE IF NOT EXISTS disabled_commands ("
            "  server_id VARCHAR(30) NOT NULL,"
            "  target_type VARCHAR(10) NOT NULL,"
            "  target_name VARCHAR(50) NOT NULL,"
            "  PRIMARY KEY (server_id, target_type, target_name)"
            ")";
        if (mysql_query(mysql_, create_disabled_commands.c_str())) {
            std::cerr << "[bronx db] failed to create table 'disabled_commands': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Create Table moderation_logs
        std::string create_modlogs =
            "CREATE TABLE IF NOT EXISTS moderation_logs ("
            "  id INT AUTO_INCREMENT PRIMARY KEY,"
            "  server_id VARCHAR(30) NOT NULL,"
            "  action VARCHAR(30) NOT NULL,"
            "  moderator_id VARCHAR(30) NOT NULL,"
            "  target_id VARCHAR(30) NOT NULL,"
            "  reason TEXT DEFAULT NULL,"
            "  extra_details TEXT DEFAULT NULL,"
            "  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "  INDEX idx_target (server_id, target_id),"
            "  INDEX idx_moderator (server_id, moderator_id)"
            ")";
        if (mysql_query(mysql_, create_modlogs.c_str())) {
            std::cerr << "[bronx db] failed to create table 'moderation_logs': " << mysql_error(mysql_) << "\n";
            return false;
        }

        std::string create_reminders =
            "CREATE TABLE IF NOT EXISTS reminders ("
            "  id INT AUTO_INCREMENT PRIMARY KEY,"
            "  user_id VARCHAR(30) NOT NULL,"
            "  channel_id VARCHAR(30) NOT NULL,"
            "  message TEXT NOT NULL,"
            "  timestamp_set BIGINT UNSIGNED NOT NULL,"
            "  timestamp_due BIGINT UNSIGNED NOT NULL"
            ")";
        if (mysql_query(mysql_, create_reminders.c_str())) {
            std::cerr << "[bronx db] failed to create table 'reminders': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Create Table action_message_configs
        std::string create_action_msgs = 
            "CREATE TABLE IF NOT EXISTS action_message_configs ("
            "  server_id VARCHAR(30) NOT NULL,"
            "  action_type VARCHAR(30) NOT NULL,"
            "  message_type VARCHAR(10) NOT NULL DEFAULT 'text',"
            "  content TEXT DEFAULT NULL,"
            "  embed_json JSON DEFAULT NULL,"
            "  PRIMARY KEY (server_id, action_type)"
            ")";
        if (mysql_query(mysql_, create_action_msgs.c_str())) {
            std::cerr << "[bronx db] failed to create table 'action_message_configs': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Create Table antiraid_configs
        std::string create_ar = 
            "CREATE TABLE IF NOT EXISTS antiraid_configs ("
            "  server_id VARCHAR(30) NOT NULL PRIMARY KEY,"
            "  joins_threshold_count INT NOT NULL DEFAULT 5,"
            "  joins_threshold_seconds INT NOT NULL DEFAULT 60,"
            "  joins_action VARCHAR(20) NOT NULL DEFAULT 'none',"
            "  spam_threshold_count INT NOT NULL DEFAULT 5,"
            "  spam_threshold_seconds INT NOT NULL DEFAULT 2,"
            "  spam_action VARCHAR(20) NOT NULL DEFAULT 'none',"
            "  default_profile_action VARCHAR(20) NOT NULL DEFAULT 'none'"
            ")";
        if (mysql_query(mysql_, create_ar.c_str())) {
            std::cerr << "[bronx db] failed to create table 'antiraid_configs': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Load existing prefixes into cache
        std::string select_query = "SELECT target_type, target_id, prefixes, default_enabled FROM prefixes";
        if (mysql_query(mysql_, select_query.c_str())) {
            std::cerr << "[bronx db] failed to load prefixes: " << mysql_error(mysql_) << "\n";
            return false;
        }

        MYSQL_RES* result = mysql_store_result(mysql_);
        if (result) {
            MYSQL_ROW row;
            std::lock_guard<std::shared_mutex> cache_lock(cache_mutex_);
            std::lock_guard<std::shared_mutex> pfx_lock(pfx_default_cache_mutex_);
            while ((row = mysql_fetch_row(result))) {
                std::string t_type = row[0];
                std::string t_id = row[1];
                std::string json_str = row[2];
                bool def_enabled = row[3] ? (std::string(row[3]) != "0") : true;

                std::vector<std::string> loaded_prefixes;
                try {
                    auto parsed = nlohmann::json::parse(json_str);
                    if (parsed.is_string()) {
                        loaded_prefixes.push_back(parsed.get<std::string>());
                    } else if (parsed.is_array()) {
                        for (const auto& item : parsed) {
                            if (item.is_string()) {
                                loaded_prefixes.push_back(item.get<std::string>());
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[bronx db] json parse error for target " << t_type << ":" << t_id << ": " << e.what() << "\n";
                }

                if (!loaded_prefixes.empty()) {
                    std::string cache_key = t_type + ":" + t_id;
                    cache_[cache_key] = loaded_prefixes;
                }
                // Cache the default_enabled flag for server rows
                if (t_type == "server") {
                    pfx_default_cache_[t_id] = def_enabled;
                }
            }
            mysql_free_result(result);
        }

        // Load existing reaction messages
        std::string select_rr_msgs = "SELECT message_id, channel_id, server_id, modal_name, embed_json FROM rr_messages";
        if (mysql_query(mysql_, select_rr_msgs.c_str())) {
            std::cerr << "[bronx db] failed to load rr_messages: " << mysql_error(mysql_) << "\n";
            return false;
        }

        MYSQL_RES* rr_msg_res = mysql_store_result(mysql_);
        if (rr_msg_res) {
            MYSQL_ROW row;
            std::lock_guard<std::shared_mutex> rr_lock(rr_cache_mutex_);
            while ((row = mysql_fetch_row(rr_msg_res))) {
                std::string msg_id = row[0];
                RRMessage msg;
                msg.message_id = msg_id;
                msg.channel_id = row[1];
                msg.server_id = row[2];
                msg.modal_name = row[3] ? row[3] : "";
                msg.embed_json = row[4] ? row[4] : "";
                rr_cache_[msg_id] = msg;
            }
            mysql_free_result(rr_msg_res);
        }

        // Load existing reaction rules
        std::string select_rr_rules = "SELECT message_id, emoji_id, role_id FROM rr_rules";
        if (mysql_query(mysql_, select_rr_rules.c_str())) {
            std::cerr << "[bronx db] failed to load rr_rules: " << mysql_error(mysql_) << "\n";
            return false;
        }

        MYSQL_RES* rr_rule_res = mysql_store_result(mysql_);
        if (rr_rule_res) {
            MYSQL_ROW row;
            std::lock_guard<std::shared_mutex> rr_lock(rr_cache_mutex_);
            while ((row = mysql_fetch_row(rr_rule_res))) {
                std::string msg_id = row[0];
                std::string emoji = row[1];
                std::string role = row[2];
                
                auto it = rr_cache_.find(msg_id);
                if (it != rr_cache_.end()) {
                    it->second.rules.push_back({emoji, role});
                }
            }
            mysql_free_result(rr_rule_res);
        }

        // Load welcome_leave_configs
        std::string select_wl = "SELECT server_id, welcome_channel_id, welcome_type, welcome_content, welcome_embed, "
                                "leave_channel_id, leave_type, leave_content, leave_embed, autorole_role_id FROM welcome_leave_configs";
        if (mysql_query(mysql_, select_wl.c_str())) {
            std::cerr << "[bronx db] failed to load welcome_leave_configs: " << mysql_error(mysql_) << "\n";
            return false;
        }

        MYSQL_RES* wl_res = mysql_store_result(mysql_);
        if (wl_res) {
            MYSQL_ROW row;
            std::lock_guard<std::shared_mutex> wl_lock(wl_cache_mutex_);
            while ((row = mysql_fetch_row(wl_res))) {
                std::string s_id = row[0];
                WelcomeLeaveConfig wl;
                wl.server_id = s_id;
                wl.welcome_channel_id = row[1] ? row[1] : "";
                wl.welcome_type = row[2] ? row[2] : "text";
                wl.welcome_content = row[3] ? row[3] : "";
                wl.welcome_embed_json = row[4] ? row[4] : "";
                wl.leave_channel_id = row[5] ? row[5] : "";
                wl.leave_type = row[6] ? row[6] : "text";
                wl.leave_content = row[7] ? row[7] : "";
                wl.leave_embed_json = row[8] ? row[8] : "";
                wl.autorole_role_id = row[9] ? row[9] : "";
                wl_cache_[s_id] = wl;
            }
            mysql_free_result(wl_res);
        }

        // Load logging_configs
        std::string select_log = "SELECT server_id, event_type, channel_id FROM logging_configs";
        if (mysql_query(mysql_, select_log.c_str()) == 0) {
            MYSQL_RES* log_res = mysql_store_result(mysql_);
            if (log_res) {
                MYSQL_ROW row;
                std::lock_guard<std::shared_mutex> log_lock(log_cache_mutex_);
                while ((row = mysql_fetch_row(log_res))) {
                    std::string s_id = row[0];
                    std::string ev_type = row[1];
                    std::string chan_id = row[2];
                    log_cache_[s_id][ev_type] = chan_id;
                }
                mysql_free_result(log_res);
            }
        }

        // Load logging_embeds
        std::string select_log_embeds = "SELECT server_id, event_type, message_type, content, embed_json FROM logging_embeds";
        if (mysql_query(mysql_, select_log_embeds.c_str()) == 0) {
            MYSQL_RES* embed_res = mysql_store_result(mysql_);
            if (embed_res) {
                MYSQL_ROW row;
                std::lock_guard<std::shared_mutex> embed_lock(log_embed_cache_mutex_);
                while ((row = mysql_fetch_row(embed_res))) {
                    std::string s_id = row[0];
                    LoggingEmbed le;
                    le.server_id = s_id;
                    le.event_type = row[1];
                    le.message_type = row[2] ? row[2] : "embed";
                    le.content = row[3] ? row[3] : "";
                    le.embed_json = row[4] ? row[4] : "";
                    log_embed_cache_[s_id][le.event_type] = le;
                }
                mysql_free_result(embed_res);
            }
        }

        // Load logging_webhooks
        std::string select_wh = "SELECT channel_id, webhook_id, webhook_token FROM logging_webhooks";
        if (mysql_query(mysql_, select_wh.c_str()) == 0) {
            MYSQL_RES* wh_res = mysql_store_result(mysql_);
            if (wh_res) {
                MYSQL_ROW row;
                std::lock_guard<std::shared_mutex> wh_lock(webhook_cache_mutex_);
                while ((row = mysql_fetch_row(wh_res))) {
                    if (row[0] && row[1] && row[2]) {
                        webhook_cache_[row[0]] = {row[1], row[2]};
                    }
                }
                mysql_free_result(wh_res);
            }
        }

        // Load trusted_entities
        std::string select_trusted = "SELECT server_id, entity_type, entity_id FROM trusted_entities";
        if (mysql_query(mysql_, select_trusted.c_str()) == 0) {
            MYSQL_RES* tr_res = mysql_store_result(mysql_);
            if (tr_res) {
                MYSQL_ROW row;
                std::lock_guard<std::shared_mutex> tr_lock(trusted_cache_mutex_);
                while ((row = mysql_fetch_row(tr_res))) {
                    if (row[0] && row[1] && row[2]) {
                        trusted_cache_[row[0]].insert(std::string(row[1]) + ":" + row[2]);
                    }
                }
                mysql_free_result(tr_res);
            }
        }

        // Load command_restrictions
        std::string select_restrictions = "SELECT server_id, target_type, target_name, restriction_type, entity_type, entity_id FROM command_restrictions";
        if (mysql_query(mysql_, select_restrictions.c_str()) == 0) {
            MYSQL_RES* rest_res = mysql_store_result(mysql_);
            if (rest_res) {
                MYSQL_ROW row;
                std::lock_guard<std::shared_mutex> rest_lock(restrictions_cache_mutex_);
                while ((row = mysql_fetch_row(rest_res))) {
                    if (row[0] && row[1] && row[2] && row[3] && row[4] && row[5]) {
                        RestrictionEntry re;
                        re.target_type = row[1];
                        re.target_name = row[2];
                        re.restriction_type = row[3];
                        re.entity_type = row[4];
                        re.entity_id = row[5];
                        restrictions_cache_[row[0]].push_back(re);
                    }
                }
                mysql_free_result(rest_res);
            }
        }

        // Load disabled_commands
        std::string select_disabled = "SELECT server_id, target_type, target_name FROM disabled_commands";
        if (mysql_query(mysql_, select_disabled.c_str()) == 0) {
            MYSQL_RES* dis_res = mysql_store_result(mysql_);
            if (dis_res) {
                MYSQL_ROW row;
                std::lock_guard<std::shared_mutex> dis_lock(disabled_cache_mutex_);
                while ((row = mysql_fetch_row(dis_res))) {
                    if (row[0] && row[1] && row[2]) {
                        disabled_cache_[row[0]].insert(std::string(row[1]) + ":" + row[2]);
                    }
                }
                mysql_free_result(dis_res);
            }
        }

        // Load action_message_configs
        std::string select_action_msgs = "SELECT server_id, action_type, message_type, content, embed_json FROM action_message_configs";
        if (mysql_query(mysql_, select_action_msgs.c_str()) == 0) {
            MYSQL_RES* am_res = mysql_store_result(mysql_);
            if (am_res) {
                MYSQL_ROW row;
                std::lock_guard<std::shared_mutex> am_lock(action_msg_cache_mutex_);
                while ((row = mysql_fetch_row(am_res))) {
                    std::string s_id = row[0];
                    ActionMessageConfig am;
                    am.server_id = s_id;
                    am.action_type = row[1];
                    am.message_type = row[2] ? row[2] : "text";
                    am.content = row[3] ? row[3] : "";
                    am.embed_json = row[4] ? row[4] : "";
                    action_msg_cache_[s_id][am.action_type] = am;
                }
                mysql_free_result(am_res);
            }
        }

        // Load antiraid_configs
        std::string select_ar = "SELECT server_id, joins_threshold_count, joins_threshold_seconds, joins_action, "
                                "spam_threshold_count, spam_threshold_seconds, spam_action, default_profile_action FROM antiraid_configs";
        if (mysql_query(mysql_, select_ar.c_str()) == 0) {
            MYSQL_RES* ar_res = mysql_store_result(mysql_);
            if (ar_res) {
                MYSQL_ROW row;
                std::lock_guard<std::shared_mutex> ar_lock(ar_cache_mutex_);
                while ((row = mysql_fetch_row(ar_res))) {
                    std::string s_id = row[0];
                    AntiRaidConfig ar;
                    ar.server_id = s_id;
                    ar.joins_threshold_count = row[1] ? std::stoi(row[1]) : 5;
                    ar.joins_threshold_seconds = row[2] ? std::stoi(row[2]) : 60;
                    ar.joins_action = row[3] ? row[3] : "none";
                    ar.spam_threshold_count = row[4] ? std::stoi(row[4]) : 5;
                    ar.spam_threshold_seconds = row[5] ? std::stoi(row[5]) : 2;
                    ar.spam_action = row[6] ? row[6] : "none";
                    ar.default_profile_action = row[7] ? row[7] : "none";
                    ar_cache_[s_id] = ar;
                }
                mysql_free_result(ar_res);
            }
        }

        std::cout << "[bronx db] database successfully initialized.\n"
                  << "           loaded " << cache_.size() << " prefix configurations.\n"
                  << "           loaded " << rr_cache_.size() << " reaction role messages.\n"
                  << "           loaded " << wl_cache_.size() << " welcome/leave configurations.\n"
                  << "           loaded " << log_cache_.size() << " logging configurations.\n"
                  << "           loaded " << log_embed_cache_.size() << " logging embed templates.\n"
                  << "           loaded " << webhook_cache_.size() << " logging webhooks.\n"
                  << "           loaded " << trusted_cache_.size() << " trusted servers configurations.\n"
                  << "           loaded " << restrictions_cache_.size() << " server commands restrictions.\n"
                  << "           loaded " << disabled_cache_.size() << " server disabled commands.\n"
                  << "           loaded " << action_msg_cache_.size() << " action message configurations.\n"
                  << "           loaded " << ar_cache_.size() << " antiraid configurations.\n";
        return true;
    }

    std::vector<std::string> get_prefixes(const std::string& target_type, const std::string& target_id) {
        std::shared_lock<std::shared_mutex> lock(cache_mutex_);
        std::string cache_key = target_type + ":" + target_id;
        auto it = cache_.find(cache_key);
        if (it != cache_.end()) {
            return it->second;
        }
        return {};
    }

    bool set_prefixes(const std::string& target_type, const std::string& target_id, const std::vector<std::string>& prefixes) {
        if (prefixes.empty()) {
            return remove_prefixes(target_type, target_id);
        }

        std::lock_guard<std::mutex> db_lock(db_mutex_);

        nlohmann::json j;
        if (prefixes.size() == 1) {
            j = prefixes[0]; // Save as a JSON string
        } else {
            j = prefixes; // Save as a JSON array of strings
        }
        std::string json_str = j.dump();

        std::string esc_type = escape_string(target_type);
        std::string esc_id = escape_string(target_id);
        std::string esc_json = escape_string(json_str);

        // SQL Upsert using ON DUPLICATE KEY UPDATE
        std::string query = 
            "INSERT INTO prefixes (target_type, target_id, prefixes) "
            "VALUES ('" + esc_type + "', '" + esc_id + "', '" + esc_json + "') "
            "ON DUPLICATE KEY UPDATE prefixes = '" + esc_json + "'";

        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to save prefixes: " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Update cache
        {
            std::lock_guard<std::shared_mutex> cache_lock(cache_mutex_);
            std::string cache_key = target_type + ":" + target_id;
            cache_[cache_key] = prefixes;
        }

        return true;
    }

    bool remove_prefixes(const std::string& target_type, const std::string& target_id) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);

        std::string esc_type = escape_string(target_type);
        std::string esc_id = escape_string(target_id);

        std::string query = "DELETE FROM prefixes WHERE target_type = '" + esc_type + "' AND target_id = '" + esc_id + "'";
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to delete prefixes: " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Remove from cache
        {
            std::lock_guard<std::shared_mutex> cache_lock(cache_mutex_);
            std::string cache_key = target_type + ":" + target_id;
            cache_.erase(cache_key);
        }

        return true;
    }

    // Returns true if the default prefix is enabled for this server (default: true).
    bool get_prefix_default_enabled(const std::string& server_id) {
        std::shared_lock<std::shared_mutex> lock(pfx_default_cache_mutex_);
        auto it = pfx_default_cache_.find(server_id);
        if (it != pfx_default_cache_.end()) return it->second;
        return true;  // default: enabled
    }

    // Sets whether the default prefix is active for a server.
    // Persists to the prefixes table (upserts a row with no custom prefixes if needed).
    bool set_prefix_default_enabled(const std::string& server_id, bool enabled) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        if (!mysql_) return false;

        std::string esc_id = escape_string(server_id);
        int val = enabled ? 1 : 0;

        // If a server row already exists, just update the flag.
        // If not, insert one with an empty prefixes JSON so the flag is stored.
        std::string sql =
            "INSERT INTO prefixes (target_type, target_id, prefixes, default_enabled) "
            "VALUES ('server', '" + esc_id + "', '[]', " + std::to_string(val) + ") "
            "ON DUPLICATE KEY UPDATE default_enabled = " + std::to_string(val);
        if (mysql_query(mysql_, sql.c_str())) {
            std::cerr << "[bronx db] set_prefix_default_enabled failed: " << mysql_error(mysql_) << "\n";
            return false;
        }

        std::lock_guard<std::shared_mutex> cache_lock(pfx_default_cache_mutex_);
        pfx_default_cache_[server_id] = enabled;
        return true;
    }

    // --- Reaction Roles Methods ---

    bool add_rr_message(const std::string& message_id, const std::string& channel_id, const std::string& server_id, const std::string& modal_name) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);

        std::string esc_msg = escape_string(message_id);
        std::string esc_ch = escape_string(channel_id);
        std::string esc_srv = escape_string(server_id);
        std::string esc_modal = escape_string(modal_name);

        std::string query = 
            "INSERT INTO rr_messages (message_id, channel_id, server_id, modal_name) "
            "VALUES ('" + esc_msg + "', '" + esc_ch + "', '" + esc_srv + "', " + 
            (modal_name.empty() ? "NULL" : "'" + esc_modal + "'") + ") "
            "ON DUPLICATE KEY UPDATE channel_id = '" + esc_ch + "', server_id = '" + esc_srv + "', modal_name = " + 
            (modal_name.empty() ? "NULL" : "'" + esc_modal + "'");

        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to save rr_message: " << mysql_error(mysql_) << "\n";
            return false;
        }

        {
            std::unique_lock<std::shared_mutex> lock(rr_cache_mutex_);
            RRMessage& msg = rr_cache_[message_id];
            msg.message_id = message_id;
            msg.channel_id = channel_id;
            msg.server_id = server_id;
            msg.modal_name = modal_name;
        }
        return true;
    }

    bool update_rr_embed_json(const std::string& message_id, const std::string& embed_json) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        std::string esc_msg = escape_string(message_id);
        std::string esc_json = escape_string(embed_json);

        std::string query = "UPDATE rr_messages SET embed_json = " + (embed_json.empty() ? "NULL" : "'" + esc_json + "'") + " WHERE message_id = '" + esc_msg + "'";
        if (mysql_query(mysql_, query.c_str()) == 0) {
            std::unique_lock<std::shared_mutex> lock(rr_cache_mutex_);
            if (rr_cache_.find(message_id) != rr_cache_.end()) {
                rr_cache_[message_id].embed_json = embed_json;
            }
            return true;
        }
        return false;
    }

    bool add_rr_rule(const std::string& message_id, const std::string& emoji_id, const std::string& role_id) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);

        std::string esc_msg = escape_string(message_id);
        std::string esc_emoji = escape_string(emoji_id);
        std::string esc_role = escape_string(role_id);

        std::string query = 
            "INSERT INTO rr_rules (message_id, emoji_id, role_id) "
            "VALUES ('" + esc_msg + "', '" + esc_emoji + "', '" + esc_role + "') "
            "ON DUPLICATE KEY UPDATE role_id = role_id";

        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to save rr_rule: " << mysql_error(mysql_) << "\n";
            return false;
        }

        {
            std::lock_guard<std::shared_mutex> cache_lock(rr_cache_mutex_);
            auto it = rr_cache_.find(message_id);
            if (it != rr_cache_.end()) {
                bool found = false;
                for (auto& rule : it->second.rules) {
                    if (rule.emoji_id == emoji_id && rule.role_id == role_id) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    it->second.rules.push_back({emoji_id, role_id});
                }
            }
        }
        return true;
    }

    bool remove_rr_rule(const std::string& message_id, const std::string& emoji_id, const std::string& role_id = "") {
        std::lock_guard<std::mutex> db_lock(db_mutex_);

        std::string esc_msg = escape_string(message_id);
        std::string esc_emoji = escape_string(emoji_id);

        std::string query = "DELETE FROM rr_rules WHERE message_id = '" + esc_msg + "' AND emoji_id = '" + esc_emoji + "'";
        if (!role_id.empty()) {
            query += " AND role_id = '" + escape_string(role_id) + "'";
        }

        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to delete rr_rule: " << mysql_error(mysql_) << "\n";
            return false;
        }

        {
            std::lock_guard<std::shared_mutex> cache_lock(rr_cache_mutex_);
            auto it = rr_cache_.find(message_id);
            if (it != rr_cache_.end()) {
                auto& rules = it->second.rules;
                rules.erase(std::remove_if(rules.begin(), rules.end(), [&](const RRRule& r) {
                    if (role_id.empty()) {
                        return r.emoji_id == emoji_id;
                    } else {
                        return r.emoji_id == emoji_id && r.role_id == role_id;
                    }
                }), rules.end());
            }
        }
        return true;
    }

    bool delete_rr_message(const std::string& message_id) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);

        std::string esc_msg = escape_string(message_id);

        std::string query = "DELETE FROM rr_messages WHERE message_id = '" + esc_msg + "'";
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to delete rr_message: " << mysql_error(mysql_) << "\n";
            return false;
        }

        {
            std::lock_guard<std::shared_mutex> cache_lock(rr_cache_mutex_);
            rr_cache_.erase(message_id);
        }
        return true;
    }

    std::optional<RRMessage> get_rr_message(const std::string& message_id) {
        std::shared_lock<std::shared_mutex> lock(rr_cache_mutex_);
        auto it = rr_cache_.find(message_id);
        if (it != rr_cache_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    std::string get_message_id_by_modal_name(const std::string& server_id, const std::string& modal_name) {
        std::shared_lock<std::shared_mutex> lock(rr_cache_mutex_);
        for (const auto& [msg_id, msg] : rr_cache_) {
            if (msg.server_id == server_id && msg.modal_name == modal_name) {
                return msg_id;
            }
        }
        return "";
    }

    std::vector<std::string> get_message_ids_by_modal(const std::string& server_id, const std::string& modal_name) {
        std::shared_lock<std::shared_mutex> lock(rr_cache_mutex_);
        std::vector<std::string> ids;
        for (const auto& [msg_id, msg] : rr_cache_) {
            if (msg.server_id == server_id && msg.modal_name == modal_name) {
                ids.push_back(msg_id);
            }
        }
        return ids;
    }

    std::vector<ModalInfo> get_server_modals(const std::string& server_id) {
        std::shared_lock<std::shared_mutex> lock(rr_cache_mutex_);
        std::unordered_map<std::string, std::pair<int, int>> modal_counts;
        for (const auto& [msg_id, msg] : rr_cache_) {
            if (msg.server_id == server_id && !msg.modal_name.empty()) {
                auto& p = modal_counts[msg.modal_name];
                p.second++;
                if (p.first == 0) {
                    p.first = msg.rules.size();
                }
            }
        }
        
        std::vector<ModalInfo> result;
        for (const auto& [name, counts] : modal_counts) {
            result.push_back({name, counts.first, counts.second});
        }
        return result;
    }

    bool increment_user_daily_messages(const std::string& guild_id, const std::string& user_id, bool is_bot, const std::string& type = "message") {
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (!mysql_) return false;
        std::string col = "messages";
        if (type == "edit") col = "edits";
        else if (type == "delete") col = "deletes";
        
        std::string sql = "INSERT INTO guild_user_activity_daily (guild_id, user_id, stat_date, is_bot, " + col + ") VALUES ('"
            + escape_string(guild_id) + "', '"
            + escape_string(user_id) + "', CURDATE(), " + (is_bot ? "TRUE" : "FALSE") + ", 1) "
            "ON DUPLICATE KEY UPDATE " + col + " = " + col + " + 1, is_bot = VALUES(is_bot)";
        return mysql_query(mysql_, sql.c_str()) == 0;
    }

    bool increment_user_daily_commands(const std::string& guild_id, const std::string& user_id, bool is_bot) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (!mysql_) return false;
        std::string sql = "INSERT INTO guild_user_activity_daily (guild_id, user_id, stat_date, is_bot, commands_used) VALUES ('"
            + escape_string(guild_id) + "', '"
            + escape_string(user_id) + "', CURDATE(), " + (is_bot ? "TRUE" : "FALSE") + ", 1) "
            "ON DUPLICATE KEY UPDATE commands_used = commands_used + 1, is_bot = VALUES(is_bot)";
        return mysql_query(mysql_, sql.c_str()) == 0;
    }

    bool increment_channel_daily_messages(const std::string& guild_id, const std::string& channel_id) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (!mysql_) return false;
        std::string sql = "INSERT INTO guild_channel_activity_daily (guild_id, channel_id, stat_date, messages) VALUES ('"
            + escape_string(guild_id) + "', '"
            + escape_string(channel_id) + "', CURDATE(), 1) "
            "ON DUPLICATE KEY UPDATE messages = messages + 1";
        return mysql_query(mysql_, sql.c_str()) == 0;
    }

    // Corrects historical rows for a known-bot user that were stored with is_bot=FALSE.
    // Called once on Ready for every bot user found in the Ready payload.
    bool fix_bot_flags(const std::string& user_id) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (!mysql_ || user_id.empty()) return false;
        std::string sql = "UPDATE guild_user_activity_daily SET is_bot = TRUE "
                          "WHERE user_id = '" + escape_string(user_id) + "' AND is_bot = FALSE";
        if (mysql_query(mysql_, sql.c_str()) != 0) return false;
        auto affected = mysql_affected_rows(mysql_);
        if (affected > 0) {
            std::cout << "[bronx db] fixed " << affected << " row(s): marked user "
                      << user_id << " as bot.\n";
        }
        return true;
    }


    bool increment_command_usage(const std::string& guild_id, const std::string& command_name, const std::string& channel_id) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (!mysql_) return false;
        std::string sql = "INSERT INTO guild_command_usage (guild_id, command_name, channel_id, usage_date, use_count) VALUES ('"
            + escape_string(guild_id) + "', '"
            + escape_string(command_name) + "', '"
            + escape_string(channel_id) + "', CURDATE(), 1) "
            "ON DUPLICATE KEY UPDATE use_count = use_count + 1";
        return mysql_query(mysql_, sql.c_str()) == 0;
    }

    struct TopUserEntry {
        std::string user_id;
        int message_count;
    };

    struct TopServerEntry {
        std::string guild_id;
        int message_count;
    };

    struct TopChannelEntry {
        std::string channel_id;
        int message_count;
    };

    struct TopCmdEntry {
        std::string command_name;
        int use_count;
    };

    struct ServerTotals {
        int total_messages = 0;
        int total_commands = 0;
        int active_users = 0;
    };

    std::vector<TopUserEntry> get_top_users(const std::string& guild_id, int days, int limit = 10) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        std::vector<TopUserEntry> entries;
        if (!mysql_) return entries;

        std::string sql = "SELECT user_id, SUM(messages) FROM guild_user_activity_daily "
                          "WHERE guild_id = '" + escape_string(guild_id) + "' "
                          "AND is_bot = FALSE "
                          "AND stat_date >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY) "
                          "GROUP BY user_id ORDER BY SUM(messages) DESC LIMIT " + std::to_string(limit);
        if (mysql_query(mysql_, sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(mysql_);
            if (res) {
                MYSQL_ROW row;
                while ((row = mysql_fetch_row(res))) {
                    if (row[0] && row[1]) {
                        entries.push_back({row[0], std::stoi(row[1])});
                    }
                }
                mysql_free_result(res);
            }
        }
        return entries;
    }

    std::vector<TopCmdEntry> get_top_commands(const std::string& guild_id, int days, int limit = 10) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        std::vector<TopCmdEntry> entries;
        if (!mysql_) return entries;

        std::string sql = "SELECT command_name, SUM(use_count) FROM guild_command_usage "
                          "WHERE guild_id = '" + escape_string(guild_id) + "' "
                          "AND usage_date >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY) "
                          "GROUP BY command_name ORDER BY SUM(use_count) DESC LIMIT " + std::to_string(limit);
        if (mysql_query(mysql_, sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(mysql_);
            if (res) {
                MYSQL_ROW row;
                while ((row = mysql_fetch_row(res))) {
                    if (row[0] && row[1]) {
                        entries.push_back({row[0], std::stoi(row[1])});
                    }
                }
                mysql_free_result(res);
            }
        }
        return entries;
    }

    std::vector<TopServerEntry> get_top_servers(int days, int limit = 10) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        std::vector<TopServerEntry> entries;
        if (!mysql_) return entries;
        std::string sql = "SELECT guild_id, SUM(messages) FROM guild_user_activity_daily "
                          "WHERE stat_date >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY) "
                          "GROUP BY guild_id ORDER BY SUM(messages) DESC LIMIT " + std::to_string(limit);
        if (mysql_query(mysql_, sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(mysql_);
            if (res) {
                MYSQL_ROW row;
                while ((row = mysql_fetch_row(res))) {
                    if (row[0] && row[1]) {
                        entries.push_back({row[0], std::stoi(row[1])});
                    }
                }
                mysql_free_result(res);
            }
        }
        return entries;
    }

    std::vector<TopUserEntry> get_top_users_global(int days, bool bots, int limit = 10) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        std::vector<TopUserEntry> entries;
        if (!mysql_) return entries;
        std::string sql = "SELECT user_id, SUM(messages) FROM guild_user_activity_daily "
                          "WHERE stat_date >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY) "
                          "AND is_bot = " + (bots ? "TRUE" : "FALSE") + " "
                          "GROUP BY user_id ORDER BY SUM(messages) DESC LIMIT " + std::to_string(limit);
        if (mysql_query(mysql_, sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(mysql_);
            if (res) {
                MYSQL_ROW row;
                while ((row = mysql_fetch_row(res))) {
                    if (row[0] && row[1]) {
                        entries.push_back({row[0], std::stoi(row[1])});
                    }
                }
                mysql_free_result(res);
            }
        }
        return entries;
    }

    std::vector<TopChannelEntry> get_top_channels(const std::string& guild_id, int days, int limit = 10) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        std::vector<TopChannelEntry> entries;
        if (!mysql_) return entries;
        std::string sql = "SELECT channel_id, SUM(messages) FROM guild_channel_activity_daily "
                          "WHERE guild_id = '" + escape_string(guild_id) + "' "
                          "AND stat_date >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY) "
                          "GROUP BY channel_id ORDER BY SUM(messages) DESC LIMIT " + std::to_string(limit);
        if (mysql_query(mysql_, sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(mysql_);
            if (res) {
                MYSQL_ROW row;
                while ((row = mysql_fetch_row(res))) {
                    if (row[0] && row[1]) {
                        entries.push_back({row[0], std::stoi(row[1])});
                    }
                }
                mysql_free_result(res);
            }
        }
        return entries;
    }

    ServerTotals get_server_totals(const std::string& guild_id, int days) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        ServerTotals totals;
        if (!mysql_) return totals;

        std::string msg_sql = "SELECT SUM(messages) FROM guild_user_activity_daily "
                              "WHERE guild_id = '" + escape_string(guild_id) + "' "
                              "AND stat_date >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY)";
        if (mysql_query(mysql_, msg_sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(mysql_);
            if (res) {
                MYSQL_ROW row = mysql_fetch_row(res);
                if (row && row[0]) {
                    totals.total_messages = std::stoi(row[0]);
                }
                mysql_free_result(res);
            }
        }

        std::string cmd_sql = "SELECT SUM(use_count) FROM guild_command_usage "
                              "WHERE guild_id = '" + escape_string(guild_id) + "' "
                              "AND usage_date >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY)";
        if (mysql_query(mysql_, cmd_sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(mysql_);
            if (res) {
                MYSQL_ROW row = mysql_fetch_row(res);
                if (row && row[0]) {
                    totals.total_commands = std::stoi(row[0]);
                }
                mysql_free_result(res);
            }
        }

        std::string users_sql = "SELECT COUNT(DISTINCT user_id) FROM guild_user_activity_daily "
                                "WHERE guild_id = '" + escape_string(guild_id) + "' "
                                "AND stat_date >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY)";
        if (mysql_query(mysql_, users_sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(mysql_);
            if (res) {
                MYSQL_ROW row = mysql_fetch_row(res);
                if (row && row[0]) {
                    totals.active_users = std::stoi(row[0]);
                }
                mysql_free_result(res);
            }
        }

        return totals;
    }

    // ── Chart data queries ────────────────────────────────────────────────────

    // Daily message totals for a server over the last N days.
    // Returns pairs of (date_string "MM-DD", message_count) ordered oldest→newest.
    std::vector<std::pair<std::string, int64_t>> get_daily_message_series(
            const std::string& guild_id, int days) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        std::vector<std::pair<std::string, int64_t>> result;
        if (!mysql_) return result;
        std::string sql =
            "SELECT DATE_FORMAT(stat_date,'%m-%d'), SUM(messages) "
            "FROM guild_user_activity_daily "
            "WHERE guild_id = '" + escape_string(guild_id) + "' "
            "AND stat_date >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY) "
            "GROUP BY stat_date ORDER BY stat_date ASC";
        if (mysql_query(mysql_, sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(mysql_);
            if (res) {
                MYSQL_ROW row;
                while ((row = mysql_fetch_row(res))) {
                    if (row[0] && row[1])
                        result.push_back({row[0], std::stoll(row[1])});
                }
                mysql_free_result(res);
            }
        }
        return result;
    }

    // Hour-of-day × weekday message matrix for a server.
    // Returns a 7×24 matrix [weekday 0=Sun..6=Sat][hour 0..23].
    std::vector<std::vector<double>> get_hourly_heatmap(
            const std::string& guild_id, int days) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        std::vector<std::vector<double>> matrix(7, std::vector<double>(24, 0.0));
        if (!mysql_) return matrix;
        // guild_user_activity_daily only has per-day granularity, so we use
        // moderation_logs created_at and message event times if available.
        // For now, distribute each day's messages evenly weighted by a heuristic.
        // The heatmap becomes accurate once we add hour-level tracking.
        // Query: sum messages grouped by weekday of stat_date as a proxy.
        std::string sql =
            "SELECT DAYOFWEEK(stat_date)-1, SUM(messages) "
            "FROM guild_user_activity_daily "
            "WHERE guild_id = '" + escape_string(guild_id) + "' "
            "AND stat_date >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY) "
            "GROUP BY DAYOFWEEK(stat_date)";
        if (mysql_query(mysql_, sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(mysql_);
            if (res) {
                MYSQL_ROW row;
                while ((row = mysql_fetch_row(res))) {
                    if (!row[0] || !row[1]) continue;
                    int dow = std::stoi(row[0]);  // 0=Sun..6=Sat
                    double total = std::stod(row[1]);
                    if (dow < 0 || dow >= 7) continue;
                    // Distribute across a realistic activity curve (peak 10-22h)
                    const double weights[24] = {
                        0.5, 0.3, 0.2, 0.1, 0.1, 0.2,  // 0-5
                        0.5, 0.8, 1.0, 1.1, 1.2, 1.3,  // 6-11
                        1.3, 1.3, 1.2, 1.2, 1.3, 1.4,  // 12-17
                        1.5, 1.5, 1.4, 1.2, 1.0, 0.7   // 18-23
                    };
                    double wsum = 0; for (auto w : weights) wsum += w;
                    for (int h = 0; h < 24; ++h)
                        matrix[dow][h] = total * (weights[h] / wsum);
                }
                mysql_free_result(res);
            }
        }
        return matrix;
    }

    struct EconomyUser {
        std::string user_id;
        int64_t wallet = 0;
        int64_t bank = 0;
        int64_t bank_limit = 10000;
        int prestige = 0;
        std::string last_beg;
        std::string last_work;
        std::string last_rob;
    };

    bool ensure_economy_user(const std::string& user_id) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (!mysql_) return false;
        std::string sql = "INSERT IGNORE INTO users (user_id) VALUES ('" + escape_string(user_id) + "')";
        return mysql_query(mysql_, sql.c_str()) == 0;
    }

    std::optional<EconomyUser> get_economy_user(const std::string& user_id) {
        ensure_economy_user(user_id);
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (!mysql_) return std::nullopt;

        std::string sql = "SELECT wallet, bank, bank_limit, prestige, "
                          "IFNULL(last_beg, ''), IFNULL(last_work, ''), IFNULL(last_rob, '') "
                          "FROM users WHERE user_id = '" + escape_string(user_id) + "'";
        if (mysql_query(mysql_, sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(mysql_);
            if (res) {
                MYSQL_ROW row = mysql_fetch_row(res);
                if (row) {
                    EconomyUser u;
                    u.user_id = user_id;
                    u.wallet = std::stoll(row[0]);
                    u.bank = std::stoll(row[1]);
                    u.bank_limit = std::stoll(row[2]);
                    u.prestige = std::stoi(row[3]);
                    u.last_beg = row[4];
                    u.last_work = row[5];
                    u.last_rob = row[6];
                    mysql_free_result(res);
                    return u;
                }
                mysql_free_result(res);
            }
        }
        return std::nullopt;
    }

    bool update_balances(const std::string& user_id, int64_t wallet_delta, int64_t bank_delta) {
        ensure_economy_user(user_id);
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (!mysql_) return false;
        std::string sql = "UPDATE users SET wallet = wallet + " + std::to_string(wallet_delta) + 
                          ", bank = bank + " + std::to_string(bank_delta) + 
                          " WHERE user_id = '" + escape_string(user_id) + "'";
        return mysql_query(mysql_, sql.c_str()) == 0;
    }

    bool update_beg_cooldown(const std::string& user_id) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (!mysql_) return false;
        std::string sql = "UPDATE users SET last_beg = CURRENT_TIMESTAMP WHERE user_id = '" + escape_string(user_id) + "'";
        return mysql_query(mysql_, sql.c_str()) == 0;
    }

    bool update_work_cooldown(const std::string& user_id) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (!mysql_) return false;
        std::string sql = "UPDATE users SET last_work = CURRENT_TIMESTAMP WHERE user_id = '" + escape_string(user_id) + "'";
        return mysql_query(mysql_, sql.c_str()) == 0;
    }

    bool update_rob_cooldown(const std::string& user_id) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (!mysql_) return false;
        std::string sql = "UPDATE users SET last_rob = CURRENT_TIMESTAMP WHERE user_id = '" + escape_string(user_id) + "'";
        return mysql_query(mysql_, sql.c_str()) == 0;
    }

    int get_cooldown_remaining(const std::string& user_id, const std::string& field, int cooldown_seconds) {
        ensure_economy_user(user_id);
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (!mysql_) return 0;
        std::string sql = "SELECT GREATEST(0, CAST(" + std::to_string(cooldown_seconds) + " AS SIGNED) - TIMESTAMPDIFF(SECOND, " + field + ", CURRENT_TIMESTAMP)) "
                          "FROM users WHERE user_id = '" + escape_string(user_id) + "'";
        if (mysql_query(mysql_, sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(mysql_);
            if (res) {
                MYSQL_ROW row = mysql_fetch_row(res);
                if (row && row[0]) {
                    int remaining = std::stoi(row[0]);
                    mysql_free_result(res);
                    return remaining;
                }
                mysql_free_result(res);
            }
        }
        return 0;
    }

    struct TopEcoEntry {
        std::string user_id;
        int64_t networth;
    };

    std::vector<TopEcoEntry> get_top_economy_users(int limit = 10) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        std::vector<TopEcoEntry> entries;
        if (!mysql_) return entries;

        std::string sql = "SELECT user_id, (wallet + bank) AS networth FROM users "
                          "ORDER BY networth DESC LIMIT " + std::to_string(limit);
        if (mysql_query(mysql_, sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(mysql_);
            if (res) {
                MYSQL_ROW row;
                while ((row = mysql_fetch_row(res))) {
                    if (row[0] && row[1]) {
                        entries.push_back({row[0], std::stoll(row[1])});
                    }
                }
                mysql_free_result(res);
            }
        }
        return entries;
    }

    std::optional<WelcomeLeaveConfig> get_welcome_leave_config(const std::string& server_id) {
        std::shared_lock<std::shared_mutex> wl_lock(wl_cache_mutex_);
        auto it = wl_cache_.find(server_id);
        if (it != wl_cache_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    bool save_welcome_leave_config(const WelcomeLeaveConfig& wl) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        
        std::string w_channel = wl.welcome_channel_id.empty() ? "NULL" : "'" + escape_string(wl.welcome_channel_id) + "'";
        std::string w_type = "'" + escape_string(wl.welcome_type) + "'";
        std::string w_content = wl.welcome_content.empty() ? "NULL" : "'" + escape_string(wl.welcome_content) + "'";
        std::string w_embed = wl.welcome_embed_json.empty() ? "NULL" : "'" + escape_string(wl.welcome_embed_json) + "'";

        std::string l_channel = wl.leave_channel_id.empty() ? "NULL" : "'" + escape_string(wl.leave_channel_id) + "'";
        std::string l_type = "'" + escape_string(wl.leave_type) + "'";
        std::string l_content = wl.leave_content.empty() ? "NULL" : "'" + escape_string(wl.leave_content) + "'";
        std::string l_embed = wl.leave_embed_json.empty() ? "NULL" : "'" + escape_string(wl.leave_embed_json) + "'";
        std::string autorole = wl.autorole_role_id.empty() ? "NULL" : "'" + escape_string(wl.autorole_role_id) + "'";

        std::string query = 
            "INSERT INTO welcome_leave_configs (server_id, welcome_channel_id, welcome_type, welcome_content, welcome_embed, "
            "leave_channel_id, leave_type, leave_content, leave_embed, autorole_role_id) VALUES ("
            "'" + escape_string(wl.server_id) + "', " + w_channel + ", " + w_type + ", " + w_content + ", " + w_embed + ", " +
            l_channel + ", " + l_type + ", " + l_content + ", " + l_embed + ", " + autorole + ") "
            "ON DUPLICATE KEY UPDATE "
            "welcome_channel_id=" + w_channel + ", "
            "welcome_type=" + w_type + ", "
            "welcome_content=" + w_content + ", "
            "welcome_embed=" + w_embed + ", "
            "leave_channel_id=" + l_channel + ", "
            "leave_type=" + l_type + ", "
            "leave_content=" + l_content + ", "
            "leave_embed=" + l_embed + ", "
            "autorole_role_id=" + autorole;

        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to save welcome/leave config: " << mysql_error(mysql_) << "\n";
            return false;
        }

        std::lock_guard<std::shared_mutex> wl_lock(wl_cache_mutex_);
        wl_cache_[wl.server_id] = wl;
        return true;
    }

    std::string get_logging_channel(const std::string& server_id, const std::string& event_type) {
        std::shared_lock<std::shared_mutex> lock(log_cache_mutex_);
        auto it = log_cache_.find(server_id);
        if (it != log_cache_.end()) {
            auto inner_it = it->second.find(event_type);
            if (inner_it != it->second.end()) {
                return inner_it->second;
            }
        }
        return "";
    }

    std::unordered_map<std::string, std::string> get_server_logging_config(const std::string& server_id) {
        std::shared_lock<std::shared_mutex> lock(log_cache_mutex_);
        auto it = log_cache_.find(server_id);
        if (it != log_cache_.end()) {
            return it->second;
        }
        return {};
    }

    std::optional<AntiRaidConfig> get_antiraid_config(const std::string& server_id) {
        std::shared_lock<std::shared_mutex> lock(ar_cache_mutex_);
        auto it = ar_cache_.find(server_id);
        if (it != ar_cache_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    bool save_antiraid_config(const AntiRaidConfig& ar) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);

        std::string esc_srv = escape_string(ar.server_id);
        std::string esc_joins_act = escape_string(ar.joins_action);
        std::string esc_spam_act = escape_string(ar.spam_action);
        std::string esc_profile_act = escape_string(ar.default_profile_action);

        std::string query = 
            "INSERT INTO antiraid_configs (server_id, joins_threshold_count, joins_threshold_seconds, joins_action, "
            "spam_threshold_count, spam_threshold_seconds, spam_action, default_profile_action) VALUES ("
            "'" + esc_srv + "', " + std::to_string(ar.joins_threshold_count) + ", " + std::to_string(ar.joins_threshold_seconds) + ", "
            "'" + esc_joins_act + "', " + std::to_string(ar.spam_threshold_count) + ", " + std::to_string(ar.spam_threshold_seconds) + ", "
            "'" + esc_spam_act + "', '" + esc_profile_act + "') "
            "ON DUPLICATE KEY UPDATE "
            "joins_threshold_count = VALUES(joins_threshold_count), "
            "joins_threshold_seconds = VALUES(joins_threshold_seconds), "
            "joins_action = VALUES(joins_action), "
            "spam_threshold_count = VALUES(spam_threshold_count), "
            "spam_threshold_seconds = VALUES(spam_threshold_seconds), "
            "spam_action = VALUES(spam_action), "
            "default_profile_action = VALUES(default_profile_action)";

        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to save antiraid config: " << mysql_error(mysql_) << "\n";
            return false;
        }

        std::lock_guard<std::shared_mutex> cache_lock(ar_cache_mutex_);
        ar_cache_[ar.server_id] = ar;
        return true;
    }

    bool save_logging_channel(const std::string& server_id, const std::string& event_type, const std::string& channel_id) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        
        std::string esc_srv = escape_string(server_id);
        std::string esc_ev = escape_string(event_type);
        std::string esc_chan = escape_string(channel_id);

        std::string query = 
            "INSERT INTO logging_configs (server_id, event_type, channel_id) VALUES ("
            "'" + esc_srv + "', '" + esc_ev + "', '" + esc_chan + "') "
            "ON DUPLICATE KEY UPDATE channel_id = '" + esc_chan + "'";

        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to save logging config: " << mysql_error(mysql_) << "\n";
            return false;
        }

        std::lock_guard<std::shared_mutex> cache_lock(log_cache_mutex_);
        log_cache_[server_id][event_type] = channel_id;
        return true;
    }

    bool remove_logging_channel(const std::string& server_id, const std::string& event_type) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);

        std::string esc_srv = escape_string(server_id);
        std::string esc_ev = escape_string(event_type);

        std::string query = "DELETE FROM logging_configs WHERE server_id = '" + esc_srv + "' AND event_type = '" + esc_ev + "'";
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to delete logging config: " << mysql_error(mysql_) << "\n";
            return false;
        }

        std::lock_guard<std::shared_mutex> cache_lock(log_cache_mutex_);
        auto it = log_cache_.find(server_id);
        if (it != log_cache_.end()) {
            it->second.erase(event_type);
            if (it->second.empty()) {
                log_cache_.erase(it);
            }
        }
        return true;
    }

    std::optional<LoggingEmbed> get_logging_embed(const std::string& server_id, const std::string& event_type) {
        std::shared_lock<std::shared_mutex> lock(log_embed_cache_mutex_);
        auto it = log_embed_cache_.find(server_id);
        if (it != log_embed_cache_.end()) {
            auto inner_it = it->second.find(event_type);
            if (inner_it != it->second.end()) {
                return inner_it->second;
            }
        }
        return std::nullopt;
    }

    bool save_logging_embed(const LoggingEmbed& le) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);

        std::string esc_srv = escape_string(le.server_id);
        std::string esc_ev = escape_string(le.event_type);
        std::string esc_type = escape_string(le.message_type);
        std::string esc_content = le.content.empty() ? "NULL" : "'" + escape_string(le.content) + "'";
        std::string esc_embed = le.embed_json.empty() ? "NULL" : "'" + escape_string(le.embed_json) + "'";

        std::string query = 
            "INSERT INTO logging_embeds (server_id, event_type, message_type, content, embed_json) VALUES ("
            "'" + esc_srv + "', '" + esc_ev + "', '" + esc_type + "', " + esc_content + ", " + esc_embed + ") "
            "ON DUPLICATE KEY UPDATE message_type = '" + esc_type + "', content = " + esc_content + ", embed_json = " + esc_embed;

        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to save logging embed: " << mysql_error(mysql_) << "\n";
            return false;
        }

        std::lock_guard<std::shared_mutex> cache_lock(log_embed_cache_mutex_);
        log_embed_cache_[le.server_id][le.event_type] = le;
        return true;
    }

    bool remove_logging_embed(const std::string& server_id, const std::string& event_type) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);

        std::string esc_srv = escape_string(server_id);
        std::string esc_ev = escape_string(event_type);

        std::string query = "DELETE FROM logging_embeds WHERE server_id = '" + esc_srv + "' AND event_type = '" + esc_ev + "'";
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to delete logging embed: " << mysql_error(mysql_) << "\n";
            return false;
        }

        std::lock_guard<std::shared_mutex> cache_lock(log_embed_cache_mutex_);
        auto it = log_embed_cache_.find(server_id);
        if (it != log_embed_cache_.end()) {
            it->second.erase(event_type);
            if (it->second.empty()) {
                log_embed_cache_.erase(it);
            }
        }
        return true;
    }

    std::optional<std::pair<std::string, std::string>> get_logging_webhook(const std::string& channel_id) {
        std::shared_lock<std::shared_mutex> lock(webhook_cache_mutex_);
        auto it = webhook_cache_.find(channel_id);
        if (it != webhook_cache_.end()) {
            return std::make_pair(it->second.id, it->second.token);
        }
        return std::nullopt;
    }

    bool save_logging_webhook(const std::string& channel_id, const std::string& webhook_id, const std::string& webhook_token) {
        {
            std::unique_lock<std::shared_mutex> lock(webhook_cache_mutex_);
            webhook_cache_[channel_id] = {webhook_id, webhook_token};
        }

        std::lock_guard<std::mutex> db_lock(db_mutex_);
        std::string esc_chan = escape_string(channel_id);
        std::string esc_id = escape_string(webhook_id);
        std::string esc_tok = escape_string(webhook_token);

        std::string query = 
            "INSERT INTO logging_webhooks (channel_id, webhook_id, webhook_token) VALUES ("
            "'" + esc_chan + "', '" + esc_id + "', '" + esc_tok + "') "
            "ON DUPLICATE KEY UPDATE webhook_id = '" + esc_id + "', webhook_token = '" + esc_tok + "'";
        
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to save logging webhook: " << mysql_error(mysql_) << "\n";
            return false;
        }
        return true;
    }

    bool remove_logging_webhook(const std::string& channel_id) {
        {
            std::unique_lock<std::shared_mutex> lock(webhook_cache_mutex_);
            webhook_cache_.erase(channel_id);
        }

        std::lock_guard<std::mutex> db_lock(db_mutex_);
        std::string esc_chan = escape_string(channel_id);
        std::string query = "DELETE FROM logging_webhooks WHERE channel_id = '" + esc_chan + "'";
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to remove logging webhook: " << mysql_error(mysql_) << "\n";
            return false;
        }
        return true;
    }

    bool is_trusted(const std::string& server_id, const std::string& user_id, const std::vector<std::string>& role_ids) {
        std::shared_lock<std::shared_mutex> lock(trusted_cache_mutex_);
        auto it = trusted_cache_.find(server_id);
        if (it == trusted_cache_.end()) return false;
        
        if (it->second.count("user:" + user_id) > 0) return true;
        for (const auto& r_id : role_ids) {
            if (it->second.count("role:" + r_id) > 0) return true;
        }
        return false;
    }

    std::vector<std::pair<std::string, std::string>> get_trusted(const std::string& server_id) {
        std::shared_lock<std::shared_mutex> lock(trusted_cache_mutex_);
        std::vector<std::pair<std::string, std::string>> list;
        auto it = trusted_cache_.find(server_id);
        if (it != trusted_cache_.end()) {
            for (const auto& val : it->second) {
                size_t colon = val.find(':');
                if (colon != std::string::npos) {
                    list.push_back({val.substr(0, colon), val.substr(colon + 1)});
                }
            }
        }
        return list;
    }

    bool add_trusted(const std::string& server_id, const std::string& type, const std::string& id) {
        {
            std::unique_lock<std::shared_mutex> lock(trusted_cache_mutex_);
            trusted_cache_[server_id].insert(type + ":" + id);
        }

        std::lock_guard<std::mutex> db_lock(db_mutex_);
        std::string esc_srv = escape_string(server_id);
        std::string esc_type = escape_string(type);
        std::string esc_id = escape_string(id);
        
        std::string query = "INSERT INTO trusted_entities (server_id, entity_type, entity_id) VALUES "
                            "('" + esc_srv + "', '" + esc_type + "', '" + esc_id + "')";
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to add trusted entity: " << mysql_error(mysql_) << "\n";
            return false;
        }
        return true;
    }

    bool remove_trusted(const std::string& server_id, const std::string& type, const std::string& id) {
        {
            std::unique_lock<std::shared_mutex> lock(trusted_cache_mutex_);
            auto it = trusted_cache_.find(server_id);
            if (it != trusted_cache_.end()) {
                it->second.erase(type + ":" + id);
            }
        }

        std::lock_guard<std::mutex> db_lock(db_mutex_);
        std::string esc_srv = escape_string(server_id);
        std::string esc_type = escape_string(type);
        std::string esc_id = escape_string(id);
        
        std::string query = "DELETE FROM trusted_entities WHERE server_id = '" + esc_srv + "' "
                            "AND entity_type = '" + esc_type + "' AND entity_id = '" + esc_id + "'";
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to remove trusted entity: " << mysql_error(mysql_) << "\n";
            return false;
        }
        return true;
    }

    std::vector<RestrictionEntry> get_restrictions(const std::string& server_id, const std::string& target_type, const std::string& target_name) {
        std::shared_lock<std::shared_mutex> lock(restrictions_cache_mutex_);
        std::vector<RestrictionEntry> list;
        auto it = restrictions_cache_.find(server_id);
        if (it != restrictions_cache_.end()) {
            for (const auto& re : it->second) {
                if (re.target_type == target_type && re.target_name == target_name) {
                    list.push_back(re);
                }
            }
        }
        return list;
    }

    bool add_restriction(const std::string& server_id, const std::string& target_type, const std::string& target_name,
                         const std::string& restriction_type, const std::string& entity_type, const std::string& entity_id) {
        RestrictionEntry re = {target_type, target_name, restriction_type, entity_type, entity_id};
        {
            std::unique_lock<std::shared_mutex> lock(restrictions_cache_mutex_);
            restrictions_cache_[server_id].push_back(re);
        }

        std::lock_guard<std::mutex> db_lock(db_mutex_);
        std::string esc_srv = escape_string(server_id);
        std::string esc_tt = escape_string(target_type);
        std::string esc_tn = escape_string(target_name);
        std::string esc_rt = escape_string(restriction_type);
        std::string esc_et = escape_string(entity_type);
        std::string esc_ei = escape_string(entity_id);

        std::string query = "INSERT INTO command_restrictions (server_id, target_type, target_name, restriction_type, entity_type, entity_id) VALUES "
                            "('" + esc_srv + "', '" + esc_tt + "', '" + esc_tn + "', '" + esc_rt + "', '" + esc_et + "', '" + esc_ei + "')";
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to add command restriction: " << mysql_error(mysql_) << "\n";
            return false;
        }
        return true;
    }

    bool remove_restriction(const std::string& server_id, const std::string& target_type, const std::string& target_name,
                            const std::string& restriction_type, const std::string& entity_type, const std::string& entity_id) {
        {
            std::unique_lock<std::shared_mutex> lock(restrictions_cache_mutex_);
            auto it = restrictions_cache_.find(server_id);
            if (it != restrictions_cache_.end()) {
                auto& vec = it->second;
                vec.erase(std::remove_if(vec.begin(), vec.end(), [&](const RestrictionEntry& re) {
                    return re.target_type == target_type && re.target_name == target_name &&
                           re.restriction_type == restriction_type && re.entity_type == entity_type &&
                           re.entity_id == entity_id;
                }), vec.end());
            }
        }

        std::lock_guard<std::mutex> db_lock(db_mutex_);
        std::string esc_srv = escape_string(server_id);
        std::string esc_tt = escape_string(target_type);
        std::string esc_tn = escape_string(target_name);
        std::string esc_rt = escape_string(restriction_type);
        std::string esc_et = escape_string(entity_type);
        std::string esc_ei = escape_string(entity_id);

        std::string query = "DELETE FROM command_restrictions WHERE server_id = '" + esc_srv + "' "
                            "AND target_type = '" + esc_tt + "' AND target_name = '" + esc_tn + "' "
                            "AND restriction_type = '" + esc_rt + "' AND entity_type = '" + esc_et + "' AND entity_id = '" + esc_ei + "'";
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to remove command restriction: " << mysql_error(mysql_) << "\n";
            return false;
        }
        return true;
    }

    bool is_disabled(const std::string& server_id, const std::string& command_name, const std::string& module_name) {
        std::shared_lock<std::shared_mutex> lock(disabled_cache_mutex_);
        auto it = disabled_cache_.find(server_id);
        if (it == disabled_cache_.end()) return false;
        
        if (it->second.count("command:" + command_name) > 0) return true;
        if (it->second.count("module:" + module_name) > 0) return true;
        return false;
    }

    std::vector<std::pair<std::string, std::string>> get_disabled_items(const std::string& server_id) {
        std::shared_lock<std::shared_mutex> lock(disabled_cache_mutex_);
        std::vector<std::pair<std::string, std::string>> list;
        auto it = disabled_cache_.find(server_id);
        if (it != disabled_cache_.end()) {
            for (const auto& val : it->second) {
                size_t colon = val.find(':');
                if (colon != std::string::npos) {
                    list.push_back({val.substr(0, colon), val.substr(colon + 1)});
                }
            }
        }
        return list;
    }

    bool disable_item(const std::string& server_id, const std::string& target_type, const std::string& target_name) {
        {
            std::unique_lock<std::shared_mutex> lock(disabled_cache_mutex_);
            disabled_cache_[server_id].insert(target_type + ":" + target_name);
        }

        std::lock_guard<std::mutex> db_lock(db_mutex_);
        std::string esc_srv = escape_string(server_id);
        std::string esc_type = escape_string(target_type);
        std::string esc_name = escape_string(target_name);

        std::string query = "INSERT INTO disabled_commands (server_id, target_type, target_name) VALUES "
                            "('" + esc_srv + "', '" + esc_type + "', '" + esc_name + "')";
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to disable command/module: " << mysql_error(mysql_) << "\n";
            return false;
        }
        return true;
    }

    bool enable_item(const std::string& server_id, const std::string& target_type, const std::string& target_name) {
        {
            std::unique_lock<std::shared_mutex> lock(disabled_cache_mutex_);
            auto it = disabled_cache_.find(server_id);
            if (it != disabled_cache_.end()) {
                it->second.erase(target_type + ":" + target_name);
            }
        }

        std::lock_guard<std::mutex> db_lock(db_mutex_);
        std::string esc_srv = escape_string(server_id);
        std::string esc_type = escape_string(target_type);
        std::string esc_name = escape_string(target_name);

        std::string query = "DELETE FROM disabled_commands WHERE server_id = '" + esc_srv + "' "
                            "AND target_type = '" + esc_type + "' AND target_name = '" + esc_name + "'";
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to enable command/module: " << mysql_error(mysql_) << "\n";
            return false;
        }
        return true;
    }

    bool add_reminder(const std::string& user_id, const std::string& channel_id, const std::string& message, uint64_t timestamp_set, uint64_t timestamp_due) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        if (!mysql_) return false;

        std::string query = "INSERT INTO reminders (user_id, channel_id, message, timestamp_set, timestamp_due) VALUES ('" + 
            escape_string(user_id) + "', '" + 
            escape_string(channel_id) + "', '" + 
            escape_string(message) + "', " + 
            std::to_string(timestamp_set) + ", " + 
            std::to_string(timestamp_due) + ")";

        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] add_reminder error: " << mysql_error(mysql_) << "\n";
            return false;
        }
        return true;
    }

    std::vector<Reminder> get_due_reminders(uint64_t current_timestamp) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        std::vector<Reminder> due;
        if (!mysql_) return due;

        std::string query = "SELECT id, user_id, channel_id, message, timestamp_set, timestamp_due FROM reminders WHERE timestamp_due <= " + std::to_string(current_timestamp);
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] get_due_reminders error: " << mysql_error(mysql_) << "\n";
            return due;
        }

        MYSQL_RES* result = mysql_store_result(mysql_);
        if (result) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(result))) {
                Reminder r;
                r.id = std::stoi(row[0]);
                r.user_id = row[1];
                r.channel_id = row[2];
                r.message = row[3];
                r.timestamp_set = std::stoull(row[4]);
                r.timestamp_due = std::stoull(row[5]);
                due.push_back(r);
            }
            mysql_free_result(result);
        }
        return due;
    }

    std::vector<Reminder> get_user_reminders(const std::string& user_id) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        std::vector<Reminder> rems;
        if (!mysql_) return rems;

        std::string query = "SELECT id, user_id, channel_id, message, timestamp_set, timestamp_due FROM reminders WHERE user_id = '" + escape_string(user_id) + "' ORDER BY timestamp_due ASC";
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] get_user_reminders error: " << mysql_error(mysql_) << "\n";
            return rems;
        }

        MYSQL_RES* result = mysql_store_result(mysql_);
        if (result) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(result))) {
                Reminder r;
                r.id = std::stoi(row[0]);
                r.user_id = row[1];
                r.channel_id = row[2];
                r.message = row[3];
                r.timestamp_set = std::stoull(row[4]);
                r.timestamp_due = std::stoull(row[5]);
                rems.push_back(r);
            }
            mysql_free_result(result);
        }
        return rems;
    }

    bool delete_reminder(int id) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        if (!mysql_) return false;

        std::string query = "DELETE FROM reminders WHERE id = " + std::to_string(id);
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] delete_reminder error: " << mysql_error(mysql_) << "\n";
            return false;
        }
        return true;
    }

    std::optional<ActionMessageConfig> get_action_message(const std::string& server_id, const std::string& action_type) {
        std::shared_lock<std::shared_mutex> lock(action_msg_cache_mutex_);
        auto s_it = action_msg_cache_.find(server_id);
        if (s_it != action_msg_cache_.end()) {
            auto a_it = s_it->second.find(action_type);
            if (a_it != s_it->second.end()) {
                return a_it->second;
            }
        }
        return std::nullopt;
    }

    bool save_action_message(const ActionMessageConfig& config) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        std::string esc_srv = escape_string(config.server_id);
        std::string esc_act = escape_string(config.action_type);
        std::string esc_type = escape_string(config.message_type);
        std::string esc_content = escape_string(config.content);
        std::string esc_json = escape_string(config.embed_json);

        std::string query = 
            "INSERT INTO action_message_configs (server_id, action_type, message_type, content, embed_json) "
            "VALUES ('" + esc_srv + "', '" + esc_act + "', '" + esc_type + "', " + 
            (config.content.empty() ? "NULL" : "'" + esc_content + "'") + ", " + 
            (config.embed_json.empty() ? "NULL" : "'" + esc_json + "'") + ") "
            "ON DUPLICATE KEY UPDATE message_type = '" + esc_type + "', "
            "content = " + (config.content.empty() ? "NULL" : "'" + esc_content + "'") + ", "
            "embed_json = " + (config.embed_json.empty() ? "NULL" : "'" + esc_json + "'");

        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to save action message config: " << mysql_error(mysql_) << "\n";
            return false;
        }

        {
            std::lock_guard<std::shared_mutex> lock(action_msg_cache_mutex_);
            action_msg_cache_[config.server_id][config.action_type] = config;
        }
        return true;
    }

    bool remove_action_message(const std::string& server_id, const std::string& action_type) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        std::string esc_srv = escape_string(server_id);
        std::string esc_act = escape_string(action_type);

        std::string query = "DELETE FROM action_message_configs WHERE server_id = '" + esc_srv + "' AND action_type = '" + esc_act + "'";
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to remove action message config: " << mysql_error(mysql_) << "\n";
            return false;
        }

        {
            std::lock_guard<std::shared_mutex> lock(action_msg_cache_mutex_);
            auto s_it = action_msg_cache_.find(server_id);
            if (s_it != action_msg_cache_.end()) {
                s_it->second.erase(action_type);
            }
        }
        return true;
    }

    std::vector<ActionMessageConfig> list_action_messages(const std::string& server_id) {
        std::shared_lock<std::shared_mutex> lock(action_msg_cache_mutex_);
        std::vector<ActionMessageConfig> res;
        auto s_it = action_msg_cache_.find(server_id);
        if (s_it != action_msg_cache_.end()) {
            for (const auto& [act, cfg] : s_it->second) {
                res.push_back(cfg);
            }
        }
        return res;
    }

    // ── Moderation logs ───────────────────────────────────────────────────────

    struct ModLogEntry {
        int         id;
        std::string action;
        std::string moderator_id;
        std::string target_id;
        std::string reason;
        std::string extra_details;
        std::string created_at;  // YYYY-MM-DD HH:MM:SS
    };

    bool record_moderation_action(const std::string& server_id, const std::string& action,
                                   const std::string& moderator_id, const std::string& target_id,
                                   const std::string& reason, const std::string& extra_details = "") {
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (!mysql_) return false;
        std::string sql =
            "INSERT INTO moderation_logs (server_id, action, moderator_id, target_id, reason, extra_details) VALUES ('"
            + escape_string(server_id) + "', '"
            + escape_string(action) + "', '"
            + escape_string(moderator_id) + "', '"
            + escape_string(target_id) + "', '"
            + escape_string(reason) + "', '"
            + escape_string(extra_details) + "')";
        return mysql_query(mysql_, sql.c_str()) == 0;
    }

    // Returns the moderation history FOR a user (actions taken against them)
    std::vector<ModLogEntry> get_mod_logs(const std::string& server_id, const std::string& target_id, int limit = 10) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        std::vector<ModLogEntry> entries;
        if (!mysql_) return entries;
        std::string sql =
            "SELECT id, action, moderator_id, target_id, COALESCE(reason,''), COALESCE(extra_details,''), "
            "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i') FROM moderation_logs "
            "WHERE server_id = '" + escape_string(server_id) + "' "
            "AND target_id = '" + escape_string(target_id) + "' "
            "ORDER BY id DESC LIMIT " + std::to_string(limit);
        if (mysql_query(mysql_, sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(mysql_);
            if (res) {
                MYSQL_ROW row;
                while ((row = mysql_fetch_row(res))) {
                    ModLogEntry e;
                    e.id            = row[0] ? std::stoi(row[0]) : 0;
                    e.action        = row[1] ? row[1] : "";
                    e.moderator_id  = row[2] ? row[2] : "";
                    e.target_id     = row[3] ? row[3] : "";
                    e.reason        = row[4] ? row[4] : "";
                    e.extra_details = row[5] ? row[5] : "";
                    e.created_at    = row[6] ? row[6] : "";
                    entries.push_back(e);
                }
                mysql_free_result(res);
            }
        }
        return entries;
    }

    // Returns the moderation actions BY a user (actions they invoked against others)
    std::vector<ModLogEntry> get_mod_actions(const std::string& server_id, const std::string& moderator_id, int limit = 10) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        std::vector<ModLogEntry> entries;
        if (!mysql_) return entries;
        std::string sql =
            "SELECT id, action, moderator_id, target_id, COALESCE(reason,''), COALESCE(extra_details,''), "
            "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i') FROM moderation_logs "
            "WHERE server_id = '" + escape_string(server_id) + "' "
            "AND moderator_id = '" + escape_string(moderator_id) + "' "
            "ORDER BY id DESC LIMIT " + std::to_string(limit);
        if (mysql_query(mysql_, sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(mysql_);
            if (res) {
                MYSQL_ROW row;
                while ((row = mysql_fetch_row(res))) {
                    ModLogEntry e;
                    e.id            = row[0] ? std::stoi(row[0]) : 0;
                    e.action        = row[1] ? row[1] : "";
                    e.moderator_id  = row[2] ? row[2] : "";
                    e.target_id     = row[3] ? row[3] : "";
                    e.reason        = row[4] ? row[4] : "";
                    e.extra_details = row[5] ? row[5] : "";
                    e.created_at    = row[6] ? row[6] : "";
                    entries.push_back(e);
                }
                mysql_free_result(res);
            }
        }
        return entries;
    }
};

} // namespace bronx
