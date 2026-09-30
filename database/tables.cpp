#include "database.h"
#include <mysql.h>
#include <iostream>
#include <string>

namespace bronx {

bool Database::init_tables() {
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
    mysql_query(mysql_, alter_rr_rules.c_str());

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
    bool has_channel_id = false;
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
        "  default_profile_action VARCHAR(100) NOT NULL DEFAULT 'none',"
        "  mentions_threshold_count INT NOT NULL DEFAULT 10,"
        "  mentions_threshold_messages INT NOT NULL DEFAULT 5,"
        "  mentions_action VARCHAR(100) NOT NULL DEFAULT 'none'"
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

    // Create Table multispam_configs
    std::string create_multispam =
        "CREATE TABLE IF NOT EXISTS multispam_configs ("
        "  server_id VARCHAR(30) NOT NULL PRIMARY KEY,"
        "  enabled TINYINT(1) NOT NULL DEFAULT 0,"
        "  action VARCHAR(100) NOT NULL DEFAULT 'timeout',"
        "  threshold_count INT NOT NULL DEFAULT 2,"
        "  time_window INT NOT NULL DEFAULT 60,"
        "  delete_messages TINYINT(1) NOT NULL DEFAULT 1,"
        "  whitelist JSON DEFAULT NULL"
        ")";
    if (mysql_query(mysql_, create_multispam.c_str())) {
      std::cerr << "[bronx db] failed to create table 'multispam_configs': "
                << mysql_error(mysql_) << "\n";
      return false;
    }


    mysql_query(mysql_, "ALTER TABLE antiraid_configs MODIFY COLUMN "
                        "joins_action VARCHAR(100) NOT NULL DEFAULT 'none'");
    mysql_query(mysql_, "ALTER TABLE antiraid_configs MODIFY COLUMN "
                        "spam_action VARCHAR(100) NOT NULL DEFAULT 'none'");
    mysql_query(mysql_,
                "ALTER TABLE antiraid_configs MODIFY COLUMN "
                "default_profile_action VARCHAR(100) NOT NULL DEFAULT 'none'");
    mysql_query(mysql_, "ALTER TABLE antiraid_configs ADD COLUMN IF NOT EXISTS "
                        "mentions_threshold_count INT NOT NULL DEFAULT 10");
    mysql_query(mysql_, "ALTER TABLE antiraid_configs ADD COLUMN IF NOT EXISTS "
                        "mentions_threshold_messages INT NOT NULL DEFAULT 5");
    mysql_query(mysql_, "ALTER TABLE antiraid_configs ADD COLUMN IF NOT EXISTS "
                        "mentions_action VARCHAR(100) NOT NULL DEFAULT 'none'");

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
    if (mysql_query(mysql_, create_rm.c_str())) {
      std::cerr << "[bronx db] failed to create table 'recurring_messages': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

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
        "  actions TEXT DEFAULT NULL,"
        "  INDEX idx_arsp_server (server_id)"
        ")";
    mysql_query(mysql_, sql_ar.c_str());
    mysql_query(mysql_, "ALTER TABLE autoresponders ADD COLUMN IF NOT EXISTS actions TEXT DEFAULT NULL");

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

    // Custom commands table
    std::string sql_ccmd =
        "CREATE TABLE IF NOT EXISTS custom_commands ("
        "  id INT AUTO_INCREMENT PRIMARY KEY,"
        "  server_id VARCHAR(30) NOT NULL,"
        "  name VARCHAR(50) NOT NULL,"
        "  description TEXT DEFAULT NULL,"
        "  message_type VARCHAR(10) NOT NULL DEFAULT 'text',"
        "  content TEXT DEFAULT NULL,"
        "  embed_json JSON DEFAULT NULL,"
        "  allowed_roles TEXT DEFAULT NULL,"
        "  owner_only TINYINT(1) NOT NULL DEFAULT 0,"
        "  actions TEXT DEFAULT NULL,"
        "  allow_roles JSON DEFAULT NULL,"
        "  allow_users JSON DEFAULT NULL,"
        "  allow_channels JSON DEFAULT NULL,"
        "  ignore_roles JSON DEFAULT NULL,"
        "  ignore_users JSON DEFAULT NULL,"
        "  ignore_channels JSON DEFAULT NULL,"
        "  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  UNIQUE KEY uq_server_cc (server_id, name),"
        "  INDEX idx_cc_server (server_id)"
        ")";
    mysql_query(mysql_, sql_ccmd.c_str());
    mysql_query(mysql_, "ALTER TABLE custom_commands ADD COLUMN IF NOT EXISTS actions TEXT DEFAULT NULL");
    mysql_query(mysql_, "ALTER TABLE custom_commands ADD COLUMN IF NOT EXISTS allow_roles JSON DEFAULT NULL");
    mysql_query(mysql_, "ALTER TABLE custom_commands ADD COLUMN IF NOT EXISTS allow_users JSON DEFAULT NULL");
    mysql_query(mysql_, "ALTER TABLE custom_commands ADD COLUMN IF NOT EXISTS allow_channels JSON DEFAULT NULL");
    mysql_query(mysql_, "ALTER TABLE custom_commands ADD COLUMN IF NOT EXISTS ignore_roles JSON DEFAULT NULL");
    mysql_query(mysql_, "ALTER TABLE custom_commands ADD COLUMN IF NOT EXISTS ignore_users JSON DEFAULT NULL");
    mysql_query(mysql_, "ALTER TABLE custom_commands ADD COLUMN IF NOT EXISTS ignore_channels JSON DEFAULT NULL");

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
        "  require_role TINYINT(1) DEFAULT 0,"
        "  require_role_id VARCHAR(30) DEFAULT '',"
        "  questions JSON DEFAULT NULL,"
        "  app_counter INT NOT NULL DEFAULT 0,"
        "  role_to_give TINYINT(1) DEFAULT 0,"
        "  role_to_give_id VARCHAR(30) DEFAULT '',"
        "  end_message TEXT DEFAULT NULL,"
        "  PRIMARY KEY (server_id, name)"
        ")";
    if (mysql_query(mysql_, create_app_panels.c_str())) {
      std::cerr << "[bronx db] failed to create table 'application_panels': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table application_submissions
    std::string create_app_subs =
        "CREATE TABLE IF NOT EXISTS application_submissions ("
        "  id INT AUTO_INCREMENT PRIMARY KEY,"
        "  server_id VARCHAR(30) NOT NULL,"
        "  panel_name VARCHAR(50) NOT NULL,"
        "  user_id VARCHAR(30) NOT NULL,"
        "  submitted_at BIGINT UNSIGNED NOT NULL,"
        "  answers_json JSON DEFAULT NULL,"
        "  status VARCHAR(20) NOT NULL DEFAULT 'pending',"
        "  output_message_id VARCHAR(30) DEFAULT '',"
        "  ticket_channel_id VARCHAR(30) DEFAULT '',"
        "  INDEX idx_app_subs_server (server_id)"
        ")";
    if (mysql_query(mysql_, create_app_subs.c_str())) {
      std::cerr << "[bronx db] failed to create table 'application_submissions': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Create Table counting_configs
    std::string create_counting_configs =
        "CREATE TABLE IF NOT EXISTS counting_configs ("
        "  server_id VARCHAR(30) NOT NULL PRIMARY KEY,"
        "  channel_id VARCHAR(30) NOT NULL,"
        "  current_number BIGINT NOT NULL DEFAULT 0,"
        "  last_user_id VARCHAR(30) DEFAULT '',"
        "  fail_role_id VARCHAR(30) DEFAULT '',"
        "  fail_role_duration INT NOT NULL DEFAULT 0,"
        "  passive TINYINT(1) NOT NULL DEFAULT 0,"
        "  highest_count BIGINT NOT NULL DEFAULT 0,"
        "  warn_double_count TINYINT(1) NOT NULL DEFAULT 0"
        ")";
    if (mysql_query(mysql_, create_counting_configs.c_str())) {
      std::cerr << "[bronx db] failed to create table 'counting_configs': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

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

    mysql_query(mysql_, "ALTER TABLE starboards ADD COLUMN IF NOT EXISTS use_embed TINYINT(1) NOT NULL DEFAULT 1");
    mysql_query(mysql_, "ALTER TABLE starboards ADD COLUMN IF NOT EXISTS message_content TEXT");
    mysql_query(mysql_, "ALTER TABLE starboards ADD COLUMN IF NOT EXISTS embed_title TEXT");
    mysql_query(mysql_, "ALTER TABLE starboards ADD COLUMN IF NOT EXISTS embed_description TEXT");
    mysql_query(mysql_, "ALTER TABLE starboards ADD COLUMN IF NOT EXISTS delete_below_threshold TINYINT(1) NOT NULL DEFAULT 1");

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
        "  roblox_id BIGINT UNSIGNED NOT NULL,"
        "  roblox_username VARCHAR(100) NOT NULL"
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
        "  lastfm_username VARCHAR(100) NOT NULL"
        ")";
    if (mysql_query(mysql_, create_lastfm_links.c_str())) {
      std::cerr << "[bronx db] failed to create table 'lastfm_links': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    if (mysql_query(mysql_, create_rss.c_str())) {
      std::cerr << "[bronx db] failed to create table 'rss_feeds': "
                << mysql_error(mysql_) << "\n";
      return false;
    }
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
                        "  rarity VARCHAR(32) NOT NULL,"
                        "  claimed_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                        "  PRIMARY KEY (user_id, rarity)"
                        ")");

    mysql_query(mysql_,
                "CREATE TABLE IF NOT EXISTS fish_boss_state ("
                "  boss_type VARCHAR(64) PRIMARY KEY,"
                "  hp_remaining BIGINT NOT NULL,"
                "  max_hp BIGINT NOT NULL,"
                "  status VARCHAR(32) NOT NULL DEFAULT 'active',"
                "  defeated_by VARCHAR(64) DEFAULT 'None',"
                "  started_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP"
                ")");

    mysql_query(mysql_,
                "CREATE TABLE IF NOT EXISTS boss_contributions ("
                "  user_id VARCHAR(64) NOT NULL,"
                "  boss_type VARCHAR(64) NOT NULL,"
                "  damage_dealt BIGINT NOT NULL DEFAULT 0,"
                "  PRIMARY KEY (user_id, boss_type)"
                ")");

    mysql_query(mysql_, "CREATE TABLE IF NOT EXISTS fish_inventory ("
                        "  id INT AUTO_INCREMENT PRIMARY KEY,"
                        "  user_id VARCHAR(64) NOT NULL,"
                        "  fish_id VARCHAR(64) NOT NULL,"
                        "  fish_name VARCHAR(128) NOT NULL,"
                        "  rarity VARCHAR(32) NOT NULL,"
                        "  odds_percent DOUBLE NOT NULL,"
                        "  weight DOUBLE NOT NULL,"
                        "  value BIGINT NOT NULL,"
                        "  caught_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                        "  INDEX idx_user_fish (user_id)"
                        ")");

    mysql_query(mysql_, "CREATE TABLE IF NOT EXISTS user_baits ("
                        "  user_id VARCHAR(64) NOT NULL,"
                        "  bait_id VARCHAR(64) NOT NULL,"
                        "  quantity INT NOT NULL DEFAULT 0,"
                        "  PRIMARY KEY (user_id, bait_id)"
                        ")");

    mysql_query(mysql_, "CREATE TABLE IF NOT EXISTS unlocked_rods ("
                        "  user_id VARCHAR(64) NOT NULL,"
                        "  rod_id VARCHAR(64) NOT NULL,"
                        "  unlocked_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                        "  PRIMARY KEY (user_id, rod_id)"
                        ")");

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

    mysql_query(mysql_, "CREATE TABLE IF NOT EXISTS geoguessr_stats ("
                        "  user_id VARCHAR(30) NOT NULL PRIMARY KEY,"
                        "  points BIGINT NOT NULL DEFAULT 0,"
                        "  correct_countries INT NOT NULL DEFAULT 0,"
                        "  exact_cities INT NOT NULL DEFAULT 0,"
                        "  games_played INT NOT NULL DEFAULT 0,"
                        "  current_streak INT NOT NULL DEFAULT 0,"
                        "  max_streak INT NOT NULL DEFAULT 0"
                        ")");

    return true;
}

} // namespace bronx
