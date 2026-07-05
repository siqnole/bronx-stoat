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
        std::string rule_type = "add"; // "add" or "remove"
    };

    struct RRMessage {
        std::string message_id;
        std::string channel_id;
        std::string server_id;
        std::string modal_name; // empty if scanned from existing msg
        std::string embed_json;
        std::string type = "standard"; // "standard", "panel", "verify"
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
        std::string welcome_dm_type = "none"; // "none", "text", or "embed"
        std::string welcome_dm_content;
        std::string welcome_dm_embed_json;
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


    // Scope filter helper (shared by autoresponder and autoreact)
    struct AutoScopeFilter {
        std::vector<std::string> allow_roles;
        std::vector<std::string> allow_users;
        std::vector<std::string> allow_channels;
        std::vector<std::string> ignore_roles;
        std::vector<std::string> ignore_users;
        std::vector<std::string> ignore_channels;
    };

    struct AutoresponderConfig {
        int id = 0;
        std::string server_id;
        std::string name;
        std::string keyword;
        std::string match_type = "contains"; // "contains" | "exact" | "starts" | "ends"
        std::string message_type = "text";   // "text" | "embed"
        std::string content;
        std::string embed_json;
        AutoScopeFilter filter;
    };

    struct AutoreactConfig {
        int id = 0;
        std::string server_id;
        std::string name;
        std::string keyword;
        std::string match_type = "contains";
        std::string emojis;   // comma-separated cleaned emoji IDs / unicode
        AutoScopeFilter filter;
    };

    struct RecurringMessage {
        int id = 0;
        std::string server_id;
        std::string channel_id;
        std::string name;            // user-defined label
        std::string message_type = "text"; // "text" or "embed"
        std::string content;         // plain-text content / pre-embed text (supports tokens)
        std::string embed_json;      // JSON object for embed fields (supports tokens)
        uint64_t interval_seconds = 300; // >= 300 (5 min)
        uint64_t last_sent = 0;      // unix timestamp
        bool owner_tokens = false;   // if true, owner-only tokens are resolved
    };

    // ── Counting ─────────────────────────────────────────────────────────────
    struct CountingConfig {
        std::string server_id;
        std::string channel_id;        // channel where counting happens
        int64_t current_number = 0;    // next expected number
        std::string last_user_id;      // user who sent the last valid count
        std::string fail_role_id;      // role ID to assign on a failed count
        uint64_t fail_role_duration = 0; // seconds, 0 = permanent
        bool passive = false;          // if true, silently ignore wrong msgs instead of breaking count
        bool warn_double_count = false; // if true, warn on double-count instead of immediately breaking
        int64_t highest_count = 0;     // all-time record
    };

    struct CountingStats {
        std::string server_id;
        std::string user_id;
        int64_t correct_counts = 0;
        int64_t failed_counts  = 0;
    };

    struct TicketConfig {
        std::string server_id;
        std::string name = "default";
        std::string modal_channel_id;
        std::string modal_message_id;
        std::string modal_content;
        std::string modal_embed_json;
        std::string reaction_emoji = "📩";
        bool input_creation_enabled = false;
        std::string naming_scheme = "ticket-{number}";
        std::string category_id;
        std::string view_roles;       // raw comma-separated list of role IDs
        std::string view_users;       // raw comma-separated list of user IDs
        std::string view_permissions; // raw comma-separated list of Revolt permission names
        std::string ticket_welcome_content;
        std::string ticket_welcome_embed_json;
        std::string ticket_close_emoji = "🔒";
        std::string ticket_close_content = "🔒 **Closing ticket...**\nThis channel will be deleted in 5 seconds.";
        std::string member_role_id;    // role to DENY in the ticket channel (the server's @member role)
        int ticket_counter = 0;
    };

    struct Ticket {
        std::string channel_id;
        std::string server_id;
        std::string user_id;
        int ticket_number = 0;
        std::string welcome_message_id;
        std::string status = "open"; // "open" or "closed"
        std::string panel_name = "default";
    };

    struct UserAFK {
        std::string user_id;
        std::string custom_template;
        bool is_afk = false;
        std::string reason;
        int64_t since = 0;
        std::string active_preset = "default";
    };

    struct RobloxLink {
        std::string user_id;
        uint64_t roblox_id = 0;
        std::string roblox_username;
    };

private:
    MYSQL* mysql_ = nullptr;
    std::unordered_map<std::string, UserAFK> user_afk_cache_;
    std::shared_mutex user_afk_cache_mutex_;

    std::unordered_map<std::string, RobloxLink> roblox_links_cache_;
    std::shared_mutex roblox_links_cache_mutex_;

    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> user_afk_presets_cache_;
    std::shared_mutex user_afk_presets_cache_mutex_;

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

    // Server ID -> TicketConfig
    std::unordered_map<std::string, TicketConfig> ticket_config_cache_;
    std::shared_mutex ticket_config_cache_mutex_;

    // Channel ID -> Ticket
    std::unordered_map<std::string, Ticket> ticket_cache_;
    std::shared_mutex ticket_cache_mutex_;

    // Server ID -> CountingConfig
    std::unordered_map<std::string, CountingConfig> counting_cache_;
    std::shared_mutex counting_cache_mutex_;

    // server_id:user_id -> CountingStats
    std::unordered_map<std::string, CountingStats> counting_stats_cache_;
    std::shared_mutex counting_stats_cache_mutex_;

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

    // id -> RecurringMessage
    std::unordered_map<int, RecurringMessage> rm_cache_;
    // server_id -> list of IDs
    std::unordered_map<std::string, std::vector<int>> rm_server_index_;
    // channel_id -> id (for uniqueness)
    std::unordered_map<std::string, int> rm_channel_index_;
    std::shared_mutex rm_cache_mutex_;

    // id -> AutoresponderConfig
    std::unordered_map<int, AutoresponderConfig> arsp_cache_;
    std::unordered_map<std::string, std::vector<int>> arsp_server_index_;
    std::shared_mutex arsp_cache_mutex_;

    // id -> AutoreactConfig
    std::unordered_map<int, AutoreactConfig> arct_cache_;
    std::unordered_map<std::string, std::vector<int>> arct_server_index_;
    std::shared_mutex arct_cache_mutex_;


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

        // Migration: Ensure type column exists on rr_messages
        std::string alter_rr_msg_type = "ALTER TABLE rr_messages ADD COLUMN IF NOT EXISTS type VARCHAR(20) NOT NULL DEFAULT 'standard'";
        if (mysql_query(mysql_, alter_rr_msg_type.c_str())) {
            std::string alter_rr_msg_type_fallback = "ALTER TABLE rr_messages ADD COLUMN type VARCHAR(20) NOT NULL DEFAULT 'standard'";
            mysql_query(mysql_, alter_rr_msg_type_fallback.c_str());
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

        // Migration: Ensure rule_type column exists on rr_rules
        std::string alter_rr_rules_type = "ALTER TABLE rr_rules ADD COLUMN IF NOT EXISTS rule_type VARCHAR(10) NOT NULL DEFAULT 'add'";
        if (mysql_query(mysql_, alter_rr_rules_type.c_str())) {
            std::string alter_rr_rules_type_fallback = "ALTER TABLE rr_rules ADD COLUMN rule_type VARCHAR(10) NOT NULL DEFAULT 'add'";
            mysql_query(mysql_, alter_rr_rules_type_fallback.c_str());
        }

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

        // Migration: Ensure DM columns exist
        mysql_query(mysql_, "ALTER TABLE welcome_leave_configs ADD COLUMN IF NOT EXISTS welcome_dm_type VARCHAR(10) NOT NULL DEFAULT 'none'");
        mysql_query(mysql_, "ALTER TABLE welcome_leave_configs ADD COLUMN IF NOT EXISTS welcome_dm_content TEXT DEFAULT NULL");
        mysql_query(mysql_, "ALTER TABLE welcome_leave_configs ADD COLUMN IF NOT EXISTS welcome_dm_embed JSON DEFAULT NULL");

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
            "  joins_action VARCHAR(100) NOT NULL DEFAULT 'none',"
            "  spam_threshold_count INT NOT NULL DEFAULT 5,"
            "  spam_threshold_seconds INT NOT NULL DEFAULT 2,"
            "  spam_action VARCHAR(100) NOT NULL DEFAULT 'none',"
            "  default_profile_action VARCHAR(100) NOT NULL DEFAULT 'none'"
            ")";
        if (mysql_query(mysql_, create_ar.c_str())) {
            std::cerr << "[bronx db] failed to create table 'antiraid_configs': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Migration: widen action columns to support comma-separated multi-action lists (e.g. "delete,warn")
        mysql_query(mysql_, "ALTER TABLE antiraid_configs MODIFY COLUMN joins_action VARCHAR(100) NOT NULL DEFAULT 'none'");
        mysql_query(mysql_, "ALTER TABLE antiraid_configs MODIFY COLUMN spam_action VARCHAR(100) NOT NULL DEFAULT 'none'");
        mysql_query(mysql_, "ALTER TABLE antiraid_configs MODIFY COLUMN default_profile_action VARCHAR(100) NOT NULL DEFAULT 'none'");

        // Create Table recurring_messages
        std::string create_rm =
            "CREATE TABLE IF NOT EXISTS recurring_messages ("
            "  id INT AUTO_INCREMENT PRIMARY KEY,"
            "  server_id VARCHAR(30) NOT NULL,"
            "  channel_id VARCHAR(30) NOT NULL,"
            "  name VARCHAR(100) NOT NULL,"
            "  message_type VARCHAR(10) NOT NULL DEFAULT 'text',"
            "  content TEXT DEFAULT NULL,"
            "  embed_json JSON DEFAULT NULL,"
            "  interval_seconds INT UNSIGNED NOT NULL DEFAULT 300,"
            "  last_sent BIGINT UNSIGNED NOT NULL DEFAULT 0,"
            "  owner_tokens TINYINT(1) NOT NULL DEFAULT 0,"
            "  INDEX idx_rm_server (server_id),"
            "  UNIQUE KEY uq_rm_channel (channel_id)"
            ")";
            // Autoresponder table
            std::string sql_ar =
                "CREATE TABLE IF NOT EXISTS autoresponders ("
                "  id INT AUTO_INCREMENT PRIMARY KEY,"
                "  server_id VARCHAR(30) NOT NULL,"
                "  name VARCHAR(100) NOT NULL,"
                "  keyword VARCHAR(200) NOT NULL,"
                "  match_type VARCHAR(10) NOT NULL DEFAULT \'contains\',"
                "  message_type VARCHAR(10) NOT NULL DEFAULT \'text\',"
                "  content TEXT DEFAULT NULL,"
                "  embed_json JSON DEFAULT NULL,"
                "  allow_roles JSON DEFAULT NULL,"
                "  allow_users JSON DEFAULT NULL,"
                "  allow_channels JSON DEFAULT NULL,"
                "  ignore_roles JSON DEFAULT NULL,"
                "  ignore_users JSON DEFAULT NULL,"
                "  ignore_channels JSON DEFAULT NULL,"
                "  INDEX idx_arsp_server (server_id)"
                ")";
            mysql_query(mysql_, sql_ar.c_str());

            // Autoreact table
            std::string sql_are =
                "CREATE TABLE IF NOT EXISTS autoreacts ("
                "  id INT AUTO_INCREMENT PRIMARY KEY,"
                "  server_id VARCHAR(30) NOT NULL,"
                "  name VARCHAR(100) NOT NULL,"
                "  keyword VARCHAR(200) NOT NULL,"
                "  match_type VARCHAR(10) NOT NULL DEFAULT \'contains\',"
                "  emojis TEXT NOT NULL,"
                "  allow_roles JSON DEFAULT NULL,"
                "  allow_users JSON DEFAULT NULL,"
                "  allow_channels JSON DEFAULT NULL,"
                "  ignore_roles JSON DEFAULT NULL,"
                "  ignore_users JSON DEFAULT NULL,"
                "  ignore_channels JSON DEFAULT NULL,"
                "  INDEX idx_arct_server (server_id)"
                ")";
            mysql_query(mysql_, sql_are.c_str());

            // Create Table ticket_configs
            std::string create_ticket_configs = 
                "CREATE TABLE IF NOT EXISTS ticket_configs ("
                "  server_id VARCHAR(30) NOT NULL,"
                "  name VARCHAR(50) NOT NULL DEFAULT 'default',"
                "  modal_channel_id VARCHAR(30) DEFAULT NULL,"
                "  modal_message_id VARCHAR(30) DEFAULT NULL,"
                "  modal_content TEXT DEFAULT NULL,"
                "  modal_embed_json TEXT DEFAULT NULL,"
                "  reaction_emoji VARCHAR(100) DEFAULT '📩',"
                "  input_creation_enabled TINYINT(1) DEFAULT 0,"
                "  naming_scheme VARCHAR(100) DEFAULT 'ticket-{number}',"
                "  category_id VARCHAR(30) DEFAULT NULL,"
                "  view_roles TEXT DEFAULT NULL,"
                "  view_users TEXT DEFAULT NULL,"
                "  view_permissions TEXT DEFAULT NULL,"
                "  ticket_welcome_content TEXT DEFAULT NULL,"
                "  ticket_welcome_embed_json TEXT DEFAULT NULL,"
                "  ticket_close_emoji VARCHAR(100) DEFAULT '🔒',"
                "  ticket_close_content TEXT DEFAULT NULL,"
                "  member_role_id VARCHAR(30) DEFAULT NULL,"
                "  ticket_counter INT NOT NULL DEFAULT 0,"
                "  PRIMARY KEY (server_id, name)"
                ")";
            if (mysql_query(mysql_, create_ticket_configs.c_str())) {
                std::cerr << "[bronx db] failed to create table 'ticket_configs': " << mysql_error(mysql_) << "\n";
                return false;
            }
            // Migration: add member_role_id for existing installs
            mysql_query(mysql_, "ALTER TABLE ticket_configs ADD COLUMN IF NOT EXISTS member_role_id VARCHAR(30) DEFAULT NULL");

            // Create Table tickets
            std::string create_tickets = 
                "CREATE TABLE IF NOT EXISTS tickets ("
                "  channel_id VARCHAR(30) NOT NULL PRIMARY KEY,"
                "  server_id VARCHAR(30) NOT NULL,"
                "  user_id VARCHAR(30) NOT NULL,"
                "  ticket_number INT NOT NULL,"
                "  welcome_message_id VARCHAR(30) DEFAULT NULL,"
                "  status VARCHAR(10) NOT NULL DEFAULT 'open',"
                "  panel_name VARCHAR(50) NOT NULL DEFAULT 'default',"
                "  INDEX idx_tickets_server (server_id)"
                ")";
            if (mysql_query(mysql_, create_tickets.c_str())) {
                std::cerr << "[bronx db] failed to create table 'tickets': " << mysql_error(mysql_) << "\n";
                return false;
            }

            // ── counting_configs ─────────────────────────────────────────────
            std::string create_counting =
                "CREATE TABLE IF NOT EXISTS counting_configs ("
                "  server_id VARCHAR(30) NOT NULL PRIMARY KEY,"
                "  channel_id VARCHAR(30) NOT NULL DEFAULT '',"
                "  current_number BIGINT NOT NULL DEFAULT 0,"
                "  last_user_id VARCHAR(30) NOT NULL DEFAULT '',"
                "  fail_role_id VARCHAR(30) NOT NULL DEFAULT '',"
                "  fail_role_duration BIGINT UNSIGNED NOT NULL DEFAULT 0,"
                "  passive TINYINT(1) NOT NULL DEFAULT 0,"
                "  highest_count BIGINT NOT NULL DEFAULT 0,"
                "  warn_double_count TINYINT(1) NOT NULL DEFAULT 0"
                ")";
            if (mysql_query(mysql_, create_counting.c_str())) {
                std::cerr << "[bronx db] failed to create table 'counting_configs': " << mysql_error(mysql_) << "\n";
                return false;
            }
            // Migration: add warn_double_count for existing installs
            mysql_query(mysql_, "ALTER TABLE counting_configs ADD COLUMN IF NOT EXISTS warn_double_count TINYINT(1) NOT NULL DEFAULT 0");

            // ── counting_stats ───────────────────────────────────────────────
            std::string create_counting_stats =
                "CREATE TABLE IF NOT EXISTS counting_stats ("
                "  server_id VARCHAR(30) NOT NULL,"
                "  user_id VARCHAR(30) NOT NULL,"
                "  correct_counts BIGINT NOT NULL DEFAULT 0,"
                "  failed_counts BIGINT NOT NULL DEFAULT 0,"
                "  PRIMARY KEY (server_id, user_id)"
                ")";
            if (mysql_query(mysql_, create_counting_stats.c_str())) {
                std::cerr << "[bronx db] failed to create table 'counting_stats': " << mysql_error(mysql_) << "\n";
                return false;
            }

            // Create Table user_afk
            std::string create_user_afk = 
                "CREATE TABLE IF NOT EXISTS user_afk ("
                "  user_id VARCHAR(30) NOT NULL PRIMARY KEY,"
                "  custom_template TEXT DEFAULT NULL,"
                "  is_afk TINYINT(1) NOT NULL DEFAULT 0,"
                "  reason TEXT DEFAULT NULL,"
                "  since BIGINT NOT NULL DEFAULT 0"
                ")";
            if (mysql_query(mysql_, create_user_afk.c_str())) {
                std::cerr << "[bronx db] failed to create table 'user_afk': " << mysql_error(mysql_) << "\n";
                return false;
            }

            // Migration: add active_preset to user_afk table
            mysql_query(mysql_, "ALTER TABLE user_afk ADD COLUMN IF NOT EXISTS active_preset VARCHAR(50) NOT NULL DEFAULT 'default'");

            // Create Table user_afk_presets
            std::string create_user_afk_presets = 
                "CREATE TABLE IF NOT EXISTS user_afk_presets ("
                "  user_id VARCHAR(30) NOT NULL,"
                "  preset_name VARCHAR(50) NOT NULL,"
                "  template_json TEXT DEFAULT NULL,"
                "  PRIMARY KEY (user_id, preset_name)"
                ")";
            if (mysql_query(mysql_, create_user_afk_presets.c_str())) {
                std::cerr << "[bronx db] failed to create table 'user_afk_presets': " << mysql_error(mysql_) << "\n";
                return false;
            }

            // Create Table roblox_links
            std::string create_roblox_links = 
                "CREATE TABLE IF NOT EXISTS roblox_links ("
                "  user_id VARCHAR(30) NOT NULL PRIMARY KEY,"
                "  roblox_id BIGINT NOT NULL,"
                "  roblox_username VARCHAR(100) NOT NULL,"
                "  linked_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP"
                ")";
            if (mysql_query(mysql_, create_roblox_links.c_str())) {
                std::cerr << "[bronx db] failed to create table 'roblox_links': " << mysql_error(mysql_) << "\n";
                return false;
            }


            // Load counting configs into cache
            {
                std::string sel_counting = "SELECT server_id, channel_id, current_number, last_user_id, fail_role_id, fail_role_duration, passive, highest_count, warn_double_count FROM counting_configs";
                if (mysql_query(mysql_, sel_counting.c_str()) == 0) {
                    MYSQL_RES* res = mysql_store_result(mysql_);
                    if (res) {
                        MYSQL_ROW row;
                        std::lock_guard<std::shared_mutex> lk(counting_cache_mutex_);
                        while ((row = mysql_fetch_row(res))) {
                            CountingConfig cfg;
                            cfg.server_id = row[0] ? row[0] : "";
                            cfg.channel_id = row[1] ? row[1] : "";
                            cfg.current_number = row[2] ? std::stoll(row[2]) : 0;
                            cfg.last_user_id = row[3] ? row[3] : "";
                            cfg.fail_role_id = row[4] ? row[4] : "";
                            cfg.fail_role_duration = row[5] ? std::stoull(row[5]) : 0;
                            cfg.passive = row[6] ? (std::string(row[6]) != "0") : false;
                            cfg.highest_count = row[7] ? std::stoll(row[7]) : 0;
                            cfg.warn_double_count = row[8] ? (std::string(row[8]) != "0") : false;
                            counting_cache_[cfg.server_id] = cfg;
                        }
                        mysql_free_result(res);
                    }
                }
            }
            // Load counting stats into cache
            {
                std::string sel_stats = "SELECT server_id, user_id, correct_counts, failed_counts FROM counting_stats";
                if (mysql_query(mysql_, sel_stats.c_str()) == 0) {
                    MYSQL_RES* res = mysql_store_result(mysql_);
                    if (res) {
                        MYSQL_ROW row;
                        std::lock_guard<std::shared_mutex> lk(counting_stats_cache_mutex_);
                        while ((row = mysql_fetch_row(res))) {
                            CountingStats st;
                            st.server_id = row[0] ? row[0] : "";
                            st.user_id   = row[1] ? row[1] : "";
                            st.correct_counts = row[2] ? std::stoll(row[2]) : 0;
                            st.failed_counts  = row[3] ? std::stoll(row[3]) : 0;
                            std::string key = st.server_id + ":" + st.user_id;
                            counting_stats_cache_[key] = st;
                        }
                        mysql_free_result(res);
                    }
                }
            }

        if (mysql_query(mysql_, create_rm.c_str())) {
            std::cerr << "[bronx db] failed to create table 'recurring_messages': " << mysql_error(mysql_) << "\n";
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
        std::string select_rr_msgs = "SELECT message_id, channel_id, server_id, modal_name, embed_json, type FROM rr_messages";
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
                msg.type = row[5] ? row[5] : "standard";
                rr_cache_[msg_id] = msg;
            }
            mysql_free_result(rr_msg_res);
        }

        // Load existing reaction rules
        std::string select_rr_rules = "SELECT message_id, emoji_id, role_id, rule_type FROM rr_rules";
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
                std::string rule_type = row[3] ? row[3] : "add";
                
                auto it = rr_cache_.find(msg_id);
                if (it != rr_cache_.end()) {
                    it->second.rules.push_back({emoji, role, rule_type});
                }
            }
            mysql_free_result(rr_rule_res);
        }

        // Load welcome_leave_configs
        std::string select_wl = "SELECT server_id, welcome_channel_id, welcome_type, welcome_content, welcome_embed, "
                                "leave_channel_id, leave_type, leave_content, leave_embed, autorole_role_id, "
                                "welcome_dm_type, welcome_dm_content, welcome_dm_embed FROM welcome_leave_configs";
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
                wl.welcome_dm_type = row[10] ? row[10] : "none";
                wl.welcome_dm_content = row[11] ? row[11] : "";
                wl.welcome_dm_embed_json = row[12] ? row[12] : "";
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

        // Load recurring_messages
        std::string select_rm = "SELECT id, server_id, channel_id, name, message_type, "
                                "COALESCE(content,''), COALESCE(embed_json,''), "
                                "interval_seconds, last_sent, owner_tokens FROM recurring_messages";
        if (mysql_query(mysql_, select_rm.c_str()) == 0) {
            MYSQL_RES* rm_res = mysql_store_result(mysql_);
            if (rm_res) {
                MYSQL_ROW row;
                std::lock_guard<std::shared_mutex> rm_lock(rm_cache_mutex_);
                while ((row = mysql_fetch_row(rm_res))) {
                    RecurringMessage rm;
                    rm.id               = row[0] ? std::stoi(row[0]) : 0;
                    rm.server_id        = row[1] ? row[1] : "";
                    rm.channel_id       = row[2] ? row[2] : "";
                    rm.name             = row[3] ? row[3] : "";
                    rm.message_type     = row[4] ? row[4] : "text";
                    rm.content          = row[5] ? row[5] : "";
                    rm.embed_json       = row[6] ? row[6] : "";
                    rm.interval_seconds = row[7] ? std::stoull(row[7]) : 300;
                    rm.last_sent        = row[8] ? std::stoull(row[8]) : 0;
                    rm.owner_tokens     = row[9] ? (std::string(row[9]) != "0") : false;
                    rm_cache_[rm.id] = rm;
                    rm_server_index_[rm.server_id].push_back(rm.id);
                    rm_channel_index_[rm.channel_id] = rm.id;
                }
                mysql_free_result(rm_res);
            }
        }

        // Load ticket_configs
        std::string select_tc = "SELECT server_id, name, modal_channel_id, modal_message_id, modal_content, modal_embed_json, "
                                "reaction_emoji, input_creation_enabled, naming_scheme, category_id, "
                                "view_roles, view_users, view_permissions, ticket_welcome_content, "
                                "ticket_welcome_embed_json, ticket_close_emoji, ticket_close_content, ticket_counter, "
                                "COALESCE(member_role_id,'') AS member_role_id FROM ticket_configs";
        if (mysql_query(mysql_, select_tc.c_str()) == 0) {
            MYSQL_RES* tc_res = mysql_store_result(mysql_);
            if (tc_res) {
                MYSQL_ROW row;
                std::lock_guard<std::shared_mutex> tc_lock(ticket_config_cache_mutex_);
                while ((row = mysql_fetch_row(tc_res))) {
                    std::string s_id = row[0] ? row[0] : "";
                    std::string p_name = row[1] ? row[1] : "default";
                    TicketConfig tc;
                    tc.server_id = s_id;
                    tc.name = p_name;
                    tc.modal_channel_id = row[2] ? row[2] : "";
                    tc.modal_message_id = row[3] ? row[3] : "";
                    tc.modal_content = row[4] ? row[4] : "";
                    tc.modal_embed_json = row[5] ? row[5] : "";
                    tc.reaction_emoji = row[6] ? row[6] : "📩";
                    tc.input_creation_enabled = row[7] ? (std::stoi(row[7]) != 0) : false;
                    tc.naming_scheme = row[8] ? row[8] : "ticket-{number}";
                    tc.category_id = row[9] ? row[9] : "";
                    tc.view_roles = row[10] ? row[10] : "";
                    tc.view_users = row[11] ? row[11] : "";
                    tc.view_permissions = row[12] ? row[12] : "";
                    tc.ticket_welcome_content = row[13] ? row[13] : "";
                    tc.ticket_welcome_embed_json = row[14] ? row[14] : "";
                    tc.ticket_close_emoji = row[15] ? row[15] : "🔒";
                    tc.ticket_close_content = row[16] ? row[16] : "";
                    tc.ticket_counter = row[17] ? std::stoi(row[17]) : 0;
                    tc.member_role_id = row[18] ? row[18] : "";
                    
                    std::string cache_key = s_id + ":" + p_name;
                    ticket_config_cache_[cache_key] = tc;
                }
                mysql_free_result(tc_res);
            }
        }

        // Load tickets
        std::string select_t = "SELECT channel_id, server_id, user_id, ticket_number, welcome_message_id, status, panel_name FROM tickets";
        if (mysql_query(mysql_, select_t.c_str()) == 0) {
            MYSQL_RES* t_res = mysql_store_result(mysql_);
            if (t_res) {
                MYSQL_ROW row;
                std::lock_guard<std::shared_mutex> t_lock(ticket_cache_mutex_);
                while ((row = mysql_fetch_row(t_res))) {
                    std::string ch_id = row[0] ? row[0] : "";
                    Ticket t;
                    t.channel_id = ch_id;
                    t.server_id = row[1] ? row[1] : "";
                    t.user_id = row[2] ? row[2] : "";
                    t.ticket_number = row[3] ? std::stoi(row[3]) : 0;
                    t.welcome_message_id = row[4] ? row[4] : "";
                    t.status = row[5] ? row[5] : "open";
                    t.panel_name = row[6] ? row[6] : "default";
                    ticket_cache_[ch_id] = t;
                }
                mysql_free_result(t_res);
            }
        }

        // Load user_afk
        std::string select_afk = "SELECT user_id, COALESCE(custom_template, ''), is_afk, COALESCE(reason, ''), since, COALESCE(active_preset, 'default') FROM user_afk";
        if (mysql_query(mysql_, select_afk.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(mysql_);
            if (res) {
                MYSQL_ROW row;
                std::lock_guard<std::shared_mutex> lock(user_afk_cache_mutex_);
                while ((row = mysql_fetch_row(res))) {
                    UserAFK afk;
                    afk.user_id = row[0] ? row[0] : "";
                    afk.custom_template = row[1] ? row[1] : "";
                    afk.is_afk = row[2] ? (std::stoi(row[2]) != 0) : false;
                    afk.reason = row[3] ? row[3] : "";
                    afk.since = row[4] ? std::stoll(row[4]) : 0;
                    afk.active_preset = row[5] ? row[5] : "default";
                    user_afk_cache_[afk.user_id] = afk;
                }
                mysql_free_result(res);
            }
        }

        // Load user_afk_presets
        std::string select_presets = "SELECT user_id, preset_name, COALESCE(template_json, '') FROM user_afk_presets";
        if (mysql_query(mysql_, select_presets.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(mysql_);
            if (res) {
                MYSQL_ROW row;
                std::lock_guard<std::shared_mutex> lock(user_afk_presets_cache_mutex_);
                while ((row = mysql_fetch_row(res))) {
                    std::string uid = row[0] ? row[0] : "";
                    std::string preset = row[1] ? row[1] : "";
                    std::string tmpl_raw = row[2] ? row[2] : "";
                    user_afk_presets_cache_[uid][preset] = tmpl_raw;
                }
                mysql_free_result(res);
            }
        }

        // Load roblox links
        std::string select_roblox = "SELECT user_id, roblox_id, roblox_username FROM roblox_links";
        if (mysql_query(mysql_, select_roblox.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(mysql_);
            if (res) {
                MYSQL_ROW row;
                std::lock_guard<std::shared_mutex> lock(roblox_links_cache_mutex_);
                while ((row = mysql_fetch_row(res))) {
                    RobloxLink link;
                    link.user_id = row[0] ? row[0] : "";
                    link.roblox_id = row[1] ? std::stoull(row[1]) : 0;
                    link.roblox_username = row[2] ? row[2] : "";
                    roblox_links_cache_[link.user_id] = link;
                }
                mysql_free_result(res);
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
                  << "           loaded " << ar_cache_.size() << " antiraid configurations.\n"
                  << "           loaded " << rm_cache_.size() << " recurring messages.\n"
                  << "           loaded " << ticket_config_cache_.size() << " ticket configurations.\n"
                  << "           loaded " << ticket_cache_.size() << " active tickets.\n"
                  << "           loaded " << user_afk_cache_.size() << " user afk configurations.\n"
                  << "           loaded " << user_afk_presets_cache_.size() << " user afk preset configurations.\n"
                  << "           loaded " << roblox_links_cache_.size() << " roblox linked accounts.\n";
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

    bool add_rr_message(const std::string& message_id, const std::string& channel_id, const std::string& server_id, const std::string& modal_name, const std::string& type = "standard") {
        std::lock_guard<std::mutex> db_lock(db_mutex_);

        std::string esc_msg = escape_string(message_id);
        std::string esc_ch = escape_string(channel_id);
        std::string esc_srv = escape_string(server_id);
        std::string esc_modal = escape_string(modal_name);
        std::string esc_type = escape_string(type);

        std::string query = 
            "INSERT INTO rr_messages (message_id, channel_id, server_id, modal_name, type) "
            "VALUES ('" + esc_msg + "', '" + esc_ch + "', '" + esc_srv + "', " + 
            (modal_name.empty() ? "NULL" : "'" + esc_modal + "'") + ", '" + esc_type + "') "
            "ON DUPLICATE KEY UPDATE channel_id = '" + esc_ch + "', server_id = '" + esc_srv + "', modal_name = " + 
            (modal_name.empty() ? "NULL" : "'" + esc_modal + "'") + ", type = '" + esc_type + "'";

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
            msg.type = type;
        }
        return true;
    }

    bool update_rr_message_type(const std::string& message_id, const std::string& type) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        std::string esc_msg = escape_string(message_id);
        std::string esc_type = escape_string(type);

        std::string query = "UPDATE rr_messages SET type = '" + esc_type + "' WHERE message_id = '" + esc_msg + "'";
        if (mysql_query(mysql_, query.c_str()) == 0) {
            std::unique_lock<std::shared_mutex> lock(rr_cache_mutex_);
            if (rr_cache_.find(message_id) != rr_cache_.end()) {
                rr_cache_[message_id].type = type;
            }
            return true;
        }
        return false;
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

    bool add_rr_rule(const std::string& message_id, const std::string& emoji_id, const std::string& role_id, const std::string& rule_type = "add") {
        std::lock_guard<std::mutex> db_lock(db_mutex_);

        std::string esc_msg = escape_string(message_id);
        std::string esc_emoji = escape_string(emoji_id);
        std::string esc_role = escape_string(role_id);
        std::string esc_rule_type = escape_string(rule_type);

        std::string query = 
            "INSERT INTO rr_rules (message_id, emoji_id, role_id, rule_type) "
            "VALUES ('" + esc_msg + "', '" + esc_emoji + "', '" + esc_role + "', '" + esc_rule_type + "') "
            "ON DUPLICATE KEY UPDATE role_id = role_id, rule_type = '" + esc_rule_type + "'";

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
                        rule.rule_type = rule_type;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    it->second.rules.push_back({emoji_id, role_id, rule_type});
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

    std::vector<TopUserEntry> get_top_users(const std::string& guild_id, int days, bool include_bots = false, int limit = 10) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        std::vector<TopUserEntry> entries;
        if (!mysql_) return entries;

        std::string sql = "SELECT user_id, SUM(messages) FROM guild_user_activity_daily "
                          "WHERE guild_id = '" + escape_string(guild_id) + "' "
                          + (include_bots ? "" : "AND is_bot = FALSE ") +
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

            // Load autoresponders cache
            {
                auto load_json_arr = [](const char* raw) -> std::vector<std::string> {
                    std::vector<std::string> out;
                    if (!raw) return out;
                    try {
                        auto j = nlohmann::json::parse(raw);
                        if (j.is_array()) {
                            for (const auto& v : j) {
                                if (v.is_string()) out.push_back(v.get<std::string>());
                            }
                        }
                    } catch (...) {}
                    return out;
                };

                if (mysql_query(mysql_,
                    "SELECT id,server_id,name,keyword,match_type,message_type,content,embed_json,"
                    "allow_roles,allow_users,allow_channels,ignore_roles,ignore_users,ignore_channels "
                    "FROM autoresponders") == 0) {
                    MYSQL_RES* res = mysql_store_result(mysql_);
                    if (res) {
                        MYSQL_ROW row;
                        while ((row = mysql_fetch_row(res))) {
                            AutoresponderConfig c;
                            c.id          = row[0] ? std::stoi(row[0]) : 0;
                            c.server_id   = row[1] ? row[1] : "";
                            c.name        = row[2] ? row[2] : "";
                            c.keyword     = row[3] ? row[3] : "";
                            c.match_type  = row[4] ? row[4] : "contains";
                            c.message_type= row[5] ? row[5] : "text";
                            c.content     = row[6] ? row[6] : "";
                            c.embed_json  = row[7] ? row[7] : "";
                            c.filter.allow_roles    = load_json_arr(row[8]);
                            c.filter.allow_users    = load_json_arr(row[9]);
                            c.filter.allow_channels = load_json_arr(row[10]);
                            c.filter.ignore_roles   = load_json_arr(row[11]);
                            c.filter.ignore_users   = load_json_arr(row[12]);
                            c.filter.ignore_channels= load_json_arr(row[13]);
                            arsp_cache_[c.id] = c;
                            arsp_server_index_[c.server_id].push_back(c.id);
                        }
                        mysql_free_result(res);
                    }
                }

                if (mysql_query(mysql_,
                    "SELECT id,server_id,name,keyword,match_type,emojis,"
                    "allow_roles,allow_users,allow_channels,ignore_roles,ignore_users,ignore_channels "
                    "FROM autoreacts") == 0) {
                    MYSQL_RES* res = mysql_store_result(mysql_);
                    if (res) {
                        MYSQL_ROW row;
                        while ((row = mysql_fetch_row(res))) {
                            AutoreactConfig c;
                            c.id          = row[0] ? std::stoi(row[0]) : 0;
                            c.server_id   = row[1] ? row[1] : "";
                            c.name        = row[2] ? row[2] : "";
                            c.keyword     = row[3] ? row[3] : "";
                            c.match_type  = row[4] ? row[4] : "contains";
                            c.emojis      = row[5] ? row[5] : "";
                            c.filter.allow_roles    = load_json_arr(row[6]);
                            c.filter.allow_users    = load_json_arr(row[7]);
                            c.filter.allow_channels = load_json_arr(row[8]);
                            c.filter.ignore_roles   = load_json_arr(row[9]);
                            c.filter.ignore_users   = load_json_arr(row[10]);
                            c.filter.ignore_channels= load_json_arr(row[11]);
                            arct_cache_[c.id] = c;
                            arct_server_index_[c.server_id].push_back(c.id);
                        }
                        mysql_free_result(res);
                    }
                }
            }
                mysql_free_result(res);
            }
        }
        return entries;
    }

    std::vector<TopServerEntry> get_top_servers(int days, bool include_bots = false, int limit = 10) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        std::vector<TopServerEntry> entries;
        if (!mysql_) return entries;
        std::string sql = "SELECT guild_id, SUM(messages) FROM guild_user_activity_daily "
                          "WHERE stat_date >= DATE_SUB(CURDATE(), INTERVAL " + std::to_string(days) + " DAY) "
                          + (include_bots ? "" : "AND is_bot = FALSE ") +
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

    ServerTotals get_server_totals(const std::string& guild_id, int days, bool include_bots = false) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        ServerTotals totals;
        if (!mysql_) return totals;

        std::string msg_sql = "SELECT SUM(messages) FROM guild_user_activity_daily "
                              "WHERE guild_id = '" + escape_string(guild_id) + "' "
                              + (include_bots ? "" : "AND is_bot = FALSE ") +
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
                                + (include_bots ? "" : "AND is_bot = FALSE ") +
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
            const std::string& guild_id, int days, bool include_bots = false) {
        std::lock_guard<std::mutex> lock(db_mutex_);
        std::vector<std::pair<std::string, int64_t>> result;
        if (!mysql_) return result;
        std::string sql =
            "SELECT DATE_FORMAT(stat_date,'%m-%d'), SUM(messages) "
            "FROM guild_user_activity_daily "
            "WHERE guild_id = '" + escape_string(guild_id) + "' "
            + (include_bots ? "" : "AND is_bot = FALSE ") +
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
            const std::string& guild_id, int days, bool include_bots = false) {
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
            + (include_bots ? "" : "AND is_bot = FALSE ") +
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

        std::string w_dm_type = "'" + escape_string(wl.welcome_dm_type) + "'";
        std::string w_dm_content = wl.welcome_dm_content.empty() ? "NULL" : "'" + escape_string(wl.welcome_dm_content) + "'";
        std::string w_dm_embed = wl.welcome_dm_embed_json.empty() ? "NULL" : "'" + escape_string(wl.welcome_dm_embed_json) + "'";

        std::string query = 
            "INSERT INTO welcome_leave_configs (server_id, welcome_channel_id, welcome_type, welcome_content, welcome_embed, "
            "leave_channel_id, leave_type, leave_content, leave_embed, autorole_role_id, "
            "welcome_dm_type, welcome_dm_content, welcome_dm_embed) VALUES ("
            "'" + escape_string(wl.server_id) + "', " + w_channel + ", " + w_type + ", " + w_content + ", " + w_embed + ", " +
            l_channel + ", " + l_type + ", " + l_content + ", " + l_embed + ", " + autorole + ", " +
            w_dm_type + ", " + w_dm_content + ", " + w_dm_embed + ") "
            "ON DUPLICATE KEY UPDATE "
            "welcome_channel_id=" + w_channel + ", "
            "welcome_type=" + w_type + ", "
            "welcome_content=" + w_content + ", "
            "welcome_embed=" + w_embed + ", "
            "leave_channel_id=" + l_channel + ", "
            "leave_type=" + l_type + ", "
            "leave_content=" + l_content + ", "
            "leave_embed=" + l_embed + ", "
            "autorole_role_id=" + autorole + ", "
            "welcome_dm_type=" + w_dm_type + ", "
            "welcome_dm_content=" + w_dm_content + ", "
            "welcome_dm_embed=" + w_dm_embed;

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

    // --- Recurring Messages CRUD ---

    // Returns true if the channel already has a recurring message scheduled.
    bool recurring_channel_occupied(const std::string& channel_id) {
        std::shared_lock<std::shared_mutex> lock(rm_cache_mutex_);
        return rm_channel_index_.count(channel_id) > 0;
    }

    // Returns number of recurring messages for a server (cached).
    int count_recurring_for_server(const std::string& server_id) {
        std::shared_lock<std::shared_mutex> lock(rm_cache_mutex_);
        auto it = rm_server_index_.find(server_id);
        if (it != rm_server_index_.end()) return static_cast<int>(it->second.size());
        return 0;
    }

    // Returns all recurring messages for a server (from cache).
    std::vector<RecurringMessage> get_recurring_messages_for_server(const std::string& server_id) {
        std::shared_lock<std::shared_mutex> lock(rm_cache_mutex_);
        std::vector<RecurringMessage> out;
        auto it = rm_server_index_.find(server_id);
        if (it != rm_server_index_.end()) {
            for (int id : it->second) {
                auto it2 = rm_cache_.find(id);
                if (it2 != rm_cache_.end()) out.push_back(it2->second);
            }
        }
        return out;
    }

    // Returns the recurring message for a channel if one exists.
    std::optional<RecurringMessage> get_recurring_by_channel(const std::string& channel_id) {
        std::shared_lock<std::shared_mutex> lock(rm_cache_mutex_);
        auto it = rm_channel_index_.find(channel_id);
        if (it != rm_channel_index_.end()) {
            auto it2 = rm_cache_.find(it->second);
            if (it2 != rm_cache_.end()) return it2->second;
        }
        return std::nullopt;
    }

    // Returns a recurring message by id, validating server ownership.
    std::optional<RecurringMessage> get_recurring_by_id(int id, const std::string& server_id) {
        std::shared_lock<std::shared_mutex> lock(rm_cache_mutex_);
        auto it = rm_cache_.find(id);
        if (it != rm_cache_.end() && it->second.server_id == server_id) return it->second;
        return std::nullopt;
    }

    // Finds a recurring message by name (case-insensitive) within a server.
    std::optional<RecurringMessage> get_recurring_by_name(const std::string& server_id, const std::string& name) {
        std::shared_lock<std::shared_mutex> lock(rm_cache_mutex_);
        std::string search = name;
        std::transform(search.begin(), search.end(), search.begin(), ::tolower);
        auto it = rm_server_index_.find(server_id);
        if (it != rm_server_index_.end()) {
            for (int id : it->second) {
                auto it2 = rm_cache_.find(id);
                if (it2 != rm_cache_.end()) {
                    std::string cname = it2->second.name;
                    std::transform(cname.begin(), cname.end(), cname.begin(), ::tolower);
                    if (cname == search) return it2->second;
                }
            }
        }
        return std::nullopt;
    }

    // Returns all recurring messages whose next fire time (last_sent + interval) <= now.
    std::vector<RecurringMessage> get_due_recurring_messages(uint64_t now) {
        std::shared_lock<std::shared_mutex> lock(rm_cache_mutex_);
        std::vector<RecurringMessage> out;
        for (const auto& [id, rm] : rm_cache_) {
            if (rm.last_sent + rm.interval_seconds <= now) {
                out.push_back(rm);
            }
        }
        return out;
    }

    // Inserts a new recurring message; updates rm.id with the auto-increment value.
    bool add_recurring_message(RecurringMessage& rm) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        if (!mysql_) return false;

        std::string esc_server  = escape_string(rm.server_id);
        std::string esc_channel = escape_string(rm.channel_id);
        std::string esc_name    = escape_string(rm.name);
        std::string esc_type    = escape_string(rm.message_type);
        std::string esc_content = escape_string(rm.content);
        std::string esc_embed_val = rm.embed_json.empty()
            ? std::string("NULL") : ("'" + escape_string(rm.embed_json) + "'");
        int owner_tok = rm.owner_tokens ? 1 : 0;

        std::string sql =
            "INSERT INTO recurring_messages "
            "(server_id, channel_id, name, message_type, content, embed_json, interval_seconds, last_sent, owner_tokens) "
            "VALUES ('" + esc_server + "', '" + esc_channel + "', '" + esc_name + "', '" + esc_type + "', "
            "'" + esc_content + "', " + esc_embed_val + ", "
            + std::to_string(rm.interval_seconds) + ", 0, " + std::to_string(owner_tok) + ")";

        if (mysql_query(mysql_, sql.c_str())) {
            std::cerr << "[bronx db] add_recurring_message failed: " << mysql_error(mysql_) << "\n";
            return false;
        }
        rm.id = static_cast<int>(mysql_insert_id(mysql_));
        rm.last_sent = 0;

        std::lock_guard<std::shared_mutex> cache_lock(rm_cache_mutex_);
        rm_cache_[rm.id] = rm;
        rm_server_index_[rm.server_id].push_back(rm.id);
        rm_channel_index_[rm.channel_id] = rm.id;
        return true;
    }

    // Removes a recurring message by id, validating server ownership.
    bool remove_recurring_message(int id, const std::string& server_id) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        if (!mysql_) return false;

        std::string sql =
            "DELETE FROM recurring_messages WHERE id = " + std::to_string(id) +
            " AND server_id = '" + escape_string(server_id) + "'";
        if (mysql_query(mysql_, sql.c_str())) {
            std::cerr << "[bronx db] remove_recurring_message failed: " << mysql_error(mysql_) << "\n";
            return false;
        }
        if (mysql_affected_rows(mysql_) == 0) return false;

        std::lock_guard<std::shared_mutex> cache_lock(rm_cache_mutex_);
        auto it = rm_cache_.find(id);
        if (it != rm_cache_.end()) {
            rm_channel_index_.erase(it->second.channel_id);
            auto& sv = rm_server_index_[it->second.server_id];
            sv.erase(std::remove(sv.begin(), sv.end(), id), sv.end());
            rm_cache_.erase(it);
        }
        return true;
    }

    // Updates all mutable fields of an existing recurring message.
    bool update_recurring_message(const RecurringMessage& rm) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        if (!mysql_) return false;

        std::string esc_channel = escape_string(rm.channel_id);
        std::string esc_name    = escape_string(rm.name);
        std::string esc_type    = escape_string(rm.message_type);
        std::string esc_content = escape_string(rm.content);
        std::string esc_embed_val = rm.embed_json.empty()
            ? std::string("NULL") : ("'" + escape_string(rm.embed_json) + "'");

        std::string sql =
            "UPDATE recurring_messages SET "
            "channel_id = '" + esc_channel + "', "
            "name = '" + esc_name + "', "
            "message_type = '" + esc_type + "', "
            "content = '" + esc_content + "', "
            "embed_json = " + esc_embed_val + ", "
            "interval_seconds = " + std::to_string(rm.interval_seconds) + ", "
            "owner_tokens = " + (rm.owner_tokens ? std::string("1") : std::string("0")) + " "
            "WHERE id = " + std::to_string(rm.id) +
            " AND server_id = '" + escape_string(rm.server_id) + "'";

        if (mysql_query(mysql_, sql.c_str())) {
            std::cerr << "[bronx db] update_recurring_message failed: " << mysql_error(mysql_) << "\n";
            return false;
        }

        std::lock_guard<std::shared_mutex> cache_lock(rm_cache_mutex_);
        auto it = rm_cache_.find(rm.id);
        if (it != rm_cache_.end()) {
            if (it->second.channel_id != rm.channel_id) {
                rm_channel_index_.erase(it->second.channel_id);
                rm_channel_index_[rm.channel_id] = rm.id;
            }
            it->second = rm;
        }
        return true;
    }

    // Updates only the last_sent timestamp after firing a recurring message.
    bool update_recurring_last_sent(int id, uint64_t ts) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        if (!mysql_) return false;

        std::string sql =
            "UPDATE recurring_messages SET last_sent = " + std::to_string(ts) +
            " WHERE id = " + std::to_string(id);
        if (mysql_query(mysql_, sql.c_str())) {
            std::cerr << "[bronx db] update_recurring_last_sent failed: " << mysql_error(mysql_) << "\n";
            return false;
        }

        std::lock_guard<std::shared_mutex> cache_lock(rm_cache_mutex_);
        auto it = rm_cache_.find(id);
        if (it != rm_cache_.end()) it->second.last_sent = ts;
        return true;
    }

    // =========================================================================
    // Autoresponder CRUD
    // =========================================================================

    static std::string json_arr_str(const std::vector<std::string>& v) {
        nlohmann::json j = v;
        return j.dump();
    }

    std::vector<AutoresponderConfig> get_autoresponders_for_server(const std::string& server_id) {
        std::shared_lock<std::shared_mutex> lock(arsp_cache_mutex_);
        std::vector<AutoresponderConfig> out;
        auto it = arsp_server_index_.find(server_id);
        if (it != arsp_server_index_.end()) {
            for (int id : it->second) {
                auto it2 = arsp_cache_.find(id);
                if (it2 != arsp_cache_.end()) out.push_back(it2->second);
            }
        }
        return out;
    }

    std::optional<AutoresponderConfig> get_autoresponder_by_id(int id, const std::string& server_id) {
        std::shared_lock<std::shared_mutex> lock(arsp_cache_mutex_);
        auto it = arsp_cache_.find(id);
        if (it != arsp_cache_.end() && it->second.server_id == server_id) return it->second;
        return std::nullopt;
    }

    std::optional<AutoresponderConfig> get_autoresponder_by_name(const std::string& server_id, const std::string& name) {
        std::shared_lock<std::shared_mutex> lock(arsp_cache_mutex_);
        std::string lower_name = name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
        auto it = arsp_server_index_.find(server_id);
        if (it != arsp_server_index_.end()) {
            for (int id : it->second) {
                auto it2 = arsp_cache_.find(id);
                if (it2 != arsp_cache_.end()) {
                    std::string cn = it2->second.name;
                    std::transform(cn.begin(), cn.end(), cn.begin(), ::tolower);
                    if (cn == lower_name) return it2->second;
                }
            }
        }
        return std::nullopt;
    }

    bool add_autoresponder(AutoresponderConfig& c) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        if (!mysql_) return false;
        auto null_or = [](const std::vector<std::string>& v) -> std::string {
            if (v.empty()) return "NULL";
            nlohmann::json j = v;
            return "'" + j.dump() + "'";
        };
        std::string sql =
            "INSERT INTO autoresponders (server_id,name,keyword,match_type,message_type,content,embed_json,"
            "allow_roles,allow_users,allow_channels,ignore_roles,ignore_users,ignore_channels) VALUES ("
            "'" + escape_string(c.server_id) + "',"
            "'" + escape_string(c.name) + "',"
            "'" + escape_string(c.keyword) + "',"
            "'" + escape_string(c.match_type) + "',"
            "'" + escape_string(c.message_type) + "',"
            "'" + escape_string(c.content) + "',"
            + (c.embed_json.empty() ? "NULL" : ("'" + escape_string(c.embed_json) + "'")) + ","
            + null_or(c.filter.allow_roles) + ","
            + null_or(c.filter.allow_users) + ","
            + null_or(c.filter.allow_channels) + ","
            + null_or(c.filter.ignore_roles) + ","
            + null_or(c.filter.ignore_users) + ","
            + null_or(c.filter.ignore_channels) + ")";
        if (mysql_query(mysql_, sql.c_str())) {
            std::cerr << "[bronx db] add_autoresponder failed: " << mysql_error(mysql_) << "\n";
            return false;
        }
        c.id = static_cast<int>(mysql_insert_id(mysql_));
        std::lock_guard<std::shared_mutex> cache_lock(arsp_cache_mutex_);
        arsp_cache_[c.id] = c;
        arsp_server_index_[c.server_id].push_back(c.id);
        return true;
    }

    bool remove_autoresponder(int id, const std::string& server_id) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        if (!mysql_) return false;
        std::string sql = "DELETE FROM autoresponders WHERE id=" + std::to_string(id) +
            " AND server_id='" + escape_string(server_id) + "'";
        if (mysql_query(mysql_, sql.c_str()) || mysql_affected_rows(mysql_) == 0) return false;
        std::lock_guard<std::shared_mutex> cache_lock(arsp_cache_mutex_);
        auto it = arsp_cache_.find(id);
        if (it != arsp_cache_.end()) {
            auto& sv = arsp_server_index_[it->second.server_id];
            sv.erase(std::remove(sv.begin(), sv.end(), id), sv.end());
            arsp_cache_.erase(it);
        }
        return true;
    }

    bool update_autoresponder(const AutoresponderConfig& c) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        if (!mysql_) return false;
        auto null_or = [](const std::vector<std::string>& v) -> std::string {
            if (v.empty()) return "NULL";
            nlohmann::json j = v;
            return "'" + j.dump() + "'";
        };
        std::string sql =
            "UPDATE autoresponders SET "
            "name='" + escape_string(c.name) + "',"
            "keyword='" + escape_string(c.keyword) + "',"
            "match_type='" + escape_string(c.match_type) + "',"
            "message_type='" + escape_string(c.message_type) + "',"
            "content='" + escape_string(c.content) + "',"
            "embed_json=" + (c.embed_json.empty() ? "NULL" : ("'" + escape_string(c.embed_json) + "'")) + ","
            "allow_roles=" + null_or(c.filter.allow_roles) + ","
            "allow_users=" + null_or(c.filter.allow_users) + ","
            "allow_channels=" + null_or(c.filter.allow_channels) + ","
            "ignore_roles=" + null_or(c.filter.ignore_roles) + ","
            "ignore_users=" + null_or(c.filter.ignore_users) + ","
            "ignore_channels=" + null_or(c.filter.ignore_channels) + " "
            "WHERE id=" + std::to_string(c.id) + " AND server_id='" + escape_string(c.server_id) + "'";
        if (mysql_query(mysql_, sql.c_str())) return false;
        std::lock_guard<std::shared_mutex> cache_lock(arsp_cache_mutex_);
        arsp_cache_[c.id] = c;
        return true;
    }

    // =========================================================================
    // Autoreact CRUD
    // =========================================================================

    std::vector<AutoreactConfig> get_autoreacts_for_server(const std::string& server_id) {
        std::shared_lock<std::shared_mutex> lock(arct_cache_mutex_);
        std::vector<AutoreactConfig> out;
        auto it = arct_server_index_.find(server_id);
        if (it != arct_server_index_.end()) {
            for (int id : it->second) {
                auto it2 = arct_cache_.find(id);
                if (it2 != arct_cache_.end()) out.push_back(it2->second);
            }
        }
        return out;
    }

    std::optional<AutoreactConfig> get_autoreact_by_id(int id, const std::string& server_id) {
        std::shared_lock<std::shared_mutex> lock(arct_cache_mutex_);
        auto it = arct_cache_.find(id);
        if (it != arct_cache_.end() && it->second.server_id == server_id) return it->second;
        return std::nullopt;
    }

    std::optional<AutoreactConfig> get_autoreact_by_name(const std::string& server_id, const std::string& name) {
        std::shared_lock<std::shared_mutex> lock(arct_cache_mutex_);
        std::string lower_name = name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
        auto it = arct_server_index_.find(server_id);
        if (it != arct_server_index_.end()) {
            for (int id : it->second) {
                auto it2 = arct_cache_.find(id);
                if (it2 != arct_cache_.end()) {
                    std::string cn = it2->second.name;
                    std::transform(cn.begin(), cn.end(), cn.begin(), ::tolower);
                    if (cn == lower_name) return it2->second;
                }
            }
        }
        return std::nullopt;
    }

    bool add_autoreact(AutoreactConfig& c) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        if (!mysql_) return false;
        auto null_or = [](const std::vector<std::string>& v) -> std::string {
            if (v.empty()) return "NULL";
            nlohmann::json j = v;
            return "'" + j.dump() + "'";
        };
        std::string sql =
            "INSERT INTO autoreacts (server_id,name,keyword,match_type,emojis,"
            "allow_roles,allow_users,allow_channels,ignore_roles,ignore_users,ignore_channels) VALUES ("
            "'" + escape_string(c.server_id) + "',"
            "'" + escape_string(c.name) + "',"
            "'" + escape_string(c.keyword) + "',"
            "'" + escape_string(c.match_type) + "',"
            "'" + escape_string(c.emojis) + "',"
            + null_or(c.filter.allow_roles) + ","
            + null_or(c.filter.allow_users) + ","
            + null_or(c.filter.allow_channels) + ","
            + null_or(c.filter.ignore_roles) + ","
            + null_or(c.filter.ignore_users) + ","
            + null_or(c.filter.ignore_channels) + ")";
        if (mysql_query(mysql_, sql.c_str())) {
            std::cerr << "[bronx db] add_autoreact failed: " << mysql_error(mysql_) << "\n";
            return false;
        }
        c.id = static_cast<int>(mysql_insert_id(mysql_));
        std::lock_guard<std::shared_mutex> cache_lock(arct_cache_mutex_);
        arct_cache_[c.id] = c;
        arct_server_index_[c.server_id].push_back(c.id);
        return true;
    }

    bool remove_autoreact(int id, const std::string& server_id) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        if (!mysql_) return false;
        std::string sql = "DELETE FROM autoreacts WHERE id=" + std::to_string(id) +
            " AND server_id='" + escape_string(server_id) + "'";
        if (mysql_query(mysql_, sql.c_str()) || mysql_affected_rows(mysql_) == 0) return false;
        std::lock_guard<std::shared_mutex> cache_lock(arct_cache_mutex_);
        auto it = arct_cache_.find(id);
        if (it != arct_cache_.end()) {
            auto& sv = arct_server_index_[it->second.server_id];
            sv.erase(std::remove(sv.begin(), sv.end(), id), sv.end());
            arct_cache_.erase(it);
        }
        return true;
    }

    bool update_autoreact(const AutoreactConfig& c) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        if (!mysql_) return false;
        auto null_or = [](const std::vector<std::string>& v) -> std::string {
            if (v.empty()) return "NULL";
            nlohmann::json j = v;
            return "'" + j.dump() + "'";
        };
        std::string sql =
            "UPDATE autoreacts SET "
            "name='" + escape_string(c.name) + "',"
            "keyword='" + escape_string(c.keyword) + "',"
            "match_type='" + escape_string(c.match_type) + "',"
            "emojis='" + escape_string(c.emojis) + "',"
            "allow_roles=" + null_or(c.filter.allow_roles) + ","
            "allow_users=" + null_or(c.filter.allow_users) + ","
            "allow_channels=" + null_or(c.filter.allow_channels) + ","
            "ignore_roles=" + null_or(c.filter.ignore_roles) + ","
            "ignore_users=" + null_or(c.filter.ignore_users) + ","
            "ignore_channels=" + null_or(c.filter.ignore_channels) + " "
            "WHERE id=" + std::to_string(c.id) + " AND server_id='" + escape_string(c.server_id) + "'";
        if (mysql_query(mysql_, sql.c_str())) return false;
        std::lock_guard<std::shared_mutex> cache_lock(arct_cache_mutex_);
        arct_cache_[c.id] = c;
        return true;
    }

    bool update_autoresponder_filter(int id, const std::string& server_id, const AutoScopeFilter& f) {
        auto opt = get_autoresponder_by_id(id, server_id);
        if (!opt) return false;
        opt->filter = f;
        return update_autoresponder(*opt);
    }

    bool update_autoreact_filter(int id, const std::string& server_id, const AutoScopeFilter& f) {
        auto opt = get_autoreact_by_id(id, server_id);
        if (!opt) return false;
        opt->filter = f;
        return update_autoreact(*opt);
    }

    // --- Ticket System Helper Methods ---

    std::optional<TicketConfig> get_ticket_config(const std::string& server_id, const std::string& name = "default") {
        std::shared_lock<std::shared_mutex> lock(ticket_config_cache_mutex_);
        std::string key = server_id + ":" + name;
        auto it = ticket_config_cache_.find(key);
        if (it != ticket_config_cache_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    bool save_ticket_config(const TicketConfig& config) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        if (!mysql_) return false;

        std::string esc_srv = escape_string(config.server_id);
        std::string esc_name = escape_string(config.name);
        std::string esc_m_chan = escape_string(config.modal_channel_id);
        std::string esc_m_msg = escape_string(config.modal_message_id);
        std::string esc_content = escape_string(config.modal_content);
        std::string esc_embed = escape_string(config.modal_embed_json);
        std::string esc_emoji = escape_string(config.reaction_emoji);
        int input_creation = config.input_creation_enabled ? 1 : 0;
        std::string esc_scheme = escape_string(config.naming_scheme);
        std::string esc_cat = escape_string(config.category_id);
        std::string esc_roles = escape_string(config.view_roles);
        std::string esc_users = escape_string(config.view_users);
        std::string esc_perms = escape_string(config.view_permissions);
        std::string esc_t_content = escape_string(config.ticket_welcome_content);
        std::string esc_t_embed = escape_string(config.ticket_welcome_embed_json);
        std::string esc_close_emoji = escape_string(config.ticket_close_emoji);
        std::string esc_close_msg = escape_string(config.ticket_close_content);
        std::string esc_member_role = escape_string(config.member_role_id);
        int counter = config.ticket_counter;

        std::string query = 
            "INSERT INTO ticket_configs (server_id, name, modal_channel_id, modal_message_id, modal_content, modal_embed_json, "
            "reaction_emoji, input_creation_enabled, naming_scheme, category_id, view_roles, view_users, view_permissions, "
            "ticket_welcome_content, ticket_welcome_embed_json, ticket_close_emoji, ticket_close_content, ticket_counter, member_role_id) "
            "VALUES ('" + esc_srv + "', '" + esc_name + "', '" + esc_m_chan + "', '" + esc_m_msg + "', '" + esc_content + "', '" + esc_embed + "', '" +
            esc_emoji + "', " + std::to_string(input_creation) + ", '" + esc_scheme + "', '" + esc_cat + "', '" + esc_roles + "', '" +
            esc_users + "', '" + esc_perms + "', '" + esc_t_content + "', '" + esc_t_embed + "', '" + esc_close_emoji + "', '" + esc_close_msg + "', " + std::to_string(counter) + ", '" + esc_member_role + "') "
            "ON DUPLICATE KEY UPDATE "
            "modal_channel_id = '" + esc_m_chan + "', "
            "modal_message_id = '" + esc_m_msg + "', "
            "modal_content = '" + esc_content + "', "
            "modal_embed_json = '" + esc_embed + "', "
            "reaction_emoji = '" + esc_emoji + "', "
            "input_creation_enabled = " + std::to_string(input_creation) + ", "
            "naming_scheme = '" + esc_scheme + "', "
            "category_id = '" + esc_cat + "', "
            "view_roles = '" + esc_roles + "', "
            "view_users = '" + esc_users + "', "
            "view_permissions = '" + esc_perms + "', "
            "ticket_welcome_content = '" + esc_t_content + "', "
            "ticket_welcome_embed_json = '" + esc_t_embed + "', "
            "ticket_close_emoji = '" + esc_close_emoji + "', "
            "ticket_close_content = '" + esc_close_msg + "', "
            "ticket_counter = " + std::to_string(counter) + ", "
            "member_role_id = '" + esc_member_role + "'";

        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to save ticket config: " << mysql_error(mysql_) << "\n";
            return false;
        }

        {
            std::lock_guard<std::shared_mutex> cache_lock(ticket_config_cache_mutex_);
            std::string key = config.server_id + ":" + config.name;
            ticket_config_cache_[key] = config;
        }
        return true;
    }

    std::optional<Ticket> get_ticket(const std::string& channel_id) {
        std::shared_lock<std::shared_mutex> lock(ticket_cache_mutex_);
        auto it = ticket_cache_.find(channel_id);
        if (it != ticket_cache_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    bool save_ticket(const Ticket& ticket) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        if (!mysql_) return false;

        std::string esc_chan = escape_string(ticket.channel_id);
        std::string esc_srv = escape_string(ticket.server_id);
        std::string esc_user = escape_string(ticket.user_id);
        std::string esc_welcome = escape_string(ticket.welcome_message_id);
        std::string esc_status = escape_string(ticket.status);
        std::string esc_panel = escape_string(ticket.panel_name);

        std::string query = 
            "INSERT INTO tickets (channel_id, server_id, user_id, ticket_number, welcome_message_id, status, panel_name) "
            "VALUES ('" + esc_chan + "', '" + esc_srv + "', '" + esc_user + "', " + std::to_string(ticket.ticket_number) + ", '" + esc_welcome + "', '" + esc_status + "', '" + esc_panel + "') "
            "ON DUPLICATE KEY UPDATE "
            "server_id = '" + esc_srv + "', "
            "user_id = '" + esc_user + "', "
            "ticket_number = " + std::to_string(ticket.ticket_number) + ", "
            "welcome_message_id = '" + esc_welcome + "', "
            "status = '" + esc_status + "', "
            "panel_name = '" + esc_panel + "'";

        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to save ticket: " << mysql_error(mysql_) << "\n";
            return false;
        }

        {
            std::lock_guard<std::shared_mutex> cache_lock(ticket_cache_mutex_);
            ticket_cache_[ticket.channel_id] = ticket;
        }
        return true;
    }

    bool delete_ticket(const std::string& channel_id) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        if (!mysql_) return false;

        std::string esc_chan = escape_string(channel_id);
        std::string query = "DELETE FROM tickets WHERE channel_id = '" + esc_chan + "'";
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to delete ticket: " << mysql_error(mysql_) << "\n";
            return false;
        }

        {
            std::lock_guard<std::shared_mutex> cache_lock(ticket_cache_mutex_);
            ticket_cache_.erase(channel_id);
        }
        return true;
    }

    std::vector<Ticket> get_server_tickets(const std::string& server_id) {
        std::shared_lock<std::shared_mutex> lock(ticket_cache_mutex_);
        std::vector<Ticket> results;
        for (const auto& [ch_id, t] : ticket_cache_) {
            if (t.server_id == server_id) {
                results.push_back(t);
            }
        }
        return results;
    }

    std::optional<Ticket> get_user_open_ticket(const std::string& server_id, const std::string& user_id) {
        std::shared_lock<std::shared_mutex> lock(ticket_cache_mutex_);
        for (const auto& [ch_id, t] : ticket_cache_) {
            if (t.server_id == server_id && t.user_id == user_id && t.status == "open") {
                return t;
            }
        }
        return std::nullopt;
    }

    std::vector<TicketConfig> get_ticket_configs_for_server(const std::string& server_id) {
        std::shared_lock<std::shared_mutex> lock(ticket_config_cache_mutex_);
        std::vector<TicketConfig> results;
        for (const auto& [key, cfg] : ticket_config_cache_) {
            if (cfg.server_id == server_id) {
                results.push_back(cfg);
            }
        }
        return results;
    }

    std::vector<TicketConfig> get_all_ticket_configs() {
        std::shared_lock<std::shared_mutex> lock(ticket_config_cache_mutex_);
        std::vector<TicketConfig> results;
        for (const auto& [key, cfg] : ticket_config_cache_) {
            results.push_back(cfg);
        }
        return results;
    }

    std::vector<Ticket> get_all_tickets() {
        std::shared_lock<std::shared_mutex> lock(ticket_cache_mutex_);
        std::vector<Ticket> results;
        for (const auto& [ch_id, t] : ticket_cache_) {
            results.push_back(t);
        }
        return results;
    }

    // ── Counting DB functions ─────────────────────────────────────────────────

    std::optional<CountingConfig> get_counting_config(const std::string& server_id) {
        std::shared_lock<std::shared_mutex> lk(counting_cache_mutex_);
        auto it = counting_cache_.find(server_id);
        if (it != counting_cache_.end()) return it->second;
        return std::nullopt;
    }

    bool save_counting_config(const CountingConfig& cfg) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        if (!mysql_) return false;

        std::string esc_srv  = escape_string(cfg.server_id);
        std::string esc_chan = escape_string(cfg.channel_id);
        std::string esc_last = escape_string(cfg.last_user_id);
        std::string esc_role = escape_string(cfg.fail_role_id);
        int passive          = cfg.passive          ? 1 : 0;
        int warn_dbl         = cfg.warn_double_count ? 1 : 0;

        std::string q =
            "INSERT INTO counting_configs (server_id, channel_id, current_number, last_user_id, fail_role_id, fail_role_duration, passive, highest_count, warn_double_count) "
            "VALUES ('" + esc_srv + "','" + esc_chan + "'," + std::to_string(cfg.current_number) + ",'" + esc_last + "','" + esc_role + "'," +
            std::to_string(cfg.fail_role_duration) + "," + std::to_string(passive) + "," + std::to_string(cfg.highest_count) + "," + std::to_string(warn_dbl) + ") "
            "ON DUPLICATE KEY UPDATE "
            "channel_id='" + esc_chan + "', "
            "current_number=" + std::to_string(cfg.current_number) + ", "
            "last_user_id='" + esc_last + "', "
            "fail_role_id='" + esc_role + "', "
            "fail_role_duration=" + std::to_string(cfg.fail_role_duration) + ", "
            "passive=" + std::to_string(passive) + ", "
            "highest_count=" + std::to_string(cfg.highest_count) + ", "
            "warn_double_count=" + std::to_string(warn_dbl);

        if (mysql_query(mysql_, q.c_str())) {
            std::cerr << "[bronx db] failed to save counting_config: " << mysql_error(mysql_) << "\n";
            return false;
        }
        {
            std::lock_guard<std::shared_mutex> lk(counting_cache_mutex_);
            counting_cache_[cfg.server_id] = cfg;
        }
        return true;
    }

    // Increment a user's correct count by 1
    void counting_increment_correct(const std::string& server_id, const std::string& user_id) {
        std::string key = server_id + ":" + user_id;
        {
            std::lock_guard<std::shared_mutex> lk(counting_stats_cache_mutex_);
            counting_stats_cache_[key].server_id = server_id;
            counting_stats_cache_[key].user_id   = user_id;
            counting_stats_cache_[key].correct_counts++;
        }
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        if (!mysql_) return;
        std::string esc_srv  = escape_string(server_id);
        std::string esc_user = escape_string(user_id);
        std::string q =
            "INSERT INTO counting_stats (server_id, user_id, correct_counts, failed_counts) "
            "VALUES ('" + esc_srv + "','" + esc_user + "',1,0) "
            "ON DUPLICATE KEY UPDATE correct_counts=correct_counts+1";
        mysql_query(mysql_, q.c_str());
    }

    // Increment a user's failed count by 1
    void counting_increment_failed(const std::string& server_id, const std::string& user_id) {
        std::string key = server_id + ":" + user_id;
        {
            std::lock_guard<std::shared_mutex> lk(counting_stats_cache_mutex_);
            counting_stats_cache_[key].server_id = server_id;
            counting_stats_cache_[key].user_id   = user_id;
            counting_stats_cache_[key].failed_counts++;
        }
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        if (!mysql_) return;
        std::string esc_srv  = escape_string(server_id);
        std::string esc_user = escape_string(user_id);
        std::string q =
            "INSERT INTO counting_stats (server_id, user_id, correct_counts, failed_counts) "
            "VALUES ('" + esc_srv + "','" + esc_user + "',0,1) "
            "ON DUPLICATE KEY UPDATE failed_counts=failed_counts+1";
        mysql_query(mysql_, q.c_str());
    }

    // Return all stats for a server sorted by correct_counts desc
    std::vector<CountingStats> get_counting_leaderboard(const std::string& server_id, int limit = 10) {
        std::shared_lock<std::shared_mutex> lk(counting_stats_cache_mutex_);
        std::vector<CountingStats> results;
        for (const auto& [key, st] : counting_stats_cache_) {
            if (st.server_id == server_id) results.push_back(st);
        }
        std::sort(results.begin(), results.end(), [](const CountingStats& a, const CountingStats& b){
            return a.correct_counts > b.correct_counts;
        });
        if ((int)results.size() > limit) results.resize(limit);
        return results;
    }

    CountingStats get_counting_stats(const std::string& server_id, const std::string& user_id) {
        std::string key = server_id + ":" + user_id;
        std::shared_lock<std::shared_mutex> lk(counting_stats_cache_mutex_);
        auto it = counting_stats_cache_.find(key);
        if (it != counting_stats_cache_.end()) return it->second;
        CountingStats st; st.server_id = server_id; st.user_id = user_id;
        return st;
    }

    // Returns top correct counter, highest count ever, and top failer for server
    struct CountingHighlights {
        std::string top_counter_id;    int64_t top_counter_score = 0;
        std::string top_failer_id;     int64_t top_failer_score  = 0;
        int64_t highest_count = 0;
    };

    CountingHighlights get_counting_highlights(const std::string& server_id) {
        CountingHighlights h;
        {
            std::shared_lock<std::shared_mutex> lk(counting_stats_cache_mutex_);
            for (const auto& [key, st] : counting_stats_cache_) {
                if (st.server_id != server_id) continue;
                if (st.correct_counts > h.top_counter_score) {
                    h.top_counter_score = st.correct_counts;
                    h.top_counter_id    = st.user_id;
                }
                if (st.failed_counts > h.top_failer_score) {
                    h.top_failer_score = st.failed_counts;
                    h.top_failer_id    = st.user_id;
                }
            }
        }
        {
            std::shared_lock<std::shared_mutex> lk(counting_cache_mutex_);
            auto it = counting_cache_.find(server_id);
            if (it != counting_cache_.end()) h.highest_count = it->second.highest_count;
        }
        return h;
    }

    std::optional<UserAFK> get_user_afk(const std::string& user_id) {
        std::shared_lock<std::shared_mutex> lock(user_afk_cache_mutex_);
        auto it = user_afk_cache_.find(user_id);
        if (it != user_afk_cache_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    bool save_user_afk(const UserAFK& afk) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        std::string esc_user_id = escape_string(afk.user_id);
        std::string esc_tmpl = escape_string(afk.custom_template);
        std::string esc_reason = escape_string(afk.reason);
        int is_afk_val = afk.is_afk ? 1 : 0;
        std::string esc_preset = escape_string(afk.active_preset);

        std::string query = 
            "INSERT INTO user_afk (user_id, custom_template, is_afk, reason, since, active_preset) "
            "VALUES ('" + esc_user_id + "', '" + esc_tmpl + "', " + std::to_string(is_afk_val) + ", '" + esc_reason + "', " + std::to_string(afk.since) + ", '" + esc_preset + "') "
            "ON DUPLICATE KEY UPDATE "
            "custom_template = '" + esc_tmpl + "', "
            "is_afk = " + std::to_string(is_afk_val) + ", "
            "reason = '" + esc_reason + "', "
            "since = " + std::to_string(afk.since) + ", "
            "active_preset = '" + esc_preset + "'";
            
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to save user afk: " << mysql_error(mysql_) << "\n";
            return false;
        }
        
        {
            std::lock_guard<std::shared_mutex> lock(user_afk_cache_mutex_);
            user_afk_cache_[afk.user_id] = afk;
        }
        return true;
    }

    std::string get_user_afk_preset(const std::string& user_id, const std::string& preset_name) {
        if (preset_name == "default") {
            auto afk_opt = get_user_afk(user_id);
            if (afk_opt) {
                return afk_opt->custom_template;
            }
            return "";
        }
        std::shared_lock<std::shared_mutex> lock(user_afk_presets_cache_mutex_);
        auto it = user_afk_presets_cache_.find(user_id);
        if (it != user_afk_presets_cache_.end()) {
            auto pit = it->second.find(preset_name);
            if (pit != it->second.end()) {
                return pit->second;
            }
        }
        // Fallback to default
        auto afk_opt = get_user_afk(user_id);
        if (afk_opt) {
            return afk_opt->custom_template;
        }
        return "";
    }

    bool save_user_afk_preset(const std::string& user_id, const std::string& preset_name, const std::string& tmpl_json) {
        if (preset_name == "default") {
            auto afk_opt = get_user_afk(user_id);
            UserAFK afk;
            if (afk_opt) afk = *afk_opt;
            afk.user_id = user_id;
            afk.custom_template = tmpl_json;
            return save_user_afk(afk);
        }

        std::lock_guard<std::mutex> db_lock(db_mutex_);
        std::string esc_user_id = escape_string(user_id);
        std::string esc_preset = escape_string(preset_name);
        std::string esc_tmpl = escape_string(tmpl_json);

        std::string query = 
            "INSERT INTO user_afk_presets (user_id, preset_name, template_json) "
            "VALUES ('" + esc_user_id + "', '" + esc_preset + "', '" + esc_tmpl + "') "
            "ON DUPLICATE KEY UPDATE template_json = '" + esc_tmpl + "'";

        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to save user afk preset: " << mysql_error(mysql_) << "\n";
            return false;
        }

        {
            std::lock_guard<std::shared_mutex> lock(user_afk_presets_cache_mutex_);
            user_afk_presets_cache_[user_id][preset_name] = tmpl_json;
        }
        return true;
    }

    bool delete_user_afk_preset(const std::string& user_id, const std::string& preset_name) {
        if (preset_name == "default") {
            // Cannot delete default preset, reset it to empty/default configuration
            auto afk_opt = get_user_afk(user_id);
            UserAFK afk;
            if (afk_opt) afk = *afk_opt;
            afk.user_id = user_id;
            afk.custom_template = "";
            return save_user_afk(afk);
        }

        std::lock_guard<std::mutex> db_lock(db_mutex_);
        std::string esc_user_id = escape_string(user_id);
        std::string esc_preset = escape_string(preset_name);

        std::string query = "DELETE FROM user_afk_presets WHERE user_id = '" + esc_user_id + "' AND preset_name = '" + esc_preset + "'";
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to delete user afk preset: " << mysql_error(mysql_) << "\n";
            return false;
        }

        {
            std::lock_guard<std::shared_mutex> lock(user_afk_presets_cache_mutex_);
            auto it = user_afk_presets_cache_.find(user_id);
            if (it != user_afk_presets_cache_.end()) {
                it->second.erase(preset_name);
            }
        }
        return true;
    }

    std::vector<std::string> get_user_afk_presets_list(const std::string& user_id) {
        std::shared_lock<std::shared_mutex> lock(user_afk_presets_cache_mutex_);
        std::vector<std::string> presets;
        auto it = user_afk_presets_cache_.find(user_id);
        if (it != user_afk_presets_cache_.end()) {
            for (const auto& [name, _] : it->second) {
                presets.push_back(name);
            }
        }
        return presets;
    }

    std::optional<RobloxLink> get_roblox_link(const std::string& user_id) {
        std::shared_lock<std::shared_mutex> lock(roblox_links_cache_mutex_);
        auto it = roblox_links_cache_.find(user_id);
        if (it != roblox_links_cache_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    std::optional<RobloxLink> get_roblox_link_by_roblox_username(const std::string& username) {
        std::shared_lock<std::shared_mutex> lock(roblox_links_cache_mutex_);
        std::string target = username;
        std::transform(target.begin(), target.end(), target.begin(), ::tolower);
        for (const auto& [_, link] : roblox_links_cache_) {
            std::string name_lower = link.roblox_username;
            std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
            if (name_lower == target) {
                return link;
            }
        }
        return std::nullopt;
    }

    std::optional<RobloxLink> get_roblox_link_by_roblox_id(uint64_t roblox_id) {
        std::shared_lock<std::shared_mutex> lock(roblox_links_cache_mutex_);
        for (const auto& [_, link] : roblox_links_cache_) {
            if (link.roblox_id == roblox_id) {
                return link;
            }
        }
        return std::nullopt;
    }

    bool save_roblox_link(const std::string& user_id, uint64_t roblox_id, const std::string& roblox_username) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        std::string esc_user_id = escape_string(user_id);
        std::string esc_roblox_username = escape_string(roblox_username);

        std::string query = 
            "INSERT INTO roblox_links (user_id, roblox_id, roblox_username) "
            "VALUES ('" + esc_user_id + "', " + std::to_string(roblox_id) + ", '" + esc_roblox_username + "') "
            "ON DUPLICATE KEY UPDATE "
            "roblox_id = " + std::to_string(roblox_id) + ", "
            "roblox_username = '" + esc_roblox_username + "'";
            
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to save roblox link: " << mysql_error(mysql_) << "\n";
            return false;
        }
        
        {
            std::lock_guard<std::shared_mutex> lock(roblox_links_cache_mutex_);
            roblox_links_cache_[user_id] = {user_id, roblox_id, roblox_username};
        }
        return true;
    }

    bool delete_roblox_link(const std::string& user_id) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);
        std::string esc_user_id = escape_string(user_id);
        std::string query = "DELETE FROM roblox_links WHERE user_id = '" + esc_user_id + "'";
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to delete roblox link: " << mysql_error(mysql_) << "\n";
            return false;
        }
        {
            std::lock_guard<std::shared_mutex> lock(roblox_links_cache_mutex_);
            roblox_links_cache_.erase(user_id);
        }
        return true;
    }

};

} // namespace bronx

