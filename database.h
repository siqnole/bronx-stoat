#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <mutex>
#include <mysql.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bronx {

class Database {
public:
  struct RestrictionEntry {
    std::string target_type; // "command" or "module"
    std::string target_name;
    std::string restriction_type; // "allow" or "restrict"
    std::string entity_type;      // "role" or "channel"
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
    std::string welcome_channel_id;    // empty/NULL if disabled
    std::string welcome_type = "text"; // "text" or "embed"
    std::string welcome_content;       // markdown message text
    std::string welcome_embed_json;    // JSON object storing builder fields
    std::string leave_channel_id;      // empty/NULL if disabled
    std::string leave_type = "text";   // "text" or "embed"
    std::string leave_content;
    std::string leave_embed_json;
    std::string autorole_role_id;         // empty/NULL if disabled
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
    std::string action_type;           // "ban", "kick", "timeout", "purge"
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

  struct MasqueradeConfig {
    std::string server_id;
    std::optional<std::string> avatar;
    std::optional<std::string> colour;
    std::optional<std::string> name;
    bool disabled = false;
    bool disable_user = false;
    bool disable_user_name = false;
    bool disable_user_avatar = false;
    bool disable_user_colour = false;
  };

  struct JailConfig {
    std::string server_id;
    std::string jail_role_id;
    std::string jail_channel_id;
  };

  struct ActiveJail {
    std::string server_id;
    std::string user_id;
    std::string roles; // comma-separated roles
    uint64_t jailed_at = 0;
    uint64_t expires_at = 0; // 0 if indefinite
  };

  struct UserMasquerade {
    std::string user_id;
    std::optional<std::string> avatar;
    std::optional<std::string> colour;
    std::optional<std::string> name;
  };

  struct UserRankCard {
    std::string user_id;
    std::string style = "image";
    std::optional<std::string> bg_color;
    std::optional<std::string> bg_image;
    std::optional<std::string> bar_active;
    std::optional<std::string> bar_dimmed;
    std::optional<std::string> text_color;
    std::optional<std::string> font_name;
  };

  struct Tupper {
    int id;
    std::string user_id;
    std::string name;
    std::string avatar_url;
    std::string prefix;
    std::string suffix;
  };

  struct TupperProxy {
    std::string message_id;
    std::string user_id;
    std::string tupper_name;
  };

  struct GuildLevelingConfig {
    std::string server_id;
    bool enabled = false;
    bool reward_coins = true;
    int coins_per_message = 1;
    int min_xp_per_message = 15;
    int max_xp_per_message = 25;
    int min_message_chars = 5;
    int xp_cooldown_seconds = 60;
    std::optional<std::string> announcement_channel;
    bool announce_levelup = true;
    std::string announcement_message = "{name} reached **Level {level}**!";
    bool sticky_roles = true;
  };

  struct LevelRole {
    int id = 0;
    std::string server_id;
    uint32_t level = 1;
    std::string role_id;
    std::string role_name;
    bool remove_previous = false;
  };

  struct UserXP {
    std::string user_id;
    std::string server_id = "0";
    uint64_t total_xp = 0;
    uint32_t level = 1;
    std::string last_message_xp;
  };

  struct XPBlacklistEntry {
    int id = 0;
    std::string server_id;
    std::string target_type; // "channel", "role", "user"
    std::string target_id;
    std::string added_by;
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
    std::string match_type =
        "contains"; // "contains" | "exact" | "starts" | "ends"
    std::string message_type = "text"; // "text" | "embed"
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
    std::string emojis; // comma-separated cleaned emoji IDs / unicode
    AutoScopeFilter filter;
  };

  //  Word Filter
  struct WordFilterGroup {
    int id = 0;
    std::string server_id;
    std::string name;                  // group label
    std::vector<std::string> patterns; // words or regex patterns
    bool server_wide = true; // if true, active in all channels by default
    // channels where the group is explicitly enabled (overrides
    // server_wide=false)
    std::vector<std::string> enabled_channels;
    // channels where the group is explicitly disabled (overrides
    // server_wide=true)
    std::vector<std::string> disabled_channels;
  };

  struct WordFilterWhitelist {
    std::string server_id;
    // entries: plain words, '#'+channel_id, '%'+role_id
    std::vector<std::string> entries;
  };

  struct RecurringMessage {
    int id = 0;
    std::string server_id;
    std::string channel_id;
    std::string name;                  // user-defined label
    std::string message_type = "text"; // "text" or "embed"
    std::string
        content; // plain-text content / pre-embed text (supports tokens)
    std::string embed_json; // JSON object for embed fields (supports tokens)
    uint64_t interval_seconds = 300; // >= 300 (5 min)
    uint64_t last_sent = 0;          // unix timestamp
    bool owner_tokens = false;       // if true, owner-only tokens are resolved
  };

  struct RssFeed {
    int id = 0;
    std::string server_id;
    std::string url;
    std::string channels; // comma-separated channel IDs
    uint64_t last_checked = 0;
    std::string seen_items;    // JSON array of strings
    int check_interval = 1200; // in seconds, default to 20 minutes (1200)
  };

  //  Counting
  struct CountingConfig {
    std::string server_id;
    std::string channel_id;          // channel where counting happens
    int64_t current_number = 0;      // next expected number
    std::string last_user_id;        // user who sent the last valid count
    std::string fail_role_id;        // role ID to assign on a failed count
    uint64_t fail_role_duration = 0; // seconds, 0 = permanent
    bool passive =
        false; // if true, silently ignore wrong msgs instead of breaking count
    bool warn_double_count =
        false; // if true, warn on double-count instead of immediately breaking
    int64_t highest_count = 0; // all-time record
  };

  struct CountingStats {
    std::string server_id;
    std::string user_id;
    int64_t correct_counts = 0;
    int64_t failed_counts = 0;
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
    std::string view_roles; // raw comma-separated list of role IDs
    std::string view_users; // raw comma-separated list of user IDs
    std::string
        view_permissions; // raw comma-separated list of Revolt permission names
    std::string ticket_welcome_content;
    std::string ticket_welcome_embed_json;
    std::string ticket_close_emoji = "🔒";
    std::string ticket_close_content =
        "🔒 **Closing ticket...**\nThis channel will be deleted in 5 seconds.";
    std::string member_role_id; // role to DENY in the ticket channel (the
                                // server's @member role)
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

  //  Social Subscriptions
  struct SocialSubscription {
    int id = 0;
    std::string server_id;
    std::string
        platform; // "youtube" | "twitch" | "tiktok" | "twitter" | "bluesky"
    std::string channel_handle; // e.g. "@MrBeast", "xqc", "did:plc:..."
    std::string channels;       // comma-separated Revolt channel IDs to post to
    std::string events;         // comma-separated: "live","vod","post" etc.
    uint64_t last_checked = 0;
    std::string seen_items; // JSON array of already-posted IDs
  };

  // Per-event embed config (same flexibility as welcome/logging embeds)
  struct SocialEmbedConfig {
    int sub_id = 0;                     // FK → social_subscriptions.id
    std::string event_type;             // "live" | "vod" | "post" | "clip"
    std::string message_type = "embed"; // "text" | "embed"
    std::string content;                // plain-text prefix / mention
    std::string embed_json;             // JSON for the embed builder
  };

  //  Command Aliases
  struct ServerCommandAlias {
    std::string server_id;
    std::string alias;  // the short-hand the user types
    std::string target; // the canonical command it maps to
  };
  struct UserCommandAlias {
    std::string user_id;
    std::string alias;
    std::string target;
  };

private:
  MYSQL *mysql_ = nullptr;
  std::unordered_map<std::string, UserAFK> user_afk_cache_;
  std::shared_mutex user_afk_cache_mutex_;

  std::unordered_map<std::string, RobloxLink> roblox_links_cache_;
  std::shared_mutex roblox_links_cache_mutex_;

  std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
      user_afk_presets_cache_;
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
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
      log_cache_;
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
  std::unordered_map<std::string, std::unordered_map<std::string, LoggingEmbed>>
      log_embed_cache_;
  std::shared_mutex log_embed_cache_mutex_;

  // Server ID -> {Action Type -> ActionMessageConfig}
  std::unordered_map<std::string,
                     std::unordered_map<std::string, ActionMessageConfig>>
      action_msg_cache_;
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

  // id -> RssFeed
  std::unordered_map<int, RssFeed> rss_cache_;
  // server_id -> list of IDs
  std::unordered_map<std::string, std::vector<int>> rss_server_index_;
  std::shared_mutex rss_cache_mutex_;

  // id -> SocialSubscription
  std::unordered_map<int, SocialSubscription> social_sub_cache_;
  std::unordered_map<std::string, std::vector<int>> social_sub_server_index_;
  std::shared_mutex social_sub_cache_mutex_;

  // sub_id:event_type -> SocialEmbedConfig
  std::unordered_map<std::string, SocialEmbedConfig> social_embed_cache_;
  std::shared_mutex social_embed_cache_mutex_;

  // server_id -> { alias -> target }
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
      server_alias_cache_;
  std::shared_mutex server_alias_cache_mutex_;

  // user_id -> { alias -> target }
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
      user_alias_cache_;
  std::shared_mutex user_alias_cache_mutex_;

  // server_id -> user aliases disabled?
  std::unordered_map<std::string, bool> server_alias_settings_cache_;
  std::shared_mutex server_alias_settings_cache_mutex_;

  // id -> AutoresponderConfig
  std::unordered_map<int, AutoresponderConfig> arsp_cache_;
  std::unordered_map<std::string, std::vector<int>> arsp_server_index_;
  std::shared_mutex arsp_cache_mutex_;

  // id -> AutoreactConfig
  std::unordered_map<int, AutoreactConfig> arct_cache_;
  std::unordered_map<std::string, std::vector<int>> arct_server_index_;
  std::shared_mutex arct_cache_mutex_;

  // Server ID -> Set of "Type:ID"
  std::unordered_map<std::string, std::unordered_set<std::string>>
      trusted_cache_;
  std::shared_mutex trusted_cache_mutex_;

  // Server ID -> List of restrictions
  std::unordered_map<std::string, std::vector<RestrictionEntry>>
      restrictions_cache_;
  std::shared_mutex restrictions_cache_mutex_;

  // Server ID -> Set of "Type:Name"
  std::unordered_map<std::string, std::unordered_set<std::string>>
      disabled_cache_;
  std::shared_mutex disabled_cache_mutex_;

  // Server ID -> default prefix enabled (true = default prefix active)
  std::unordered_map<std::string, bool> pfx_default_cache_;
  std::shared_mutex pfx_default_cache_mutex_;

  std::unordered_map<std::string, MasqueradeConfig> masquerade_cache_;
  std::shared_mutex masquerade_cache_mutex_;

  std::unordered_map<std::string, UserMasquerade> user_masq_cache_;
  std::shared_mutex user_masq_cache_mutex_;

  std::unordered_map<std::string, UserRankCard> user_rank_card_cache_;
  std::shared_mutex user_rank_card_cache_mutex_;

  std::unordered_map<std::string, std::vector<Tupper>> tupper_cache_;
  std::shared_mutex tupper_cache_mutex_;

  // Leveling caches
  std::unordered_map<std::string, GuildLevelingConfig> leveling_config_cache_;
  std::shared_mutex leveling_config_cache_mutex_;

  std::unordered_map<std::string, std::vector<LevelRole>> level_roles_cache_;
  std::shared_mutex level_roles_cache_mutex_;

  std::unordered_map<std::string, std::vector<XPBlacklistEntry>>
      xp_blacklist_cache_;
  std::shared_mutex xp_blacklist_cache_mutex_;

  std::string escape_string(const std::string &input) {
    if (!mysql_)
      return "";
    std::vector<char> buffer(input.size() * 2 + 1);
    mysql_real_escape_string(mysql_, buffer.data(), input.c_str(),
                             input.size());
    return std::string(buffer.data());
  }

public:
  Database() = default;

  std::function<void(const std::string &, int64_t, int64_t)> on_balance_update =
      nullptr;
  std::function<void(const UserAFK &)> on_afk_save = nullptr;

  ~Database() {
    if (mysql_) {
      mysql_close(mysql_);
    }
  }

  bool init(const std::string &host, const std::string &user,
            const std::string &password, const std::string &dbname,
            unsigned int port) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    mysql_ = mysql_init(nullptr);
    if (!mysql_) {
      std::cerr << "[bronx db] mysql_init failed\n";
      return false;
    }

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
        "  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE "
        "CURRENT_TIMESTAMP"
        ")";
    if (mysql_query(mysql_, create_servers_table.c_str())) {
      std::cerr << "[bronx db] failed to create table 'servers': "
                << mysql_error(mysql_) << "\n";
      return false;
    }

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
          cfg.xp_cooldown_seconds = row[7] ? std::stoi(row[7]) : 60;
          if (row[8])
            cfg.announcement_channel = row[8];
          cfg.announce_levelup = row[9] && std::string(row[9]) == "1";
          cfg.announcement_message = row[10] ? row[10] : "";
          cfg.sticky_roles = row[11] && std::string(row[11]) == "1";
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

    std::cout << "[bronx db] database successfully initialized.\n";
    return true;
  }

  std::string create_dashboard_token(const std::string &user_id,
                                     const std::string &user_name) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return "";

    static const char hex_chars[] = "0123456789abcdef";
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 15);

    std::string token = "brx_tkn_";
    for (int i = 0; i < 32; ++i) {
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

  void upsert_server(const std::string &server_id, const std::string &name,
                     const std::string &icon_id = "") {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_ || server_id.empty())
      return;

    std::string esc_id = escape_string(server_id);
    std::string esc_name = escape_string(name);
    std::string esc_icon = escape_string(icon_id);

    std::string sql =
        "INSERT INTO servers (server_id, name, icon_id) VALUES ('" + esc_id +
        "', '" + esc_name + "', '" + esc_icon +
        "') "
        "ON DUPLICATE KEY UPDATE name=VALUES(name), icon_id=VALUES(icon_id)";
    mysql_query(mysql_, sql.c_str());
  }

  void upsert_server_member(const std::string &server_id,
                            const std::string &user_id, bool is_owner = false,
                            bool is_admin = false) {
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

  void print_cache_stats() {
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

  std::vector<std::string> get_prefixes(const std::string &target_type,
                                        const std::string &target_id) {
    std::shared_lock<std::shared_mutex> lock(cache_mutex_);
    std::string cache_key = target_type + ":" + target_id;
    auto it = cache_.find(cache_key);
    if (it != cache_.end()) {
      return it->second;
    }
    return {};
  }

  bool set_prefixes(const std::string &target_type,
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

  bool remove_prefixes(const std::string &target_type,
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

  // Returns true if the default prefix is enabled for this server (default:
  // true).
  bool get_prefix_default_enabled(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(pfx_default_cache_mutex_);
    auto it = pfx_default_cache_.find(server_id);
    if (it != pfx_default_cache_.end())
      return it->second;
    return true; // default: enabled
  }

  // Sets whether the default prefix is active for a server.
  // Persists to the prefixes table (upserts a row with no custom prefixes if
  // needed).
  bool set_prefix_default_enabled(const std::string &server_id, bool enabled) {
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

  // --- Reaction Roles Methods ---

  bool add_rr_message(const std::string &message_id,
                      const std::string &channel_id,
                      const std::string &server_id,
                      const std::string &modal_name,
                      const std::string &type = "standard") {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_msg = escape_string(message_id);
    std::string esc_ch = escape_string(channel_id);
    std::string esc_srv = escape_string(server_id);
    std::string esc_modal = escape_string(modal_name);
    std::string esc_type = escape_string(type);

    std::string query = "INSERT INTO rr_messages (message_id, channel_id, "
                        "server_id, modal_name, type) "
                        "VALUES ('" +
                        esc_msg + "', '" + esc_ch + "', '" + esc_srv + "', " +
                        (modal_name.empty() ? "NULL" : "'" + esc_modal + "'") +
                        ", '" + esc_type +
                        "') "
                        "ON DUPLICATE KEY UPDATE channel_id = '" +
                        esc_ch + "', server_id = '" + esc_srv +
                        "', modal_name = " +
                        (modal_name.empty() ? "NULL" : "'" + esc_modal + "'") +
                        ", type = '" + esc_type + "'";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save rr_message: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    {
      std::unique_lock<std::shared_mutex> lock(rr_cache_mutex_);
      RRMessage &msg = rr_cache_[message_id];
      msg.message_id = message_id;
      msg.channel_id = channel_id;
      msg.server_id = server_id;
      msg.modal_name = modal_name;
      msg.type = type;
    }
    return true;
  }

  bool update_rr_message_type(const std::string &message_id,
                              const std::string &type) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string esc_msg = escape_string(message_id);
    std::string esc_type = escape_string(type);

    std::string query = "UPDATE rr_messages SET type = '" + esc_type +
                        "' WHERE message_id = '" + esc_msg + "'";
    if (mysql_query(mysql_, query.c_str()) == 0) {
      std::unique_lock<std::shared_mutex> lock(rr_cache_mutex_);
      if (rr_cache_.find(message_id) != rr_cache_.end()) {
        rr_cache_[message_id].type = type;
      }
      return true;
    }
    return false;
  }

  bool update_rr_embed_json(const std::string &message_id,
                            const std::string &embed_json) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string esc_msg = escape_string(message_id);
    std::string esc_json = escape_string(embed_json);

    std::string query = "UPDATE rr_messages SET embed_json = " +
                        (embed_json.empty() ? "NULL" : "'" + esc_json + "'") +
                        " WHERE message_id = '" + esc_msg + "'";
    if (mysql_query(mysql_, query.c_str()) == 0) {
      std::unique_lock<std::shared_mutex> lock(rr_cache_mutex_);
      if (rr_cache_.find(message_id) != rr_cache_.end()) {
        rr_cache_[message_id].embed_json = embed_json;
      }
      return true;
    }
    return false;
  }

  bool add_rr_rule(const std::string &message_id, const std::string &emoji_id,
                   const std::string &role_id,
                   const std::string &rule_type = "add") {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_msg = escape_string(message_id);
    std::string esc_emoji = escape_string(emoji_id);
    std::string esc_role = escape_string(role_id);
    std::string esc_rule_type = escape_string(rule_type);

    std::string query =
        "INSERT INTO rr_rules (message_id, emoji_id, role_id, rule_type) "
        "VALUES ('" +
        esc_msg + "', '" + esc_emoji + "', '" + esc_role + "', '" +
        esc_rule_type +
        "') "
        "ON DUPLICATE KEY UPDATE role_id = role_id, rule_type = '" +
        esc_rule_type + "'";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save rr_rule: " << mysql_error(mysql_)
                << "\n";
      return false;
    }

    {
      std::lock_guard<std::shared_mutex> cache_lock(rr_cache_mutex_);
      auto it = rr_cache_.find(message_id);
      if (it != rr_cache_.end()) {
        bool found = false;
        for (auto &rule : it->second.rules) {
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

  bool remove_rr_rule(const std::string &message_id,
                      const std::string &emoji_id,
                      const std::string &role_id = "") {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_msg = escape_string(message_id);
    std::string esc_emoji = escape_string(emoji_id);

    std::string query = "DELETE FROM rr_rules WHERE message_id = '" + esc_msg +
                        "' AND emoji_id = '" + esc_emoji + "'";
    if (!role_id.empty()) {
      query += " AND role_id = '" + escape_string(role_id) + "'";
    }

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to delete rr_rule: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    {
      std::lock_guard<std::shared_mutex> cache_lock(rr_cache_mutex_);
      auto it = rr_cache_.find(message_id);
      if (it != rr_cache_.end()) {
        auto &rules = it->second.rules;
        rules.erase(std::remove_if(rules.begin(), rules.end(),
                                   [&](const RRRule &r) {
                                     if (role_id.empty()) {
                                       return r.emoji_id == emoji_id;
                                     } else {
                                       return r.emoji_id == emoji_id &&
                                              r.role_id == role_id;
                                     }
                                   }),
                    rules.end());
      }
    }
    return true;
  }

  bool delete_rr_message(const std::string &message_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_msg = escape_string(message_id);

    std::string query =
        "DELETE FROM rr_messages WHERE message_id = '" + esc_msg + "'";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to delete rr_message: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    {
      std::lock_guard<std::shared_mutex> cache_lock(rr_cache_mutex_);
      rr_cache_.erase(message_id);
    }
    return true;
  }

  std::optional<RRMessage> get_rr_message(const std::string &message_id) {
    std::shared_lock<std::shared_mutex> lock(rr_cache_mutex_);
    auto it = rr_cache_.find(message_id);
    if (it != rr_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  std::string get_message_id_by_modal_name(const std::string &server_id,
                                           const std::string &modal_name) {
    std::shared_lock<std::shared_mutex> lock(rr_cache_mutex_);
    for (const auto &[msg_id, msg] : rr_cache_) {
      if (msg.server_id == server_id && msg.modal_name == modal_name) {
        return msg_id;
      }
    }
    return "";
  }

  std::vector<std::string>
  get_message_ids_by_modal(const std::string &server_id,
                           const std::string &modal_name) {
    std::shared_lock<std::shared_mutex> lock(rr_cache_mutex_);
    std::vector<std::string> ids;
    for (const auto &[msg_id, msg] : rr_cache_) {
      if (msg.server_id == server_id && msg.modal_name == modal_name) {
        ids.push_back(msg_id);
      }
    }
    return ids;
  }

  std::vector<ModalInfo> get_server_modals(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(rr_cache_mutex_);
    std::unordered_map<std::string, std::pair<int, int>> modal_counts;
    for (const auto &[msg_id, msg] : rr_cache_) {
      if (msg.server_id == server_id && !msg.modal_name.empty()) {
        auto &p = modal_counts[msg.modal_name];
        p.second++;
        if (p.first == 0) {
          p.first = msg.rules.size();
        }
      }
    }

    std::vector<ModalInfo> result;
    for (const auto &[name, counts] : modal_counts) {
      result.push_back({name, counts.first, counts.second});
    }
    return result;
  }

  bool increment_user_daily_messages(const std::string &guild_id,
                                     const std::string &user_id, bool is_bot,
                                     const std::string &type = "message") {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string col = "messages";
    if (type == "edit")
      col = "edits";
    else if (type == "delete")
      col = "deletes";

    std::string sql = "INSERT INTO guild_user_activity_daily (guild_id, "
                      "user_id, stat_date, is_bot, " +
                      col + ") VALUES ('" + escape_string(guild_id) + "', '" +
                      escape_string(user_id) + "', CURDATE(), " +
                      (is_bot ? "TRUE" : "FALSE") +
                      ", 1) "
                      "ON DUPLICATE KEY UPDATE " +
                      col + " = " + col + " + 1, is_bot = VALUES(is_bot)";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool increment_user_daily_commands(const std::string &guild_id,
                                     const std::string &user_id, bool is_bot) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "INSERT INTO guild_user_activity_daily (guild_id, "
                      "user_id, stat_date, is_bot, commands_used) VALUES ('" +
                      escape_string(guild_id) + "', '" +
                      escape_string(user_id) + "', CURDATE(), " +
                      (is_bot ? "TRUE" : "FALSE") +
                      ", 1) "
                      "ON DUPLICATE KEY UPDATE commands_used = commands_used + "
                      "1, is_bot = VALUES(is_bot)";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool increment_channel_daily_messages(const std::string &guild_id,
                                        const std::string &channel_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "INSERT INTO guild_channel_activity_daily (guild_id, "
                      "channel_id, stat_date, messages) VALUES ('" +
                      escape_string(guild_id) + "', '" +
                      escape_string(channel_id) +
                      "', CURDATE(), 1) "
                      "ON DUPLICATE KEY UPDATE messages = messages + 1";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool increment_user_channel_messages(const std::string &guild_id,
                                       const std::string &channel_id,
                                       const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT INTO guild_user_channel_activity_daily (guild_id, channel_id, "
        "user_id, stat_date, messages) VALUES ('" +
        escape_string(guild_id) + "', '" + escape_string(channel_id) + "', '" +
        escape_string(user_id) +
        "', CURDATE(), 1) "
        "ON DUPLICATE KEY UPDATE messages = messages + 1";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool increment_user_channel_commands(const std::string &guild_id,
                                       const std::string &channel_id,
                                       const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT INTO guild_user_channel_activity_daily (guild_id, channel_id, "
        "user_id, stat_date, commands_used) VALUES ('" +
        escape_string(guild_id) + "', '" + escape_string(channel_id) + "', '" +
        escape_string(user_id) +
        "', CURDATE(), 1) "
        "ON DUPLICATE KEY UPDATE commands_used = commands_used + 1";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool increment_guild_member_joins(const std::string &guild_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "INSERT INTO guild_member_events_daily (guild_id, "
                      "event_date, joins, leaves) VALUES ('" +
                      escape_string(guild_id) +
                      "', CURDATE(), 1, 0) "
                      "ON DUPLICATE KEY UPDATE joins = joins + 1";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool increment_guild_member_leaves(const std::string &guild_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "INSERT INTO guild_member_events_daily (guild_id, "
                      "event_date, joins, leaves) VALUES ('" +
                      escape_string(guild_id) +
                      "', CURDATE(), 0, 1) "
                      "ON DUPLICATE KEY UPDATE leaves = leaves + 1";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  struct UserChannelActivityEntry {
    std::string channel_id;
    int messages = 0;
    int commands_used = 0;
  };

  struct UserGuildActivityEntry {
    std::string guild_id;
    int messages = 0;
    int commands_used = 0;
  };

  std::vector<UserChannelActivityEntry>
  get_user_channel_stats(const std::string &guild_id,
                         const std::string &user_id, int days) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<UserChannelActivityEntry> entries;
    if (!mysql_)
      return entries;

    std::string sql = "SELECT channel_id, SUM(messages), SUM(commands_used) "
                      "FROM guild_user_channel_activity_daily "
                      "WHERE guild_id = '" +
                      escape_string(guild_id) +
                      "' "
                      "AND user_id = '" +
                      escape_string(user_id) +
                      "' "
                      "AND stat_date >= DATE_SUB(CURDATE(), INTERVAL " +
                      std::to_string(days) +
                      " DAY) "
                      "GROUP BY channel_id ORDER BY SUM(messages) DESC";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          if (row[0]) {
            UserChannelActivityEntry entry;
            entry.channel_id = row[0];
            entry.messages = row[1] ? std::stoi(row[1]) : 0;
            entry.commands_used = row[2] ? std::stoi(row[2]) : 0;
            entries.push_back(entry);
          }
        }
        mysql_free_result(res);
      }
    }
    return entries;
  }

  std::vector<UserGuildActivityEntry>
  get_user_global_stats(const std::string &user_id, int days) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<UserGuildActivityEntry> entries;
    if (!mysql_)
      return entries;

    std::string sql = "SELECT guild_id, SUM(messages), SUM(commands_used) FROM "
                      "guild_user_activity_daily "
                      "WHERE user_id = '" +
                      escape_string(user_id) +
                      "' "
                      "AND stat_date >= DATE_SUB(CURDATE(), INTERVAL " +
                      std::to_string(days) +
                      " DAY) "
                      "GROUP BY guild_id ORDER BY SUM(messages) DESC";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          if (row[0]) {
            UserGuildActivityEntry entry;
            entry.guild_id = row[0];
            entry.messages = row[1] ? std::stoi(row[1]) : 0;
            entry.commands_used = row[2] ? std::stoi(row[2]) : 0;
            entries.push_back(entry);
          }
        }
        mysql_free_result(res);
      }
    }
    return entries;
  }

  // Corrects historical rows for a known-bot user that were stored with
  // is_bot=FALSE. Called once on Ready for every bot user found in the Ready
  // payload.
  bool fix_bot_flags(const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_ || user_id.empty())
      return false;
    std::string sql = "UPDATE guild_user_activity_daily SET is_bot = TRUE "
                      "WHERE user_id = '" +
                      escape_string(user_id) + "' AND is_bot = FALSE";
    if (mysql_query(mysql_, sql.c_str()) != 0)
      return false;
    auto affected = mysql_affected_rows(mysql_);
    if (affected > 0) {
      std::cout << "[bronx db] fixed " << affected << " row(s): marked user "
                << user_id << " as bot.\n";
    }
    return true;
  }

  bool increment_command_usage(const std::string &guild_id,
                               const std::string &command_name,
                               const std::string &channel_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT INTO guild_command_usage (guild_id, command_name, channel_id, "
        "usage_date, use_count) VALUES ('" +
        escape_string(guild_id) + "', '" + escape_string(command_name) +
        "', '" + escape_string(channel_id) +
        "', CURDATE(), 1) "
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

  std::vector<TopUserEntry> get_top_users(const std::string &guild_id, int days,
                                          bool include_bots = false,
                                          int limit = 10) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<TopUserEntry> entries;
    if (!mysql_)
      return entries;

    std::string sql =
        "SELECT user_id, SUM(messages) FROM guild_user_activity_daily "
        "WHERE guild_id = '" +
        escape_string(guild_id) + "' " +
        (include_bots ? "" : "AND is_bot = FALSE ") +
        "AND stat_date >= DATE_SUB(CURDATE(), INTERVAL " +
        std::to_string(days) +
        " DAY) "
        "GROUP BY user_id ORDER BY SUM(messages) DESC LIMIT " +
        std::to_string(limit);
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
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

  std::vector<TopCmdEntry> get_top_commands(const std::string &guild_id,
                                            int days, int limit = 10) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<TopCmdEntry> entries;
    if (!mysql_)
      return entries;

    std::string sql =
        "SELECT command_name, SUM(use_count) FROM guild_command_usage "
        "WHERE guild_id = '" +
        escape_string(guild_id) +
        "' "
        "AND usage_date >= DATE_SUB(CURDATE(), INTERVAL " +
        std::to_string(days) +
        " DAY) "
        "GROUP BY command_name ORDER BY SUM(use_count) DESC LIMIT " +
        std::to_string(limit);
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
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

  std::vector<TopCmdEntry> get_top_commands_global(int days, int limit = 10) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<TopCmdEntry> entries;
    if (!mysql_)
      return entries;

    std::string sql =
        "SELECT command_name, SUM(use_count) FROM guild_command_usage "
        "WHERE usage_date >= DATE_SUB(CURDATE(), INTERVAL " +
        std::to_string(days) +
        " DAY) "
        "GROUP BY command_name ORDER BY SUM(use_count) DESC LIMIT " +
        std::to_string(limit);
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
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

  std::vector<TopServerEntry>
  get_top_servers(int days, bool include_bots = false, int limit = 10) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<TopServerEntry> entries;
    if (!mysql_)
      return entries;
    std::string sql =
        "SELECT guild_id, SUM(messages) FROM guild_user_activity_daily "
        "WHERE stat_date >= DATE_SUB(CURDATE(), INTERVAL " +
        std::to_string(days) + " DAY) " +
        (include_bots ? "" : "AND is_bot = FALSE ") +
        "GROUP BY guild_id ORDER BY SUM(messages) DESC LIMIT " +
        std::to_string(limit);
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
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

  std::vector<TopUserEntry> get_top_users_global(int days, bool bots,
                                                 int limit = 10) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<TopUserEntry> entries;
    if (!mysql_)
      return entries;
    std::string sql =
        "SELECT user_id, SUM(messages) FROM guild_user_activity_daily "
        "WHERE stat_date >= DATE_SUB(CURDATE(), INTERVAL " +
        std::to_string(days) +
        " DAY) "
        "AND is_bot = " +
        (bots ? "TRUE" : "FALSE") +
        " "
        "GROUP BY user_id ORDER BY SUM(messages) DESC LIMIT " +
        std::to_string(limit);
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
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

  std::vector<TopChannelEntry> get_top_channels(const std::string &guild_id,
                                                int days, int limit = 10) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<TopChannelEntry> entries;
    if (!mysql_)
      return entries;
    std::string sql =
        "SELECT channel_id, SUM(messages) FROM guild_channel_activity_daily "
        "WHERE guild_id = '" +
        escape_string(guild_id) +
        "' "
        "AND stat_date >= DATE_SUB(CURDATE(), INTERVAL " +
        std::to_string(days) +
        " DAY) "
        "GROUP BY channel_id ORDER BY SUM(messages) DESC LIMIT " +
        std::to_string(limit);
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
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

  ServerTotals get_server_totals(const std::string &guild_id, int days,
                                 bool include_bots = false) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    ServerTotals totals;
    if (!mysql_)
      return totals;

    std::string msg_sql = "SELECT SUM(messages) FROM guild_user_activity_daily "
                          "WHERE guild_id = '" +
                          escape_string(guild_id) + "' " +
                          (include_bots ? "" : "AND is_bot = FALSE ") +
                          "AND stat_date >= DATE_SUB(CURDATE(), INTERVAL " +
                          std::to_string(days) + " DAY)";
    if (mysql_query(mysql_, msg_sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0]) {
          totals.total_messages = std::stoi(row[0]);
        }
        mysql_free_result(res);
      }
    }

    std::string cmd_sql = "SELECT SUM(use_count) FROM guild_command_usage "
                          "WHERE guild_id = '" +
                          escape_string(guild_id) +
                          "' "
                          "AND usage_date >= DATE_SUB(CURDATE(), INTERVAL " +
                          std::to_string(days) + " DAY)";
    if (mysql_query(mysql_, cmd_sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0]) {
          totals.total_commands = std::stoi(row[0]);
        }
        mysql_free_result(res);
      }
    }

    std::string users_sql =
        "SELECT COUNT(DISTINCT user_id) FROM guild_user_activity_daily "
        "WHERE guild_id = '" +
        escape_string(guild_id) + "' " +
        (include_bots ? "" : "AND is_bot = FALSE ") +
        "AND stat_date >= DATE_SUB(CURDATE(), INTERVAL " +
        std::to_string(days) + " DAY)";
    if (mysql_query(mysql_, users_sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
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

  //  Chart data queries

  // Daily message totals for a server over the last N days.
  // Returns pairs of (date_string "MM-DD", message_count) ordered
  // oldest→newest.
  std::vector<std::pair<std::string, int64_t>>
  get_daily_message_series(const std::string &guild_id, int days,
                           bool include_bots = false) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<std::pair<std::string, int64_t>> result;
    if (!mysql_)
      return result;
    std::string sql = "SELECT DATE_FORMAT(stat_date,'%m-%d'), SUM(messages) "
                      "FROM guild_user_activity_daily "
                      "WHERE guild_id = '" +
                      escape_string(guild_id) + "' " +
                      (include_bots ? "" : "AND is_bot = FALSE ") +
                      "AND stat_date >= DATE_SUB(CURDATE(), INTERVAL " +
                      std::to_string(days) +
                      " DAY) "
                      "GROUP BY stat_date ORDER BY stat_date ASC";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
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
  std::vector<std::vector<double>>
  get_hourly_heatmap(const std::string &guild_id, int days,
                     bool include_bots = false) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<std::vector<double>> matrix(7, std::vector<double>(24, 0.0));
    if (!mysql_)
      return matrix;
    // guild_user_activity_daily only has per-day granularity, so we use
    // moderation_logs created_at and message event times if available.
    // For now, distribute each day's messages evenly weighted by a heuristic.
    // The heatmap becomes accurate once we add hour-level tracking.
    // Query: sum messages grouped by weekday of stat_date as a proxy.
    std::string sql = "SELECT DAYOFWEEK(stat_date)-1, SUM(messages) "
                      "FROM guild_user_activity_daily "
                      "WHERE guild_id = '" +
                      escape_string(guild_id) + "' " +
                      (include_bots ? "" : "AND is_bot = FALSE ") +
                      "AND stat_date >= DATE_SUB(CURDATE(), INTERVAL " +
                      std::to_string(days) +
                      " DAY) "
                      "GROUP BY DAYOFWEEK(stat_date)";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          if (!row[0] || !row[1])
            continue;
          int dow = std::stoi(row[0]); // 0=Sun..6=Sat
          double total = std::stod(row[1]);
          if (dow < 0 || dow >= 7)
            continue;
          // Distribute across a realistic activity curve (peak 10-22h)
          const double weights[24] = {
              0.5, 0.3, 0.2, 0.1, 0.1, 0.2, // 0-5
              0.5, 0.8, 1.0, 1.1, 1.2, 1.3, // 6-11
              1.3, 1.3, 1.2, 1.2, 1.3, 1.4, // 12-17
              1.5, 1.5, 1.4, 1.2, 1.0, 0.7  // 18-23
          };
          double wsum = 0;
          for (auto w : weights)
            wsum += w;
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

  bool ensure_economy_user(const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "INSERT IGNORE INTO users (user_id) VALUES ('" +
                      escape_string(user_id) + "')";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::optional<EconomyUser> get_economy_user(const std::string &user_id) {
    ensure_economy_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return std::nullopt;

    std::string sql =
        "SELECT wallet, bank, bank_limit, prestige, "
        "IFNULL(last_beg, ''), IFNULL(last_work, ''), IFNULL(last_rob, '') "
        "FROM users WHERE user_id = '" +
        escape_string(user_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
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

  bool update_balances(const std::string &user_id, int64_t wallet_delta,
                       int64_t bank_delta) {
    ensure_economy_user(user_id);
    bool success = false;
    {
      std::lock_guard<std::mutex> lock(db_mutex_);
      if (!mysql_)
        return false;
      std::string sql = "UPDATE users SET wallet = wallet + " +
                        std::to_string(wallet_delta) + ", bank = bank + " +
                        std::to_string(bank_delta) + " WHERE user_id = '" +
                        escape_string(user_id) + "'";
      success = (mysql_query(mysql_, sql.c_str()) == 0);
    }
    if (success && on_balance_update) {
      on_balance_update(user_id, wallet_delta, bank_delta);
    }
    return success;
  }

  bool prestige_user(const std::string &user_id, int64_t new_bank_limit) {
    ensure_economy_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "UPDATE users SET wallet = 0, bank = 0, prestige = "
                      "prestige + 1, bank_limit = " +
                      std::to_string(new_bank_limit) + " WHERE user_id = '" +
                      escape_string(user_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool update_beg_cooldown(const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "UPDATE users SET last_beg = CURRENT_TIMESTAMP WHERE user_id = '" +
        escape_string(user_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool update_work_cooldown(const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "UPDATE users SET last_work = CURRENT_TIMESTAMP WHERE user_id = '" +
        escape_string(user_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool update_rob_cooldown(const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "UPDATE users SET last_rob = CURRENT_TIMESTAMP WHERE user_id = '" +
        escape_string(user_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  int get_cooldown_remaining(const std::string &user_id,
                             const std::string &field, int cooldown_seconds) {
    ensure_economy_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return 0;
    std::string sql = "SELECT GREATEST(0, CAST(" +
                      std::to_string(cooldown_seconds) +
                      " AS SIGNED) - TIMESTAMPDIFF(SECOND, " + field +
                      ", CURRENT_TIMESTAMP)) "
                      "FROM users WHERE user_id = '" +
                      escape_string(user_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
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
    if (!mysql_)
      return entries;

    std::string sql = "SELECT user_id, (wallet + bank) AS networth FROM users "
                      "ORDER BY networth DESC LIMIT " +
                      std::to_string(limit);
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
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

  //  General Economy Items & Custom Catalogs System
  struct UserEconomyItem {
    std::string item_id;
    int quantity = 0;
    int uses_left = 0;
    uint64_t active_until = 0;
  };

  struct CustomShopItem {
    std::string item_id;
    std::string name;
    std::string type = "general";
    int64_t cost = 0;
    std::string description;
    int uses_default = 0;
    int duration_seconds = 0;
  };

  struct CustomRodRow {
    std::string rod_id;
    std::string name;
    int64_t cost = 0;
    int token_cost = 0;
    int req_prestige = 0;
    double luck_mult = 1.0;
    int base_cd = 30;
    std::string description;
  };

  struct CustomBaitRow {
    std::string bait_id;
    std::string name;
    int pack_size = 10;
    int64_t cost = 0;
    int token_cost = 0;
    int req_prestige = 0;
    double luck_bonus = 0.0;
    int cd_delta = 0;
    std::string description;
  };

  struct CustomFishRow {
    std::string fish_id;
    std::string name;
    std::string rarity = "Common";
    double weight_min = 1.0;
    double weight_max = 5.0;
    int64_t val_min = 50;
    int64_t val_max = 150;
    double base_odds = 0.50;
  };

  bool set_user_balances(const std::string &user_id, int64_t wallet,
                         int64_t bank) {
    ensure_economy_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "UPDATE users SET wallet = " + std::to_string(wallet) +
                      ", bank = " + std::to_string(bank) +
                      " WHERE user_id = '" + escape_string(user_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool set_user_prestige(const std::string &user_id, int prestige_level) {
    ensure_economy_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "UPDATE users SET prestige = " + std::to_string(prestige_level) +
        " WHERE user_id = '" + escape_string(user_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool set_fishing_prestige(const std::string &user_id, int prestige_level) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "UPDATE user_fishing SET fish_prestige = " +
                      std::to_string(prestige_level) + " WHERE user_id = '" +
                      escape_string(user_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool add_user_economy_item(const std::string &user_id,
                             const std::string &item_id, int quantity = 1,
                             int uses = 0, int duration_seconds = 0) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string esc_u = escape_string(user_id);
    std::string esc_i = escape_string(item_id);

    uint64_t active_until = 0;
    if (duration_seconds > 0) {
      auto now = std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
      active_until = now + duration_seconds;
    }

    std::string sql =
        "INSERT INTO user_economy_items (user_id, item_id, quantity, "
        "uses_left, active_until) "
        "VALUES ('" +
        esc_u + "', '" + esc_i + "', " + std::to_string(quantity) + ", " +
        std::to_string(uses) + ", " + std::to_string(active_until) +
        ") "
        "ON DUPLICATE KEY UPDATE quantity = quantity + " +
        std::to_string(quantity) + ", uses_left = uses_left + " +
        std::to_string(uses) +
        (duration_seconds > 0
             ? (", active_until = " + std::to_string(active_until))
             : "");
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool remove_user_economy_item(const std::string &user_id,
                                const std::string &item_id, int quantity = 1) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string esc_u = escape_string(user_id);
    std::string esc_i = escape_string(item_id);

    std::string sql_sel =
        "SELECT quantity FROM user_economy_items WHERE user_id = '" + esc_u +
        "' AND item_id = '" + esc_i + "'";
    if (mysql_query(mysql_, sql_sel.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          int curr_q = std::stoi(row[0]);
          mysql_free_result(res);
          if (curr_q <= quantity) {
            std::string sql_del =
                "DELETE FROM user_economy_items WHERE user_id = '" + esc_u +
                "' AND item_id = '" + esc_i + "'";
            return mysql_query(mysql_, sql_del.c_str()) == 0;
          } else {
            std::string sql_upd =
                "UPDATE user_economy_items SET quantity = quantity - " +
                std::to_string(quantity) + " WHERE user_id = '" + esc_u +
                "' AND item_id = '" + esc_i + "'";
            return mysql_query(mysql_, sql_upd.c_str()) == 0;
          }
        }
        mysql_free_result(res);
      }
    }
    return false;
  }

  std::vector<UserEconomyItem>
  get_user_economy_items(const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<UserEconomyItem> items;
    if (!mysql_)
      return items;

    std::string sql = "SELECT item_id, quantity, uses_left, active_until FROM "
                      "user_economy_items WHERE user_id = '" +
                      escape_string(user_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          UserEconomyItem it;
          it.item_id = row[0] ? row[0] : "";
          it.quantity = row[1] ? std::stoi(row[1]) : 0;
          it.uses_left = row[2] ? std::stoi(row[2]) : 0;
          it.active_until = row[3] ? std::stoull(row[3]) : 0;
          items.push_back(it);
        }
        mysql_free_result(res);
      }
    }
    return items;
  }

  bool is_item_active(const std::string &user_id, const std::string &item_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "SELECT active_until FROM user_economy_items WHERE user_id = '" +
        escape_string(user_id) + "' AND item_id = '" + escape_string(item_id) +
        "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0]) {
          uint64_t until = std::stoull(row[0]);
          auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
          mysql_free_result(res);
          return (until > static_cast<uint64_t>(now));
        }
        mysql_free_result(res);
      }
    }
    return false;
  }

  bool activate_item(const std::string &user_id, const std::string &item_id,
                     int duration_seconds) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    uint64_t active_until = now + duration_seconds;
    std::string sql = "UPDATE user_economy_items SET active_until = " +
                      std::to_string(active_until) + " WHERE user_id = '" +
                      escape_string(user_id) + "' AND item_id = '" +
                      escape_string(item_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool use_item_charge(const std::string &user_id, const std::string &item_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string esc_u = escape_string(user_id);
    std::string esc_i = escape_string(item_id);

    std::string sql_sel =
        "SELECT uses_left, quantity FROM user_economy_items WHERE user_id = '" +
        esc_u + "' AND item_id = '" + esc_i + "'";
    if (mysql_query(mysql_, sql_sel.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          int uses = std::stoi(row[0]);
          int qty = std::stoi(row[1]);
          mysql_free_result(res);
          if (uses > 1) {
            std::string sql_upd = "UPDATE user_economy_items SET uses_left = "
                                  "uses_left - 1 WHERE user_id = '" +
                                  esc_u + "' AND item_id = '" + esc_i + "'";
            return mysql_query(mysql_, sql_upd.c_str()) == 0;
          } else if (qty > 1) {
            std::string sql_upd =
                "UPDATE user_economy_items SET quantity = quantity - 1, "
                "uses_left = 5 WHERE user_id = '" +
                esc_u + "' AND item_id = '" + esc_i + "'";
            return mysql_query(mysql_, sql_upd.c_str()) == 0;
          } else {
            std::string sql_del =
                "DELETE FROM user_economy_items WHERE user_id = '" + esc_u +
                "' AND item_id = '" + esc_i + "'";
            return mysql_query(mysql_, sql_del.c_str()) == 0;
          }
        }
        mysql_free_result(res);
      }
    }
    return false;
  }

  bool save_custom_shop_item(const CustomShopItem &item) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT INTO custom_shop_items (item_id, name, type, cost, "
        "description, uses_default, duration_seconds) "
        "VALUES ('" +
        escape_string(item.item_id) + "', '" + escape_string(item.name) +
        "', '" + escape_string(item.type) + "', " + std::to_string(item.cost) +
        ", '" + escape_string(item.description) + "', " +
        std::to_string(item.uses_default) + ", " +
        std::to_string(item.duration_seconds) +
        ") "
        "ON DUPLICATE KEY UPDATE name=VALUES(name), type=VALUES(type), "
        "cost=VALUES(cost), description=VALUES(description), "
        "uses_default=VALUES(uses_default), "
        "duration_seconds=VALUES(duration_seconds)";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::vector<CustomShopItem> get_custom_shop_items() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<CustomShopItem> items;
    if (!mysql_)
      return items;

    std::string sql =
        "SELECT item_id, name, type, cost, IFNULL(description,''), "
        "uses_default, duration_seconds FROM custom_shop_items";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          CustomShopItem item;
          item.item_id = row[0] ? row[0] : "";
          item.name = row[1] ? row[1] : "";
          item.type = row[2] ? row[2] : "general";
          item.cost = row[3] ? std::stoll(row[3]) : 0;
          item.description = row[4] ? row[4] : "";
          item.uses_default = row[5] ? std::stoi(row[5]) : 0;
          item.duration_seconds = row[6] ? std::stoi(row[6]) : 0;
          items.push_back(item);
        }
        mysql_free_result(res);
      }
    }
    return items;
  }

  bool save_custom_rod(const CustomRodRow &r) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT INTO custom_rods (rod_id, name, cost, token_cost, "
        "req_prestige, luck_mult, base_cd, description) "
        "VALUES ('" +
        escape_string(r.rod_id) + "', '" + escape_string(r.name) + "', " +
        std::to_string(r.cost) + ", " + std::to_string(r.token_cost) + ", " +
        std::to_string(r.req_prestige) + ", " + std::to_string(r.luck_mult) +
        ", " + std::to_string(r.base_cd) + ", '" +
        escape_string(r.description) +
        "') "
        "ON DUPLICATE KEY UPDATE name=VALUES(name), cost=VALUES(cost), "
        "token_cost=VALUES(token_cost), req_prestige=VALUES(req_prestige), "
        "luck_mult=VALUES(luck_mult), base_cd=VALUES(base_cd), "
        "description=VALUES(description)";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::vector<CustomRodRow> get_custom_rods() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<CustomRodRow> rods;
    if (!mysql_)
      return rods;

    std::string sql =
        "SELECT rod_id, name, cost, token_cost, req_prestige, luck_mult, "
        "base_cd, IFNULL(description,'') FROM custom_rods";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          CustomRodRow r;
          r.rod_id = row[0] ? row[0] : "";
          r.name = row[1] ? row[1] : "";
          r.cost = row[2] ? std::stoll(row[2]) : 0;
          r.token_cost = row[3] ? std::stoi(row[3]) : 0;
          r.req_prestige = row[4] ? std::stoi(row[4]) : 0;
          r.luck_mult = row[5] ? std::stod(row[5]) : 1.0;
          r.base_cd = row[6] ? std::stoi(row[6]) : 30;
          r.description = row[7] ? row[7] : "";
          rods.push_back(r);
        }
        mysql_free_result(res);
      }
    }
    return rods;
  }

  bool save_custom_bait(const CustomBaitRow &b) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT INTO custom_baits (bait_id, name, pack_size, cost, token_cost, "
        "req_prestige, luck_bonus, cd_delta, description) "
        "VALUES ('" +
        escape_string(b.bait_id) + "', '" + escape_string(b.name) + "', " +
        std::to_string(b.pack_size) + ", " + std::to_string(b.cost) + ", " +
        std::to_string(b.token_cost) + ", " + std::to_string(b.req_prestige) +
        ", " + std::to_string(b.luck_bonus) + ", " +
        std::to_string(b.cd_delta) + ", '" + escape_string(b.description) +
        "') "
        "ON DUPLICATE KEY UPDATE name=VALUES(name), "
        "pack_size=VALUES(pack_size), cost=VALUES(cost), "
        "token_cost=VALUES(token_cost), req_prestige=VALUES(req_prestige), "
        "luck_bonus=VALUES(luck_bonus), cd_delta=VALUES(cd_delta), "
        "description=VALUES(description)";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::vector<CustomBaitRow> get_custom_baits() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<CustomBaitRow> baits;
    if (!mysql_)
      return baits;

    std::string sql =
        "SELECT bait_id, name, pack_size, cost, token_cost, req_prestige, "
        "luck_bonus, cd_delta, IFNULL(description,'') FROM custom_baits";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          CustomBaitRow b;
          b.bait_id = row[0] ? row[0] : "";
          b.name = row[1] ? row[1] : "";
          b.pack_size = row[2] ? std::stoi(row[2]) : 10;
          b.cost = row[3] ? std::stoll(row[3]) : 0;
          b.token_cost = row[4] ? std::stoi(row[4]) : 0;
          b.req_prestige = row[5] ? std::stoi(row[5]) : 0;
          b.luck_bonus = row[6] ? std::stod(row[6]) : 0.0;
          b.cd_delta = row[7] ? std::stoi(row[7]) : 0;
          b.description = row[8] ? row[8] : "";
          baits.push_back(b);
        }
        mysql_free_result(res);
      }
    }
    return baits;
  }

  bool save_custom_fish(const CustomFishRow &f) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT INTO custom_fish (fish_id, name, rarity, weight_min, "
        "weight_max, val_min, val_max, base_odds) "
        "VALUES ('" +
        escape_string(f.fish_id) + "', '" + escape_string(f.name) + "', '" +
        escape_string(f.rarity) + "', " + std::to_string(f.weight_min) + ", " +
        std::to_string(f.weight_max) + ", " + std::to_string(f.val_min) + ", " +
        std::to_string(f.val_max) + ", " + std::to_string(f.base_odds) +
        ") "
        "ON DUPLICATE KEY UPDATE name=VALUES(name), rarity=VALUES(rarity), "
        "weight_min=VALUES(weight_min), weight_max=VALUES(weight_max), "
        "val_min=VALUES(val_min), val_max=VALUES(val_max), "
        "base_odds=VALUES(base_odds)";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::vector<CustomFishRow> get_custom_fish() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<CustomFishRow> fish;
    if (!mysql_)
      return fish;

    std::string sql = "SELECT fish_id, name, rarity, weight_min, weight_max, "
                      "val_min, val_max, base_odds FROM custom_fish";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          CustomFishRow f;
          f.fish_id = row[0] ? row[0] : "";
          f.name = row[1] ? row[1] : "";
          f.rarity = row[2] ? row[2] : "Common";
          f.weight_min = row[3] ? std::stod(row[3]) : 1.0;
          f.weight_max = row[4] ? std::stod(row[4]) : 5.0;
          f.val_min = row[5] ? std::stoll(row[5]) : 50;
          f.val_max = row[6] ? std::stoll(row[6]) : 150;
          f.base_odds = row[7] ? std::stod(row[7]) : 0.50;
          fish.push_back(f);
        }
        mysql_free_result(res);
      }
    }
    return fish;
  }

  //  Fishing Economy Database System
  struct FishingUser {
    std::string user_id;
    std::string rod_id = "wooden_rod";
    std::string bait_id = "worm";
    int fish_prestige = 0;
    int prestige_tokens = 0;
    int total_catches = 0;
    double total_weight = 0.0;
    int64_t total_earned = 0;
    std::string rarest_catch_name = "None";
    double rarest_catch_odds = 1.0;
    int rod_luck_level = 0;
    int rod_cd_level = 0;
    int rod_double_level = 0;
    int rod_rare_level = 0;
    std::string active_rod_effect = "none";
    std::string last_fish_time;
    double temp_luck_boost = 1.0;
    int temp_luck_casts = 0;
    int event_tokens = 0;
    std::string active_pet_id = "none";
    double perm_luck = 0.0;
    double perm_val_mult = 1.0;
    double perm_wt_mult = 1.0;
    int perm_cd_reduction = 0;
  };

  struct FishBossState {
    std::string boss_type; // "daily", "monthly", "seasonal"
    std::string boss_name;
    std::string season_theme; // "Summer", "Fall", "Winter", "Spring"
    int64_t current_hp = 100000;
    int64_t max_hp = 100000;
    std::string req_type = "catches"; // "catches", "sell_value", "total_weight"
    int64_t req_progress = 0;
    int64_t req_target = 5000;
  };

  struct CaughtFish {
    int id = 0;
    std::string user_id;
    std::string fish_id;
    std::string fish_name;
    std::string rarity;
    double odds_percent = 0.0;
    double weight = 0.0;
    int64_t value = 0;
    std::string caught_at;
  };

  struct FishBaitEntry {
    std::string bait_id;
    int quantity = 0;
  };

  bool ensure_fishing_user(const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string esc_uid = escape_string(user_id);
    std::string sql =
        "INSERT IGNORE INTO user_fishing (user_id) VALUES ('" + esc_uid + "')";
    mysql_query(mysql_, sql.c_str());

    std::string sql_rod =
        "INSERT IGNORE INTO user_unlocked_rods (user_id, rod_id) VALUES ('" +
        esc_uid + "', 'wooden_rod')";
    mysql_query(mysql_, sql_rod.c_str());

    std::string sql_worm = "INSERT IGNORE INTO user_fish_baits (user_id, "
                           "bait_id, quantity) VALUES ('" +
                           esc_uid + "', 'worm', 10)";
    mysql_query(mysql_, sql_worm.c_str());
    return true;
  }

  std::optional<FishingUser> get_fishing_user(const std::string &user_id) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return std::nullopt;

    std::string sql =
        "SELECT rod_id, bait_id, fish_prestige, prestige_tokens, "
        "total_catches, total_weight, total_earned, rarest_catch_name, "
        "rarest_catch_odds, rod_luck_level, rod_cd_level, rod_double_level, "
        "rod_rare_level, active_rod_effect, IFNULL(last_fish_time, ''), "
        "temp_luck_boost, temp_luck_casts, IFNULL(event_tokens, 0), "
        "IFNULL(active_pet_id, 'none'), IFNULL(perm_luck, 0.0), "
        "IFNULL(perm_val_mult, 1.0), IFNULL(perm_wt_mult, 1.0), "
        "IFNULL(perm_cd_reduction, 0) FROM user_fishing WHERE user_id = '" +
        escape_string(user_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          FishingUser u;
          u.user_id = user_id;
          u.rod_id = row[0] ? row[0] : "wooden_rod";
          u.bait_id = row[1] ? row[1] : "worm";
          u.fish_prestige = row[2] ? std::stoi(row[2]) : 0;
          u.prestige_tokens = row[3] ? std::stoi(row[3]) : 0;
          u.total_catches = row[4] ? std::stoi(row[4]) : 0;
          u.total_weight = row[5] ? std::stod(row[5]) : 0.0;
          u.total_earned = row[6] ? std::stoll(row[6]) : 0;
          u.rarest_catch_name = row[7] ? row[7] : "None";
          u.rarest_catch_odds = row[8] ? std::stod(row[8]) : 1.0;
          u.rod_luck_level = row[9] ? std::stoi(row[9]) : 0;
          u.rod_cd_level = row[10] ? std::stoi(row[10]) : 0;
          u.rod_double_level = row[11] ? std::stoi(row[11]) : 0;
          u.rod_rare_level = row[12] ? std::stoi(row[12]) : 0;
          u.active_rod_effect = row[13] ? row[13] : "none";
          u.last_fish_time = row[14] ? row[14] : "";
          u.temp_luck_boost = row[15] ? std::stod(row[15]) : 1.0;
          u.temp_luck_casts = row[16] ? std::stoi(row[16]) : 0;
          u.event_tokens = row[17] ? std::stoi(row[17]) : 0;
          u.active_pet_id = row[18] ? row[18] : "none";
          u.perm_luck = row[19] ? std::stod(row[19]) : 0.0;
          u.perm_val_mult = row[20] ? std::stod(row[20]) : 1.0;
          u.perm_wt_mult = row[21] ? std::stod(row[21]) : 1.0;
          u.perm_cd_reduction = row[22] ? std::stoi(row[22]) : 0;
          mysql_free_result(res);
          return u;
        }
        mysql_free_result(res);
      }
    }
    return std::nullopt;
  }

  bool update_fishing_user(const FishingUser &u) {
    ensure_fishing_user(u.user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string sql = "UPDATE user_fishing SET "
                      "rod_id = '" +
                      escape_string(u.rod_id) +
                      "', "
                      "bait_id = '" +
                      escape_string(u.bait_id) +
                      "', "
                      "fish_prestige = " +
                      std::to_string(u.fish_prestige) +
                      ", "
                      "prestige_tokens = " +
                      std::to_string(u.prestige_tokens) +
                      ", "
                      "total_catches = " +
                      std::to_string(u.total_catches) +
                      ", "
                      "total_weight = " +
                      std::to_string(u.total_weight) +
                      ", "
                      "total_earned = " +
                      std::to_string(u.total_earned) +
                      ", "
                      "rarest_catch_name = '" +
                      escape_string(u.rarest_catch_name) +
                      "', "
                      "rarest_catch_odds = " +
                      std::to_string(u.rarest_catch_odds) +
                      ", "
                      "rod_luck_level = " +
                      std::to_string(u.rod_luck_level) +
                      ", "
                      "rod_cd_level = " +
                      std::to_string(u.rod_cd_level) +
                      ", "
                      "rod_double_level = " +
                      std::to_string(u.rod_double_level) +
                      ", "
                      "rod_rare_level = " +
                      std::to_string(u.rod_rare_level) +
                      ", "
                      "active_rod_effect = '" +
                      escape_string(u.active_rod_effect) +
                      "', "
                      "temp_luck_boost = " +
                      std::to_string(u.temp_luck_boost) +
                      ", "
                      "temp_luck_casts = " +
                      std::to_string(u.temp_luck_casts) +
                      ", "
                      "event_tokens = " +
                      std::to_string(u.event_tokens) +
                      ", "
                      "active_pet_id = '" +
                      escape_string(u.active_pet_id) +
                      "', "
                      "perm_luck = " +
                      std::to_string(u.perm_luck) +
                      ", "
                      "perm_val_mult = " +
                      std::to_string(u.perm_val_mult) +
                      ", "
                      "perm_wt_mult = " +
                      std::to_string(u.perm_wt_mult) +
                      ", "
                      "perm_cd_reduction = " +
                      std::to_string(u.perm_cd_reduction) +
                      " "
                      "WHERE user_id = '" +
                      escape_string(u.user_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool add_user_pet(const std::string &user_id, const std::string &pet_id) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT IGNORE INTO user_pets (user_id, pet_id) VALUES ('" +
        escape_string(user_id) + "', '" + escape_string(pet_id) + "')";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool has_claimed_fishdex_reward(const std::string &user_id, const std::string &rarity) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "SELECT COUNT(*) FROM user_fishdex_claims WHERE user_id = '" +
                      escape_string(user_id) + "' AND rarity = '" + escape_string(rarity) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        int count = row && row[0] ? std::stoi(row[0]) : 0;
        mysql_free_result(res);
        return count > 0;
      }
    }
    return false;
  }

  bool save_fishdex_claim(const std::string &user_id, const std::string &rarity) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "INSERT INTO user_fishdex_claims (user_id, rarity) VALUES ('" +
                      escape_string(user_id) + "', '" + escape_string(rarity) + "')";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::vector<std::string> get_user_pets(const std::string &user_id) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<std::string> pets;
    if (!mysql_)
      return pets;
    std::string sql = "SELECT pet_id FROM user_pets WHERE user_id = '" +
                      escape_string(user_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          if (row[0])
            pets.push_back(row[0]);
        }
        mysql_free_result(res);
      }
    }
    return pets;
  }

  bool has_user_pet(const std::string &user_id, const std::string &pet_id) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "SELECT 1 FROM user_pets WHERE user_id = '" +
                      escape_string(user_id) + "' AND pet_id = '" +
                      escape_string(pet_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        bool found = (mysql_num_rows(res) > 0);
        mysql_free_result(res);
        return found;
      }
    }
    return false;
  }

  std::optional<FishBossState> get_global_boss(const std::string &boss_type) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return std::nullopt;
    std::string sql = "SELECT boss_type, boss_name, season_theme, current_hp, "
                      "max_hp, req_type, req_progress, req_target FROM "
                      "global_fish_bosses WHERE boss_type = '" +
                      escape_string(boss_type) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          FishBossState b;
          b.boss_type = row[0] ? row[0] : boss_type;
          b.boss_name = row[1] ? row[1] : "";
          b.season_theme = row[2] ? row[2] : "Summer";
          b.current_hp = row[3] ? std::stoll(row[3]) : 100000;
          b.max_hp = row[4] ? std::stoll(row[4]) : 100000;
          b.req_type = row[5] ? row[5] : "catches";
          b.req_progress = row[6] ? std::stoll(row[6]) : 0;
          b.req_target = row[7] ? std::stoll(row[7]) : 1000;
          mysql_free_result(res);
          return b;
        }
        mysql_free_result(res);
      }
    }
    return std::nullopt;
  }

  bool save_global_boss(const FishBossState &b) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT INTO global_fish_bosses (boss_type, boss_name, season_theme, "
        "current_hp, max_hp, req_type, req_progress, req_target) VALUES ('" +
        escape_string(b.boss_type) + "', '" + escape_string(b.boss_name) +
        "', '" + escape_string(b.season_theme) + "', " +
        std::to_string(b.current_hp) + ", " + std::to_string(b.max_hp) + ", '" +
        escape_string(b.req_type) + "', " + std::to_string(b.req_progress) +
        ", " + std::to_string(b.req_target) +
        ") "
        "ON DUPLICATE KEY UPDATE "
        "boss_name = '" +
        escape_string(b.boss_name) +
        "', "
        "season_theme = '" +
        escape_string(b.season_theme) +
        "', "
        "current_hp = " +
        std::to_string(b.current_hp) +
        ", "
        "max_hp = " +
        std::to_string(b.max_hp) +
        ", "
        "req_type = '" +
        escape_string(b.req_type) +
        "', "
        "req_progress = " +
        std::to_string(b.req_progress) +
        ", "
        "req_target = " +
        std::to_string(b.req_target);
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool add_user_boss_contribution(const std::string &user_id,
                                  const std::string &boss_type,
                                  int64_t amount) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT INTO user_boss_contributions (user_id, boss_type, "
        "contribution_amount) VALUES ('" +
        escape_string(user_id) + "', '" + escape_string(boss_type) + "', " +
        std::to_string(amount) +
        ") "
        "ON DUPLICATE KEY UPDATE contribution_amount = contribution_amount + " +
        std::to_string(amount);
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  int64_t get_user_boss_contribution(const std::string &user_id,
                                     const std::string &boss_type) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return 0;
    std::string sql = "SELECT contribution_amount FROM user_boss_contributions "
                      "WHERE user_id = '" +
                      escape_string(user_id) + "' AND boss_type = '" +
                      escape_string(boss_type) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0]) {
          int64_t val = std::stoll(row[0]);
          mysql_free_result(res);
          return val;
        }
        mysql_free_result(res);
      }
    }
    return 0;
  }

  bool update_last_fish_time(const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "UPDATE user_fishing SET last_fish_time = "
                      "CURRENT_TIMESTAMP WHERE user_id = '" +
                      escape_string(user_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  int get_fish_cooldown_remaining(const std::string &user_id,
                                  int cooldown_seconds) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return 0;
    std::string sql =
        "SELECT GREATEST(0, CAST(" + std::to_string(cooldown_seconds) +
        " AS SIGNED) - TIMESTAMPDIFF(SECOND, last_fish_time, "
        "CURRENT_TIMESTAMP)) FROM user_fishing WHERE user_id = '" +
        escape_string(user_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
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

  bool add_fish_to_inventory(const std::string &user_id,
                             const std::string &fish_id,
                             const std::string &fish_name,
                             const std::string &rarity, double odds_percent,
                             double weight, int64_t value) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT INTO user_fish_inventory (user_id, fish_id, fish_name, rarity, "
        "odds_percent, weight, value) VALUES ('" +
        escape_string(user_id) + "', '" + escape_string(fish_id) + "', '" +
        escape_string(fish_name) + "', '" + escape_string(rarity) + "', " +
        std::to_string(odds_percent) + ", " + std::to_string(weight) + ", " +
        std::to_string(value) + ")";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::vector<CaughtFish> get_user_fish_inventory(const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<CaughtFish> list;
    if (!mysql_)
      return list;
    std::string sql =
        "SELECT id, fish_id, fish_name, rarity, odds_percent, weight, value, "
        "IFNULL(caught_at, '') FROM user_fish_inventory WHERE user_id = '" +
        escape_string(user_id) + "' ORDER BY value DESC";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          CaughtFish cf;
          cf.id = std::stoi(row[0]);
          cf.user_id = user_id;
          cf.fish_id = row[1] ? row[1] : "";
          cf.fish_name = row[2] ? row[2] : "";
          cf.rarity = row[3] ? row[3] : "";
          cf.odds_percent = row[4] ? std::stod(row[4]) : 0.0;
          cf.weight = row[5] ? std::stod(row[5]) : 0.0;
          cf.value = row[6] ? std::stoll(row[6]) : 0;
          cf.caught_at = row[7] ? row[7] : "";
          list.push_back(cf);
        }
        mysql_free_result(res);
      }
    }
    return list;
  }

  std::pair<int, int64_t> sell_all_user_fish(const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return {0, 0};

    int count = 0;
    int64_t total_val = 0;
    std::string esc_uid = escape_string(user_id);
    std::string sql_select = "SELECT COUNT(*), IFNULL(SUM(value), 0) FROM "
                             "user_fish_inventory WHERE user_id = '" +
                             esc_uid + "'";
    if (mysql_query(mysql_, sql_select.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          count = row[0] ? std::stoi(row[0]) : 0;
          total_val = row[1] ? std::stoll(row[1]) : 0;
        }
        mysql_free_result(res);
      }
    }

    if (count > 0) {
      std::string sql_del =
          "DELETE FROM user_fish_inventory WHERE user_id = '" + esc_uid + "'";
      mysql_query(mysql_, sql_del.c_str());
      std::string sql_add_coins = "UPDATE users SET wallet = wallet + " +
                                  std::to_string(total_val) +
                                  " WHERE user_id = '" + esc_uid + "'";
      mysql_query(mysql_, sql_add_coins.c_str());
      std::string sql_track_earned =
          "UPDATE user_fishing SET total_earned = total_earned + " +
          std::to_string(total_val) + " WHERE user_id = '" + esc_uid + "'";
      mysql_query(mysql_, sql_track_earned.c_str());
    }
    return {count, total_val};
  }

  std::pair<int, int64_t> sell_user_fish_by_rarity(const std::string &user_id,
                                                   const std::string &rarity) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return {0, 0};

    int count = 0;
    int64_t total_val = 0;
    std::string esc_uid = escape_string(user_id);
    std::string esc_rarity = escape_string(rarity);

    std::string sql_select = "SELECT COUNT(*), IFNULL(SUM(value), 0) FROM "
                             "user_fish_inventory WHERE user_id = '" +
                             esc_uid + "' AND LOWER(rarity) = LOWER('" +
                             esc_rarity + "')";
    if (mysql_query(mysql_, sql_select.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          count = row[0] ? std::stoi(row[0]) : 0;
          total_val = row[1] ? std::stoll(row[1]) : 0;
        }
        mysql_free_result(res);
      }
    }

    if (count > 0) {
      std::string sql_del =
          "DELETE FROM user_fish_inventory WHERE user_id = '" + esc_uid +
          "' AND LOWER(rarity) = LOWER('" + esc_rarity + "')";
      mysql_query(mysql_, sql_del.c_str());
      std::string sql_add_coins = "UPDATE users SET wallet = wallet + " +
                                  std::to_string(total_val) +
                                  " WHERE user_id = '" + esc_uid + "'";
      mysql_query(mysql_, sql_add_coins.c_str());
      std::string sql_track_earned =
          "UPDATE user_fishing SET total_earned = total_earned + " +
          std::to_string(total_val) + " WHERE user_id = '" + esc_uid + "'";
      mysql_query(mysql_, sql_track_earned.c_str());
    }
    return {count, total_val};
  }

  bool transfer_fish(uint64_t fish_id, const std::string &sender_id, const std::string &recipient_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "UPDATE user_fish_inventory SET user_id = '" +
                      escape_string(recipient_id) + "' WHERE id = " +
                      std::to_string(fish_id) + " AND user_id = '" +
                      escape_string(sender_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0 && mysql_affected_rows(mysql_) > 0;
  }

  bool delete_single_fish(const std::string &user_id, uint64_t fish_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "DELETE FROM user_fish_inventory WHERE id = " +
                      std::to_string(fish_id) + " AND user_id = '" +
                      escape_string(user_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0 && mysql_affected_rows(mysql_) > 0;
  }

  int get_bait_count(const std::string &user_id, const std::string &bait_id) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return 0;
    std::string sql = "SELECT quantity FROM user_fish_baits WHERE user_id = '" +
                      escape_string(user_id) + "' AND bait_id = '" +
                      escape_string(bait_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0]) {
          int qty = std::stoi(row[0]);
          mysql_free_result(res);
          return qty;
        }
        mysql_free_result(res);
      }
    }
    return 0;
  }

  bool add_bait(const std::string &user_id, const std::string &bait_id,
                int quantity) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT INTO user_fish_baits (user_id, bait_id, quantity) VALUES ('" +
        escape_string(user_id) + "', '" + escape_string(bait_id) + "', " +
        std::to_string(quantity) +
        ") ON DUPLICATE KEY UPDATE quantity = quantity + " +
        std::to_string(quantity);
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool consume_bait(const std::string &user_id, const std::string &bait_id) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "UPDATE user_fish_baits SET quantity = GREATEST(0, "
                      "quantity - 1) WHERE user_id = '" +
                      escape_string(user_id) + "' AND bait_id = '" +
                      escape_string(bait_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::vector<FishBaitEntry> get_user_baits(const std::string &user_id) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<FishBaitEntry> list;
    if (!mysql_)
      return list;
    std::string sql =
        "SELECT bait_id, quantity FROM user_fish_baits WHERE user_id = '" +
        escape_string(user_id) + "' AND quantity > 0";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          if (row[0] && row[1]) {
            list.push_back({row[0], std::stoi(row[1])});
          }
        }
        mysql_free_result(res);
      }
    }
    return list;
  }

  bool unlock_rod(const std::string &user_id, const std::string &rod_id) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT IGNORE INTO user_unlocked_rods (user_id, rod_id) VALUES ('" +
        escape_string(user_id) + "', '" + escape_string(rod_id) + "')";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::vector<std::string> get_unlocked_rods(const std::string &user_id) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<std::string> list;
    if (!mysql_)
      return list;
    std::string sql =
        "SELECT rod_id FROM user_unlocked_rods WHERE user_id = '" +
        escape_string(user_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          if (row[0])
            list.push_back(row[0]);
        }
        mysql_free_result(res);
      }
    }
    return list;
  }

  bool has_unlocked_rod(const std::string &user_id, const std::string &rod_id) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "SELECT 1 FROM user_unlocked_rods WHERE user_id = '" +
                      escape_string(user_id) + "' AND rod_id = '" +
                      escape_string(rod_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        bool found = (mysql_num_rows(res) > 0);
        mysql_free_result(res);
        return found;
      }
    }
    return false;
  }

  std::vector<FishingUser> get_top_fishers_by_odds(int limit = 10) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<FishingUser> list;
    if (!mysql_)
      return list;
    std::string sql =
        "SELECT user_id, rod_id, bait_id, fish_prestige, prestige_tokens, "
        "total_catches, total_weight, total_earned, rarest_catch_name, "
        "rarest_catch_odds FROM user_fishing WHERE rarest_catch_name != 'None' "
        "ORDER BY rarest_catch_odds ASC LIMIT " +
        std::to_string(limit);
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          FishingUser u;
          u.user_id = row[0] ? row[0] : "";
          u.rod_id = row[1] ? row[1] : "";
          u.bait_id = row[2] ? row[2] : "";
          u.fish_prestige = row[3] ? std::stoi(row[3]) : 0;
          u.prestige_tokens = row[4] ? std::stoi(row[4]) : 0;
          u.total_catches = row[5] ? std::stoi(row[5]) : 0;
          u.total_weight = row[6] ? std::stod(row[6]) : 0.0;
          u.total_earned = row[7] ? std::stoll(row[7]) : 0;
          u.rarest_catch_name = row[8] ? row[8] : "None";
          u.rarest_catch_odds = row[9] ? std::stod(row[9]) : 1.0;
          list.push_back(u);
        }
        mysql_free_result(res);
      }
    }
    return list;
  }

  std::optional<MasqueradeConfig>
  get_masquerade_config(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(masquerade_cache_mutex_);
    auto it = masquerade_cache_.find(server_id);
    if (it != masquerade_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool save_masquerade_config(const MasqueradeConfig &cfg) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_server_id = escape_string(cfg.server_id);
    std::string val_avatar =
        cfg.avatar ? "'" + escape_string(*cfg.avatar) + "'" : "NULL";
    std::string val_colour =
        cfg.colour ? "'" + escape_string(*cfg.colour) + "'" : "NULL";
    std::string val_name =
        cfg.name ? "'" + escape_string(*cfg.name) + "'" : "NULL";
    std::string val_disabled = cfg.disabled ? "1" : "0";
    std::string val_disable_user = cfg.disable_user ? "1" : "0";
    std::string val_disable_user_name = cfg.disable_user_name ? "1" : "0";
    std::string val_disable_user_avatar = cfg.disable_user_avatar ? "1" : "0";
    std::string val_disable_user_colour = cfg.disable_user_colour ? "1" : "0";

    std::string query =
        "INSERT INTO masquerade_configs (server_id, avatar, colour, name, "
        "disabled, disable_user, disable_user_name, disable_user_avatar, "
        "disable_user_colour) "
        "VALUES ('" +
        esc_server_id + "', " + val_avatar + ", " + val_colour + ", " +
        val_name + ", " + val_disabled + ", " + val_disable_user + ", " +
        val_disable_user_name + ", " + val_disable_user_avatar + ", " +
        val_disable_user_colour +
        ") "
        "ON DUPLICATE KEY UPDATE avatar = " +
        val_avatar + ", colour = " + val_colour + ", name = " + val_name +
        ", disabled = " + val_disabled +
        ", disable_user = " + val_disable_user +
        ", disable_user_name = " + val_disable_user_name +
        ", disable_user_avatar = " + val_disable_user_avatar +
        ", disable_user_colour = " + val_disable_user_colour;

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save masquerade_config: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Update cache
    {
      std::lock_guard<std::shared_mutex> cache_lock(masquerade_cache_mutex_);
      masquerade_cache_[cfg.server_id] = cfg;
    }

    return true;
  }

  bool delete_masquerade_config(const std::string &server_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_server = escape_string(server_id);
    std::string query =
        "DELETE FROM masquerade_configs WHERE server_id = '" + esc_server + "'";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to delete masquerade_config: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Update cache
    {
      std::lock_guard<std::shared_mutex> cache_lock(masquerade_cache_mutex_);
      masquerade_cache_.erase(server_id);
    }

    return true;
  }

  std::optional<UserMasquerade>
  get_user_masquerade(const std::string &user_id) {
    std::shared_lock<std::shared_mutex> lock(user_masq_cache_mutex_);
    auto it = user_masq_cache_.find(user_id);
    if (it != user_masq_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool save_user_masquerade(const UserMasquerade &um) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_uid = escape_string(um.user_id);
    std::string val_avatar =
        um.avatar ? "'" + escape_string(*um.avatar) + "'" : "NULL";
    std::string val_colour =
        um.colour ? "'" + escape_string(*um.colour) + "'" : "NULL";
    std::string val_name =
        um.name ? "'" + escape_string(*um.name) + "'" : "NULL";

    std::string query =
        "INSERT INTO user_masquerades (user_id, avatar, colour, name) "
        "VALUES ('" +
        esc_uid + "', " + val_avatar + ", " + val_colour + ", " + val_name +
        ") "
        "ON DUPLICATE KEY UPDATE avatar = " +
        val_avatar + ", colour = " + val_colour + ", name = " + val_name;

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save user_masquerade: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    {
      std::lock_guard<std::shared_mutex> cache_lock(user_masq_cache_mutex_);
      user_masq_cache_[um.user_id] = um;
    }
    return true;
  }

  std::optional<UserRankCard>
  get_user_rank_card(const std::string &user_id) {
    std::shared_lock<std::shared_mutex> lock(user_rank_card_cache_mutex_);
    auto it = user_rank_card_cache_.find(user_id);
    if (it != user_rank_card_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool save_user_rank_card(const UserRankCard &urc) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_uid = escape_string(urc.user_id);
    std::string val_style = escape_string(urc.style);
    std::string val_bg_color = urc.bg_color ? "'" + escape_string(*urc.bg_color) + "'" : "NULL";
    std::string val_bg_image = urc.bg_image ? "'" + escape_string(*urc.bg_image) + "'" : "NULL";
    std::string val_bar_active = urc.bar_active ? "'" + escape_string(*urc.bar_active) + "'" : "NULL";
    std::string val_bar_dimmed = urc.bar_dimmed ? "'" + escape_string(*urc.bar_dimmed) + "'" : "NULL";
    std::string val_text_color = urc.text_color ? "'" + escape_string(*urc.text_color) + "'" : "NULL";
    std::string val_font_name = urc.font_name ? "'" + escape_string(*urc.font_name) + "'" : "NULL";

    std::string query =
        "INSERT INTO user_rank_card (user_id, style, bg_color, bg_image, bar_active, bar_dimmed, text_color, font_name) "
        "VALUES ('" + esc_uid + "', '" + val_style + "', " + val_bg_color + ", " + val_bg_image + ", " + 
        val_bar_active + ", " + val_bar_dimmed + ", " + val_text_color + ", " + val_font_name + ") "
        "ON DUPLICATE KEY UPDATE style = '" + val_style + "', bg_color = " + val_bg_color + ", bg_image = " + val_bg_image + 
        ", bar_active = " + val_bar_active + ", bar_dimmed = " + val_bar_dimmed + ", text_color = " + val_text_color + 
        ", font_name = " + val_font_name;

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save user_rank_card: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    {
      std::lock_guard<std::shared_mutex> cache_lock(user_rank_card_cache_mutex_);
      user_rank_card_cache_[urc.user_id] = urc;
    }
    return true;
  }

  bool delete_user_masquerade(const std::string &user_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_uid = escape_string(user_id);
    std::string query =
        "DELETE FROM user_masquerades WHERE user_id = '" + esc_uid + "'";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to delete user_masquerade: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    {
      std::lock_guard<std::shared_mutex> cache_lock(user_masq_cache_mutex_);
      user_masq_cache_.erase(user_id);
    }
    return true;
  }

  std::vector<Tupper> get_user_tuppers(const std::string &user_id) {
    std::shared_lock<std::shared_mutex> lock(tupper_cache_mutex_);
    auto it = tupper_cache_.find(user_id);
    if (it != tupper_cache_.end()) {
      return it->second;
    }
    return {};
  }

  bool add_tupper(const std::string &user_id, const std::string &name,
                  const std::string &avatar_url, const std::string &prefix,
                  const std::string &suffix) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_user_id = escape_string(user_id);
    std::string esc_name = escape_string(name);
    std::string esc_avatar = escape_string(avatar_url);
    std::string esc_prefix = escape_string(prefix);
    std::string esc_suffix = escape_string(suffix);

    std::string query =
        "INSERT INTO tuppers (user_id, name, avatar_url, prefix, suffix) "
        "VALUES ('" +
        esc_user_id + "', '" + esc_name + "', '" + esc_avatar + "', '" +
        esc_prefix + "', '" + esc_suffix + "')";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to add tupper: " << mysql_error(mysql_)
                << "\n";
      return false;
    }

    // Fetch inserted ID
    int insert_id = static_cast<int>(mysql_insert_id(mysql_));

    // Update cache
    {
      std::lock_guard<std::shared_mutex> cache_lock(tupper_cache_mutex_);
      Tupper t{insert_id, user_id, name, avatar_url, prefix, suffix};
      tupper_cache_[user_id].push_back(t);
    }

    return true;
  }

  bool remove_tupper(const std::string &user_id, const std::string &name) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_user_id = escape_string(user_id);
    std::string esc_name = escape_string(name);

    std::string query = "DELETE FROM tuppers WHERE user_id = '" + esc_user_id +
                        "' AND name = '" + esc_name + "'";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to remove tupper: " << mysql_error(mysql_)
                << "\n";
      return false;
    }

    // Update cache
    {
      std::lock_guard<std::shared_mutex> cache_lock(tupper_cache_mutex_);
      auto &list = tupper_cache_[user_id];
      list.erase(
          std::remove_if(list.begin(), list.end(),
                         [&name](const Tupper &t) { return t.name == name; }),
          list.end());
    }

    return true;
  }

  bool update_tupper_avatar(const std::string &user_id, const std::string &name,
                            const std::string &avatar_url) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_user_id = escape_string(user_id);
    std::string esc_name = escape_string(name);
    std::string esc_avatar = escape_string(avatar_url);

    std::string query = "UPDATE tuppers SET avatar_url = '" + esc_avatar +
                        "' WHERE user_id = '" + esc_user_id + "' AND name = '" +
                        esc_name + "'";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to update tupper avatar: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Update cache
    {
      std::lock_guard<std::shared_mutex> cache_lock(tupper_cache_mutex_);
      for (auto &t : tupper_cache_[user_id]) {
        if (t.name == name) {
          t.avatar_url = avatar_url;
          break;
        }
      }
    }

    return true;
  }

  bool update_tupper_brackets(const std::string &user_id,
                              const std::string &name,
                              const std::string &prefix,
                              const std::string &suffix) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_user_id = escape_string(user_id);
    std::string esc_name = escape_string(name);
    std::string esc_prefix = escape_string(prefix);
    std::string esc_suffix = escape_string(suffix);

    std::string query = "UPDATE tuppers SET prefix = '" + esc_prefix +
                        "', suffix = '" + esc_suffix + "' WHERE user_id = '" +
                        esc_user_id + "' AND name = '" + esc_name + "'";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to update tupper brackets: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Update cache
    {
      std::lock_guard<std::shared_mutex> cache_lock(tupper_cache_mutex_);
      for (auto &t : tupper_cache_[user_id]) {
        if (t.name == name) {
          t.prefix = prefix;
          t.suffix = suffix;
          break;
        }
      }
    }

    return true;
  }

  bool rename_tupper(const std::string &user_id, const std::string &old_name,
                     const std::string &new_name) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_user_id = escape_string(user_id);
    std::string esc_old_name = escape_string(old_name);
    std::string esc_new_name = escape_string(new_name);

    std::string query = "UPDATE tuppers SET name = '" + esc_new_name +
                        "' WHERE user_id = '" + esc_user_id + "' AND name = '" +
                        esc_old_name + "'";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to rename tupper: " << mysql_error(mysql_)
                << "\n";
      return false;
    }

    // Update cache
    {
      std::lock_guard<std::shared_mutex> cache_lock(tupper_cache_mutex_);
      for (auto &t : tupper_cache_[user_id]) {
        if (t.name == old_name) {
          t.name = new_name;
          break;
        }
      }
    }

    return true;
  }

  bool add_tupper_proxy(const std::string &message_id,
                        const std::string &user_id,
                        const std::string &tupper_name) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_msg_id = escape_string(message_id);
    std::string esc_user_id = escape_string(user_id);
    std::string esc_tupper = escape_string(tupper_name);

    std::string query =
        "INSERT INTO tupper_proxies (message_id, user_id, tupper_name) "
        "VALUES ('" +
        esc_msg_id + "', '" + esc_user_id + "', '" + esc_tupper + "')";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to add tupper proxy: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    return true;
  }

  std::optional<TupperProxy> get_tupper_proxy(const std::string &message_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_msg_id = escape_string(message_id);
    std::string query =
        "SELECT user_id, tupper_name FROM tupper_proxies WHERE message_id = '" +
        esc_msg_id + "'";

    if (mysql_query(mysql_, query.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          TupperProxy tp;
          tp.message_id = message_id;
          tp.user_id = row[0] ? row[0] : "";
          tp.tupper_name = row[1] ? row[1] : "";
          mysql_free_result(res);
          return tp;
        }
        mysql_free_result(res);
      }
    }

    return std::nullopt;
  }

  bool delete_tupper_proxy(const std::string &message_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_msg_id = escape_string(message_id);
    std::string query =
        "DELETE FROM tupper_proxies WHERE message_id = '" + esc_msg_id + "'";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to delete tupper proxy: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    return true;
  }

  std::optional<WelcomeLeaveConfig>
  get_welcome_leave_config(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> wl_lock(wl_cache_mutex_);
    auto it = wl_cache_.find(server_id);
    if (it != wl_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool save_welcome_leave_config(const WelcomeLeaveConfig &wl) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string w_channel =
        wl.welcome_channel_id.empty()
            ? "NULL"
            : "'" + escape_string(wl.welcome_channel_id) + "'";
    std::string w_type = "'" + escape_string(wl.welcome_type) + "'";
    std::string w_content = wl.welcome_content.empty()
                                ? "NULL"
                                : "'" + escape_string(wl.welcome_content) + "'";
    std::string w_embed =
        wl.welcome_embed_json.empty()
            ? "NULL"
            : "'" + escape_string(wl.welcome_embed_json) + "'";

    std::string l_channel =
        wl.leave_channel_id.empty()
            ? "NULL"
            : "'" + escape_string(wl.leave_channel_id) + "'";
    std::string l_type = "'" + escape_string(wl.leave_type) + "'";
    std::string l_content = wl.leave_content.empty()
                                ? "NULL"
                                : "'" + escape_string(wl.leave_content) + "'";
    std::string l_embed = wl.leave_embed_json.empty()
                              ? "NULL"
                              : "'" + escape_string(wl.leave_embed_json) + "'";
    std::string autorole = wl.autorole_role_id.empty()
                               ? "NULL"
                               : "'" + escape_string(wl.autorole_role_id) + "'";

    std::string w_dm_type = "'" + escape_string(wl.welcome_dm_type) + "'";
    std::string w_dm_content =
        wl.welcome_dm_content.empty()
            ? "NULL"
            : "'" + escape_string(wl.welcome_dm_content) + "'";
    std::string w_dm_embed =
        wl.welcome_dm_embed_json.empty()
            ? "NULL"
            : "'" + escape_string(wl.welcome_dm_embed_json) + "'";

    std::string query =
        "INSERT INTO welcome_leave_configs (server_id, welcome_channel_id, "
        "welcome_type, welcome_content, welcome_embed, "
        "leave_channel_id, leave_type, leave_content, leave_embed, "
        "autorole_role_id, "
        "welcome_dm_type, welcome_dm_content, welcome_dm_embed) VALUES ("
        "'" +
        escape_string(wl.server_id) + "', " + w_channel + ", " + w_type + ", " +
        w_content + ", " + w_embed + ", " + l_channel + ", " + l_type + ", " +
        l_content + ", " + l_embed + ", " + autorole + ", " + w_dm_type + ", " +
        w_dm_content + ", " + w_dm_embed +
        ") "
        "ON DUPLICATE KEY UPDATE "
        "welcome_channel_id=" +
        w_channel +
        ", "
        "welcome_type=" +
        w_type +
        ", "
        "welcome_content=" +
        w_content +
        ", "
        "welcome_embed=" +
        w_embed +
        ", "
        "leave_channel_id=" +
        l_channel +
        ", "
        "leave_type=" +
        l_type +
        ", "
        "leave_content=" +
        l_content +
        ", "
        "leave_embed=" +
        l_embed +
        ", "
        "autorole_role_id=" +
        autorole +
        ", "
        "welcome_dm_type=" +
        w_dm_type +
        ", "
        "welcome_dm_content=" +
        w_dm_content +
        ", "
        "welcome_dm_embed=" +
        w_dm_embed;

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save welcome/leave config: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    std::lock_guard<std::shared_mutex> wl_lock(wl_cache_mutex_);
    wl_cache_[wl.server_id] = wl;
    return true;
  }

  std::string get_logging_channel(const std::string &server_id,
                                  const std::string &event_type) {
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

  std::unordered_map<std::string, std::string>
  get_server_logging_config(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(log_cache_mutex_);
    auto it = log_cache_.find(server_id);
    if (it != log_cache_.end()) {
      return it->second;
    }
    return {};
  }

  std::optional<AntiRaidConfig>
  get_antiraid_config(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(ar_cache_mutex_);
    auto it = ar_cache_.find(server_id);
    if (it != ar_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool save_antiraid_config(const AntiRaidConfig &ar) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_srv = escape_string(ar.server_id);
    std::string esc_joins_act = escape_string(ar.joins_action);
    std::string esc_spam_act = escape_string(ar.spam_action);
    std::string esc_profile_act = escape_string(ar.default_profile_action);

    std::string query =
        "INSERT INTO antiraid_configs (server_id, joins_threshold_count, "
        "joins_threshold_seconds, joins_action, "
        "spam_threshold_count, spam_threshold_seconds, spam_action, "
        "default_profile_action) VALUES ("
        "'" +
        esc_srv + "', " + std::to_string(ar.joins_threshold_count) + ", " +
        std::to_string(ar.joins_threshold_seconds) +
        ", "
        "'" +
        esc_joins_act + "', " + std::to_string(ar.spam_threshold_count) + ", " +
        std::to_string(ar.spam_threshold_seconds) +
        ", "
        "'" +
        esc_spam_act + "', '" + esc_profile_act +
        "') "
        "ON DUPLICATE KEY UPDATE "
        "joins_threshold_count = VALUES(joins_threshold_count), "
        "joins_threshold_seconds = VALUES(joins_threshold_seconds), "
        "joins_action = VALUES(joins_action), "
        "spam_threshold_count = VALUES(spam_threshold_count), "
        "spam_threshold_seconds = VALUES(spam_threshold_seconds), "
        "spam_action = VALUES(spam_action), "
        "default_profile_action = VALUES(default_profile_action)";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save antiraid config: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    std::lock_guard<std::shared_mutex> cache_lock(ar_cache_mutex_);
    ar_cache_[ar.server_id] = ar;
    return true;
  }

  bool save_logging_channel(const std::string &server_id,
                            const std::string &event_type,
                            const std::string &channel_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_srv = escape_string(server_id);
    std::string esc_ev = escape_string(event_type);
    std::string esc_chan = escape_string(channel_id);

    std::string query = "INSERT INTO logging_configs (server_id, event_type, "
                        "channel_id) VALUES ("
                        "'" +
                        esc_srv + "', '" + esc_ev + "', '" + esc_chan +
                        "') "
                        "ON DUPLICATE KEY UPDATE channel_id = '" +
                        esc_chan + "'";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save logging config: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    std::lock_guard<std::shared_mutex> cache_lock(log_cache_mutex_);
    log_cache_[server_id][event_type] = channel_id;
    return true;
  }

  bool remove_logging_channel(const std::string &server_id,
                              const std::string &event_type) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_srv = escape_string(server_id);
    std::string esc_ev = escape_string(event_type);

    std::string query = "DELETE FROM logging_configs WHERE server_id = '" +
                        esc_srv + "' AND event_type = '" + esc_ev + "'";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to delete logging config: "
                << mysql_error(mysql_) << "\n";
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

  std::optional<LoggingEmbed> get_logging_embed(const std::string &server_id,
                                                const std::string &event_type) {
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

  bool save_logging_embed(const LoggingEmbed &le) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_srv = escape_string(le.server_id);
    std::string esc_ev = escape_string(le.event_type);
    std::string esc_type = escape_string(le.message_type);
    std::string esc_content =
        le.content.empty() ? "NULL" : "'" + escape_string(le.content) + "'";
    std::string esc_embed = le.embed_json.empty()
                                ? "NULL"
                                : "'" + escape_string(le.embed_json) + "'";

    std::string query = "INSERT INTO logging_embeds (server_id, event_type, "
                        "message_type, content, embed_json) VALUES ("
                        "'" +
                        esc_srv + "', '" + esc_ev + "', '" + esc_type + "', " +
                        esc_content + ", " + esc_embed +
                        ") "
                        "ON DUPLICATE KEY UPDATE message_type = '" +
                        esc_type + "', content = " + esc_content +
                        ", embed_json = " + esc_embed;

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save logging embed: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    std::lock_guard<std::shared_mutex> cache_lock(log_embed_cache_mutex_);
    log_embed_cache_[le.server_id][le.event_type] = le;
    return true;
  }

  bool remove_logging_embed(const std::string &server_id,
                            const std::string &event_type) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_srv = escape_string(server_id);
    std::string esc_ev = escape_string(event_type);

    std::string query = "DELETE FROM logging_embeds WHERE server_id = '" +
                        esc_srv + "' AND event_type = '" + esc_ev + "'";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to delete logging embed: "
                << mysql_error(mysql_) << "\n";
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

  std::optional<std::pair<std::string, std::string>>
  get_logging_webhook(const std::string &channel_id) {
    std::shared_lock<std::shared_mutex> lock(webhook_cache_mutex_);
    auto it = webhook_cache_.find(channel_id);
    if (it != webhook_cache_.end()) {
      return std::make_pair(it->second.id, it->second.token);
    }
    return std::nullopt;
  }

  bool save_logging_webhook(const std::string &channel_id,
                            const std::string &webhook_id,
                            const std::string &webhook_token) {
    {
      std::unique_lock<std::shared_mutex> lock(webhook_cache_mutex_);
      webhook_cache_[channel_id] = {webhook_id, webhook_token};
    }

    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string esc_chan = escape_string(channel_id);
    std::string esc_id = escape_string(webhook_id);
    std::string esc_tok = escape_string(webhook_token);

    std::string query = "INSERT INTO logging_webhooks (channel_id, webhook_id, "
                        "webhook_token) VALUES ("
                        "'" +
                        esc_chan + "', '" + esc_id + "', '" + esc_tok +
                        "') "
                        "ON DUPLICATE KEY UPDATE webhook_id = '" +
                        esc_id + "', webhook_token = '" + esc_tok + "'";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save logging webhook: "
                << mysql_error(mysql_) << "\n";
      return false;
    }
    return true;
  }

  bool remove_logging_webhook(const std::string &channel_id) {
    {
      std::unique_lock<std::shared_mutex> lock(webhook_cache_mutex_);
      webhook_cache_.erase(channel_id);
    }

    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string esc_chan = escape_string(channel_id);
    std::string query =
        "DELETE FROM logging_webhooks WHERE channel_id = '" + esc_chan + "'";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to remove logging webhook: "
                << mysql_error(mysql_) << "\n";
      return false;
    }
    return true;
  }

  bool is_trusted(const std::string &server_id, const std::string &user_id,
                  const std::vector<std::string> &role_ids) {
    std::shared_lock<std::shared_mutex> lock(trusted_cache_mutex_);
    auto it = trusted_cache_.find(server_id);
    if (it == trusted_cache_.end())
      return false;

    if (it->second.count("user:" + user_id) > 0)
      return true;
    for (const auto &r_id : role_ids) {
      if (it->second.count("role:" + r_id) > 0)
        return true;
    }
    return false;
  }

  std::vector<std::pair<std::string, std::string>>
  get_trusted(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(trusted_cache_mutex_);
    std::vector<std::pair<std::string, std::string>> list;
    auto it = trusted_cache_.find(server_id);
    if (it != trusted_cache_.end()) {
      for (const auto &val : it->second) {
        size_t colon = val.find(':');
        if (colon != std::string::npos) {
          list.push_back({val.substr(0, colon), val.substr(colon + 1)});
        }
      }
    }
    return list;
  }

  bool add_trusted(const std::string &server_id, const std::string &type,
                   const std::string &id) {
    {
      std::unique_lock<std::shared_mutex> lock(trusted_cache_mutex_);
      trusted_cache_[server_id].insert(type + ":" + id);
    }

    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string esc_srv = escape_string(server_id);
    std::string esc_type = escape_string(type);
    std::string esc_id = escape_string(id);

    std::string query = "INSERT INTO trusted_entities (server_id, entity_type, "
                        "entity_id) VALUES "
                        "('" +
                        esc_srv + "', '" + esc_type + "', '" + esc_id + "')";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to add trusted entity: "
                << mysql_error(mysql_) << "\n";
      return false;
    }
    return true;
  }

  bool remove_trusted(const std::string &server_id, const std::string &type,
                      const std::string &id) {
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

    std::string query = "DELETE FROM trusted_entities WHERE server_id = '" +
                        esc_srv +
                        "' "
                        "AND entity_type = '" +
                        esc_type + "' AND entity_id = '" + esc_id + "'";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to remove trusted entity: "
                << mysql_error(mysql_) << "\n";
      return false;
    }
    return true;
  }

  std::vector<RestrictionEntry>
  get_restrictions(const std::string &server_id, const std::string &target_type,
                   const std::string &target_name) {
    std::shared_lock<std::shared_mutex> lock(restrictions_cache_mutex_);
    std::vector<RestrictionEntry> list;
    auto it = restrictions_cache_.find(server_id);
    if (it != restrictions_cache_.end()) {
      for (const auto &re : it->second) {
        if (re.target_type == target_type && re.target_name == target_name) {
          list.push_back(re);
        }
      }
    }
    return list;
  }

  bool add_restriction(const std::string &server_id,
                       const std::string &target_type,
                       const std::string &target_name,
                       const std::string &restriction_type,
                       const std::string &entity_type,
                       const std::string &entity_id) {
    RestrictionEntry re = {target_type, target_name, restriction_type,
                           entity_type, entity_id};
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

    std::string query =
        "INSERT INTO command_restrictions (server_id, target_type, "
        "target_name, restriction_type, entity_type, entity_id) VALUES "
        "('" +
        esc_srv + "', '" + esc_tt + "', '" + esc_tn + "', '" + esc_rt + "', '" +
        esc_et + "', '" + esc_ei + "')";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to add command restriction: "
                << mysql_error(mysql_) << "\n";
      return false;
    }
    return true;
  }

  bool remove_restriction(const std::string &server_id,
                          const std::string &target_type,
                          const std::string &target_name,
                          const std::string &restriction_type,
                          const std::string &entity_type,
                          const std::string &entity_id) {
    {
      std::unique_lock<std::shared_mutex> lock(restrictions_cache_mutex_);
      auto it = restrictions_cache_.find(server_id);
      if (it != restrictions_cache_.end()) {
        auto &vec = it->second;
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                                 [&](const RestrictionEntry &re) {
                                   return re.target_type == target_type &&
                                          re.target_name == target_name &&
                                          re.restriction_type ==
                                              restriction_type &&
                                          re.entity_type == entity_type &&
                                          re.entity_id == entity_id;
                                 }),
                  vec.end());
      }
    }

    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string esc_srv = escape_string(server_id);
    std::string esc_tt = escape_string(target_type);
    std::string esc_tn = escape_string(target_name);
    std::string esc_rt = escape_string(restriction_type);
    std::string esc_et = escape_string(entity_type);
    std::string esc_ei = escape_string(entity_id);

    std::string query = "DELETE FROM command_restrictions WHERE server_id = '" +
                        esc_srv +
                        "' "
                        "AND target_type = '" +
                        esc_tt + "' AND target_name = '" + esc_tn +
                        "' "
                        "AND restriction_type = '" +
                        esc_rt + "' AND entity_type = '" + esc_et +
                        "' AND entity_id = '" + esc_ei + "'";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to remove command restriction: "
                << mysql_error(mysql_) << "\n";
      return false;
    }
    return true;
  }

  bool is_disabled(const std::string &server_id,
                   const std::string &command_name,
                   const std::string &module_name) {
    std::shared_lock<std::shared_mutex> lock(disabled_cache_mutex_);
    auto it = disabled_cache_.find(server_id);
    if (it == disabled_cache_.end())
      return false;

    if (it->second.count("command:" + command_name) > 0)
      return true;
    if (it->second.count("module:" + module_name) > 0)
      return true;
    return false;
  }

  std::vector<std::pair<std::string, std::string>>
  get_disabled_items(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(disabled_cache_mutex_);
    std::vector<std::pair<std::string, std::string>> list;
    auto it = disabled_cache_.find(server_id);
    if (it != disabled_cache_.end()) {
      for (const auto &val : it->second) {
        size_t colon = val.find(':');
        if (colon != std::string::npos) {
          list.push_back({val.substr(0, colon), val.substr(colon + 1)});
        }
      }
    }
    return list;
  }

  bool disable_item(const std::string &server_id,
                    const std::string &target_type,
                    const std::string &target_name) {
    {
      std::unique_lock<std::shared_mutex> lock(disabled_cache_mutex_);
      disabled_cache_[server_id].insert(target_type + ":" + target_name);
    }

    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string esc_srv = escape_string(server_id);
    std::string esc_type = escape_string(target_type);
    std::string esc_name = escape_string(target_name);

    std::string query = "INSERT INTO disabled_commands (server_id, "
                        "target_type, target_name) VALUES "
                        "('" +
                        esc_srv + "', '" + esc_type + "', '" + esc_name + "')";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to disable command/module: "
                << mysql_error(mysql_) << "\n";
      return false;
    }
    return true;
  }

  bool enable_item(const std::string &server_id, const std::string &target_type,
                   const std::string &target_name) {
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

    std::string query = "DELETE FROM disabled_commands WHERE server_id = '" +
                        esc_srv +
                        "' "
                        "AND target_type = '" +
                        esc_type + "' AND target_name = '" + esc_name + "'";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to enable command/module: "
                << mysql_error(mysql_) << "\n";
      return false;
    }
    return true;
  }

  bool add_reminder(const std::string &user_id, const std::string &channel_id,
                    const std::string &message, uint64_t timestamp_set,
                    uint64_t timestamp_due) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string query =
        "INSERT INTO reminders (user_id, channel_id, message, timestamp_set, "
        "timestamp_due) VALUES ('" +
        escape_string(user_id) + "', '" + escape_string(channel_id) + "', '" +
        escape_string(message) + "', " + std::to_string(timestamp_set) + ", " +
        std::to_string(timestamp_due) + ")";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] add_reminder error: " << mysql_error(mysql_)
                << "\n";
      return false;
    }
    return true;
  }

  std::vector<Reminder> get_due_reminders(uint64_t current_timestamp) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::vector<Reminder> due;
    if (!mysql_)
      return due;

    std::string query =
        "SELECT id, user_id, channel_id, message, timestamp_set, timestamp_due "
        "FROM reminders WHERE timestamp_due <= " +
        std::to_string(current_timestamp);
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] get_due_reminders error: " << mysql_error(mysql_)
                << "\n";
      return due;
    }

    MYSQL_RES *result = mysql_store_result(mysql_);
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

  std::vector<Reminder> get_user_reminders(const std::string &user_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::vector<Reminder> rems;
    if (!mysql_)
      return rems;

    std::string query =
        "SELECT id, user_id, channel_id, message, timestamp_set, timestamp_due "
        "FROM reminders WHERE user_id = '" +
        escape_string(user_id) + "' ORDER BY timestamp_due ASC";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] get_user_reminders error: "
                << mysql_error(mysql_) << "\n";
      return rems;
    }

    MYSQL_RES *result = mysql_store_result(mysql_);
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
    if (!mysql_)
      return false;

    std::string query =
        "DELETE FROM reminders WHERE id = " + std::to_string(id);
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] delete_reminder error: " << mysql_error(mysql_)
                << "\n";
      return false;
    }
    return true;
  }

  std::optional<ActionMessageConfig>
  get_action_message(const std::string &server_id,
                     const std::string &action_type) {
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

  bool save_action_message(const ActionMessageConfig &config) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string esc_srv = escape_string(config.server_id);
    std::string esc_act = escape_string(config.action_type);
    std::string esc_type = escape_string(config.message_type);
    std::string esc_content = escape_string(config.content);
    std::string esc_json = escape_string(config.embed_json);

    std::string query =
        "INSERT INTO action_message_configs (server_id, action_type, "
        "message_type, content, embed_json) "
        "VALUES ('" +
        esc_srv + "', '" + esc_act + "', '" + esc_type + "', " +
        (config.content.empty() ? "NULL" : "'" + esc_content + "'") + ", " +
        (config.embed_json.empty() ? "NULL" : "'" + esc_json + "'") +
        ") "
        "ON DUPLICATE KEY UPDATE message_type = '" +
        esc_type +
        "', "
        "content = " +
        (config.content.empty() ? "NULL" : "'" + esc_content + "'") +
        ", "
        "embed_json = " +
        (config.embed_json.empty() ? "NULL" : "'" + esc_json + "'");

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save action message config: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    {
      std::lock_guard<std::shared_mutex> lock(action_msg_cache_mutex_);
      action_msg_cache_[config.server_id][config.action_type] = config;
    }
    return true;
  }

  bool remove_action_message(const std::string &server_id,
                             const std::string &action_type) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string esc_srv = escape_string(server_id);
    std::string esc_act = escape_string(action_type);

    std::string query =
        "DELETE FROM action_message_configs WHERE server_id = '" + esc_srv +
        "' AND action_type = '" + esc_act + "'";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to remove action message config: "
                << mysql_error(mysql_) << "\n";
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

  std::vector<ActionMessageConfig>
  list_action_messages(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(action_msg_cache_mutex_);
    std::vector<ActionMessageConfig> res;
    auto s_it = action_msg_cache_.find(server_id);
    if (s_it != action_msg_cache_.end()) {
      for (const auto &[act, cfg] : s_it->second) {
        res.push_back(cfg);
      }
    }
    return res;
  }

  std::optional<JailConfig> get_jail_config(const std::string &server_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return std::nullopt;
    std::string sql =
        "SELECT server_id, COALESCE(jail_role_id,''), "
        "COALESCE(jail_channel_id,'') FROM jail_configs WHERE server_id = '" +
        escape_string(server_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          JailConfig cfg;
          cfg.server_id = row[0] ? row[0] : "";
          cfg.jail_role_id = row[1] ? row[1] : "";
          cfg.jail_channel_id = row[2] ? row[2] : "";
          mysql_free_result(res);
          return cfg;
        }
        mysql_free_result(res);
      }
    }
    return std::nullopt;
  }

  bool save_jail_config(const JailConfig &cfg) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "INSERT INTO jail_configs (server_id, jail_role_id, "
                      "jail_channel_id) VALUES ('" +
                      escape_string(cfg.server_id) + "', '" +
                      escape_string(cfg.jail_role_id) + "', '" +
                      escape_string(cfg.jail_channel_id) +
                      "') "
                      "ON DUPLICATE KEY UPDATE jail_role_id = '" +
                      escape_string(cfg.jail_role_id) +
                      "', jail_channel_id = '" +
                      escape_string(cfg.jail_channel_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::optional<ActiveJail> get_active_jail(const std::string &server_id,
                                            const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return std::nullopt;
    std::string sql = "SELECT server_id, user_id, roles, jailed_at, expires_at "
                      "FROM active_jails WHERE server_id = '" +
                      escape_string(server_id) + "' AND user_id = '" +
                      escape_string(user_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          ActiveJail j;
          j.server_id = row[0] ? row[0] : "";
          j.user_id = row[1] ? row[1] : "";
          j.roles = row[2] ? row[2] : "";
          j.jailed_at = row[3] ? std::stoull(row[3]) : 0;
          j.expires_at = row[4] ? std::stoull(row[4]) : 0;
          mysql_free_result(res);
          return j;
        }
        mysql_free_result(res);
      }
    }
    return std::nullopt;
  }

  bool add_active_jail(const ActiveJail &jail) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "INSERT INTO active_jails (server_id, user_id, roles, "
                      "jailed_at, expires_at) VALUES ('" +
                      escape_string(jail.server_id) + "', '" +
                      escape_string(jail.user_id) + "', '" +
                      escape_string(jail.roles) + "', " +
                      std::to_string(jail.jailed_at) + ", " +
                      std::to_string(jail.expires_at) +
                      ") "
                      "ON DUPLICATE KEY UPDATE roles = '" +
                      escape_string(jail.roles) +
                      "', jailed_at = " + std::to_string(jail.jailed_at) +
                      ", expires_at = " + std::to_string(jail.expires_at);
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool remove_active_jail(const std::string &server_id,
                          const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "DELETE FROM active_jails WHERE server_id = '" +
                      escape_string(server_id) + "' AND user_id = '" +
                      escape_string(user_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::vector<ActiveJail> get_expired_jails(uint64_t now_unix) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<ActiveJail> entries;
    if (!mysql_)
      return entries;
    std::string sql =
        "SELECT server_id, user_id, roles, jailed_at, expires_at FROM "
        "active_jails WHERE expires_at > 0 AND expires_at <= " +
        std::to_string(now_unix);
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          ActiveJail j;
          j.server_id = row[0] ? row[0] : "";
          j.user_id = row[1] ? row[1] : "";
          j.roles = row[2] ? row[2] : "";
          j.jailed_at = row[3] ? std::stoull(row[3]) : 0;
          j.expires_at = row[4] ? std::stoull(row[4]) : 0;
          entries.push_back(j);
        }
        mysql_free_result(res);
      }
    }
    return entries;
  }

  //  Moderation logs

  struct ModLogEntry {
    int id;
    std::string action;
    std::string moderator_id;
    std::string target_id;
    std::string reason;
    std::string extra_details;
    std::string created_at; // YYYY-MM-DD HH:MM:SS
  };

  bool record_moderation_action(const std::string &server_id,
                                const std::string &action,
                                const std::string &moderator_id,
                                const std::string &target_id,
                                const std::string &reason,
                                const std::string &extra_details = "") {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT INTO moderation_logs (server_id, action, moderator_id, "
        "target_id, reason, extra_details) VALUES ('" +
        escape_string(server_id) + "', '" + escape_string(action) + "', '" +
        escape_string(moderator_id) + "', '" + escape_string(target_id) +
        "', '" + escape_string(reason) + "', '" + escape_string(extra_details) +
        "')";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool delete_moderation_action(const std::string &server_id, int id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "DELETE FROM moderation_logs WHERE server_id = '" +
                      escape_string(server_id) +
                      "' AND id = " + std::to_string(id);
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  // Returns the moderation history FOR a user (actions taken against them)
  std::vector<ModLogEntry> get_mod_logs(const std::string &server_id,
                                        const std::string &target_id,
                                        int limit = 10) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<ModLogEntry> entries;
    if (!mysql_)
      return entries;
    std::string sql =
        "SELECT id, action, moderator_id, target_id, COALESCE(reason,''), "
        "COALESCE(extra_details,''), "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i') FROM moderation_logs "
        "WHERE server_id = '" +
        escape_string(server_id) +
        "' "
        "AND target_id = '" +
        escape_string(target_id) +
        "' "
        "ORDER BY id DESC LIMIT " +
        std::to_string(limit);
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          ModLogEntry e;
          e.id = row[0] ? std::stoi(row[0]) : 0;
          e.action = row[1] ? row[1] : "";
          e.moderator_id = row[2] ? row[2] : "";
          e.target_id = row[3] ? row[3] : "";
          e.reason = row[4] ? row[4] : "";
          e.extra_details = row[5] ? row[5] : "";
          e.created_at = row[6] ? row[6] : "";
          entries.push_back(e);
        }
        mysql_free_result(res);
      }
    }
    return entries;
  }

  // Returns the moderation actions BY a user (actions they invoked against
  // others)
  std::vector<ModLogEntry> get_mod_actions(const std::string &server_id,
                                           const std::string &moderator_id,
                                           int limit = 10) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<ModLogEntry> entries;
    if (!mysql_)
      return entries;
    std::string sql =
        "SELECT id, action, moderator_id, target_id, COALESCE(reason,''), "
        "COALESCE(extra_details,''), "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i') FROM moderation_logs "
        "WHERE server_id = '" +
        escape_string(server_id) +
        "' "
        "AND moderator_id = '" +
        escape_string(moderator_id) +
        "' "
        "ORDER BY id DESC LIMIT " +
        std::to_string(limit);
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          ModLogEntry e;
          e.id = row[0] ? std::stoi(row[0]) : 0;
          e.action = row[1] ? row[1] : "";
          e.moderator_id = row[2] ? row[2] : "";
          e.target_id = row[3] ? row[3] : "";
          e.reason = row[4] ? row[4] : "";
          e.extra_details = row[5] ? row[5] : "";
          e.created_at = row[6] ? row[6] : "";
          entries.push_back(e);
        }
        mysql_free_result(res);
      }
    }
    return entries;
  }

  // --- Recurring Messages CRUD ---

  // Returns true if the channel already has a recurring message scheduled.
  bool recurring_channel_occupied(const std::string &channel_id) {
    std::shared_lock<std::shared_mutex> lock(rm_cache_mutex_);
    return rm_channel_index_.count(channel_id) > 0;
  }

  // Returns number of recurring messages for a server (cached).
  int count_recurring_for_server(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(rm_cache_mutex_);
    auto it = rm_server_index_.find(server_id);
    if (it != rm_server_index_.end())
      return static_cast<int>(it->second.size());
    return 0;
  }

  // Returns all recurring messages for a server (from cache).
  std::vector<RecurringMessage>
  get_recurring_messages_for_server(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(rm_cache_mutex_);
    std::vector<RecurringMessage> out;
    auto it = rm_server_index_.find(server_id);
    if (it != rm_server_index_.end()) {
      for (int id : it->second) {
        auto it2 = rm_cache_.find(id);
        if (it2 != rm_cache_.end())
          out.push_back(it2->second);
      }
    }
    return out;
  }

  // Returns the recurring message for a channel if one exists.
  std::optional<RecurringMessage>
  get_recurring_by_channel(const std::string &channel_id) {
    std::shared_lock<std::shared_mutex> lock(rm_cache_mutex_);
    auto it = rm_channel_index_.find(channel_id);
    if (it != rm_channel_index_.end()) {
      auto it2 = rm_cache_.find(it->second);
      if (it2 != rm_cache_.end())
        return it2->second;
    }
    return std::nullopt;
  }

  // Returns a recurring message by id, validating server ownership.
  std::optional<RecurringMessage>
  get_recurring_by_id(int id, const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(rm_cache_mutex_);
    auto it = rm_cache_.find(id);
    if (it != rm_cache_.end() && it->second.server_id == server_id)
      return it->second;
    return std::nullopt;
  }

  // Finds a recurring message by name (case-insensitive) within a server.
  std::optional<RecurringMessage>
  get_recurring_by_name(const std::string &server_id, const std::string &name) {
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
          if (cname == search)
            return it2->second;
        }
      }
    }
    return std::nullopt;
  }

  // Returns all recurring messages whose next fire time (last_sent + interval)
  // <= now.
  std::vector<RecurringMessage> get_due_recurring_messages(uint64_t now) {
    std::shared_lock<std::shared_mutex> lock(rm_cache_mutex_);
    std::vector<RecurringMessage> out;
    for (const auto &[id, rm] : rm_cache_) {
      if (rm.last_sent + rm.interval_seconds <= now) {
        out.push_back(rm);
      }
    }
    return out;
  }

  // Inserts a new recurring message; updates rm.id with the auto-increment
  // value.
  bool add_recurring_message(RecurringMessage &rm) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string esc_server = escape_string(rm.server_id);
    std::string esc_channel = escape_string(rm.channel_id);
    std::string esc_name = escape_string(rm.name);
    std::string esc_type = escape_string(rm.message_type);
    std::string esc_content = escape_string(rm.content);
    std::string esc_embed_val =
        rm.embed_json.empty() ? std::string("NULL")
                              : ("'" + escape_string(rm.embed_json) + "'");
    int owner_tok = rm.owner_tokens ? 1 : 0;

    std::string sql = "INSERT INTO recurring_messages "
                      "(server_id, channel_id, name, message_type, content, "
                      "embed_json, interval_seconds, last_sent, owner_tokens) "
                      "VALUES ('" +
                      esc_server + "', '" + esc_channel + "', '" + esc_name +
                      "', '" + esc_type +
                      "', "
                      "'" +
                      esc_content + "', " + esc_embed_val + ", " +
                      std::to_string(rm.interval_seconds) + ", 0, " +
                      std::to_string(owner_tok) + ")";

    if (mysql_query(mysql_, sql.c_str())) {
      std::cerr << "[bronx db] add_recurring_message failed: "
                << mysql_error(mysql_) << "\n";
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
  bool remove_recurring_message(int id, const std::string &server_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string sql =
        "DELETE FROM recurring_messages WHERE id = " + std::to_string(id) +
        " AND server_id = '" + escape_string(server_id) + "'";
    if (mysql_query(mysql_, sql.c_str())) {
      std::cerr << "[bronx db] remove_recurring_message failed: "
                << mysql_error(mysql_) << "\n";
      return false;
    }
    if (mysql_affected_rows(mysql_) == 0)
      return false;

    std::lock_guard<std::shared_mutex> cache_lock(rm_cache_mutex_);
    auto it = rm_cache_.find(id);
    if (it != rm_cache_.end()) {
      rm_channel_index_.erase(it->second.channel_id);
      auto &sv = rm_server_index_[it->second.server_id];
      sv.erase(std::remove(sv.begin(), sv.end(), id), sv.end());
      rm_cache_.erase(it);
    }
    return true;
  }

  // Updates all mutable fields of an existing recurring message.
  bool update_recurring_message(const RecurringMessage &rm) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string esc_channel = escape_string(rm.channel_id);
    std::string esc_name = escape_string(rm.name);
    std::string esc_type = escape_string(rm.message_type);
    std::string esc_content = escape_string(rm.content);
    std::string esc_embed_val =
        rm.embed_json.empty() ? std::string("NULL")
                              : ("'" + escape_string(rm.embed_json) + "'");

    std::string sql = "UPDATE recurring_messages SET "
                      "channel_id = '" +
                      esc_channel +
                      "', "
                      "name = '" +
                      esc_name +
                      "', "
                      "message_type = '" +
                      esc_type +
                      "', "
                      "content = '" +
                      esc_content +
                      "', "
                      "embed_json = " +
                      esc_embed_val +
                      ", "
                      "interval_seconds = " +
                      std::to_string(rm.interval_seconds) +
                      ", "
                      "owner_tokens = " +
                      (rm.owner_tokens ? std::string("1") : std::string("0")) +
                      " "
                      "WHERE id = " +
                      std::to_string(rm.id) + " AND server_id = '" +
                      escape_string(rm.server_id) + "'";

    if (mysql_query(mysql_, sql.c_str())) {
      std::cerr << "[bronx db] update_recurring_message failed: "
                << mysql_error(mysql_) << "\n";
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
    if (!mysql_)
      return false;

    std::string sql =
        "UPDATE recurring_messages SET last_sent = " + std::to_string(ts) +
        " WHERE id = " + std::to_string(id);
    if (mysql_query(mysql_, sql.c_str())) {
      std::cerr << "[bronx db] update_recurring_last_sent failed: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    std::lock_guard<std::shared_mutex> cache_lock(rm_cache_mutex_);
    auto it = rm_cache_.find(id);
    if (it != rm_cache_.end())
      it->second.last_sent = ts;
    return true;
  }

  // --- RSS Feeds CRUD ---

  std::optional<RssFeed> get_rss_by_id(int id, const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(rss_cache_mutex_);
    auto it = rss_cache_.find(id);
    if (it != rss_cache_.end() && it->second.server_id == server_id) {
      return it->second;
    }
    return std::nullopt;
  }

  std::optional<RssFeed> get_rss_by_url(const std::string &server_id,
                                        const std::string &url) {
    std::shared_lock<std::shared_mutex> lock(rss_cache_mutex_);
    auto it = rss_server_index_.find(server_id);
    if (it != rss_server_index_.end()) {
      for (int id : it->second) {
        auto fit = rss_cache_.find(id);
        if (fit != rss_cache_.end() && fit->second.url == url) {
          return fit->second;
        }
      }
    }
    return std::nullopt;
  }

  std::vector<RssFeed> get_rss_feeds_for_server(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(rss_cache_mutex_);
    std::vector<RssFeed> out;
    auto it = rss_server_index_.find(server_id);
    if (it != rss_server_index_.end()) {
      for (int id : it->second) {
        auto fit = rss_cache_.find(id);
        if (fit != rss_cache_.end()) {
          out.push_back(fit->second);
        }
      }
    }
    return out;
  }

  std::vector<RssFeed> get_all_rss_feeds() {
    std::shared_lock<std::shared_mutex> lock(rss_cache_mutex_);
    std::vector<RssFeed> out;
    out.reserve(rss_cache_.size());
    for (const auto &[id, feed] : rss_cache_) {
      out.push_back(feed);
    }
    return out;
  }

  bool add_rss_feed(RssFeed &feed) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string esc_server = escape_string(feed.server_id);
    std::string esc_url = escape_string(feed.url);
    std::string esc_channels = escape_string(feed.channels);
    std::string esc_seen =
        feed.seen_items.empty() ? "[]" : escape_string(feed.seen_items);

    std::string sql = "INSERT INTO rss_feeds (server_id, url, channels, "
                      "last_checked, seen_items, check_interval) "
                      "VALUES ('" +
                      esc_server + "', '" + esc_url + "', '" + esc_channels +
                      "', 0, '" + esc_seen + "', " +
                      std::to_string(feed.check_interval) + ")";

    if (mysql_query(mysql_, sql.c_str())) {
      std::cerr << "[bronx db] add_rss_feed failed: " << mysql_error(mysql_)
                << "\n";
      return false;
    }

    feed.id = static_cast<int>(mysql_insert_id(mysql_));
    feed.last_checked = 0;

    std::lock_guard<std::shared_mutex> cache_lock(rss_cache_mutex_);
    rss_cache_[feed.id] = feed;
    rss_server_index_[feed.server_id].push_back(feed.id);
    return true;
  }

  bool remove_rss_feed(int id, const std::string &server_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string sql = "DELETE FROM rss_feeds WHERE id = " + std::to_string(id) +
                      " AND server_id = '" + escape_string(server_id) + "'";
    if (mysql_query(mysql_, sql.c_str())) {
      std::cerr << "[bronx db] remove_rss_feed failed: " << mysql_error(mysql_)
                << "\n";
      return false;
    }
    if (mysql_affected_rows(mysql_) == 0)
      return false;

    std::lock_guard<std::shared_mutex> cache_lock(rss_cache_mutex_);
    auto it = rss_cache_.find(id);
    if (it != rss_cache_.end()) {
      auto &sv = rss_server_index_[it->second.server_id];
      sv.erase(std::remove(sv.begin(), sv.end(), id), sv.end());
      rss_cache_.erase(it);
    }
    return true;
  }

  bool update_rss_feed_channels(int id, const std::string &server_id,
                                const std::string &channels) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string sql = "UPDATE rss_feeds SET channels = '" +
                      escape_string(channels) +
                      "' "
                      "WHERE id = " +
                      std::to_string(id) + " AND server_id = '" +
                      escape_string(server_id) + "'";
    if (mysql_query(mysql_, sql.c_str())) {
      std::cerr << "[bronx db] update_rss_feed_channels failed: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    std::lock_guard<std::shared_mutex> cache_lock(rss_cache_mutex_);
    auto it = rss_cache_.find(id);
    if (it != rss_cache_.end()) {
      it->second.channels = channels;
    }
    return true;
  }

  bool update_rss_feed_check(int id, uint64_t last_checked,
                             const std::string &seen_items) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string sql =
        "UPDATE rss_feeds SET last_checked = " + std::to_string(last_checked) +
        ", seen_items = '" + escape_string(seen_items) +
        "' "
        "WHERE id = " +
        std::to_string(id);
    if (mysql_query(mysql_, sql.c_str())) {
      std::cerr << "[bronx db] update_rss_feed_check failed: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    std::lock_guard<std::shared_mutex> cache_lock(rss_cache_mutex_);
    auto it = rss_cache_.find(id);
    if (it != rss_cache_.end()) {
      it->second.last_checked = last_checked;
      it->second.seen_items = seen_items;
    }
    return true;
  }

  bool update_rss_feed_interval(int id, int interval) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string sql =
        "UPDATE rss_feeds SET check_interval = " + std::to_string(interval) +
        " WHERE id = " + std::to_string(id);
    if (mysql_query(mysql_, sql.c_str())) {
      std::cerr << "[bronx db] update_rss_feed_interval failed: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    std::lock_guard<std::shared_mutex> cache_lock(rss_cache_mutex_);
    auto it = rss_cache_.find(id);
    if (it != rss_cache_.end()) {
      it->second.check_interval = interval;
    }
    return true;
  }

  // =========================================================================
  // Autoresponder CRUD
  // =========================================================================

  static std::string json_arr_str(const std::vector<std::string> &v) {
    nlohmann::json j = v;
    return j.dump();
  }

  std::vector<AutoresponderConfig>
  get_autoresponders_for_server(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(arsp_cache_mutex_);
    std::vector<AutoresponderConfig> out;
    auto it = arsp_server_index_.find(server_id);
    if (it != arsp_server_index_.end()) {
      for (int id : it->second) {
        auto it2 = arsp_cache_.find(id);
        if (it2 != arsp_cache_.end())
          out.push_back(it2->second);
      }
    }
    return out;
  }

  std::optional<AutoresponderConfig>
  get_autoresponder_by_id(int id, const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(arsp_cache_mutex_);
    auto it = arsp_cache_.find(id);
    if (it != arsp_cache_.end() && it->second.server_id == server_id)
      return it->second;
    return std::nullopt;
  }

  std::optional<AutoresponderConfig>
  get_autoresponder_by_name(const std::string &server_id,
                            const std::string &name) {
    std::shared_lock<std::shared_mutex> lock(arsp_cache_mutex_);
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   ::tolower);
    auto it = arsp_server_index_.find(server_id);
    if (it != arsp_server_index_.end()) {
      for (int id : it->second) {
        auto it2 = arsp_cache_.find(id);
        if (it2 != arsp_cache_.end()) {
          std::string cn = it2->second.name;
          std::transform(cn.begin(), cn.end(), cn.begin(), ::tolower);
          if (cn == lower_name)
            return it2->second;
        }
      }
    }
    return std::nullopt;
  }

  bool add_autoresponder(AutoresponderConfig &c) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;
    auto null_or = [](const std::vector<std::string> &v) -> std::string {
      if (v.empty())
        return "NULL";
      nlohmann::json j = v;
      return "'" + j.dump() + "'";
    };
    std::string sql =
        "INSERT INTO autoresponders "
        "(server_id,name,keyword,match_type,message_type,content,embed_json,"
        "allow_roles,allow_users,allow_channels,ignore_roles,ignore_users,"
        "ignore_channels) VALUES ("
        "'" +
        escape_string(c.server_id) +
        "',"
        "'" +
        escape_string(c.name) +
        "',"
        "'" +
        escape_string(c.keyword) +
        "',"
        "'" +
        escape_string(c.match_type) +
        "',"
        "'" +
        escape_string(c.message_type) +
        "',"
        "'" +
        escape_string(c.content) + "'," +
        (c.embed_json.empty() ? "NULL"
                              : ("'" + escape_string(c.embed_json) + "'")) +
        "," + null_or(c.filter.allow_roles) + "," +
        null_or(c.filter.allow_users) + "," + null_or(c.filter.allow_channels) +
        "," + null_or(c.filter.ignore_roles) + "," +
        null_or(c.filter.ignore_users) + "," +
        null_or(c.filter.ignore_channels) + ")";
    if (mysql_query(mysql_, sql.c_str())) {
      std::cerr << "[bronx db] add_autoresponder failed: "
                << mysql_error(mysql_) << "\n";
      return false;
    }
    c.id = static_cast<int>(mysql_insert_id(mysql_));
    std::lock_guard<std::shared_mutex> cache_lock(arsp_cache_mutex_);
    arsp_cache_[c.id] = c;
    arsp_server_index_[c.server_id].push_back(c.id);
    return true;
  }

  bool remove_autoresponder(int id, const std::string &server_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "DELETE FROM autoresponders WHERE id=" + std::to_string(id) +
        " AND server_id='" + escape_string(server_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) || mysql_affected_rows(mysql_) == 0)
      return false;
    std::lock_guard<std::shared_mutex> cache_lock(arsp_cache_mutex_);
    auto it = arsp_cache_.find(id);
    if (it != arsp_cache_.end()) {
      auto &sv = arsp_server_index_[it->second.server_id];
      sv.erase(std::remove(sv.begin(), sv.end(), id), sv.end());
      arsp_cache_.erase(it);
    }
    return true;
  }

  bool update_autoresponder(const AutoresponderConfig &c) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;
    auto null_or = [](const std::vector<std::string> &v) -> std::string {
      if (v.empty())
        return "NULL";
      nlohmann::json j = v;
      return "'" + j.dump() + "'";
    };
    std::string sql =
        "UPDATE autoresponders SET "
        "name='" +
        escape_string(c.name) +
        "',"
        "keyword='" +
        escape_string(c.keyword) +
        "',"
        "match_type='" +
        escape_string(c.match_type) +
        "',"
        "message_type='" +
        escape_string(c.message_type) +
        "',"
        "content='" +
        escape_string(c.content) +
        "',"
        "embed_json=" +
        (c.embed_json.empty() ? "NULL"
                              : ("'" + escape_string(c.embed_json) + "'")) +
        ","
        "allow_roles=" +
        null_or(c.filter.allow_roles) +
        ","
        "allow_users=" +
        null_or(c.filter.allow_users) +
        ","
        "allow_channels=" +
        null_or(c.filter.allow_channels) +
        ","
        "ignore_roles=" +
        null_or(c.filter.ignore_roles) +
        ","
        "ignore_users=" +
        null_or(c.filter.ignore_users) +
        ","
        "ignore_channels=" +
        null_or(c.filter.ignore_channels) +
        " "
        "WHERE id=" +
        std::to_string(c.id) + " AND server_id='" + escape_string(c.server_id) +
        "'";
    if (mysql_query(mysql_, sql.c_str()))
      return false;
    std::lock_guard<std::shared_mutex> cache_lock(arsp_cache_mutex_);
    arsp_cache_[c.id] = c;
    return true;
  }

  // =========================================================================
  // Autoreact CRUD
  // =========================================================================

  std::vector<AutoreactConfig>
  get_autoreacts_for_server(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(arct_cache_mutex_);
    std::vector<AutoreactConfig> out;
    auto it = arct_server_index_.find(server_id);
    if (it != arct_server_index_.end()) {
      for (int id : it->second) {
        auto it2 = arct_cache_.find(id);
        if (it2 != arct_cache_.end())
          out.push_back(it2->second);
      }
    }
    return out;
  }

  std::optional<AutoreactConfig>
  get_autoreact_by_id(int id, const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(arct_cache_mutex_);
    auto it = arct_cache_.find(id);
    if (it != arct_cache_.end() && it->second.server_id == server_id)
      return it->second;
    return std::nullopt;
  }

  std::optional<AutoreactConfig>
  get_autoreact_by_name(const std::string &server_id, const std::string &name) {
    std::shared_lock<std::shared_mutex> lock(arct_cache_mutex_);
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   ::tolower);
    auto it = arct_server_index_.find(server_id);
    if (it != arct_server_index_.end()) {
      for (int id : it->second) {
        auto it2 = arct_cache_.find(id);
        if (it2 != arct_cache_.end()) {
          std::string cn = it2->second.name;
          std::transform(cn.begin(), cn.end(), cn.begin(), ::tolower);
          if (cn == lower_name)
            return it2->second;
        }
      }
    }
    return std::nullopt;
  }

  bool add_autoreact(AutoreactConfig &c) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;
    auto null_or = [](const std::vector<std::string> &v) -> std::string {
      if (v.empty())
        return "NULL";
      nlohmann::json j = v;
      return "'" + j.dump() + "'";
    };
    std::string sql =
        "INSERT INTO autoreacts (server_id,name,keyword,match_type,emojis,"
        "allow_roles,allow_users,allow_channels,ignore_roles,ignore_users,"
        "ignore_channels) VALUES ("
        "'" +
        escape_string(c.server_id) +
        "',"
        "'" +
        escape_string(c.name) +
        "',"
        "'" +
        escape_string(c.keyword) +
        "',"
        "'" +
        escape_string(c.match_type) +
        "',"
        "'" +
        escape_string(c.emojis) + "'," + null_or(c.filter.allow_roles) + "," +
        null_or(c.filter.allow_users) + "," + null_or(c.filter.allow_channels) +
        "," + null_or(c.filter.ignore_roles) + "," +
        null_or(c.filter.ignore_users) + "," +
        null_or(c.filter.ignore_channels) + ")";
    if (mysql_query(mysql_, sql.c_str())) {
      std::cerr << "[bronx db] add_autoreact failed: " << mysql_error(mysql_)
                << "\n";
      return false;
    }
    c.id = static_cast<int>(mysql_insert_id(mysql_));
    std::lock_guard<std::shared_mutex> cache_lock(arct_cache_mutex_);
    arct_cache_[c.id] = c;
    arct_server_index_[c.server_id].push_back(c.id);
    return true;
  }

  bool remove_autoreact(int id, const std::string &server_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "DELETE FROM autoreacts WHERE id=" + std::to_string(id) +
                      " AND server_id='" + escape_string(server_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) || mysql_affected_rows(mysql_) == 0)
      return false;
    std::lock_guard<std::shared_mutex> cache_lock(arct_cache_mutex_);
    auto it = arct_cache_.find(id);
    if (it != arct_cache_.end()) {
      auto &sv = arct_server_index_[it->second.server_id];
      sv.erase(std::remove(sv.begin(), sv.end(), id), sv.end());
      arct_cache_.erase(it);
    }
    return true;
  }

  bool update_autoreact(const AutoreactConfig &c) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;
    auto null_or = [](const std::vector<std::string> &v) -> std::string {
      if (v.empty())
        return "NULL";
      nlohmann::json j = v;
      return "'" + j.dump() + "'";
    };
    std::string sql = "UPDATE autoreacts SET "
                      "name='" +
                      escape_string(c.name) +
                      "',"
                      "keyword='" +
                      escape_string(c.keyword) +
                      "',"
                      "match_type='" +
                      escape_string(c.match_type) +
                      "',"
                      "emojis='" +
                      escape_string(c.emojis) +
                      "',"
                      "allow_roles=" +
                      null_or(c.filter.allow_roles) +
                      ","
                      "allow_users=" +
                      null_or(c.filter.allow_users) +
                      ","
                      "allow_channels=" +
                      null_or(c.filter.allow_channels) +
                      ","
                      "ignore_roles=" +
                      null_or(c.filter.ignore_roles) +
                      ","
                      "ignore_users=" +
                      null_or(c.filter.ignore_users) +
                      ","
                      "ignore_channels=" +
                      null_or(c.filter.ignore_channels) +
                      " "
                      "WHERE id=" +
                      std::to_string(c.id) + " AND server_id='" +
                      escape_string(c.server_id) + "'";
    if (mysql_query(mysql_, sql.c_str()))
      return false;
    std::lock_guard<std::shared_mutex> cache_lock(arct_cache_mutex_);
    arct_cache_[c.id] = c;
    return true;
  }

  bool update_autoresponder_filter(int id, const std::string &server_id,
                                   const AutoScopeFilter &f) {
    auto opt = get_autoresponder_by_id(id, server_id);
    if (!opt)
      return false;
    opt->filter = f;
    return update_autoresponder(*opt);
  }

  bool update_autoreact_filter(int id, const std::string &server_id,
                               const AutoScopeFilter &f) {
    auto opt = get_autoreact_by_id(id, server_id);
    if (!opt)
      return false;
    opt->filter = f;
    return update_autoreact(*opt);
  }

  // --- Ticket System Helper Methods ---

  std::optional<TicketConfig>
  get_ticket_config(const std::string &server_id,
                    const std::string &name = "default") {
    std::shared_lock<std::shared_mutex> lock(ticket_config_cache_mutex_);
    std::string key = server_id + ":" + name;
    auto it = ticket_config_cache_.find(key);
    if (it != ticket_config_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool save_ticket_config(const TicketConfig &config) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

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
        "INSERT INTO ticket_configs (server_id, name, modal_channel_id, "
        "modal_message_id, modal_content, modal_embed_json, "
        "reaction_emoji, input_creation_enabled, naming_scheme, category_id, "
        "view_roles, view_users, view_permissions, "
        "ticket_welcome_content, ticket_welcome_embed_json, "
        "ticket_close_emoji, ticket_close_content, ticket_counter, "
        "member_role_id) "
        "VALUES ('" +
        esc_srv + "', '" + esc_name + "', '" + esc_m_chan + "', '" + esc_m_msg +
        "', '" + esc_content + "', '" + esc_embed + "', '" + esc_emoji + "', " +
        std::to_string(input_creation) + ", '" + esc_scheme + "', '" + esc_cat +
        "', '" + esc_roles + "', '" + esc_users + "', '" + esc_perms + "', '" +
        esc_t_content + "', '" + esc_t_embed + "', '" + esc_close_emoji +
        "', '" + esc_close_msg + "', " + std::to_string(counter) + ", '" +
        esc_member_role +
        "') "
        "ON DUPLICATE KEY UPDATE "
        "modal_channel_id = '" +
        esc_m_chan +
        "', "
        "modal_message_id = '" +
        esc_m_msg +
        "', "
        "modal_content = '" +
        esc_content +
        "', "
        "modal_embed_json = '" +
        esc_embed +
        "', "
        "reaction_emoji = '" +
        esc_emoji +
        "', "
        "input_creation_enabled = " +
        std::to_string(input_creation) +
        ", "
        "naming_scheme = '" +
        esc_scheme +
        "', "
        "category_id = '" +
        esc_cat +
        "', "
        "view_roles = '" +
        esc_roles +
        "', "
        "view_users = '" +
        esc_users +
        "', "
        "view_permissions = '" +
        esc_perms +
        "', "
        "ticket_welcome_content = '" +
        esc_t_content +
        "', "
        "ticket_welcome_embed_json = '" +
        esc_t_embed +
        "', "
        "ticket_close_emoji = '" +
        esc_close_emoji +
        "', "
        "ticket_close_content = '" +
        esc_close_msg +
        "', "
        "ticket_counter = " +
        std::to_string(counter) +
        ", "
        "member_role_id = '" +
        esc_member_role + "'";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save ticket config: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    {
      std::lock_guard<std::shared_mutex> cache_lock(ticket_config_cache_mutex_);
      std::string key = config.server_id + ":" + config.name;
      ticket_config_cache_[key] = config;
    }
    return true;
  }

  std::optional<Ticket> get_ticket(const std::string &channel_id) {
    std::shared_lock<std::shared_mutex> lock(ticket_cache_mutex_);
    auto it = ticket_cache_.find(channel_id);
    if (it != ticket_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool save_ticket(const Ticket &ticket) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string esc_chan = escape_string(ticket.channel_id);
    std::string esc_srv = escape_string(ticket.server_id);
    std::string esc_user = escape_string(ticket.user_id);
    std::string esc_welcome = escape_string(ticket.welcome_message_id);
    std::string esc_status = escape_string(ticket.status);
    std::string esc_panel = escape_string(ticket.panel_name);

    std::string query =
        "INSERT INTO tickets (channel_id, server_id, user_id, ticket_number, "
        "welcome_message_id, status, panel_name) "
        "VALUES ('" +
        esc_chan + "', '" + esc_srv + "', '" + esc_user + "', " +
        std::to_string(ticket.ticket_number) + ", '" + esc_welcome + "', '" +
        esc_status + "', '" + esc_panel +
        "') "
        "ON DUPLICATE KEY UPDATE "
        "server_id = '" +
        esc_srv +
        "', "
        "user_id = '" +
        esc_user +
        "', "
        "ticket_number = " +
        std::to_string(ticket.ticket_number) +
        ", "
        "welcome_message_id = '" +
        esc_welcome +
        "', "
        "status = '" +
        esc_status +
        "', "
        "panel_name = '" +
        esc_panel + "'";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save ticket: " << mysql_error(mysql_)
                << "\n";
      return false;
    }

    {
      std::lock_guard<std::shared_mutex> cache_lock(ticket_cache_mutex_);
      ticket_cache_[ticket.channel_id] = ticket;
    }
    return true;
  }

  bool delete_ticket(const std::string &channel_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string esc_chan = escape_string(channel_id);
    std::string query =
        "DELETE FROM tickets WHERE channel_id = '" + esc_chan + "'";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to delete ticket: " << mysql_error(mysql_)
                << "\n";
      return false;
    }

    {
      std::lock_guard<std::shared_mutex> cache_lock(ticket_cache_mutex_);
      ticket_cache_.erase(channel_id);
    }
    return true;
  }

  std::vector<Ticket> get_server_tickets(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(ticket_cache_mutex_);
    std::vector<Ticket> results;
    for (const auto &[ch_id, t] : ticket_cache_) {
      if (t.server_id == server_id) {
        results.push_back(t);
      }
    }
    return results;
  }

  std::optional<Ticket> get_user_open_ticket(const std::string &server_id,
                                             const std::string &user_id) {
    std::shared_lock<std::shared_mutex> lock(ticket_cache_mutex_);
    for (const auto &[ch_id, t] : ticket_cache_) {
      if (t.server_id == server_id && t.user_id == user_id &&
          t.status == "open") {
        return t;
      }
    }
    return std::nullopt;
  }

  std::vector<TicketConfig>
  get_ticket_configs_for_server(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(ticket_config_cache_mutex_);
    std::vector<TicketConfig> results;
    for (const auto &[key, cfg] : ticket_config_cache_) {
      if (cfg.server_id == server_id) {
        results.push_back(cfg);
      }
    }
    return results;
  }

  std::vector<TicketConfig> get_all_ticket_configs() {
    std::shared_lock<std::shared_mutex> lock(ticket_config_cache_mutex_);
    std::vector<TicketConfig> results;
    for (const auto &[key, cfg] : ticket_config_cache_) {
      results.push_back(cfg);
    }
    return results;
  }

  std::vector<Ticket> get_all_tickets() {
    std::shared_lock<std::shared_mutex> lock(ticket_cache_mutex_);
    std::vector<Ticket> results;
    for (const auto &[ch_id, t] : ticket_cache_) {
      results.push_back(t);
    }
    return results;
  }

  //  Counting DB functions

  std::optional<CountingConfig>
  get_counting_config(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lk(counting_cache_mutex_);
    auto it = counting_cache_.find(server_id);
    if (it != counting_cache_.end())
      return it->second;
    return std::nullopt;
  }

  bool save_counting_config(const CountingConfig &cfg) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string esc_srv = escape_string(cfg.server_id);
    std::string esc_chan = escape_string(cfg.channel_id);
    std::string esc_last = escape_string(cfg.last_user_id);
    std::string esc_role = escape_string(cfg.fail_role_id);
    int passive = cfg.passive ? 1 : 0;
    int warn_dbl = cfg.warn_double_count ? 1 : 0;

    std::string q =
        "INSERT INTO counting_configs (server_id, channel_id, current_number, "
        "last_user_id, fail_role_id, fail_role_duration, passive, "
        "highest_count, warn_double_count) "
        "VALUES ('" +
        esc_srv + "','" + esc_chan + "'," + std::to_string(cfg.current_number) +
        ",'" + esc_last + "','" + esc_role + "'," +
        std::to_string(cfg.fail_role_duration) + "," + std::to_string(passive) +
        "," + std::to_string(cfg.highest_count) + "," +
        std::to_string(warn_dbl) +
        ") "
        "ON DUPLICATE KEY UPDATE "
        "channel_id='" +
        esc_chan +
        "', "
        "current_number=" +
        std::to_string(cfg.current_number) +
        ", "
        "last_user_id='" +
        esc_last +
        "', "
        "fail_role_id='" +
        esc_role +
        "', "
        "fail_role_duration=" +
        std::to_string(cfg.fail_role_duration) +
        ", "
        "passive=" +
        std::to_string(passive) +
        ", "
        "highest_count=" +
        std::to_string(cfg.highest_count) +
        ", "
        "warn_double_count=" +
        std::to_string(warn_dbl);

    if (mysql_query(mysql_, q.c_str())) {
      std::cerr << "[bronx db] failed to save counting_config: "
                << mysql_error(mysql_) << "\n";
      return false;
    }
    {
      std::lock_guard<std::shared_mutex> lk(counting_cache_mutex_);
      counting_cache_[cfg.server_id] = cfg;
    }
    return true;
  }

  // Increment a user's correct count by 1
  void counting_increment_correct(const std::string &server_id,
                                  const std::string &user_id) {
    std::string key = server_id + ":" + user_id;
    {
      std::lock_guard<std::shared_mutex> lk(counting_stats_cache_mutex_);
      counting_stats_cache_[key].server_id = server_id;
      counting_stats_cache_[key].user_id = user_id;
      counting_stats_cache_[key].correct_counts++;
    }
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return;
    std::string esc_srv = escape_string(server_id);
    std::string esc_user = escape_string(user_id);
    std::string q = "INSERT INTO counting_stats (server_id, user_id, "
                    "correct_counts, failed_counts) "
                    "VALUES ('" +
                    esc_srv + "','" + esc_user +
                    "',1,0) "
                    "ON DUPLICATE KEY UPDATE correct_counts=correct_counts+1";
    mysql_query(mysql_, q.c_str());
  }

  // Increment a user's failed count by 1
  void counting_increment_failed(const std::string &server_id,
                                 const std::string &user_id) {
    std::string key = server_id + ":" + user_id;
    {
      std::lock_guard<std::shared_mutex> lk(counting_stats_cache_mutex_);
      counting_stats_cache_[key].server_id = server_id;
      counting_stats_cache_[key].user_id = user_id;
      counting_stats_cache_[key].failed_counts++;
    }
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return;
    std::string esc_srv = escape_string(server_id);
    std::string esc_user = escape_string(user_id);
    std::string q = "INSERT INTO counting_stats (server_id, user_id, "
                    "correct_counts, failed_counts) "
                    "VALUES ('" +
                    esc_srv + "','" + esc_user +
                    "',0,1) "
                    "ON DUPLICATE KEY UPDATE failed_counts=failed_counts+1";
    mysql_query(mysql_, q.c_str());
  }

  // Return all stats for a server sorted by correct_counts desc
  std::vector<CountingStats>
  get_counting_leaderboard(const std::string &server_id, int limit = 10) {
    std::shared_lock<std::shared_mutex> lk(counting_stats_cache_mutex_);
    std::vector<CountingStats> results;
    for (const auto &[key, st] : counting_stats_cache_) {
      if (st.server_id == server_id)
        results.push_back(st);
    }
    std::sort(results.begin(), results.end(),
              [](const CountingStats &a, const CountingStats &b) {
                return a.correct_counts > b.correct_counts;
              });
    if ((int)results.size() > limit)
      results.resize(limit);
    return results;
  }

  CountingStats get_counting_stats(const std::string &server_id,
                                   const std::string &user_id) {
    std::string key = server_id + ":" + user_id;
    std::shared_lock<std::shared_mutex> lk(counting_stats_cache_mutex_);
    auto it = counting_stats_cache_.find(key);
    if (it != counting_stats_cache_.end())
      return it->second;
    CountingStats st;
    st.server_id = server_id;
    st.user_id = user_id;
    return st;
  }

  // Returns top correct counter, highest count ever, and top failer for server
  struct CountingHighlights {
    std::string top_counter_id;
    int64_t top_counter_score = 0;
    std::string top_failer_id;
    int64_t top_failer_score = 0;
    int64_t highest_count = 0;
  };

  CountingHighlights get_counting_highlights(const std::string &server_id) {
    CountingHighlights h;
    {
      std::shared_lock<std::shared_mutex> lk(counting_stats_cache_mutex_);
      for (const auto &[key, st] : counting_stats_cache_) {
        if (st.server_id != server_id)
          continue;
        if (st.correct_counts > h.top_counter_score) {
          h.top_counter_score = st.correct_counts;
          h.top_counter_id = st.user_id;
        }
        if (st.failed_counts > h.top_failer_score) {
          h.top_failer_score = st.failed_counts;
          h.top_failer_id = st.user_id;
        }
      }
    }
    {
      std::shared_lock<std::shared_mutex> lk(counting_cache_mutex_);
      auto it = counting_cache_.find(server_id);
      if (it != counting_cache_.end())
        h.highest_count = it->second.highest_count;
    }
    return h;
  }

  std::optional<UserAFK> get_user_afk(const std::string &user_id) {
    std::shared_lock<std::shared_mutex> lock(user_afk_cache_mutex_);
    auto it = user_afk_cache_.find(user_id);
    if (it != user_afk_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool save_user_afk(const UserAFK &afk) {
    bool success = false;
    {
      std::lock_guard<std::mutex> db_lock(db_mutex_);
      std::string esc_user_id = escape_string(afk.user_id);
      std::string esc_tmpl = escape_string(afk.custom_template);
      std::string esc_reason = escape_string(afk.reason);
      int is_afk_val = afk.is_afk ? 1 : 0;
      std::string esc_preset = escape_string(afk.active_preset);

      std::string query = "INSERT INTO user_afk (user_id, custom_template, "
                          "is_afk, reason, since, active_preset) "
                          "VALUES ('" +
                          esc_user_id + "', '" + esc_tmpl + "', " +
                          std::to_string(is_afk_val) + ", '" + esc_reason +
                          "', " + std::to_string(afk.since) + ", '" +
                          esc_preset +
                          "') "
                          "ON DUPLICATE KEY UPDATE "
                          "custom_template = '" +
                          esc_tmpl +
                          "', "
                          "is_afk = " +
                          std::to_string(is_afk_val) +
                          ", "
                          "reason = '" +
                          esc_reason +
                          "', "
                          "since = " +
                          std::to_string(afk.since) +
                          ", "
                          "active_preset = '" +
                          esc_preset + "'";

      if (mysql_query(mysql_, query.c_str())) {
        std::cerr << "[bronx db] failed to save user afk: "
                  << mysql_error(mysql_) << "\n";
        return false;
      }
      success = true;
    }

    if (success) {
      {
        std::lock_guard<std::shared_mutex> lock(user_afk_cache_mutex_);
        user_afk_cache_[afk.user_id] = afk;
      }
      if (on_afk_save) {
        on_afk_save(afk);
      }
    }
    return success;
  }

  std::string get_user_afk_preset(const std::string &user_id,
                                  const std::string &preset_name) {
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

  bool save_user_afk_preset(const std::string &user_id,
                            const std::string &preset_name,
                            const std::string &tmpl_json) {
    if (preset_name == "default") {
      auto afk_opt = get_user_afk(user_id);
      UserAFK afk;
      if (afk_opt)
        afk = *afk_opt;
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
        "VALUES ('" +
        esc_user_id + "', '" + esc_preset + "', '" + esc_tmpl +
        "') "
        "ON DUPLICATE KEY UPDATE template_json = '" +
        esc_tmpl + "'";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save user afk preset: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    {
      std::lock_guard<std::shared_mutex> lock(user_afk_presets_cache_mutex_);
      user_afk_presets_cache_[user_id][preset_name] = tmpl_json;
    }
    return true;
  }

  bool delete_user_afk_preset(const std::string &user_id,
                              const std::string &preset_name) {
    if (preset_name == "default") {
      // Cannot delete default preset, reset it to empty/default configuration
      auto afk_opt = get_user_afk(user_id);
      UserAFK afk;
      if (afk_opt)
        afk = *afk_opt;
      afk.user_id = user_id;
      afk.custom_template = "";
      return save_user_afk(afk);
    }

    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string esc_user_id = escape_string(user_id);
    std::string esc_preset = escape_string(preset_name);

    std::string query = "DELETE FROM user_afk_presets WHERE user_id = '" +
                        esc_user_id + "' AND preset_name = '" + esc_preset +
                        "'";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to delete user afk preset: "
                << mysql_error(mysql_) << "\n";
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

  std::vector<std::string>
  get_user_afk_presets_list(const std::string &user_id) {
    std::shared_lock<std::shared_mutex> lock(user_afk_presets_cache_mutex_);
    std::vector<std::string> presets;
    auto it = user_afk_presets_cache_.find(user_id);
    if (it != user_afk_presets_cache_.end()) {
      for (const auto &[name, _] : it->second) {
        presets.push_back(name);
      }
    }
    return presets;
  }

  std::optional<RobloxLink> get_roblox_link(const std::string &user_id) {
    std::shared_lock<std::shared_mutex> lock(roblox_links_cache_mutex_);
    auto it = roblox_links_cache_.find(user_id);
    if (it != roblox_links_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  std::optional<RobloxLink>
  get_roblox_link_by_roblox_username(const std::string &username) {
    std::shared_lock<std::shared_mutex> lock(roblox_links_cache_mutex_);
    std::string target = username;
    std::transform(target.begin(), target.end(), target.begin(), ::tolower);
    for (const auto &[_, link] : roblox_links_cache_) {
      std::string name_lower = link.roblox_username;
      std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(),
                     ::tolower);
      if (name_lower == target) {
        return link;
      }
    }
    return std::nullopt;
  }

  std::optional<RobloxLink> get_roblox_link_by_roblox_id(uint64_t roblox_id) {
    std::shared_lock<std::shared_mutex> lock(roblox_links_cache_mutex_);
    for (const auto &[_, link] : roblox_links_cache_) {
      if (link.roblox_id == roblox_id) {
        return link;
      }
    }
    return std::nullopt;
  }

  bool save_roblox_link(const std::string &user_id, uint64_t roblox_id,
                        const std::string &roblox_username) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string esc_user_id = escape_string(user_id);
    std::string esc_roblox_username = escape_string(roblox_username);

    std::string query =
        "INSERT INTO roblox_links (user_id, roblox_id, roblox_username) "
        "VALUES ('" +
        esc_user_id + "', " + std::to_string(roblox_id) + ", '" +
        esc_roblox_username +
        "') "
        "ON DUPLICATE KEY UPDATE "
        "roblox_id = " +
        std::to_string(roblox_id) +
        ", "
        "roblox_username = '" +
        esc_roblox_username + "'";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save roblox link: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    {
      std::lock_guard<std::shared_mutex> lock(roblox_links_cache_mutex_);
      roblox_links_cache_[user_id] = {user_id, roblox_id, roblox_username};
    }
    return true;
  }

  bool delete_roblox_link(const std::string &user_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string esc_user_id = escape_string(user_id);
    std::string query =
        "DELETE FROM roblox_links WHERE user_id = '" + esc_user_id + "'";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to delete roblox link: "
                << mysql_error(mysql_) << "\n";
      return false;
    }
    {
      std::lock_guard<std::shared_mutex> lock(roblox_links_cache_mutex_);
      roblox_links_cache_.erase(user_id);
    }
    return true;
  }

  struct AccountLink {
    std::string stoat_id;
    std::string fluxer_id;
    std::string discord_id;
  };

  bool save_account_link(const std::string &stoat_id,
                         const std::string &fluxer_id,
                         const std::string &discord_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string val_stoat =
        stoat_id.empty() ? "NULL" : "'" + escape_string(stoat_id) + "'";
    std::string val_fluxer =
        fluxer_id.empty() ? "NULL" : "'" + escape_string(fluxer_id) + "'";
    std::string val_discord =
        discord_id.empty() ? "NULL" : "'" + escape_string(discord_id) + "'";

    std::string query = "REPLACE INTO account_links (stoat_id, fluxer_id, "
                        "discord_id) VALUES (" +
                        val_stoat + ", " + val_fluxer + ", " + val_discord +
                        ")";
    return mysql_query(mysql_, query.c_str()) == 0;
  }

  std::optional<AccountLink> get_account_link(const std::string &platform,
                                              const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return std::nullopt;

    std::string query =
        "SELECT stoat_id, fluxer_id, discord_id FROM account_links WHERE " +
        platform + "_id = '" + escape_string(user_id) + "'";
    if (mysql_query(mysql_, query.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          AccountLink link;
          link.stoat_id = row[0] ? row[0] : "";
          link.fluxer_id = row[1] ? row[1] : "";
          link.discord_id = row[2] ? row[2] : "";
          mysql_free_result(res);
          return link;
        }
        mysql_free_result(res);
      }
    }
    return std::nullopt;
  }

  //  Social Subscriptions CRUD

  std::vector<SocialSubscription> get_all_social_subs() {
    std::shared_lock<std::shared_mutex> lock(social_sub_cache_mutex_);
    std::vector<SocialSubscription> out;
    out.reserve(social_sub_cache_.size());
    for (const auto &[id, sub] : social_sub_cache_)
      out.push_back(sub);
    return out;
  }

  std::vector<SocialSubscription>
  get_social_subs_for_server(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(social_sub_cache_mutex_);
    std::vector<SocialSubscription> out;
    auto it = social_sub_server_index_.find(server_id);
    if (it == social_sub_server_index_.end())
      return out;
    for (int id : it->second) {
      auto fit = social_sub_cache_.find(id);
      if (fit != social_sub_cache_.end())
        out.push_back(fit->second);
    }
    return out;
  }

  std::optional<SocialSubscription>
  get_social_sub_by_id(int id, const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(social_sub_cache_mutex_);
    auto it = social_sub_cache_.find(id);
    if (it != social_sub_cache_.end() && it->second.server_id == server_id)
      return it->second;
    return std::nullopt;
  }

  bool add_social_sub(SocialSubscription &sub) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string q = "INSERT INTO social_subscriptions "
                    "(server_id,platform,channel_handle,channels,events,last_"
                    "checked,seen_items) VALUES ('" +
                    escape_string(sub.server_id) + "','" +
                    escape_string(sub.platform) + "','" +
                    escape_string(sub.channel_handle) + "','" +
                    escape_string(sub.channels) + "','" +
                    escape_string(sub.events) + "',0,'[]')";
    if (mysql_query(mysql_, q.c_str())) {
      std::cerr << "[bronx db] add_social_sub failed: " << mysql_error(mysql_)
                << "\n";
      return false;
    }
    sub.id = static_cast<int>(mysql_insert_id(mysql_));
    {
      std::lock_guard<std::shared_mutex> cache_lock(social_sub_cache_mutex_);
      social_sub_cache_[sub.id] = sub;
      social_sub_server_index_[sub.server_id].push_back(sub.id);
    }
    return true;
  }

  bool remove_social_sub(int id, const std::string &server_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string q =
        "DELETE FROM social_subscriptions WHERE id=" + std::to_string(id) +
        " AND server_id='" + escape_string(server_id) + "'";
    if (mysql_query(mysql_, q.c_str())) {
      std::cerr << "[bronx db] remove_social_sub failed: "
                << mysql_error(mysql_) << "\n";
      return false;
    }
    {
      std::lock_guard<std::shared_mutex> cache_lock(social_sub_cache_mutex_);
      social_sub_cache_.erase(id);
      auto &idx = social_sub_server_index_[server_id];
      idx.erase(std::remove(idx.begin(), idx.end(), id), idx.end());
    }
    // Also remove its embed configs
    {
      std::lock_guard<std::mutex> dbl(db_mutex_); // already held – inner query
    }
    mysql_query(mysql_, ("DELETE FROM social_embed_configs WHERE sub_id=" +
                         std::to_string(id))
                            .c_str());
    {
      std::lock_guard<std::shared_mutex> sec_lock(social_embed_cache_mutex_);
      for (const std::string ev : {"live", "vod", "post", "clip"}) {
        social_embed_cache_.erase(std::to_string(id) + ":" + ev);
      }
    }
    return true;
  }

  bool update_social_sub_channels(int id, const std::string &server_id,
                                  const std::string &channels) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string q = "UPDATE social_subscriptions SET channels='" +
                    escape_string(channels) +
                    "' WHERE id=" + std::to_string(id) + " AND server_id='" +
                    escape_string(server_id) + "'";
    if (mysql_query(mysql_, q.c_str()))
      return false;
    {
      std::lock_guard<std::shared_mutex> cache_lock(social_sub_cache_mutex_);
      auto it = social_sub_cache_.find(id);
      if (it != social_sub_cache_.end())
        it->second.channels = channels;
    }
    return true;
  }

  bool update_social_sub_events(int id, const std::string &server_id,
                                const std::string &events) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string q = "UPDATE social_subscriptions SET events='" +
                    escape_string(events) + "' WHERE id=" + std::to_string(id) +
                    " AND server_id='" + escape_string(server_id) + "'";
    if (mysql_query(mysql_, q.c_str()))
      return false;
    {
      std::lock_guard<std::shared_mutex> cache_lock(social_sub_cache_mutex_);
      auto it = social_sub_cache_.find(id);
      if (it != social_sub_cache_.end())
        it->second.events = events;
    }
    return true;
  }

  bool update_social_sub_check(int id, uint64_t last_checked,
                               const std::string &seen_items) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string q = "UPDATE social_subscriptions SET last_checked=" +
                    std::to_string(last_checked) + ", seen_items='" +
                    escape_string(seen_items) +
                    "' WHERE id=" + std::to_string(id);
    if (mysql_query(mysql_, q.c_str()))
      return false;
    {
      std::lock_guard<std::shared_mutex> cache_lock(social_sub_cache_mutex_);
      auto it = social_sub_cache_.find(id);
      if (it != social_sub_cache_.end()) {
        it->second.last_checked = last_checked;
        it->second.seen_items = seen_items;
      }
    }
    return true;
  }

  //  Social Embed Config CRUD

  std::optional<SocialEmbedConfig>
  get_social_embed(int sub_id, const std::string &event_type) {
    std::shared_lock<std::shared_mutex> lock(social_embed_cache_mutex_);
    std::string key = std::to_string(sub_id) + ":" + event_type;
    auto it = social_embed_cache_.find(key);
    if (it != social_embed_cache_.end())
      return it->second;
    return std::nullopt;
  }

  bool save_social_embed(const SocialEmbedConfig &cfg) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string q =
        "INSERT INTO social_embed_configs "
        "(sub_id,event_type,message_type,content,embed_json) VALUES (" +
        std::to_string(cfg.sub_id) + ",'" + escape_string(cfg.event_type) +
        "','" + escape_string(cfg.message_type) + "','" +
        escape_string(cfg.content) + "','" + escape_string(cfg.embed_json) +
        "') "
        "ON DUPLICATE KEY UPDATE "
        "message_type='" +
        escape_string(cfg.message_type) +
        "', "
        "content='" +
        escape_string(cfg.content) +
        "', "
        "embed_json='" +
        escape_string(cfg.embed_json) + "'";
    if (mysql_query(mysql_, q.c_str())) {
      std::cerr << "[bronx db] save_social_embed failed: "
                << mysql_error(mysql_) << "\n";
      return false;
    }
    {
      std::lock_guard<std::shared_mutex> lock(social_embed_cache_mutex_);
      std::string key = std::to_string(cfg.sub_id) + ":" + cfg.event_type;
      social_embed_cache_[key] = cfg;
    }
    return true;
  }

  bool delete_social_embed(int sub_id, const std::string &event_type) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string q = "DELETE FROM social_embed_configs WHERE sub_id=" +
                    std::to_string(sub_id) + " AND event_type='" +
                    escape_string(event_type) + "'";
    if (mysql_query(mysql_, q.c_str()))
      return false;
    {
      std::lock_guard<std::shared_mutex> lock(social_embed_cache_mutex_);
      social_embed_cache_.erase(std::to_string(sub_id) + ":" + event_type);
    }
    return true;
  }

  //  Server Command Aliases CRUD

  std::unordered_map<std::string, std::string>
  get_server_aliases(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(server_alias_cache_mutex_);
    auto it = server_alias_cache_.find(server_id);
    return (it != server_alias_cache_.end())
               ? it->second
               : std::unordered_map<std::string, std::string>{};
  }

  bool set_server_alias(const std::string &server_id, const std::string &alias,
                        const std::string &target) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string q = "INSERT INTO server_command_aliases "
                    "(server_id,alias,target) VALUES ('" +
                    escape_string(server_id) + "','" + escape_string(alias) +
                    "','" + escape_string(target) +
                    "') ON DUPLICATE KEY UPDATE target='" +
                    escape_string(target) + "'";
    if (mysql_query(mysql_, q.c_str())) {
      std::cerr << "[bronx db] set_server_alias failed: " << mysql_error(mysql_)
                << "\n";
      return false;
    }
    std::lock_guard<std::shared_mutex> cache_lock(server_alias_cache_mutex_);
    server_alias_cache_[server_id][alias] = target;
    return true;
  }

  bool delete_server_alias(const std::string &server_id,
                           const std::string &alias) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string q = "DELETE FROM server_command_aliases WHERE server_id='" +
                    escape_string(server_id) + "' AND alias='" +
                    escape_string(alias) + "'";
    if (mysql_query(mysql_, q.c_str()))
      return false;
    std::lock_guard<std::shared_mutex> cache_lock(server_alias_cache_mutex_);
    auto it = server_alias_cache_.find(server_id);
    if (it != server_alias_cache_.end())
      it->second.erase(alias);
    return true;
  }

  //  User Command Aliases CRUD

  std::unordered_map<std::string, std::string>
  get_user_aliases(const std::string &user_id) {
    std::shared_lock<std::shared_mutex> lock(user_alias_cache_mutex_);
    auto it = user_alias_cache_.find(user_id);
    return (it != user_alias_cache_.end())
               ? it->second
               : std::unordered_map<std::string, std::string>{};
  }

  bool set_user_alias(const std::string &user_id, const std::string &alias,
                      const std::string &target) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string q =
        "INSERT INTO user_command_aliases (user_id,alias,target) VALUES ('" +
        escape_string(user_id) + "','" + escape_string(alias) + "','" +
        escape_string(target) + "') ON DUPLICATE KEY UPDATE target='" +
        escape_string(target) + "'";
    if (mysql_query(mysql_, q.c_str())) {
      std::cerr << "[bronx db] set_user_alias failed: " << mysql_error(mysql_)
                << "\n";
      return false;
    }
    std::lock_guard<std::shared_mutex> cache_lock(user_alias_cache_mutex_);
    user_alias_cache_[user_id][alias] = target;
    return true;
  }

  bool delete_user_alias(const std::string &user_id, const std::string &alias) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string q = "DELETE FROM user_command_aliases WHERE user_id='" +
                    escape_string(user_id) + "' AND alias='" +
                    escape_string(alias) + "'";
    if (mysql_query(mysql_, q.c_str()))
      return false;
    std::lock_guard<std::shared_mutex> cache_lock(user_alias_cache_mutex_);
    auto it = user_alias_cache_.find(user_id);
    if (it != user_alias_cache_.end())
      it->second.erase(alias);
    return true;
  }

  //  Server Alias Settings

  bool get_user_aliases_disabled(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(
        server_alias_settings_cache_mutex_);
    auto it = server_alias_settings_cache_.find(server_id);
    return (it != server_alias_settings_cache_.end()) && it->second;
  }

  bool set_user_aliases_disabled(const std::string &server_id, bool disabled) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string q = "INSERT INTO server_alias_settings "
                    "(server_id,user_aliases_disabled) VALUES ('" +
                    escape_string(server_id) + "'," + (disabled ? "1" : "0") +
                    ") ON DUPLICATE KEY UPDATE user_aliases_disabled=" +
                    (disabled ? "1" : "0");
    if (mysql_query(mysql_, q.c_str()))
      return false;
    std::lock_guard<std::shared_mutex> cache_lock(
        server_alias_settings_cache_mutex_);
    server_alias_settings_cache_[server_id] = disabled;
    return true;
  }

  //  Alias resolution (used by on_message pre-processor in main.cpp)
  // Priority: server alias > user alias (unless disabled for this server).
  std::string resolve_alias(const std::string &server_id,
                            const std::string &user_id,
                            const std::string &cmd) {
    {
      std::shared_lock<std::shared_mutex> lock(server_alias_cache_mutex_);
      auto sit = server_alias_cache_.find(server_id);
      if (sit != server_alias_cache_.end()) {
        auto ait = sit->second.find(cmd);
        if (ait != sit->second.end())
          return ait->second;
      }
    }
    if (!get_user_aliases_disabled(server_id)) {
      std::shared_lock<std::shared_mutex> lock(user_alias_cache_mutex_);
      auto uit = user_alias_cache_.find(user_id);
      if (uit != user_alias_cache_.end()) {
        auto ait = uit->second.find(cmd);
        if (ait != uit->second.end())
          return ait->second;
      }
    }
    return "";
  }

  //  Word Filter CRUD

  static std::vector<std::string> parse_json_str_arr(const char *raw) {
    if (!raw)
      return {};
    try {
      auto j = nlohmann::json::parse(raw);
      if (!j.is_array())
        return {};
      std::vector<std::string> out;
      for (const auto &e : j)
        if (e.is_string())
          out.push_back(e.get<std::string>());
      return out;
    } catch (...) {
      return {};
    }
  }

  bool filter_create_group(const std::string &server_id,
                           const std::string &name) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::string q = "INSERT IGNORE INTO word_filter_groups "
                    "(server_id, name, patterns, server_wide, "
                    "enabled_channels, disabled_channels) VALUES ('" +
                    escape_string(server_id) + "','" + escape_string(name) +
                    "','[]',1,'[]','[]')";
    return mysql_query(mysql_, q.c_str()) == 0;
  }

  bool filter_delete_group(const std::string &server_id,
                           const std::string &name) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::string q = "DELETE FROM word_filter_groups WHERE server_id='" +
                    escape_string(server_id) + "' AND name='" +
                    escape_string(name) + "'";
    return mysql_query(mysql_, q.c_str()) == 0 &&
           mysql_affected_rows(mysql_) > 0;
  }

  std::vector<WordFilterGroup>
  filter_list_groups(const std::string &server_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::string q = "SELECT id, name, patterns, server_wide, enabled_channels, "
                    "disabled_channels "
                    "FROM word_filter_groups WHERE server_id='" +
                    escape_string(server_id) + "'";
    std::vector<WordFilterGroup> out;
    if (mysql_query(mysql_, q.c_str()))
      return out;
    MYSQL_RES *res = mysql_store_result(mysql_);
    if (!res)
      return out;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
      WordFilterGroup g;
      g.id = row[0] ? std::atoi(row[0]) : 0;
      g.server_id = server_id;
      g.name = row[1] ? row[1] : "";
      g.patterns = parse_json_str_arr(row[2]);
      g.server_wide = row[3] && std::string(row[3]) != "0";
      g.enabled_channels = parse_json_str_arr(row[4]);
      g.disabled_channels = parse_json_str_arr(row[5]);
      out.push_back(std::move(g));
    }
    mysql_free_result(res);
    return out;
  }

  std::optional<WordFilterGroup> filter_get_group(const std::string &server_id,
                                                  const std::string &name) {
    auto groups = filter_list_groups(server_id);
    for (auto &g : groups)
      if (g.name == name)
        return g;
    return std::nullopt;
  }

  bool filter_add_pattern(const std::string &server_id, const std::string &name,
                          const std::string &pattern) {
    auto grp = filter_get_group(server_id, name);
    if (!grp)
      return false;
    auto &pats = grp->patterns;
    if (std::find(pats.begin(), pats.end(), pattern) != pats.end())
      return true;
    pats.push_back(pattern);
    nlohmann::json j = pats;
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::string q = "UPDATE word_filter_groups SET patterns='" +
                    escape_string(j.dump()) + "' WHERE server_id='" +
                    escape_string(server_id) + "' AND name='" +
                    escape_string(name) + "'";
    return mysql_query(mysql_, q.c_str()) == 0;
  }

  bool filter_remove_pattern(const std::string &server_id,
                             const std::string &name,
                             const std::string &pattern) {
    auto grp = filter_get_group(server_id, name);
    if (!grp)
      return false;
    auto &pats = grp->patterns;
    auto it = std::find(pats.begin(), pats.end(), pattern);
    if (it == pats.end())
      return false;
    pats.erase(it);
    nlohmann::json j = pats;
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::string q = "UPDATE word_filter_groups SET patterns='" +
                    escape_string(j.dump()) + "' WHERE server_id='" +
                    escape_string(server_id) + "' AND name='" +
                    escape_string(name) + "'";
    return mysql_query(mysql_, q.c_str()) == 0;
  }

  // Toggle group server-wide (channel_id empty) or for a specific channel.
  bool filter_toggle(const std::string &server_id, const std::string &name,
                     const std::string &channel_id, bool enable) {
    auto grp = filter_get_group(server_id, name);
    if (!grp)
      return false;
    if (channel_id.empty()) {
      grp->server_wide = enable;
      grp->enabled_channels.clear();
      grp->disabled_channels.clear();
    } else {
      if (enable) {
        auto &en = grp->enabled_channels;
        if (std::find(en.begin(), en.end(), channel_id) == en.end())
          en.push_back(channel_id);
        auto &dis = grp->disabled_channels;
        dis.erase(std::remove(dis.begin(), dis.end(), channel_id), dis.end());
      } else {
        auto &dis = grp->disabled_channels;
        if (std::find(dis.begin(), dis.end(), channel_id) == dis.end())
          dis.push_back(channel_id);
        auto &en = grp->enabled_channels;
        en.erase(std::remove(en.begin(), en.end(), channel_id), en.end());
      }
    }
    nlohmann::json jen = grp->enabled_channels;
    nlohmann::json jdis = grp->disabled_channels;
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::string q = "UPDATE word_filter_groups SET server_wide=" +
                    std::string(grp->server_wide ? "1" : "0") +
                    ", enabled_channels='" + escape_string(jen.dump()) +
                    "', disabled_channels='" + escape_string(jdis.dump()) +
                    "' WHERE server_id='" + escape_string(server_id) +
                    "' AND name='" + escape_string(name) + "'";
    return mysql_query(mysql_, q.c_str()) == 0;
  }

  static bool filter_group_active_in(const WordFilterGroup &g,
                                     const std::string &channel_id) {
    if (std::find(g.disabled_channels.begin(), g.disabled_channels.end(),
                  channel_id) != g.disabled_channels.end())
      return false;
    if (std::find(g.enabled_channels.begin(), g.enabled_channels.end(),
                  channel_id) != g.enabled_channels.end())
      return true;
    return g.server_wide;
  }

  WordFilterWhitelist filter_get_whitelist(const std::string &server_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    WordFilterWhitelist wl;
    wl.server_id = server_id;
    std::string q =
        "SELECT entries FROM word_filter_whitelist WHERE server_id='" +
        escape_string(server_id) + "'";
    if (mysql_query(mysql_, q.c_str()))
      return wl;
    MYSQL_RES *res = mysql_store_result(mysql_);
    if (!res)
      return wl;
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row)
      wl.entries = parse_json_str_arr(row[0]);
    mysql_free_result(res);
    return wl;
  }

  bool filter_whitelist_add(const std::string &server_id,
                            const std::string &entry) {
    auto wl = filter_get_whitelist(server_id);
    if (std::find(wl.entries.begin(), wl.entries.end(), entry) !=
        wl.entries.end())
      return true;
    wl.entries.push_back(entry);
    nlohmann::json j = wl.entries;
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::string q =
        "INSERT INTO word_filter_whitelist (server_id, entries) VALUES ('" +
        escape_string(server_id) + "','" + escape_string(j.dump()) +
        "') ON DUPLICATE KEY UPDATE entries='" + escape_string(j.dump()) + "'";
    return mysql_query(mysql_, q.c_str()) == 0;
  }

  bool filter_whitelist_remove(const std::string &server_id,
                               const std::string &entry) {
    auto wl = filter_get_whitelist(server_id);
    auto it = std::find(wl.entries.begin(), wl.entries.end(), entry);
    if (it == wl.entries.end())
      return false;
    wl.entries.erase(it);
    nlohmann::json j = wl.entries;
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::string q =
        "INSERT INTO word_filter_whitelist (server_id, entries) VALUES ('" +
        escape_string(server_id) + "','" + escape_string(j.dump()) +
        "') ON DUPLICATE KEY UPDATE entries='" + escape_string(j.dump()) + "'";
    return mysql_query(mysql_, q.c_str()) == 0;
  }

  //  Leveling Calculations

  static uint32_t calculate_level_from_xp(uint64_t xp) {
    if (xp == 0)
      return 1;
    uint32_t level = 1;
    while (calculate_xp_for_level(level + 1) <= xp) {
      level++;
      if (level > 10000)
        break; // Safety cap
    }
    return level;
  }

  static uint64_t calculate_xp_for_level(uint32_t level) {
    if (level <= 1)
      return 0;
    return static_cast<uint64_t>(100.0 * std::pow(level - 1, 1.5));
  }

  static uint64_t calculate_xp_for_next_level(uint32_t current_level) {
    return calculate_xp_for_level(current_level + 1);
  }

  //  Server Leveling Config CRUD

  std::optional<GuildLevelingConfig>
  get_guild_leveling_config(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(leveling_config_cache_mutex_);
    auto it = leveling_config_cache_.find(server_id);
    if (it != leveling_config_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool create_guild_leveling_config(const std::string &server_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string q =
        "INSERT IGNORE INTO guild_leveling_config (server_id) VALUES ('" +
        escape_string(server_id) + "')";
    if (mysql_query(mysql_, q.c_str()))
      return false;

    // Reload it into cache
    GuildLevelingConfig cfg;
    cfg.server_id = server_id;
    {
      std::lock_guard<std::shared_mutex> cache_lock(
          leveling_config_cache_mutex_);
      leveling_config_cache_[server_id] = cfg;
    }
    return true;
  }

  bool update_guild_leveling_config(const GuildLevelingConfig &cfg) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string esc_server_id = escape_string(cfg.server_id);
    std::string val_enabled = cfg.enabled ? "1" : "0";
    std::string val_reward_coins = cfg.reward_coins ? "1" : "0";
    std::string val_coins = std::to_string(cfg.coins_per_message);
    std::string val_min_xp = std::to_string(cfg.min_xp_per_message);
    std::string val_max_xp = std::to_string(cfg.max_xp_per_message);
    std::string val_min_chars = std::to_string(cfg.min_message_chars);
    std::string val_cooldown = std::to_string(cfg.xp_cooldown_seconds);
    std::string val_channel =
        cfg.announcement_channel
            ? "'" + escape_string(*cfg.announcement_channel) + "'"
            : "NULL";
    std::string val_announce = cfg.announce_levelup ? "1" : "0";
    std::string val_message =
        "'" + escape_string(cfg.announcement_message) + "'";
    std::string val_sticky = cfg.sticky_roles ? "1" : "0";

    std::string q =
        "INSERT INTO guild_leveling_config (server_id, enabled, reward_coins, "
        "coins_per_message, min_xp_per_message, max_xp_per_message, "
        "min_message_chars, xp_cooldown_seconds, announcement_channel, "
        "announce_levelup, level_up_message, sticky_roles) "
        "VALUES ('" +
        esc_server_id + "', " + val_enabled + ", " + val_reward_coins + ", " +
        val_coins + ", " + val_min_xp + ", " + val_max_xp + ", " +
        val_min_chars + ", " + val_cooldown + ", " + val_channel + ", " +
        val_announce + ", " + val_message + ", " + val_sticky +
        ") "
        "ON DUPLICATE KEY UPDATE enabled=" +
        val_enabled + ", reward_coins=" + val_reward_coins +
        ", coins_per_message=" + val_coins +
        ", min_xp_per_message=" + val_min_xp +
        ", max_xp_per_message=" + val_max_xp +
        ", min_message_chars=" + val_min_chars +
        ", xp_cooldown_seconds=" + val_cooldown +
        ", announcement_channel=" + val_channel +
        ", announce_levelup=" + val_announce +
        ", level_up_message=" + val_message + ", sticky_roles=" + val_sticky;

    if (mysql_query(mysql_, q.c_str())) {
      std::cerr << "[bronx db] update_guild_leveling_config failed: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Update cache
    {
      std::lock_guard<std::shared_mutex> cache_lock(
          leveling_config_cache_mutex_);
      leveling_config_cache_[cfg.server_id] = cfg;
    }
    return true;
  }

  void invalidate_guild_leveling_config_cache(const std::string &server_id) {
    std::lock_guard<std::shared_mutex> cache_lock(leveling_config_cache_mutex_);
    leveling_config_cache_.erase(server_id);
  }

  //  Level Roles CRUD

  std::vector<LevelRole> get_level_roles(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(level_roles_cache_mutex_);
    auto it = level_roles_cache_.find(server_id);
    if (it != level_roles_cache_.end())
      return it->second;
    return {};
  }

  std::optional<LevelRole> get_level_role_at_level(const std::string &server_id,
                                                   uint32_t level) {
    std::shared_lock<std::shared_mutex> lock(level_roles_cache_mutex_);
    auto it = level_roles_cache_.find(server_id);
    if (it != level_roles_cache_.end()) {
      for (const auto &r : it->second) {
        if (r.level == level)
          return r;
      }
    }
    return std::nullopt;
  }

  bool create_level_role(const LevelRole &role) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string esc_server_id = escape_string(role.server_id);
    std::string val_level = std::to_string(role.level);
    std::string esc_role_id = escape_string(role.role_id);
    std::string esc_role_name = escape_string(role.role_name);
    std::string val_remove = role.remove_previous ? "1" : "0";

    std::string q = "INSERT INTO guild_level_roles (server_id, level, role_id, "
                    "role_name, remove_previous) "
                    "VALUES ('" +
                    esc_server_id + "', " + val_level + ", '" + esc_role_id +
                    "', '" + esc_role_name + "', " + val_remove +
                    ") "
                    "ON DUPLICATE KEY UPDATE role_id = '" +
                    esc_role_id + "', role_name = '" + esc_role_name +
                    "', remove_previous = " + val_remove;

    if (mysql_query(mysql_, q.c_str())) {
      std::cerr << "[bronx db] create_level_role failed: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    // Update Cache
    {
      std::lock_guard<std::shared_mutex> cache_lock(level_roles_cache_mutex_);
      auto &list = level_roles_cache_[role.server_id];
      auto it = std::find_if(list.begin(), list.end(), [&](const LevelRole &r) {
        return r.level == role.level;
      });
      if (it != list.end()) {
        *it = role;
      } else {
        list.push_back(role);
      }
      // Keep sorted by level
      std::sort(list.begin(), list.end(),
                [](const LevelRole &a, const LevelRole &b) {
                  return a.level < b.level;
                });
    }
    return true;
  }

  bool delete_level_role(const std::string &server_id, uint32_t level) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string q = "DELETE FROM guild_level_roles WHERE server_id = '" +
                    escape_string(server_id) +
                    "' AND level = " + std::to_string(level);
    if (mysql_query(mysql_, q.c_str()))
      return false;

    // Update Cache
    {
      std::lock_guard<std::shared_mutex> cache_lock(level_roles_cache_mutex_);
      auto &list = level_roles_cache_[server_id];
      list.erase(
          std::remove_if(list.begin(), list.end(),
                         [&](const LevelRole &r) { return r.level == level; }),
          list.end());
    }
    return true;
  }

  //  XP Blacklist CRUD

  std::vector<XPBlacklistEntry> get_xp_blacklist(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(xp_blacklist_cache_mutex_);
    auto it = xp_blacklist_cache_.find(server_id);
    if (it != xp_blacklist_cache_.end())
      return it->second;
    return {};
  }

  bool add_xp_blacklist(const XPBlacklistEntry &entry) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string esc_server_id = escape_string(entry.server_id);
    std::string esc_target_type = escape_string(entry.target_type);
    std::string esc_target_id = escape_string(entry.target_id);
    std::string esc_added_by = escape_string(entry.added_by);

    std::string q = "INSERT IGNORE INTO guild_xp_blacklist (server_id, "
                    "target_type, target_id, added_by) "
                    "VALUES ('" +
                    esc_server_id + "', '" + esc_target_type + "', '" +
                    esc_target_id + "', '" + esc_added_by + "')";
    if (mysql_query(mysql_, q.c_str()))
      return false;

    // Update Cache
    {
      std::lock_guard<std::shared_mutex> cache_lock(xp_blacklist_cache_mutex_);
      auto &list = xp_blacklist_cache_[entry.server_id];
      auto it = std::find_if(list.begin(), list.end(),
                             [&](const XPBlacklistEntry &e) {
                               return e.target_type == entry.target_type &&
                                      e.target_id == entry.target_id;
                             });
      if (it == list.end()) {
        list.push_back(entry);
      }
    }
    return true;
  }

  bool remove_xp_blacklist(const std::string &server_id,
                           const std::string &target_type,
                           const std::string &target_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string q = "DELETE FROM guild_xp_blacklist WHERE server_id = '" +
                    escape_string(server_id) + "' AND target_type = '" +
                    escape_string(target_type) + "' AND target_id = '" +
                    escape_string(target_id) + "'";
    if (mysql_query(mysql_, q.c_str()))
      return false;

    // Update Cache
    {
      std::lock_guard<std::shared_mutex> cache_lock(xp_blacklist_cache_mutex_);
      auto &list = xp_blacklist_cache_[server_id];
      list.erase(std::remove_if(list.begin(), list.end(),
                                [&](const XPBlacklistEntry &e) {
                                  return e.target_type == target_type &&
                                         e.target_id == target_id;
                                }),
                 list.end());
    }
    return true;
  }

  bool is_xp_blocked(const std::string &server_id,
                     const std::string &channel_id, const std::string &user_id,
                     const std::vector<std::string> &role_ids) {
    std::shared_lock<std::shared_mutex> lock(xp_blacklist_cache_mutex_);
    auto it = xp_blacklist_cache_.find(server_id);
    if (it == xp_blacklist_cache_.end())
      return false;

    for (const auto &entry : it->second) {
      if (entry.target_type == "user" && entry.target_id == user_id)
        return true;
      if (entry.target_type == "channel" && entry.target_id == channel_id)
        return true;
      if (entry.target_type == "role") {
        for (const auto &rid : role_ids) {
          if (entry.target_id == rid)
            return true;
        }
      }
    }
    return false;
  }

  //  XP operations

  std::optional<UserXP> get_user_xp(const std::string &user_id,
                                    const std::string &server_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return std::nullopt;

    std::string q =
        "SELECT user_id, server_id, total_xp, level, IFNULL(last_message_xp, "
        "'') FROM user_xp WHERE user_id = '" +
        escape_string(user_id) + "' AND server_id = '" +
        escape_string(server_id) + "'";
    if (mysql_query(mysql_, q.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          UserXP xp;
          xp.user_id = row[0] ? row[0] : "";
          xp.server_id = row[1] ? row[1] : "";
          xp.total_xp = row[2] ? std::stoull(row[2]) : 0;
          xp.level = row[3] ? std::stoul(row[3]) : 1;
          xp.last_message_xp = row[4];
          mysql_free_result(res);
          return xp;
        }
        mysql_free_result(res);
      }
    }
    return std::nullopt;
  }

  bool add_xp(const std::string &user_id, uint64_t xp_amount,
              uint32_t &new_level, bool &leveled_up,
              const std::string &server_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;

    leveled_up = false;
    std::string esc_uid = escape_string(user_id);
    std::string esc_sid = escape_string(server_id);
    std::string val_xp = std::to_string(xp_amount);

    // Upsert the XP row
    std::string q = "INSERT INTO user_xp (user_id, server_id, total_xp, level, "
                    "last_message_xp) "
                    "VALUES ('" +
                    esc_uid + "', '" + esc_sid + "', " + val_xp +
                    ", 1, NOW()) "
                    "ON DUPLICATE KEY UPDATE total_xp = total_xp + " +
                    val_xp + ", last_message_xp = NOW()";
    if (mysql_query(mysql_, q.c_str()))
      return false;

    // Fetch new total_xp to calculate if level changed
    std::string q_fetch =
        "SELECT total_xp, level FROM user_xp WHERE user_id = '" + esc_uid +
        "' AND server_id = '" + esc_sid + "'";
    if (mysql_query(mysql_, q_fetch.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          uint64_t total_xp = row[0] ? std::stoull(row[0]) : 0;
          uint32_t old_level = row[1] ? std::stoul(row[1]) : 1;
          new_level = calculate_level_from_xp(total_xp);
          if (new_level > old_level) {
            leveled_up = true;
            std::string q_upd =
                "UPDATE user_xp SET level = " + std::to_string(new_level) +
                " WHERE user_id = '" + esc_uid + "' AND server_id = '" +
                esc_sid + "'";
            mysql_query(mysql_, q_upd.c_str());
          }
        }
        mysql_free_result(res);
      }
    }
    return true;
  }

  bool reset_guild_xp(const std::string &server_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::string q = "DELETE FROM user_xp WHERE server_id = '" +
                    escape_string(server_id) + "'";
    return mysql_query(mysql_, q.c_str()) == 0;
  }

  bool reset_user_guild_xp(const std::string &user_id,
                           const std::string &server_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::string q = "DELETE FROM user_xp WHERE user_id = '" +
                    escape_string(user_id) + "' AND server_id = '" +
                    escape_string(server_id) + "'";
    return mysql_query(mysql_, q.c_str()) == 0;
  }

  std::vector<UserXP> get_server_xp_leaderboard(const std::string &server_id,
                                                int limit) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<UserXP> leaderboard;
    if (!mysql_)
      return leaderboard;

    std::string q =
        "SELECT user_id, total_xp, level FROM user_xp WHERE server_id = '" +
        escape_string(server_id) + "' ORDER BY total_xp DESC LIMIT " +
        std::to_string(limit);
    if (mysql_query(mysql_, q.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          UserXP xp;
          xp.user_id = row[0] ? row[0] : "";
          xp.server_id = server_id;
          xp.total_xp = row[1] ? std::stoull(row[1]) : 0;
          xp.level = row[2] ? std::stoul(row[2]) : 1;
          leaderboard.push_back(xp);
        }
        mysql_free_result(res);
      }
    }
    return leaderboard;
  }

  int get_user_server_xp_rank(const std::string &user_id,
                              const std::string &server_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return 0;

    std::string q = "SELECT COUNT(*) FROM user_xp WHERE server_id = '" +
                    escape_string(server_id) +
                    "' AND total_xp > (SELECT IFNULL(total_xp, 0) FROM user_xp "
                    "WHERE user_id = '" +
                    escape_string(user_id) + "' AND server_id = '" +
                    escape_string(server_id) + "')";
    if (mysql_query(mysql_, q.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          int count = row[0] ? std::stoi(row[0]) : 0;
          mysql_free_result(res);
          return count + 1; // 1-indexed rank
        }
        mysql_free_result(res);
      }
    }
    return 0;
  }
};

} // namespace bronx
