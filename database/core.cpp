#include "database.h"
#include <mysql.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bronx {

  bool Database::init(const std::string &host, const std::string &user,
            const std::string &password, const std::string &dbname,
            unsigned int port) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    mysql_ = mysql_init(nullptr);
    if (!mysql_) {
      std::cerr << "[bronx db] mysql_init failed\n";
      return false;
    }

    // Set connection options (prevent hanging and auto-reconnect)
    my_bool reconnect = 1;
    mysql_options(mysql_, MYSQL_OPT_RECONNECT, &reconnect);
    unsigned int timeout_sec = 10;
    mysql_options(mysql_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout_sec);
    mysql_options(mysql_, MYSQL_OPT_READ_TIMEOUT, &timeout_sec);
    mysql_options(mysql_, MYSQL_OPT_WRITE_TIMEOUT, &timeout_sec);

    // Connect to server (without DB name first, to create it if it doesn't
    // exist)
    if (!mysql_real_connect(mysql_, host.c_str(), user.c_str(),
                            password.c_str(), nullptr, port, nullptr, 0)) {
      std::cerr << "[bronx db] connection failed: " << mysql_error(mysql_)
                << "\n";
      return false;
    }

    // Create Database if it doesn't exist
    std::string create_db = "CREATE DATABASE IF NOT EXISTS " + dbname;
    if (mysql_query(mysql_, create_db.c_str())) {
      std::cerr << "[bronx db] failed to create database '" << dbname
                << "': " << mysql_error(mysql_) << "\n";
      return false;
    }

    // Select the Database
    if (mysql_select_db(mysql_, dbname.c_str())) {
      std::cerr << "[bronx db] failed to select database '" << dbname
                << "': " << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table prefixes: JSON column is native in MariaDB and MySQL
    std::string create_table = "CREATE TABLE IF NOT EXISTS prefixes ("
                               "  target_type VARCHAR(10) NOT NULL,"
                               "  target_id VARCHAR(30) NOT NULL,"
                               "  prefixes JSON NOT NULL,"
                               "  PRIMARY KEY (target_type, target_id)"
                               ")";
    if (mysql_query(mysql_, create_table.c_str())) {
      std::cerr << "[bronx db] failed to create table 'prefixes': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Migration: add default_enabled column if it doesn't exist yet
    mysql_query(mysql_, "ALTER TABLE prefixes ADD COLUMN IF NOT EXISTS "
                        "default_enabled TINYINT(1) NOT NULL DEFAULT 1");

    // Create Table rr_messages
    std::string create_rr_msgs = "CREATE TABLE IF NOT EXISTS rr_messages ("
                                 "  message_id VARCHAR(30) NOT NULL,"
                                 "  channel_id VARCHAR(30) NOT NULL,"
                                 "  server_id VARCHAR(30) NOT NULL,"
                                 "  modal_name VARCHAR(50) DEFAULT NULL,"
                                 "  PRIMARY KEY (message_id)"
                                 ")";
    if (mysql_query(mysql_, create_rr_msgs.c_str())) {
      std::cerr << "[bronx db] failed to create table 'rr_messages': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Migration: Ensure embed_json column exists
    std::string alter_rr_msg = "ALTER TABLE rr_messages ADD COLUMN IF NOT "
                               "EXISTS embed_json TEXT DEFAULT NULL";
    if (mysql_query(mysql_, alter_rr_msg.c_str())) {
      std::string alter_rr_msg_fallback =
          "ALTER TABLE rr_messages ADD COLUMN embed_json TEXT DEFAULT NULL";
      mysql_query(mysql_, alter_rr_msg_fallback.c_str());
    }

    // Migration: Ensure type column exists on rr_messages
    std::string alter_rr_msg_type =
        "ALTER TABLE rr_messages ADD COLUMN IF NOT EXISTS type VARCHAR(20) NOT "
        "NULL DEFAULT 'standard'";
    if (mysql_query(mysql_, alter_rr_msg_type.c_str())) {
      std::string alter_rr_msg_type_fallback =
          "ALTER TABLE rr_messages ADD COLUMN type VARCHAR(20) NOT NULL "
          "DEFAULT 'standard'";
      mysql_query(mysql_, alter_rr_msg_type_fallback.c_str());
    }

    // Create Table rr_rules
    std::string create_rr_rules =
        "CREATE TABLE IF NOT EXISTS rr_rules ("
        "  message_id VARCHAR(30) NOT NULL,"
        "  emoji_id VARCHAR(100) NOT NULL,"
        "  role_id VARCHAR(30) NOT NULL,"
        "  PRIMARY KEY (message_id, emoji_id, role_id),"
        "  FOREIGN KEY (message_id) REFERENCES rr_messages(message_id) ON "
        "DELETE CASCADE"
        ")";
    if (mysql_query(mysql_, create_rr_rules.c_str())) {
      std::cerr << "[bronx db] failed to create table 'rr_rules': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Migration: Update primary key to allow multiple roles per emoji
    std::string alter_rr_rules = "ALTER TABLE rr_rules DROP PRIMARY KEY, ADD "
                                 "PRIMARY KEY(message_id, emoji_id, role_id)";
    mysql_query(mysql_,
                alter_rr_rules.c_str()); // Ignore error if it fails (e.g. if it
                                         // already has the correct PK)

    // Migration: Ensure rule_type column exists on rr_rules
    std::string alter_rr_rules_type =
        "ALTER TABLE rr_rules ADD COLUMN IF NOT EXISTS rule_type VARCHAR(10) "
        "NOT NULL DEFAULT 'add'";
    if (mysql_query(mysql_, alter_rr_rules_type.c_str())) {
      std::string alter_rr_rules_type_fallback =
          "ALTER TABLE rr_rules ADD COLUMN rule_type VARCHAR(10) NOT NULL "
          "DEFAULT 'add'";
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
      std::cerr << "[bronx db] failed to create table 'guild_command_usage': "
                << mysql_error(mysql_) << "\n";
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
      std::cerr
          << "[bronx db] failed to create table 'guild_user_activity_daily': "
          << mysql_error(mysql_) << "\n";
      return false;
    }

    // Add is_bot column if not exists in case of existing tables
    std::string alter_user_activity =
        "ALTER TABLE guild_user_activity_daily ADD COLUMN IF NOT EXISTS is_bot "
        "BOOLEAN NOT NULL DEFAULT FALSE";
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
      std::cerr << "[bronx db] failed to create table "
                   "'guild_channel_activity_daily': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table guild_user_channel_activity_daily
    std::string create_user_channel_activity =
        "CREATE TABLE IF NOT EXISTS guild_user_channel_activity_daily ("
        "  guild_id VARCHAR(30) NOT NULL,"
        "  channel_id VARCHAR(30) NOT NULL,"
        "  user_id VARCHAR(30) NOT NULL,"
        "  stat_date DATE NOT NULL,"
        "  messages INT UNSIGNED NOT NULL DEFAULT 0,"
        "  commands_used INT UNSIGNED NOT NULL DEFAULT 0,"
        "  PRIMARY KEY (guild_id, channel_id, user_id, stat_date)"
        ")";
    if (mysql_query(mysql_, create_user_channel_activity.c_str())) {
      std::cerr << "[bronx db] failed to create table "
                   "'guild_user_channel_activity_daily': "
                << mysql_error(mysql_) << "\n";
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
      std::cerr << "[bronx db] failed to create table 'users': "
                << mysql_error(mysql_) << "\n";
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
      std::cerr << "[bronx db] failed to create table 'welcome_leave_configs': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Migration: Ensure autorole_role_id column exists
    std::string alter_wl =
        "ALTER TABLE welcome_leave_configs ADD COLUMN IF NOT EXISTS "
        "autorole_role_id VARCHAR(30) DEFAULT NULL";
    if (mysql_query(mysql_, alter_wl.c_str())) {
      std::string alter_wl_fallback =
          "ALTER TABLE welcome_leave_configs ADD COLUMN autorole_role_id "
          "VARCHAR(30) DEFAULT NULL";
      mysql_query(mysql_, alter_wl_fallback.c_str());
    }

    // Migration: Ensure DM columns exist
    mysql_query(mysql_,
                "ALTER TABLE welcome_leave_configs ADD COLUMN IF NOT EXISTS "
                "welcome_dm_type VARCHAR(10) NOT NULL DEFAULT 'none'");
    mysql_query(mysql_, "ALTER TABLE welcome_leave_configs ADD COLUMN IF NOT "
                        "EXISTS welcome_dm_content TEXT DEFAULT NULL");
    mysql_query(mysql_, "ALTER TABLE welcome_leave_configs ADD COLUMN IF NOT "
                        "EXISTS welcome_dm_embed JSON DEFAULT NULL");

    // Create Table logging_configs
    std::string create_log_configs =
        "CREATE TABLE IF NOT EXISTS logging_configs ("
        "  server_id VARCHAR(30) NOT NULL,"
        "  event_type VARCHAR(30) NOT NULL,"
        "  channel_id VARCHAR(30) NOT NULL,"
        "  PRIMARY KEY (server_id, event_type)"
        ")";
    if (mysql_query(mysql_, create_log_configs.c_str())) {
      std::cerr << "[bronx db] failed to create table 'logging_configs': "
                << mysql_error(mysql_) << "\n";
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
      std::cerr << "[bronx db] failed to create table 'logging_embeds': "
                << mysql_error(mysql_) << "\n";
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
      std::cerr << "[bronx db] failed to create table 'logging_webhooks': "
                << mysql_error(mysql_) << "\n";
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
      std::cerr << "[bronx db] failed to create table 'trusted_entities': "
                << mysql_error(mysql_) << "\n";
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
        "  PRIMARY KEY (server_id, target_type, target_name, restriction_type, "
        "entity_type, entity_id)"
        ")";
    if (mysql_query(mysql_, create_command_restrictions.c_str())) {
      std::cerr << "[bronx db] failed to create table 'command_restrictions': "
                << mysql_error(mysql_) << "\n";
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
      std::cerr << "[bronx db] failed to create table 'disabled_commands': "
                << mysql_error(mysql_) << "\n";
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
      std::cerr << "[bronx db] failed to create table 'moderation_logs': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Alter Table moderation_logs to add channel_id if it doesn't exist
    bool has_channel_id;
    if (mysql_query(mysql_, "SELECT channel_id FROM moderation_logs LIMIT 1") == 0) {
      has_channel_id = true;
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        mysql_free_result(res);
      }
    }
    if (!has_channel_id) {
      mysql_query(mysql_, "ALTER TABLE moderation_logs ADD COLUMN channel_id VARCHAR(30) DEFAULT NULL");
    }

    // Create Table command_usage_history
    std::string create_cmd_history =
        "CREATE TABLE IF NOT EXISTS command_usage_history ("
        "  id INT AUTO_INCREMENT PRIMARY KEY,"
        "  server_id VARCHAR(30) NOT NULL,"
        "  command_name VARCHAR(64) NOT NULL,"
        "  user_id VARCHAR(30) NOT NULL,"
        "  channel_id VARCHAR(30) NOT NULL,"
        "  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  INDEX idx_user (server_id, user_id),"
        "  INDEX idx_command (server_id, command_name)"
        ")";
    if (mysql_query(mysql_, create_cmd_history.c_str())) {
      std::cerr << "[bronx db] failed to create table 'command_usage_history': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    std::string create_reminders = "CREATE TABLE IF NOT EXISTS reminders ("
                                   "  id INT AUTO_INCREMENT PRIMARY KEY,"
                                   "  user_id VARCHAR(30) NOT NULL,"
                                   "  channel_id VARCHAR(30) NOT NULL,"
                                   "  message TEXT NOT NULL,"
                                   "  timestamp_set BIGINT UNSIGNED NOT NULL,"
                                   "  timestamp_due BIGINT UNSIGNED NOT NULL"
                                   ")";
    if (mysql_query(mysql_, create_reminders.c_str())) {
      std::cerr << "[bronx db] failed to create table 'reminders': "
                << mysql_error(mysql_) << "\n";
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
      std::cerr
          << "[bronx db] failed to create table 'action_message_configs': "
          << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table jail_configs
    std::string create_jail_configs =
        "CREATE TABLE IF NOT EXISTS jail_configs ("
        "  server_id VARCHAR(30) NOT NULL PRIMARY KEY,"
        "  jail_role_id VARCHAR(30) DEFAULT NULL,"
        "  jail_channel_id VARCHAR(30) DEFAULT NULL"
        ")";
    if (mysql_query(mysql_, create_jail_configs.c_str())) {
      std::cerr << "[bronx db] failed to create table 'jail_configs': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table active_jails
    std::string create_active_jails =
        "CREATE TABLE IF NOT EXISTS active_jails ("
        "  server_id VARCHAR(30) NOT NULL,"
        "  user_id VARCHAR(30) NOT NULL,"
        "  roles TEXT NOT NULL,"
        "  jailed_at BIGINT UNSIGNED NOT NULL,"
        "  expires_at BIGINT UNSIGNED NOT NULL,"
        "  PRIMARY KEY (server_id, user_id)"
        ")";
    if (mysql_query(mysql_, create_active_jails.c_str())) {
      std::cerr << "[bronx db] failed to create table 'active_jails': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table masquerade_configs
    std::string create_masq =
        "CREATE TABLE IF NOT EXISTS masquerade_configs ("
        "  server_id VARCHAR(30) NOT NULL PRIMARY KEY,"
        "  avatar VARCHAR(256) DEFAULT NULL,"
        "  colour VARCHAR(128) DEFAULT NULL,"
        "  name VARCHAR(32) DEFAULT NULL,"
        "  disabled TINYINT(1) NOT NULL DEFAULT 0,"
        "  disable_user TINYINT(1) NOT NULL DEFAULT 0,"
        "  disable_user_name TINYINT(1) NOT NULL DEFAULT 0,"
        "  disable_user_avatar TINYINT(1) NOT NULL DEFAULT 0,"
        "  disable_user_colour TINYINT(1) NOT NULL DEFAULT 0"
        ")";
    if (mysql_query(mysql_, create_masq.c_str())) {
      std::cerr << "[bronx db] failed to create table 'masquerade_configs': "
                << mysql_error(mysql_) << "\n";
      return false;
    }
    // Migration: add columns if they don't exist yet
    mysql_query(mysql_, "ALTER TABLE masquerade_configs ADD COLUMN IF NOT "
                        "EXISTS disabled TINYINT(1) NOT NULL DEFAULT 0");
    mysql_query(mysql_, "ALTER TABLE masquerade_configs ADD COLUMN IF NOT "
                        "EXISTS disable_user TINYINT(1) NOT NULL DEFAULT 0");
    mysql_query(mysql_,
                "ALTER TABLE masquerade_configs ADD COLUMN IF NOT EXISTS "
                "disable_user_name TINYINT(1) NOT NULL DEFAULT 0");
    mysql_query(mysql_,
                "ALTER TABLE masquerade_configs ADD COLUMN IF NOT EXISTS "
                "disable_user_avatar TINYINT(1) NOT NULL DEFAULT 0");
    mysql_query(mysql_,
                "ALTER TABLE masquerade_configs ADD COLUMN IF NOT EXISTS "
                "disable_user_colour TINYINT(1) NOT NULL DEFAULT 0");

    // Create Table user_masquerades
    std::string create_user_masq =
        "CREATE TABLE IF NOT EXISTS user_masquerades ("
        "  user_id VARCHAR(30) NOT NULL PRIMARY KEY,"
        "  avatar  VARCHAR(256) DEFAULT NULL,"
        "  colour  VARCHAR(128) DEFAULT NULL,"
        "  name    VARCHAR(32)  DEFAULT NULL"
        ")";
    if (mysql_query(mysql_, create_user_masq.c_str())) {
      std::cerr << "[bronx db] failed to create table 'user_masquerades': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table user_timezones
    std::string create_user_tz =
        "CREATE TABLE IF NOT EXISTS user_timezones ("
        "  user_id VARCHAR(30) NOT NULL PRIMARY KEY,"
        "  timezone VARCHAR(100) NOT NULL"
        ")";
    if (mysql_query(mysql_, create_user_tz.c_str())) {
      std::cerr << "[bronx db] failed to create table 'user_timezones': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table bot_ignored_users
    std::string create_bot_ignored =
        "CREATE TABLE IF NOT EXISTS bot_ignored_users ("
        "  user_id VARCHAR(30) NOT NULL PRIMARY KEY"
        ")";
    if (mysql_query(mysql_, create_bot_ignored.c_str())) {
      std::cerr << "[bronx db] failed to create table 'bot_ignored_users': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table user_rank_card
    std::string create_urc = 
        "CREATE TABLE IF NOT EXISTS user_rank_card ("
        "  user_id VARCHAR(30) NOT NULL PRIMARY KEY,"
        "  style VARCHAR(10) NOT NULL DEFAULT 'image',"
        "  bg_color VARCHAR(30) DEFAULT NULL,"
        "  bg_image VARCHAR(512) DEFAULT NULL,"
        "  bar_active VARCHAR(30) DEFAULT NULL,"
        "  bar_dimmed VARCHAR(30) DEFAULT NULL,"
        "  text_color VARCHAR(30) DEFAULT NULL,"
        "  font_name VARCHAR(50) DEFAULT NULL"
        ")";
    if (mysql_query(mysql_, create_urc.c_str())) {
      std::cerr << "[bronx db] failed to create table 'user_rank_card': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table tuppers
    std::string create_tuppers = "CREATE TABLE IF NOT EXISTS tuppers ("
                                 "  id INT AUTO_INCREMENT PRIMARY KEY,"
                                 "  user_id VARCHAR(30) NOT NULL,"
                                 "  name VARCHAR(32) NOT NULL,"
                                 "  avatar_url VARCHAR(256) NOT NULL,"
                                 "  prefix VARCHAR(30) DEFAULT NULL,"
                                 "  suffix VARCHAR(30) DEFAULT NULL,"
                                 "  UNIQUE KEY uq_user_tupper (user_id, name)"
                                 ")";
    if (mysql_query(mysql_, create_tuppers.c_str())) {
      std::cerr << "[bronx db] failed to create table 'tuppers': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table dashboard_tokens
    std::string create_dash_tokens =
        "CREATE TABLE IF NOT EXISTS dashboard_tokens ("
        "  token VARCHAR(64) NOT NULL PRIMARY KEY,"
        "  user_id VARCHAR(30) NOT NULL,"
        "  user_name VARCHAR(64) DEFAULT NULL,"
        "  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  expires_at TIMESTAMP NOT NULL"
        ")";
    if (mysql_query(mysql_, create_dash_tokens.c_str())) {
      std::cerr << "[bronx db] failed to create table 'dashboard_tokens': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table servers
    std::string create_servers_table =
        "CREATE TABLE IF NOT EXISTS servers ("
        "  server_id VARCHAR(30) NOT NULL PRIMARY KEY,"
        "  name VARCHAR(100) NOT NULL,"
        "  icon_id VARCHAR(100) DEFAULT NULL,"
        "  banner_id VARCHAR(100) DEFAULT NULL,"
        "  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE "
        "CURRENT_TIMESTAMP"
        ")";
    if (mysql_query(mysql_, create_servers_table.c_str())) {
      std::cerr << "[bronx db] failed to create table 'servers': "
                << mysql_error(mysql_) << "\n";
      return false;
    }
    mysql_query(mysql_, "ALTER TABLE servers ADD COLUMN IF NOT EXISTS banner_id VARCHAR(100) DEFAULT NULL");

    // Create Table server_members
    std::string create_server_members =
        "CREATE TABLE IF NOT EXISTS server_members ("
        "  server_id VARCHAR(30) NOT NULL,"
        "  user_id VARCHAR(30) NOT NULL,"
        "  is_owner TINYINT(1) NOT NULL DEFAULT 0,"
        "  is_admin TINYINT(1) NOT NULL DEFAULT 0,"
        "  PRIMARY KEY (server_id, user_id)"
        ")";
    if (mysql_query(mysql_, create_server_members.c_str())) {
      std::cerr << "[bronx db] failed to create table 'server_members': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table tupper_proxies
    std::string create_tupper_proxies =
        "CREATE TABLE IF NOT EXISTS tupper_proxies ("
        "  message_id VARCHAR(30) NOT NULL PRIMARY KEY,"
        "  user_id VARCHAR(30) NOT NULL,"
        "  tupper_name VARCHAR(32) NOT NULL"
        ")";
    if (mysql_query(mysql_, create_tupper_proxies.c_str())) {
      std::cerr << "[bronx db] failed to create table 'tupper_proxies': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table guild_leveling_config
    std::string create_glc =
        "CREATE TABLE IF NOT EXISTS guild_leveling_config ("
        "  server_id VARCHAR(30) NOT NULL PRIMARY KEY,"
        "  enabled TINYINT(1) NOT NULL DEFAULT 0,"
        "  reward_coins TINYINT(1) NOT NULL DEFAULT 1,"
        "  coins_per_message INT NOT NULL DEFAULT 1,"
        "  min_xp_per_message INT NOT NULL DEFAULT 15,"
        "  max_xp_per_message INT NOT NULL DEFAULT 25,"
        "  min_message_chars INT NOT NULL DEFAULT 5,"
        "  max_message_chars INT NOT NULL DEFAULT 2000,"
        "  xp_cooldown_seconds INT NOT NULL DEFAULT 60,"
        "  announcement_channel VARCHAR(30) DEFAULT NULL,"
        "  announce_levelup TINYINT(1) NOT NULL DEFAULT 1,"
        "  level_up_message VARCHAR(500) NOT NULL DEFAULT '{name} reached "
        "**Level {level}**!',"
        "  sticky_roles TINYINT(1) NOT NULL DEFAULT 1,"
        "  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE "
        "CURRENT_TIMESTAMP"
        ")";
    if (mysql_query(mysql_, create_glc.c_str())) {
      std::cerr << "[bronx db] failed to create table 'guild_leveling_config': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Migration: add max_message_chars if it doesn't exist
    mysql_query(mysql_, "ALTER TABLE guild_leveling_config ADD COLUMN IF NOT EXISTS max_message_chars INT NOT NULL DEFAULT 2000");

    // Create Table guild_level_roles
    std::string create_glr =
        "CREATE TABLE IF NOT EXISTS guild_level_roles ("
        "  id INT AUTO_INCREMENT PRIMARY KEY,"
        "  server_id VARCHAR(30) NOT NULL,"
        "  level INT UNSIGNED NOT NULL,"
        "  role_id VARCHAR(30) NOT NULL,"
        "  role_name VARCHAR(100) NOT NULL,"
        "  remove_previous TINYINT(1) NOT NULL DEFAULT 0,"
        "  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  UNIQUE KEY uq_server_level (server_id, level)"
        ")";
    if (mysql_query(mysql_, create_glr.c_str())) {
      std::cerr << "[bronx db] failed to create table 'guild_level_roles': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table guild_xp_blacklist
    std::string create_gxb =
        "CREATE TABLE IF NOT EXISTS guild_xp_blacklist ("
        "  id INT AUTO_INCREMENT PRIMARY KEY,"
        "  server_id VARCHAR(30) NOT NULL,"
        "  target_type VARCHAR(10) NOT NULL,"
        "  target_id VARCHAR(30) NOT NULL,"
        "  added_by VARCHAR(30) NOT NULL,"
        "  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  UNIQUE KEY uq_server_target (server_id, target_type, target_id)"
        ")";
    if (mysql_query(mysql_, create_gxb.c_str())) {
      std::cerr << "[bronx db] failed to create table 'guild_xp_blacklist': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table user_xp
    std::string create_uxp = "CREATE TABLE IF NOT EXISTS user_xp ("
                             "  user_id VARCHAR(30) NOT NULL,"
                             "  server_id VARCHAR(30) NOT NULL DEFAULT '0',"
                             "  total_xp BIGINT UNSIGNED NOT NULL DEFAULT 0,"
                             "  level INT UNSIGNED NOT NULL DEFAULT 1,"
                             "  last_message_xp TIMESTAMP NULL DEFAULT NULL,"
                             "  PRIMARY KEY (user_id, server_id),"
                             "  INDEX idx_server_xp (server_id, total_xp DESC)"
                             ")";
    if (mysql_query(mysql_, create_uxp.c_str())) {
      std::cerr << "[bronx db] failed to create table 'user_xp': "
                << mysql_error(mysql_) << "\n";
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
      std::cerr << "[bronx db] failed to create table 'antiraid_configs': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table honeypot_configs
    std::string create_hp =
        "CREATE TABLE IF NOT EXISTS honeypot_configs ("
        "  server_id VARCHAR(30) NOT NULL PRIMARY KEY,"
        "  channel_id VARCHAR(30) DEFAULT NULL,"
        "  honeypot_message TEXT DEFAULT NULL,"
        "  disclaimer_message TEXT DEFAULT NULL,"
        "  action VARCHAR(50) NOT NULL DEFAULT 'ban',"
        "  message_triggers TINYINT(1) NOT NULL DEFAULT 1,"
        "  react_triggers TINYINT(1) NOT NULL DEFAULT 0,"
        "  enabled TINYINT(1) NOT NULL DEFAULT 0"
        ")";
    if (mysql_query(mysql_, create_hp.c_str())) {
      std::cerr << "[bronx db] failed to create table 'honeypot_configs': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Migration: widen action columns to support comma-separated multi-action
    // lists (e.g. "delete,warn")
    mysql_query(mysql_, "ALTER TABLE antiraid_configs MODIFY COLUMN "
                        "joins_action VARCHAR(100) NOT NULL DEFAULT 'none'");
    mysql_query(mysql_, "ALTER TABLE antiraid_configs MODIFY COLUMN "
                        "spam_action VARCHAR(100) NOT NULL DEFAULT 'none'");
    mysql_query(mysql_,
                "ALTER TABLE antiraid_configs MODIFY COLUMN "
                "default_profile_action VARCHAR(100) NOT NULL DEFAULT 'none'");

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

    // Create Table rss_feeds
    std::string create_rss =
        "CREATE TABLE IF NOT EXISTS rss_feeds ("
        "  id INT AUTO_INCREMENT PRIMARY KEY,"
        "  server_id VARCHAR(30) NOT NULL,"
        "  url VARCHAR(1024) NOT NULL,"
        "  channels TEXT NOT NULL,"
        "  last_checked BIGINT UNSIGNED NOT NULL DEFAULT 0,"
        "  seen_items TEXT DEFAULT NULL,"
        "  check_interval INT UNSIGNED NOT NULL DEFAULT 1200,"
        "  INDEX idx_rss_server (server_id)"
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
      std::cerr << "[bronx db] failed to create table 'ticket_configs': "
                << mysql_error(mysql_) << "\n";
      return false;
    }
    // Migration: add member_role_id for existing installs
    mysql_query(mysql_, "ALTER TABLE ticket_configs ADD COLUMN IF NOT EXISTS "
                        "member_role_id VARCHAR(30) DEFAULT NULL");

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
      std::cerr << "[bronx db] failed to create table 'tickets': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table application_panels
    std::string create_app_panels =
        "CREATE TABLE IF NOT EXISTS application_panels ("
        "  server_id VARCHAR(30) NOT NULL,"
        "  name VARCHAR(50) NOT NULL,"
        "  trigger_command VARCHAR(50) DEFAULT '',"
        "  trigger_emoji VARCHAR(100) DEFAULT '',"
        "  modal_channel_id VARCHAR(30) DEFAULT '',"
        "  modal_message_id VARCHAR(30) DEFAULT '',"
        "  modal_content TEXT DEFAULT NULL,"
        "  modal_embed_json TEXT DEFAULT NULL,"
        "  output_channel_id VARCHAR(30) DEFAULT '',"
        "  mode VARCHAR(20) NOT NULL DEFAULT 'post',"
        "  view_roles TEXT DEFAULT NULL,"
        "  view_users TEXT DEFAULT NULL,"
        "  view_permissions TEXT DEFAULT NULL,"
        "  member_role_id VARCHAR(30) DEFAULT NULL,"
        "  naming_scheme VARCHAR(100) DEFAULT 'app-{number}',"
        "  welcome_content TEXT DEFAULT NULL,"
        "  close_emoji VARCHAR(100) DEFAULT '🔒',"
        "  require_role TINYINT(1) NOT NULL DEFAULT 0,"
        "  require_role_id VARCHAR(30) DEFAULT '',"
        "  role_to_give TINYINT(1) NOT NULL DEFAULT 0,"
        "  role_to_give_id VARCHAR(30) DEFAULT '',"
        "  end_message TEXT DEFAULT NULL,"
        "  questions JSON DEFAULT NULL,"
        "  app_counter INT NOT NULL DEFAULT 0,"
        "  PRIMARY KEY (server_id, name)"
        ")";
    if (mysql_query(mysql_, create_app_panels.c_str())) {
      std::cerr << "[bronx db] failed to create table 'application_panels': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    mysql_query(mysql_, "ALTER TABLE application_panels ADD COLUMN IF NOT EXISTS role_to_give TINYINT(1) NOT NULL DEFAULT 0");
    mysql_query(mysql_, "ALTER TABLE application_panels ADD COLUMN IF NOT EXISTS role_to_give_id VARCHAR(30) DEFAULT ''");
    mysql_query(mysql_, "ALTER TABLE application_panels ADD COLUMN IF NOT EXISTS end_message TEXT DEFAULT NULL");

    // Create Table application_submissions
    std::string create_app_submissions =
        "CREATE TABLE IF NOT EXISTS application_submissions ("
        "  id INT AUTO_INCREMENT PRIMARY KEY,"
        "  server_id VARCHAR(30) NOT NULL,"
        "  panel_name VARCHAR(50) NOT NULL,"
        "  user_id VARCHAR(30) NOT NULL,"
        "  submitted_at BIGINT UNSIGNED NOT NULL DEFAULT 0,"
        "  answers_json TEXT DEFAULT NULL,"
        "  status VARCHAR(20) NOT NULL DEFAULT 'pending',"
        "  output_message_id VARCHAR(30) DEFAULT '',"
        "  ticket_channel_id VARCHAR(30) DEFAULT '',"
        "  INDEX idx_app_sub_server (server_id, panel_name),"
        "  INDEX idx_app_sub_user (user_id)"
        ")";
    if (mysql_query(mysql_, create_app_submissions.c_str())) {
      std::cerr << "[bronx db] failed to create table 'application_submissions': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    //  counting_configs
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
      std::cerr << "[bronx db] failed to create table 'counting_configs': "
                << mysql_error(mysql_) << "\n";
      return false;
    }
    // Migration: add warn_double_count for existing installs
    mysql_query(mysql_, "ALTER TABLE counting_configs ADD COLUMN IF NOT EXISTS "
                        "warn_double_count TINYINT(1) NOT NULL DEFAULT 0");

    //  counting_stats
    std::string create_counting_stats =
        "CREATE TABLE IF NOT EXISTS counting_stats ("
        "  server_id VARCHAR(30) NOT NULL,"
        "  user_id VARCHAR(30) NOT NULL,"
        "  correct_counts BIGINT NOT NULL DEFAULT 0,"
        "  failed_counts BIGINT NOT NULL DEFAULT 0,"
        "  PRIMARY KEY (server_id, user_id)"
        ")";
    if (mysql_query(mysql_, create_counting_stats.c_str())) {
      std::cerr << "[bronx db] failed to create table 'counting_stats': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table user_afk
    std::string create_user_afk = "CREATE TABLE IF NOT EXISTS user_afk ("
                                  "  user_id VARCHAR(30) NOT NULL PRIMARY KEY,"
                                  "  custom_template TEXT DEFAULT NULL,"
                                  "  is_afk TINYINT(1) NOT NULL DEFAULT 0,"
                                  "  reason TEXT DEFAULT NULL,"
                                  "  since BIGINT NOT NULL DEFAULT 0"
                                  ")";
    if (mysql_query(mysql_, create_user_afk.c_str())) {
      std::cerr << "[bronx db] failed to create table 'user_afk': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table server_listings
    std::string create_server_listings =
        "CREATE TABLE IF NOT EXISTS server_listings ("
        "  server_id VARCHAR(30) NOT NULL PRIMARY KEY,"
        "  name VARCHAR(100) NOT NULL,"
        "  mode VARCHAR(20) NOT NULL DEFAULT 'disabled',"
        "  tags TEXT DEFAULT NULL,"
        "  description TEXT DEFAULT NULL,"
        "  embed_json TEXT DEFAULT NULL,"
        "  invite_url TEXT DEFAULT NULL,"
        "  advertising_channel_id VARCHAR(100) DEFAULT NULL,"
        "  status VARCHAR(20) NOT NULL DEFAULT 'pending',"
        "  last_bumped_at BIGINT UNSIGNED NOT NULL DEFAULT 0,"
        "  created_at BIGINT UNSIGNED NOT NULL DEFAULT 0,"
        "  bump_count INT NOT NULL DEFAULT 0"
        ")";
    if (mysql_query(mysql_, create_server_listings.c_str())) {
      std::cerr << "[bronx db] failed to create table 'server_listings': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    mysql_query(mysql_, "ALTER TABLE server_listings MODIFY COLUMN advertising_channel_id VARCHAR(100) DEFAULT NULL");

    // Migration: add active_preset to user_afk table
    mysql_query(mysql_, "ALTER TABLE user_afk ADD COLUMN IF NOT EXISTS "
                        "active_preset VARCHAR(50) NOT NULL DEFAULT 'default'");

    // Create Table user_afk_presets
    std::string create_user_afk_presets =
        "CREATE TABLE IF NOT EXISTS user_afk_presets ("
        "  user_id VARCHAR(30) NOT NULL,"
        "  preset_name VARCHAR(50) NOT NULL,"
        "  template_json TEXT DEFAULT NULL,"
        "  PRIMARY KEY (user_id, preset_name)"
        ")";
    if (mysql_query(mysql_, create_user_afk_presets.c_str())) {
      std::cerr << "[bronx db] failed to create table 'user_afk_presets': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table starboards
  std::string create_starboards =
      "CREATE TABLE IF NOT EXISTS starboards ("
      "  server_id VARCHAR(30) NOT NULL,"
      "  board_name VARCHAR(50) NOT NULL,"
      "  channel_id VARCHAR(30) NOT NULL DEFAULT '',"
      "  threshold INT NOT NULL DEFAULT 3,"
      "  emojis TEXT NOT NULL,"
      "  allow_self TINYINT(1) NOT NULL DEFAULT 1,"
        "  delete_below_threshold TINYINT(1) NOT NULL DEFAULT 1,"
        "  use_embed TINYINT(1) NOT NULL DEFAULT 1,"
        "  message_content TEXT,"
        "  embed_title TEXT,"
        "  embed_description TEXT,"
        "  PRIMARY KEY (server_id, board_name)"
        ")";
    if (mysql_query(mysql_, create_starboards.c_str())) {
      std::cerr << "[bronx db] failed to create table 'starboards': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Alter table starboards to add columns if they don't exist
    mysql_query(mysql_, "ALTER TABLE starboards ADD COLUMN IF NOT EXISTS use_embed TINYINT(1) NOT NULL DEFAULT 1");
    mysql_query(mysql_, "ALTER TABLE starboards ADD COLUMN IF NOT EXISTS message_content TEXT");
    mysql_query(mysql_, "ALTER TABLE starboards ADD COLUMN IF NOT EXISTS embed_title TEXT");
    mysql_query(mysql_, "ALTER TABLE starboards ADD COLUMN IF NOT EXISTS embed_description TEXT");
    mysql_query(mysql_, "ALTER TABLE starboards ADD COLUMN IF NOT EXISTS delete_below_threshold TINYINT(1) NOT NULL DEFAULT 1");
    // Widen emojis column from VARCHAR(255) to TEXT to support boards with many custom emojis
    mysql_query(mysql_, "ALTER TABLE starboards MODIFY COLUMN emojis TEXT NOT NULL DEFAULT '⭐'");

    // Create Table starboard_messages
    std::string create_starboard_messages =
        "CREATE TABLE IF NOT EXISTS starboard_messages ("
        "  server_id VARCHAR(30) NOT NULL,"
        "  board_name VARCHAR(50) NOT NULL,"
        "  original_message_id VARCHAR(30) NOT NULL,"
        "  starboard_message_id VARCHAR(30) NOT NULL,"
        "  PRIMARY KEY (server_id, board_name, original_message_id)"
        ")";
    if (mysql_query(mysql_, create_starboard_messages.c_str())) {
      std::cerr << "[bronx db] failed to create table 'starboard_messages': "
                << mysql_error(mysql_) << "\n";
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
      std::cerr << "[bronx db] failed to create table 'roblox_links': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table lastfm_links
    std::string create_lastfm_links =
        "CREATE TABLE IF NOT EXISTS lastfm_links ("
        "  user_id VARCHAR(30) NOT NULL PRIMARY KEY,"
        "  lastfm_username VARCHAR(100) NOT NULL,"
        "  linked_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ")";
    if (mysql_query(mysql_, create_lastfm_links.c_str())) {
      std::cerr << "[bronx db] failed to create table 'lastfm_links': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table account_links
    std::string create_account_links =
        "CREATE TABLE IF NOT EXISTS account_links ("
        "  stoat_id VARCHAR(30) DEFAULT NULL,"
        "  fluxer_id VARCHAR(30) DEFAULT NULL,"
        "  discord_id VARCHAR(30) DEFAULT NULL,"
        "  UNIQUE KEY (stoat_id),"
        "  UNIQUE KEY (fluxer_id),"
        "  UNIQUE KEY (discord_id)"
        ")";
    if (mysql_query(mysql_, create_account_links.c_str())) {
      std::cerr << "[bronx db] failed to create table 'account_links': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table ban_links
    std::string create_ban_links =
        "CREATE TABLE IF NOT EXISTS ban_links ("
        "  guild_id VARCHAR(36) NOT NULL,"
        "  target_guild_id VARCHAR(36) NOT NULL,"
        "  PRIMARY KEY (guild_id, target_guild_id)"
        ")";
    if (mysql_query(mysql_, create_ban_links.c_str())) {
      std::cerr << "[bronx db] failed to create table 'ban_links': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Load counting configs into cache
    {
      std::string sel_counting =
          "SELECT server_id, channel_id, current_number, last_user_id, "
          "fail_role_id, fail_role_duration, passive, highest_count, "
          "warn_double_count FROM counting_configs";
      if (mysql_query(mysql_, sel_counting.c_str()) == 0) {
        MYSQL_RES *res = mysql_store_result(mysql_);
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
            cfg.warn_double_count =
                row[8] ? (std::string(row[8]) != "0") : false;
            counting_cache_[cfg.server_id] = cfg;
          }
          mysql_free_result(res);
        }
      }
    }
    // Load counting stats into cache
    {
      std::string sel_stats = "SELECT server_id, user_id, correct_counts, "
                              "failed_counts FROM counting_stats";
      if (mysql_query(mysql_, sel_stats.c_str()) == 0) {
        MYSQL_RES *res = mysql_store_result(mysql_);
        if (res) {
          MYSQL_ROW row;
          std::lock_guard<std::shared_mutex> lk(counting_stats_cache_mutex_);
          while ((row = mysql_fetch_row(res))) {
            CountingStats st;
            st.server_id = row[0] ? row[0] : "";
            st.user_id = row[1] ? row[1] : "";
            st.correct_counts = row[2] ? std::stoll(row[2]) : 0;
            st.failed_counts = row[3] ? std::stoll(row[3]) : 0;
            std::string key = st.server_id + ":" + st.user_id;
            counting_stats_cache_[key] = st;
          }
          mysql_free_result(res);
        }
      }
    }

    if (mysql_query(mysql_, create_rm.c_str())) {
      std::cerr << "[bronx db] failed to create table 'recurring_messages': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    if (mysql_query(mysql_, create_rss.c_str())) {
      std::cerr << "[bronx db] failed to create table 'rss_feeds': "
                << mysql_error(mysql_) << "\n";
      return false;
    }
    // Migration: add check_interval column to rss_feeds if it doesn't exist yet
    mysql_query(mysql_, "ALTER TABLE rss_feeds ADD COLUMN IF NOT EXISTS "
                        "check_interval INT UNSIGNED NOT NULL DEFAULT 1200");

    // Create Table social_subscriptions
    mysql_query(mysql_,
                "CREATE TABLE IF NOT EXISTS social_subscriptions ("
                "  id INT AUTO_INCREMENT PRIMARY KEY,"
                "  server_id VARCHAR(30) NOT NULL,"
                "  platform VARCHAR(20) NOT NULL,"
                "  channel_handle VARCHAR(256) NOT NULL,"
                "  channels TEXT NOT NULL DEFAULT '',"
                "  events VARCHAR(256) NOT NULL DEFAULT 'live,vod,post',"
                "  last_checked BIGINT UNSIGNED NOT NULL DEFAULT 0,"
                "  seen_items TEXT DEFAULT NULL,"
                "  INDEX idx_social_server (server_id)"
                ")");

    // Create Table social_embed_configs
    mysql_query(mysql_, "CREATE TABLE IF NOT EXISTS social_embed_configs ("
                        "  sub_id INT NOT NULL,"
                        "  event_type VARCHAR(20) NOT NULL,"
                        "  message_type VARCHAR(10) NOT NULL DEFAULT 'embed',"
                        "  content TEXT DEFAULT NULL,"
                        "  embed_json JSON DEFAULT NULL,"
                        "  PRIMARY KEY (sub_id, event_type)"
                        ")");

    // Create Table server_command_aliases
    mysql_query(mysql_, "CREATE TABLE IF NOT EXISTS server_command_aliases ("
                        "  server_id VARCHAR(30) NOT NULL,"
                        "  alias VARCHAR(100) NOT NULL,"
                        "  target VARCHAR(100) NOT NULL,"
                        "  PRIMARY KEY (server_id, alias)"
                        ")");

    // Create Table user_command_aliases
    mysql_query(mysql_, "CREATE TABLE IF NOT EXISTS user_command_aliases ("
                        "  user_id VARCHAR(30) NOT NULL,"
                        "  alias VARCHAR(100) NOT NULL,"
                        "  target VARCHAR(100) NOT NULL,"
                        "  PRIMARY KEY (user_id, alias)"
                        ")");

    // Create Table server_alias_settings
    mysql_query(mysql_, "CREATE TABLE IF NOT EXISTS server_alias_settings ("
                        "  server_id VARCHAR(30) NOT NULL PRIMARY KEY,"
                        "  user_aliases_disabled TINYINT(1) NOT NULL DEFAULT 0"
                        ")");

    //  Word Filter tables
    mysql_query(mysql_, "CREATE TABLE IF NOT EXISTS word_filter_groups ("
                        "  id INT AUTO_INCREMENT PRIMARY KEY,"
                        "  server_id VARCHAR(30) NOT NULL,"
                        "  name VARCHAR(100) NOT NULL,"
                        "  patterns JSON NOT NULL DEFAULT ('[]'),"
                        "  server_wide TINYINT(1) NOT NULL DEFAULT 1,"
                        "  enabled_channels JSON NOT NULL DEFAULT ('[]'),"
                        "  disabled_channels JSON NOT NULL DEFAULT ('[]'),"
                        "  UNIQUE KEY uq_filter_group (server_id, name),"
                        "  INDEX idx_filter_server (server_id)"
                        ")");

    mysql_query(mysql_, "CREATE TABLE IF NOT EXISTS word_filter_whitelist ("
                        "  server_id VARCHAR(30) NOT NULL PRIMARY KEY,"
                        "  entries JSON NOT NULL DEFAULT ('[]')"
                        ")");

    // Create Fishing Economy Tables
    mysql_query(mysql_,
                "CREATE TABLE IF NOT EXISTS user_fishing ("
                "  user_id VARCHAR(64) PRIMARY KEY,"
                "  rod_id VARCHAR(64) NOT NULL DEFAULT 'wooden_rod',"
                "  bait_id VARCHAR(64) NOT NULL DEFAULT 'worm',"
                "  fish_prestige INT NOT NULL DEFAULT 0,"
                "  prestige_tokens INT NOT NULL DEFAULT 0,"
                "  total_catches INT NOT NULL DEFAULT 0,"
                "  total_weight DOUBLE NOT NULL DEFAULT 0.0,"
                "  total_earned BIGINT NOT NULL DEFAULT 0,"
                "  rarest_catch_name VARCHAR(128) NOT NULL DEFAULT 'None',"
                "  rarest_catch_odds DOUBLE NOT NULL DEFAULT 1.0,"
                "  rod_luck_level INT NOT NULL DEFAULT 0,"
                "  rod_cd_level INT NOT NULL DEFAULT 0,"
                "  rod_double_level INT NOT NULL DEFAULT 0,"
                "  rod_rare_level INT NOT NULL DEFAULT 0,"
                "  active_rod_effect VARCHAR(64) NOT NULL DEFAULT 'none',"
                "  last_fish_time TIMESTAMP NULL DEFAULT NULL,"
                "  temp_luck_boost DOUBLE NOT NULL DEFAULT 1.0,"
                "  temp_luck_casts INT NOT NULL DEFAULT 0"
                ")");

    mysql_query(mysql_, "ALTER TABLE user_fishing ADD COLUMN IF NOT EXISTS "
                        "event_tokens INT NOT NULL DEFAULT 0");
    mysql_query(mysql_, "ALTER TABLE user_fishing ADD COLUMN IF NOT EXISTS "
                        "active_pet_id VARCHAR(64) NOT NULL DEFAULT 'none'");
    mysql_query(mysql_, "ALTER TABLE user_fishing ADD COLUMN IF NOT EXISTS "
                        "perm_luck DOUBLE NOT NULL DEFAULT 0.0");
    mysql_query(mysql_, "ALTER TABLE user_fishing ADD COLUMN IF NOT EXISTS "
                        "perm_val_mult DOUBLE NOT NULL DEFAULT 1.0");
    mysql_query(mysql_, "ALTER TABLE user_fishing ADD COLUMN IF NOT EXISTS "
                        "perm_wt_mult DOUBLE NOT NULL DEFAULT 1.0");
    mysql_query(mysql_, "ALTER TABLE user_fishing ADD COLUMN IF NOT EXISTS "
                        "perm_cd_reduction INT NOT NULL DEFAULT 0");

    mysql_query(mysql_,
                "CREATE TABLE IF NOT EXISTS user_pets ("
                "  user_id VARCHAR(64) NOT NULL,"
                "  pet_id VARCHAR(64) NOT NULL,"
                "  acquired_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                "  PRIMARY KEY (user_id, pet_id)"
                ")");

    mysql_query(mysql_, "CREATE TABLE IF NOT EXISTS user_fishdex_claims ("
                        "  user_id VARCHAR(64) NOT NULL,"
                        "  rarity VARCHAR(64) NOT NULL,"
                        "  PRIMARY KEY (user_id, rarity)"
                        ")");

    mysql_query(mysql_, "CREATE TABLE IF NOT EXISTS global_fish_bosses ("
                        "  boss_type VARCHAR(32) PRIMARY KEY,"
                        "  boss_name VARCHAR(128) NOT NULL,"
                        "  season_theme VARCHAR(32) NOT NULL,"
                        "  current_hp BIGINT NOT NULL,"
                        "  max_hp BIGINT NOT NULL,"
                        "  req_type VARCHAR(64) NOT NULL,"
                        "  req_progress BIGINT NOT NULL DEFAULT 0,"
                        "  req_target BIGINT NOT NULL DEFAULT 1000"
                        ")");

    mysql_query(mysql_, "CREATE TABLE IF NOT EXISTS user_boss_contributions ("
                        "  user_id VARCHAR(64) NOT NULL,"
                        "  boss_type VARCHAR(32) NOT NULL,"
                        "  contribution_amount BIGINT NOT NULL DEFAULT 0,"
                        "  PRIMARY KEY (user_id, boss_type)"
                        ")");

    mysql_query(mysql_,
                "CREATE TABLE IF NOT EXISTS user_fish_inventory ("
                "  id INT AUTO_INCREMENT PRIMARY KEY,"
                "  user_id VARCHAR(64) NOT NULL,"
                "  fish_id VARCHAR(64) NOT NULL,"
                "  fish_name VARCHAR(128) NOT NULL,"
                "  rarity VARCHAR(32) NOT NULL,"
                "  odds_percent DOUBLE NOT NULL,"
                "  weight DOUBLE NOT NULL,"
                "  value BIGINT NOT NULL,"
                "  caught_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                "  INDEX idx_fish_inv_user (user_id)"
                ")");

    mysql_query(mysql_, "CREATE TABLE IF NOT EXISTS user_fish_baits ("
                        "  user_id VARCHAR(64) NOT NULL,"
                        "  bait_id VARCHAR(64) NOT NULL,"
                        "  quantity INT NOT NULL DEFAULT 0,"
                        "  PRIMARY KEY (user_id, bait_id)"
                        ")");

    mysql_query(mysql_, "CREATE TABLE IF NOT EXISTS user_unlocked_rods ("
                        "  user_id VARCHAR(64) NOT NULL,"
                        "  rod_id VARCHAR(64) NOT NULL,"
                        "  PRIMARY KEY (user_id, rod_id)"
                        ")");

    // Create General Economy Items and Custom Catalog Tables
    mysql_query(mysql_, "CREATE TABLE IF NOT EXISTS user_economy_items ("
                        "  user_id VARCHAR(64) NOT NULL,"
                        "  item_id VARCHAR(64) NOT NULL,"
                        "  quantity INT NOT NULL DEFAULT 1,"
                        "  uses_left INT NOT NULL DEFAULT 0,"
                        "  active_until BIGINT NOT NULL DEFAULT 0,"
                        "  PRIMARY KEY (user_id, item_id)"
                        ")");

    mysql_query(mysql_, "CREATE TABLE IF NOT EXISTS custom_shop_items ("
                        "  item_id VARCHAR(64) PRIMARY KEY,"
                        "  name VARCHAR(128) NOT NULL,"
                        "  type VARCHAR(64) NOT NULL DEFAULT 'general',"
                        "  cost BIGINT NOT NULL DEFAULT 0,"
                        "  description TEXT DEFAULT NULL,"
                        "  uses_default INT NOT NULL DEFAULT 0,"
                        "  duration_seconds INT NOT NULL DEFAULT 0"
                        ")");

    mysql_query(mysql_, "CREATE TABLE IF NOT EXISTS custom_rods ("
                        "  rod_id VARCHAR(64) PRIMARY KEY,"
                        "  name VARCHAR(128) NOT NULL,"
                        "  cost BIGINT NOT NULL DEFAULT 0,"
                        "  token_cost INT NOT NULL DEFAULT 0,"
                        "  req_prestige INT NOT NULL DEFAULT 0,"
                        "  luck_mult DOUBLE NOT NULL DEFAULT 1.0,"
                        "  base_cd INT NOT NULL DEFAULT 30,"
                        "  description TEXT DEFAULT NULL"
                        ")");

    mysql_query(mysql_, "CREATE TABLE IF NOT EXISTS custom_baits ("
                        "  bait_id VARCHAR(64) PRIMARY KEY,"
                        "  name VARCHAR(128) NOT NULL,"
                        "  pack_size INT NOT NULL DEFAULT 10,"
                        "  cost BIGINT NOT NULL DEFAULT 0,"
                        "  token_cost INT NOT NULL DEFAULT 0,"
                        "  req_prestige INT NOT NULL DEFAULT 0,"
                        "  luck_bonus DOUBLE NOT NULL DEFAULT 0.0,"
                        "  cd_delta INT NOT NULL DEFAULT 0,"
                        "  description TEXT DEFAULT NULL"
                        ")");

    mysql_query(mysql_, "CREATE TABLE IF NOT EXISTS custom_fish ("
                        "  fish_id VARCHAR(64) PRIMARY KEY,"
                        "  name VARCHAR(128) NOT NULL,"
                        "  rarity VARCHAR(32) NOT NULL DEFAULT 'Common',"
                        "  weight_min DOUBLE NOT NULL DEFAULT 1.0,"
                        "  weight_max DOUBLE NOT NULL DEFAULT 5.0,"
                        "  val_min BIGINT NOT NULL DEFAULT 50,"
                        "  val_max BIGINT NOT NULL DEFAULT 150,"
                        "  base_odds DOUBLE NOT NULL DEFAULT 0.50"
                        ")");

    // Load existing prefixes into cache
    std::string select_query = "SELECT target_type, target_id, prefixes, "
                               "default_enabled FROM prefixes";
    if (mysql_query(mysql_, select_query.c_str())) {
      std::cerr << "[bronx db] failed to load prefixes: " << mysql_error(mysql_)
                << "\n";
      return false;
    }

    MYSQL_RES *result = mysql_store_result(mysql_);
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
            for (const auto &item : parsed) {
              if (item.is_string()) {
                loaded_prefixes.push_back(item.get<std::string>());
              }
            }
          }
        } catch (const std::exception &e) {
          std::cerr << "[bronx db] json parse error for target " << t_type
                    << ":" << t_id << ": " << e.what() << "\n";
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
    std::string select_rr_msgs =
        "SELECT message_id, channel_id, server_id, modal_name, embed_json, "
        "type FROM rr_messages";
    if (mysql_query(mysql_, select_rr_msgs.c_str())) {
      std::cerr << "[bronx db] failed to load rr_messages: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    MYSQL_RES *rr_msg_res = mysql_store_result(mysql_);
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
    std::string select_rr_rules =
        "SELECT message_id, emoji_id, role_id, rule_type FROM rr_rules";
    if (mysql_query(mysql_, select_rr_rules.c_str())) {
      std::cerr << "[bronx db] failed to load rr_rules: " << mysql_error(mysql_)
                << "\n";
      return false;
    }

    MYSQL_RES *rr_rule_res = mysql_store_result(mysql_);
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
    std::string select_wl = "SELECT server_id, welcome_channel_id, "
                            "welcome_type, welcome_content, welcome_embed, "
                            "leave_channel_id, leave_type, leave_content, "
                            "leave_embed, autorole_role_id, "
                            "welcome_dm_type, welcome_dm_content, "
                            "welcome_dm_embed FROM welcome_leave_configs";
    if (mysql_query(mysql_, select_wl.c_str())) {
      std::cerr << "[bronx db] failed to load welcome_leave_configs: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    MYSQL_RES *wl_res = mysql_store_result(mysql_);
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
    std::string select_log =
        "SELECT server_id, event_type, channel_id FROM logging_configs";
    if (mysql_query(mysql_, select_log.c_str()) == 0) {
      MYSQL_RES *log_res = mysql_store_result(mysql_);
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
    std::string select_log_embeds =
        "SELECT server_id, event_type, message_type, content, embed_json FROM "
        "logging_embeds";
    if (mysql_query(mysql_, select_log_embeds.c_str()) == 0) {
      MYSQL_RES *embed_res = mysql_store_result(mysql_);
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
    std::string select_wh =
        "SELECT channel_id, webhook_id, webhook_token FROM logging_webhooks";
    if (mysql_query(mysql_, select_wh.c_str()) == 0) {
      MYSQL_RES *wh_res = mysql_store_result(mysql_);
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
    std::string select_trusted =
        "SELECT server_id, entity_type, entity_id FROM trusted_entities";
    if (mysql_query(mysql_, select_trusted.c_str()) == 0) {
      MYSQL_RES *tr_res = mysql_store_result(mysql_);
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
    std::string select_restrictions =
        "SELECT server_id, target_type, target_name, restriction_type, "
        "entity_type, entity_id FROM command_restrictions";
    if (mysql_query(mysql_, select_restrictions.c_str()) == 0) {
      MYSQL_RES *rest_res = mysql_store_result(mysql_);
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
    std::string select_disabled =
        "SELECT server_id, target_type, target_name FROM disabled_commands";
    if (mysql_query(mysql_, select_disabled.c_str()) == 0) {
      MYSQL_RES *dis_res = mysql_store_result(mysql_);
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
    std::string select_action_msgs =
        "SELECT server_id, action_type, message_type, content, embed_json FROM "
        "action_message_configs";
    if (mysql_query(mysql_, select_action_msgs.c_str()) == 0) {
      MYSQL_RES *am_res = mysql_store_result(mysql_);
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
    std::string select_ar =
        "SELECT server_id, joins_threshold_count, joins_threshold_seconds, "
        "joins_action, "
        "spam_threshold_count, spam_threshold_seconds, spam_action, "
        "default_profile_action FROM antiraid_configs";
    if (mysql_query(mysql_, select_ar.c_str()) == 0) {
      MYSQL_RES *ar_res = mysql_store_result(mysql_);
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

    // Load honeypot_configs
    std::string select_hp =
        "SELECT server_id, channel_id, COALESCE(honeypot_message, ''), "
        "COALESCE(disclaimer_message, ''), action, message_triggers, "
        "react_triggers, enabled FROM honeypot_configs";
    if (mysql_query(mysql_, select_hp.c_str()) == 0) {
      MYSQL_RES *hp_res = mysql_store_result(mysql_);
      if (hp_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> hp_lock(hp_cache_mutex_);
        while ((row = mysql_fetch_row(hp_res))) {
          std::string s_id = row[0];
          HoneypotConfig hp;
          hp.server_id = s_id;
          hp.channel_id = row[1] ? row[1] : "";
          hp.honeypot_message = row[2] ? row[2] : "";
          hp.disclaimer_message = row[3] ? row[3] : "";
          hp.action = row[4] ? row[4] : "ban";
          hp.message_triggers = row[5] ? (std::stoi(row[5]) != 0) : true;
          hp.react_triggers = row[6] ? (std::stoi(row[6]) != 0) : false;
          hp.enabled = row[7] ? (std::stoi(row[7]) != 0) : false;
          hp_cache_[s_id] = hp;
        }
        mysql_free_result(hp_res);
      }
    }

    // Load recurring_messages
    std::string select_rm =
        "SELECT id, server_id, channel_id, name, message_type, "
        "COALESCE(content,''), COALESCE(embed_json,''), "
        "interval_seconds, last_sent, owner_tokens FROM recurring_messages";
    if (mysql_query(mysql_, select_rm.c_str()) == 0) {
      MYSQL_RES *rm_res = mysql_store_result(mysql_);
      if (rm_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> rm_lock(rm_cache_mutex_);
        while ((row = mysql_fetch_row(rm_res))) {
          RecurringMessage rm;
          rm.id = row[0] ? std::stoi(row[0]) : 0;
          rm.server_id = row[1] ? row[1] : "";
          rm.channel_id = row[2] ? row[2] : "";
          rm.name = row[3] ? row[3] : "";
          rm.message_type = row[4] ? row[4] : "text";
          rm.content = row[5] ? row[5] : "";
          rm.embed_json = row[6] ? row[6] : "";
          rm.interval_seconds = row[7] ? std::stoull(row[7]) : 300;
          rm.last_sent = row[8] ? std::stoull(row[8]) : 0;
          rm.owner_tokens = row[9] ? (std::string(row[9]) != "0") : false;
          rm_cache_[rm.id] = rm;
          rm_server_index_[rm.server_id].push_back(rm.id);
          rm_channel_index_[rm.channel_id] = rm.id;
        }
        mysql_free_result(rm_res);
      }
    }

    // Load rss_feeds
    std::string select_rss =
        "SELECT id, server_id, url, channels, last_checked, "
        "COALESCE(seen_items,'[]'), check_interval FROM rss_feeds";
    if (mysql_query(mysql_, select_rss.c_str()) == 0) {
      MYSQL_RES *rss_res = mysql_store_result(mysql_);
      if (rss_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> rss_lock(rss_cache_mutex_);
        while ((row = mysql_fetch_row(rss_res))) {
          RssFeed feed;
          feed.id = row[0] ? std::stoi(row[0]) : 0;
          feed.server_id = row[1] ? row[1] : "";
          feed.url = row[2] ? row[2] : "";
          feed.channels = row[3] ? row[3] : "";
          feed.last_checked = row[4] ? std::stoull(row[4]) : 0;
          feed.seen_items = row[5] ? row[5] : "[]";
          feed.check_interval = row[6] ? std::stoi(row[6]) : 1200;
          rss_cache_[feed.id] = feed;
          rss_server_index_[feed.server_id].push_back(feed.id);
        }
        mysql_free_result(rss_res);
      }
    }

    // Load social_subscriptions
    if (mysql_query(
            mysql_,
            "SELECT "
            "id,server_id,platform,channel_handle,channels,events,last_checked,"
            "COALESCE(seen_items,'[]') FROM social_subscriptions") == 0) {
      MYSQL_RES *ss_res = mysql_store_result(mysql_);
      if (ss_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> ss_lock(social_sub_cache_mutex_);
        while ((row = mysql_fetch_row(ss_res))) {
          SocialSubscription sub;
          sub.id = row[0] ? std::stoi(row[0]) : 0;
          sub.server_id = row[1] ? row[1] : "";
          sub.platform = row[2] ? row[2] : "";
          sub.channel_handle = row[3] ? row[3] : "";
          sub.channels = row[4] ? row[4] : "";
          sub.events = row[5] ? row[5] : "live,vod,post";
          sub.last_checked = row[6] ? std::stoull(row[6]) : 0;
          sub.seen_items = row[7] ? row[7] : "[]";
          social_sub_cache_[sub.id] = sub;
          social_sub_server_index_[sub.server_id].push_back(sub.id);
        }
        mysql_free_result(ss_res);
      }
    }

    // Load social_embed_configs
    if (mysql_query(mysql_,
                    "SELECT "
                    "sub_id,event_type,message_type,COALESCE(content,''),"
                    "COALESCE(embed_json,'') FROM social_embed_configs") == 0) {
      MYSQL_RES *sec_res = mysql_store_result(mysql_);
      if (sec_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> sec_lock(social_embed_cache_mutex_);
        while ((row = mysql_fetch_row(sec_res))) {
          SocialEmbedConfig sec;
          sec.sub_id = row[0] ? std::stoi(row[0]) : 0;
          sec.event_type = row[1] ? row[1] : "";
          sec.message_type = row[2] ? row[2] : "embed";
          sec.content = row[3] ? row[3] : "";
          sec.embed_json = row[4] ? row[4] : "";
          std::string key = std::to_string(sec.sub_id) + ":" + sec.event_type;
          social_embed_cache_[key] = sec;
        }
        mysql_free_result(sec_res);
      }
    }

    // Load server_command_aliases
    if (mysql_query(
            mysql_,
            "SELECT server_id,alias,target FROM server_command_aliases") == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> lock(server_alias_cache_mutex_);
        while ((row = mysql_fetch_row(res))) {
          if (row[0] && row[1] && row[2])
            server_alias_cache_[row[0]][row[1]] = row[2];
        }
        mysql_free_result(res);
      }
    }

    // Load user_command_aliases
    if (mysql_query(mysql_,
                    "SELECT user_id,alias,target FROM user_command_aliases") ==
        0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> lock(user_alias_cache_mutex_);
        while ((row = mysql_fetch_row(res))) {
          if (row[0] && row[1] && row[2])
            user_alias_cache_[row[0]][row[1]] = row[2];
        }
        mysql_free_result(res);
      }
    }

    // Load server_alias_settings
    if (mysql_query(mysql_, "SELECT server_id,user_aliases_disabled FROM "
                            "server_alias_settings") == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> lock(
            server_alias_settings_cache_mutex_);
        while ((row = mysql_fetch_row(res))) {
          if (row[0])
            server_alias_settings_cache_[row[0]] =
                row[1] && std::string(row[1]) != "0";
        }
        mysql_free_result(res);
      }
    }

    // Load ticket_configs
    std::string select_tc =
        "SELECT server_id, name, modal_channel_id, modal_message_id, "
        "modal_content, modal_embed_json, "
        "reaction_emoji, input_creation_enabled, naming_scheme, category_id, "
        "view_roles, view_users, view_permissions, ticket_welcome_content, "
        "ticket_welcome_embed_json, ticket_close_emoji, ticket_close_content, "
        "ticket_counter, "
        "COALESCE(member_role_id,'') AS member_role_id FROM ticket_configs";
    if (mysql_query(mysql_, select_tc.c_str()) == 0) {
      MYSQL_RES *tc_res = mysql_store_result(mysql_);
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
    std::string select_t =
        "SELECT channel_id, server_id, user_id, ticket_number, "
        "welcome_message_id, status, panel_name FROM tickets";
    if (mysql_query(mysql_, select_t.c_str()) == 0) {
      MYSQL_RES *t_res = mysql_store_result(mysql_);
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

    // Load application_panels
    std::string select_ap =
        "SELECT server_id, name, trigger_command, trigger_emoji, "
        "modal_channel_id, modal_message_id, modal_content, modal_embed_json, "
        "output_channel_id, mode, view_roles, view_users, view_permissions, "
        "member_role_id, naming_scheme, welcome_content, close_emoji, "
        "require_role, require_role_id, questions, app_counter, "
        "role_to_give, role_to_give_id, end_message FROM application_panels";
    if (mysql_query(mysql_, select_ap.c_str()) == 0) {
      MYSQL_RES *ap_res = mysql_store_result(mysql_);
      if (ap_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> ap_lock(app_panel_cache_mutex_);
        while ((row = mysql_fetch_row(ap_res))) {
          std::string s_id = row[0] ? row[0] : "";
          std::string p_name = row[1] ? row[1] : "";
          ApplicationPanel ap;
          ap.server_id = s_id;
          ap.name = p_name;
          ap.trigger_command = row[2] ? row[2] : "";
          ap.trigger_emoji = row[3] ? row[3] : "";
          ap.modal_channel_id = row[4] ? row[4] : "";
          ap.modal_message_id = row[5] ? row[5] : "";
          ap.modal_content = row[6] ? row[6] : "";
          ap.modal_embed_json = row[7] ? row[7] : "";
          ap.output_channel_id = row[8] ? row[8] : "";
          ap.mode = row[9] ? row[9] : "post";
          ap.view_roles = row[10] ? row[10] : "";
          ap.view_users = row[11] ? row[11] : "";
          ap.view_permissions = row[12] ? row[12] : "";
          ap.member_role_id = row[13] ? row[13] : "";
          ap.naming_scheme = row[14] ? row[14] : "app-{number}";
          ap.welcome_content = row[15] ? row[15] : "";
          ap.close_emoji = row[16] ? row[16] : "🔒";
          ap.require_role = row[17] ? (std::stoi(row[17]) != 0) : false;
          ap.require_role_id = row[18] ? row[18] : "";

          std::string q_json = row[19] ? row[19] : "[]";
          try {
            auto parsed = nlohmann::json::parse(q_json);
            if (parsed.is_array()) {
              for (const auto& q : parsed) {
                if (q.is_string()) {
                  ap.questions.push_back(q.get<std::string>());
                }
              }
            }
          } catch (...) {}

          ap.app_counter = row[20] ? std::stoi(row[20]) : 0;
          ap.role_to_give = row[21] ? (std::stoi(row[21]) != 0) : false;
          ap.role_to_give_id = row[22] ? row[22] : "";
          ap.end_message = row[23] ? row[23] : "";

          std::string cache_key = s_id + ":" + p_name;
          app_panel_cache_[cache_key] = ap;
        }
        mysql_free_result(ap_res);
      }
    }

    // Load user_afk
    std::string select_afk = "SELECT user_id, COALESCE(custom_template, ''), "
                             "is_afk, COALESCE(reason, ''), since, "
                             "COALESCE(active_preset, 'default') FROM user_afk";
    if (mysql_query(mysql_, select_afk.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
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
    std::string select_presets =
        "SELECT user_id, preset_name, COALESCE(template_json, '') FROM "
        "user_afk_presets";
    if (mysql_query(mysql_, select_presets.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
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

    // Load autoresponders cache
    {
      auto load_json_arr = [](const char *raw) -> std::vector<std::string> {
        std::vector<std::string> out;
        if (!raw)
          return out;
        try {
          auto j = nlohmann::json::parse(raw);
          if (j.is_array()) {
            for (const auto &v : j) {
              if (v.is_string())
                out.push_back(v.get<std::string>());
            }
          }
        } catch (...) {
        }
        return out;
      };

      if (mysql_query(mysql_, "SELECT "
                              "id,server_id,name,keyword,match_type,message_"
                              "type,content,embed_json,"
                              "allow_roles,allow_users,allow_channels,ignore_"
                              "roles,ignore_users,ignore_channels "
                              "FROM autoresponders") == 0) {
        MYSQL_RES *res = mysql_store_result(mysql_);
        if (res) {
          MYSQL_ROW row;
          std::lock_guard<std::shared_mutex> arsp_lock(arsp_cache_mutex_);
          while ((row = mysql_fetch_row(res))) {
            AutoresponderConfig c;
            c.id = row[0] ? std::stoi(row[0]) : 0;
            c.server_id = row[1] ? row[1] : "";
            c.name = row[2] ? row[2] : "";
            c.keyword = row[3] ? row[3] : "";
            c.match_type = row[4] ? row[4] : "contains";
            c.message_type = row[5] ? row[5] : "text";
            c.content = row[6] ? row[6] : "";
            c.embed_json = row[7] ? row[7] : "";
            c.filter.allow_roles = load_json_arr(row[8]);
            c.filter.allow_users = load_json_arr(row[9]);
            c.filter.allow_channels = load_json_arr(row[10]);
            c.filter.ignore_roles = load_json_arr(row[11]);
            c.filter.ignore_users = load_json_arr(row[12]);
            c.filter.ignore_channels = load_json_arr(row[13]);
            arsp_cache_[c.id] = c;
            arsp_server_index_[c.server_id].push_back(c.id);
          }
          mysql_free_result(res);
        }
      }

      if (mysql_query(mysql_,
                      "SELECT id,server_id,name,keyword,match_type,emojis,"
                      "allow_roles,allow_users,allow_channels,ignore_roles,"
                      "ignore_users,ignore_channels "
                      "FROM autoreacts") == 0) {
        MYSQL_RES *res = mysql_store_result(mysql_);
        if (res) {
          MYSQL_ROW row;
          std::lock_guard<std::shared_mutex> arct_lock(arct_cache_mutex_);
          while ((row = mysql_fetch_row(res))) {
            AutoreactConfig c;
            c.id = row[0] ? std::stoi(row[0]) : 0;
            c.server_id = row[1] ? row[1] : "";
            c.name = row[2] ? row[2] : "";
            c.keyword = row[3] ? row[3] : "";
            c.match_type = row[4] ? row[4] : "contains";
            c.emojis = row[5] ? row[5] : "";
            c.filter.allow_roles = load_json_arr(row[6]);
            c.filter.allow_users = load_json_arr(row[7]);
            c.filter.allow_channels = load_json_arr(row[8]);
            c.filter.ignore_roles = load_json_arr(row[9]);
            c.filter.ignore_users = load_json_arr(row[10]);
            c.filter.ignore_channels = load_json_arr(row[11]);
            arct_cache_[c.id] = c;
            arct_server_index_[c.server_id].push_back(c.id);
          }
          mysql_free_result(res);
        }
      }
    }

    // Load masquerade_configs
    std::string select_masq =
        "SELECT server_id, avatar, colour, name, disabled, disable_user, "
        "disable_user_name, disable_user_avatar, disable_user_colour FROM "
        "masquerade_configs";
    if (mysql_query(mysql_, select_masq.c_str()) == 0) {
      MYSQL_RES *masq_res = mysql_store_result(mysql_);
      if (masq_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> masq_lock(masquerade_cache_mutex_);
        while ((row = mysql_fetch_row(masq_res))) {
          std::string s_id = row[0] ? row[0] : "";
          MasqueradeConfig cfg;
          cfg.server_id = s_id;
          if (row[1])
            cfg.avatar = row[1];
          if (row[2])
            cfg.colour = row[2];
          if (row[3])
            cfg.name = row[3];
          cfg.disabled = row[4] && std::string(row[4]) == "1";
          cfg.disable_user = row[5] && std::string(row[5]) == "1";
          cfg.disable_user_name = row[6] && std::string(row[6]) == "1";
          cfg.disable_user_avatar = row[7] && std::string(row[7]) == "1";
          cfg.disable_user_colour = row[8] && std::string(row[8]) == "1";
          masquerade_cache_[s_id] = cfg;
        }
        mysql_free_result(masq_res);
      }
    }

    // Load user_masquerades
    std::string select_user_masq =
        "SELECT user_id, avatar, colour, name FROM user_masquerades";
    if (mysql_query(mysql_, select_user_masq.c_str()) == 0) {
      MYSQL_RES *um_res = mysql_store_result(mysql_);
      if (um_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> um_lock(user_masq_cache_mutex_);
        while ((row = mysql_fetch_row(um_res))) {
          std::string u_id = row[0] ? row[0] : "";
          UserMasquerade um;
          um.user_id = u_id;
          if (row[1])
            um.avatar = row[1];
          if (row[2])
            um.colour = row[2];
          if (row[3])
            um.name = row[3];
          user_masq_cache_[u_id] = um;
        }
        mysql_free_result(um_res);
      }
    }

    // Load user_timezones
    std::string select_user_tzs =
        "SELECT user_id, timezone FROM user_timezones";
    if (mysql_query(mysql_, select_user_tzs.c_str()) == 0) {
      MYSQL_RES *tz_res = mysql_store_result(mysql_);
      if (tz_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> tz_lock(user_timezones_cache_mutex_);
        while ((row = mysql_fetch_row(tz_res))) {
          std::string u_id = row[0] ? row[0] : "";
          std::string tz = row[1] ? row[1] : "";
          user_timezones_cache_[u_id] = tz;
        }
        mysql_free_result(tz_res);
      }
    }

    // Load bot_ignored_users
    std::string select_bot_ignored =
        "SELECT user_id FROM bot_ignored_users";
    if (mysql_query(mysql_, select_bot_ignored.c_str()) == 0) {
      MYSQL_RES *ignored_res = mysql_store_result(mysql_);
      if (ignored_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> ignored_lock(ignored_users_cache_mutex_);
        while ((row = mysql_fetch_row(ignored_res))) {
          std::string u_id = row[0] ? row[0] : "";
          ignored_users_cache_.insert(u_id);
        }
        mysql_free_result(ignored_res);
      }
    }

    // Load user_rank_card
    std::string select_user_rc =
        "SELECT user_id, style, bg_color, bg_image, bar_active, bar_dimmed, text_color, font_name FROM user_rank_card";
    if (mysql_query(mysql_, select_user_rc.c_str()) == 0) {
      MYSQL_RES *rc_res = mysql_store_result(mysql_);
      if (rc_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> rc_lock(user_rank_card_cache_mutex_);
        while ((row = mysql_fetch_row(rc_res))) {
          std::string u_id = row[0] ? row[0] : "";
          UserRankCard urc;
          urc.user_id = u_id;
          urc.style = row[1] ? row[1] : "image";
          if (row[2]) urc.bg_color = row[2];
          if (row[3]) urc.bg_image = row[3];
          if (row[4]) urc.bar_active = row[4];
          if (row[5]) urc.bar_dimmed = row[5];
          if (row[6]) urc.text_color = row[6];
          if (row[7]) urc.font_name = row[7];
          user_rank_card_cache_[u_id] = urc;
        }
        mysql_free_result(rc_res);
      }
    }

    // Load tuppers
    std::string select_tuppers =
        "SELECT id, user_id, name, avatar_url, COALESCE(prefix,''), "
        "COALESCE(suffix,'') FROM tuppers";
    if (mysql_query(mysql_, select_tuppers.c_str()) == 0) {
      MYSQL_RES *tupper_res = mysql_store_result(mysql_);
      if (tupper_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> tupper_lock(tupper_cache_mutex_);
        while ((row = mysql_fetch_row(tupper_res))) {
          Tupper t;
          t.id = row[0] ? std::stoi(row[0]) : 0;
          t.user_id = row[1] ? row[1] : "";
          t.name = row[2] ? row[2] : "";
          t.avatar_url = row[3] ? row[3] : "";
          t.prefix = row[4] ? row[4] : "";
          t.suffix = row[5] ? row[5] : "";
          tupper_cache_[t.user_id].push_back(t);
        }
        mysql_free_result(tupper_res);
      }
    }

    // Load guild_leveling_config
    std::string select_glc_sql =
        "SELECT server_id, enabled, reward_coins, coins_per_message, "
        "min_xp_per_message, max_xp_per_message, min_message_chars, "
        "max_message_chars, "
        "xp_cooldown_seconds, announcement_channel, announce_levelup, "
        "level_up_message, sticky_roles FROM guild_leveling_config";
    if (mysql_query(mysql_, select_glc_sql.c_str()) == 0) {
      MYSQL_RES *glc_res = mysql_store_result(mysql_);
      if (glc_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> glc_lock(
            leveling_config_cache_mutex_);
        while ((row = mysql_fetch_row(glc_res))) {
          std::string s_id = row[0] ? row[0] : "";
          GuildLevelingConfig cfg;
          cfg.server_id = s_id;
          cfg.enabled = row[1] && std::string(row[1]) == "1";
          cfg.reward_coins = row[2] && std::string(row[2]) == "1";
          cfg.coins_per_message = row[3] ? std::stoi(row[3]) : 1;
          cfg.min_xp_per_message = row[4] ? std::stoi(row[4]) : 15;
          cfg.max_xp_per_message = row[5] ? std::stoi(row[5]) : 25;
          cfg.min_message_chars = row[6] ? std::stoi(row[6]) : 5;
          cfg.max_message_chars = row[7] ? std::stoi(row[7]) : 2000;
          cfg.xp_cooldown_seconds = row[8] ? std::stoi(row[8]) : 60;
          if (row[9])
            cfg.announcement_channel = row[9];
          cfg.announce_levelup = row[10] && std::string(row[10]) == "1";
          cfg.announcement_message = row[11] ? row[11] : "";
          cfg.sticky_roles = row[12] && std::string(row[12]) == "1";
          leveling_config_cache_[s_id] = cfg;
        }
        mysql_free_result(glc_res);
      }
    }

    // Load guild_level_roles
    std::string select_glr_sql =
        "SELECT id, server_id, level, role_id, role_name, remove_previous FROM "
        "guild_level_roles";
    if (mysql_query(mysql_, select_glr_sql.c_str()) == 0) {
      MYSQL_RES *glr_res = mysql_store_result(mysql_);
      if (glr_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> glr_lock(level_roles_cache_mutex_);
        while ((row = mysql_fetch_row(glr_res))) {
          LevelRole r;
          r.id = row[0] ? std::stoi(row[0]) : 0;
          r.server_id = row[1] ? row[1] : "";
          r.level = row[2] ? std::stoul(row[2]) : 1;
          r.role_id = row[3] ? row[3] : "";
          r.role_name = row[4] ? row[4] : "";
          r.remove_previous = row[5] && std::string(row[5]) == "1";
          level_roles_cache_[r.server_id].push_back(r);
        }
        mysql_free_result(glr_res);
      }
    }

    // Load guild_xp_blacklist
    std::string select_gxb_sql = "SELECT id, server_id, target_type, "
                                 "target_id, added_by FROM guild_xp_blacklist";
    if (mysql_query(mysql_, select_gxb_sql.c_str()) == 0) {
      MYSQL_RES *gxb_res = mysql_store_result(mysql_);
      if (gxb_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> gxb_lock(xp_blacklist_cache_mutex_);
        while ((row = mysql_fetch_row(gxb_res))) {
          XPBlacklistEntry entry;
          entry.id = row[0] ? std::stoi(row[0]) : 0;
          entry.server_id = row[1] ? row[1] : "";
          entry.target_type = row[2] ? row[2] : "";
          entry.target_id = row[3] ? row[3] : "";
          entry.added_by = row[4] ? row[4] : "";
          xp_blacklist_cache_[entry.server_id].push_back(entry);
        }
        mysql_free_result(gxb_res);
      }
    }

    // Load starboards into cache
    if (mysql_query(mysql_, "SELECT server_id, board_name, channel_id, threshold, emojis, allow_self, use_embed, message_content, embed_title, embed_description, delete_below_threshold FROM starboards") == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> lock(starboard_configs_cache_mutex_);
        while ((row = mysql_fetch_row(res))) {
          if (row[0] && row[1] && row[2] && row[3] && row[4] && row[5]) {
            StarboardConfig cfg;
            cfg.server_id = row[0];
            cfg.board_name = row[1];
            cfg.channel_id = row[2];
            cfg.threshold = std::stoi(row[3]);
            cfg.emojis = row[4];
            cfg.allow_self = std::stoi(row[5]) != 0;
            if (row[6]) cfg.use_embed = std::stoi(row[6]) != 0;
            if (row[7]) cfg.message_content = row[7];
            if (row[8]) cfg.embed_title = row[8];
            if (row[9]) cfg.embed_description = row[9];
            if (row[10]) cfg.delete_below_threshold = std::stoi(row[10]) != 0;
            starboard_configs_cache_[cfg.server_id][cfg.board_name] = cfg;
          }
        }
        mysql_free_result(res);
      }
    }

    // Load starboard_messages into cache
    if (mysql_query(mysql_, "SELECT server_id, board_name, original_message_id, starboard_message_id FROM starboard_messages") == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> lock(starboard_messages_cache_mutex_);
        while ((row = mysql_fetch_row(res))) {
          if (row[0] && row[1] && row[2] && row[3]) {
            starboard_messages_cache_[row[0]][row[1]][row[2]] = row[3];
          }
        }
        mysql_free_result(res);
      }
    }

    std::cout << "[bronx db] database successfully initialized.\n";
    return true;
  }

  std::string Database::create_dashboard_token(const std::string &user_id,
                                     const std::string &user_name) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return "";

    static const char hex_chars[] = "0123456789abcdef";
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 15);

    std::string token = "brx_tkn_";
    for (int i; i < 32; ++i) {
      token += hex_chars[dist(rng)];
    }

    std::string esc_token = escape_string(token);
    std::string esc_user_id = escape_string(user_id);
    std::string esc_user_name = escape_string(user_name);

    std::string del_sql =
        "DELETE FROM dashboard_tokens WHERE user_id = '" + esc_user_id + "'";
    mysql_query(mysql_, del_sql.c_str());

    std::string ins_sql =
        "INSERT INTO dashboard_tokens (token, user_id, user_name, expires_at) "
        "VALUES ('" +
        esc_token + "', '" + esc_user_id + "', '" + esc_user_name +
        "', "
        "DATE_ADD(NOW(), INTERVAL 24 HOUR))";
    if (mysql_query(mysql_, ins_sql.c_str())) {
      std::cerr << "[bronx db] failed to insert dashboard token: "
                << mysql_error(mysql_) << "\n";
      return "";
    }

    return token;
  }

  void Database::upsert_server(const std::string &server_id, const std::string &name,
                     const std::string &icon_id,
                     const std::string &banner_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_ || server_id.empty())
      return;

    std::string esc_id = escape_string(server_id);
    std::string esc_name = escape_string(name);
    std::string esc_icon = escape_string(icon_id);
    std::string esc_banner = escape_string(banner_id);

    std::string sql =
        "INSERT INTO servers (server_id, name, icon_id, banner_id) VALUES ('" + esc_id +
        "', '" + esc_name + "', '" + esc_icon + "', '" + esc_banner +
        "') "
        "ON DUPLICATE KEY UPDATE name=VALUES(name), icon_id=VALUES(icon_id), banner_id=VALUES(banner_id)";
    mysql_query(mysql_, sql.c_str());
  }

  bool Database::server_exists(const std::string &server_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_ || server_id.empty())
      return false;

    std::string esc_id = escape_string(server_id);
    std::string sql = "SELECT 1 FROM servers WHERE server_id = '" + esc_id + "' LIMIT 1";
    if (mysql_query(mysql_, sql.c_str()) != 0)
      return false;

    MYSQL_RES *res = mysql_store_result(mysql_);
    bool exists = false;
    if (res) {
      if (mysql_num_rows(res) > 0) {
        exists = true;
      }
      mysql_free_result(res);
    }
    return exists;
  }

  void Database::upsert_server_member(const std::string &server_id,
                            const std::string &user_id, bool is_owner,
                            bool is_admin) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_ || server_id.empty() || user_id.empty())
      return;

    std::string esc_sid = escape_string(server_id);
    std::string esc_uid = escape_string(user_id);

    std::string sql =
        "INSERT INTO server_members (server_id, user_id, is_owner, is_admin) "
        "VALUES ('" +
        esc_sid + "', '" + esc_uid + "', " + (is_owner ? "1" : "0") + ", " +
        (is_admin ? "1" : "0") +
        ") "
        "ON DUPLICATE KEY UPDATE is_owner=VALUES(is_owner), "
        "is_admin=VALUES(is_admin)";
    mysql_query(mysql_, sql.c_str());
  }

  void Database::print_cache_stats() {
    std::cout << "           loaded " << masquerade_cache_.size()
              << " masquerade configurations.\n"
              << "           loaded " << tupper_cache_.size()
              << " user tupper profiles.\n"
              << "           loaded " << leveling_config_cache_.size()
              << " leveling configurations.\n"
              << "           loaded " << level_roles_cache_.size()
              << " level role rewards servers.\n"
              << "           loaded " << xp_blacklist_cache_.size()
              << " server blacklist mappings.\n"
              << "           loaded " << cache_.size()
              << " prefix configurations.\n"
              << "           loaded " << rr_cache_.size()
              << " reaction role messages.\n"
              << "           loaded " << wl_cache_.size()
              << " welcome/leave configurations.\n"
              << "           loaded " << log_cache_.size()
              << " logging configurations.\n"
              << "           loaded " << log_embed_cache_.size()
              << " logging embed templates.\n"
              << "           loaded " << webhook_cache_.size()
              << " logging webhooks.\n"
              << "           loaded " << trusted_cache_.size()
              << " trusted servers configurations.\n"
              << "           loaded " << restrictions_cache_.size()
              << " server commands restrictions.\n"
              << "           loaded " << disabled_cache_.size()
              << " server disabled commands.\n"
              << "           loaded " << action_msg_cache_.size()
              << " action message configurations.\n"
              << "           loaded " << ar_cache_.size()
              << " antiraid configurations.\n"
              << "           loaded " << rm_cache_.size()
              << " recurring messages.\n"
              << "           loaded " << rss_cache_.size() << " RSS feeds.\n"
              << "           loaded " << social_sub_cache_.size()
              << " social subscriptions.\n"
              << "           loaded " << ticket_config_cache_.size()
              << " ticket configurations.\n"
              << "           loaded " << ticket_cache_.size()
              << " active tickets.\n"
              << "           loaded " << user_afk_cache_.size()
              << " user afk configurations.\n"
              << "           loaded " << user_afk_presets_cache_.size()
              << " user afk preset configurations.\n"
              << "           loaded " << roblox_links_cache_.size()
              << " roblox linked accounts.\n"
              << "           loaded " << arsp_cache_.size()
              << " autoresponders.\n"
              << "           loaded " << arct_cache_.size() << " autoreacts.\n";
  }

  std::vector<std::string> Database::get_prefixes(const std::string &target_type,
                                        const std::string &target_id) {
    std::shared_lock<std::shared_mutex> lock(cache_mutex_);
    std::string cache_key = target_type + ":" + target_id;
    auto it = cache_.find(cache_key);
    if (it != cache_.end()) {
      return it->second;
    }
    return {};
  }

  bool Database::set_prefixes(const std::string &target_type,
                    const std::string &target_id,
                    const std::vector<std::string> &prefixes) {
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
        "VALUES ('" +
        esc_type + "', '" + esc_id + "', '" + esc_json +
        "') "
        "ON DUPLICATE KEY UPDATE prefixes = '" +
        esc_json + "'";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save prefixes: " << mysql_error(mysql_)
                << "\n";
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

  bool Database::remove_prefixes(const std::string &target_type,
                       const std::string &target_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_type = escape_string(target_type);
    std::string esc_id = escape_string(target_id);

    std::string query = "DELETE FROM prefixes WHERE target_type = '" +
                        esc_type + "' AND target_id = '" + esc_id + "'";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to delete prefixes: "
                << mysql_error(mysql_) << "\n";
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

  bool Database::get_prefix_default_enabled(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(pfx_default_cache_mutex_);
    auto it = pfx_default_cache_.find(server_id);
    if (it != pfx_default_cache_.end())
      return it->second;
    return true; // default: enabled
  }

  bool Database::set_prefix_default_enabled(const std::string &server_id, bool enabled) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string esc_id = escape_string(server_id);
    int val = enabled ? 1 : 0;

    // If a server row already exists, just update the flag.
    // If not, insert one with an empty prefixes JSON so the flag is stored.
    std::string sql = "INSERT INTO prefixes (target_type, target_id, prefixes, "
                      "default_enabled) "
                      "VALUES ('server', '" +
                      esc_id + "', '[]', " + std::to_string(val) +
                      ") "
                      "ON DUPLICATE KEY UPDATE default_enabled = " +
                      std::to_string(val);
    if (mysql_query(mysql_, sql.c_str())) {
      std::cerr << "[bronx db] set_prefix_default_enabled failed: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    std::lock_guard<std::shared_mutex> cache_lock(pfx_default_cache_mutex_);
    pfx_default_cache_[server_id] = enabled;
    return true;
  }

  bool Database::is_server_owner(const std::string &server_id, const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_ || server_id.empty() || user_id.empty())
      return false;
    std::string esc_sid = escape_string(server_id);
    std::string esc_uid = escape_string(user_id);
    std::string query = "SELECT is_owner FROM server_members WHERE server_id = '" +
                        esc_sid + "' AND user_id = '" + esc_uid + "'";
    if (mysql_query(mysql_, query.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        bool owner = row && row[0] && std::string(row[0]) == "1";
        mysql_free_result(res);
        return owner;
      }
    }
    return false;
  }

  void Database::purge_server_data(const std::string& server_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_ || server_id.empty())
      return;
    std::string esc = escape_string(server_id);
    const char *tables[] = {
        "prefixes",          "rr_messages",       "rr_rules",
        "server_aliases",    "user_aliases",      "guild_leveling_config",
        "honeypot_configs",
        "level_roles",       "xp_blacklist",      "user_xp",
        "counting_config",   "masquerade_config", "user_masquerade",
        "tuppers",           "tupper_proxies",    "starboards",
        "starboard_messages","welcome_leave_config","logging_channels",
        "logging_embeds",    "logging_webhooks",  "action_messages",
        "antiraid_config",   "jail_config",       "active_jails",
        "moderation_actions","command_usage",     "server_members",
        "ticket_configs",    "tickets",           "application_panels",
        "application_submissions","server_listings","recurring_messages",
        "rss_feeds",         "social_subs",       "social_embeds",
        "autoresponders",    "autoreacts",        "word_filter_groups",
        "word_filter_patterns","word_filter_whitelist","trusted_users",
        "disabled_items",    "user_economy_items","users",
        "fishing_users",     "fish_inventory",    "user_pets",
        "fish_boss_state",   "boss_contributions","custom_shop_items",
        "custom_rods",       "custom_baits",      "custom_fish",
        "user_baits",        "unlocked_rods",     "guild_user_activity_daily",
        "guild_channel_activity_daily","guild_member_events_daily",
        "guild_user_activity","guild_channel_activity","command_stats",
        "user_afk",
    };
    for (const char *table : tables) {
      std::string sql = "DELETE FROM " + std::string(table) +
                        " WHERE server_id = '" + esc + "'";
      mysql_query(mysql_, sql.c_str());
    }
    // Also delete from tables that use guild_id
    const char *guild_tables[] = {
        "guild_user_activity_daily","guild_channel_activity_daily",
        "guild_member_events_daily","guild_user_activity","guild_channel_activity",
    };
    for (const char *table : guild_tables) {
      std::string sql = "DELETE FROM " + std::string(table) +
                        " WHERE guild_id = '" + esc + "'";
      mysql_query(mysql_, sql.c_str());
    }
  }

} // namespace bronx
