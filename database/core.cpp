#include "database.h"
#include <mysql.h>
#include <algorithm>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <shared_mutex>
#include <string>
#include <vector>

namespace bronx {

  bool Database::init(const std::string &host, const std::string &user,
                      const std::string &password, const std::string &dbname,
                      unsigned int port) {
    std::lock_guard<std::mutex> lock(db_mutex_);

    mysql_ = mysql_init(nullptr);
    if (!mysql_) {
      std::cerr << "[bronx db] mysql_init failed\n";
      return false;
    }

    // Set connection timeout and reconnect options
    unsigned int timeout = 10;
    bool reconnect = true;
    mysql_options(mysql_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(mysql_, MYSQL_OPT_RECONNECT, &reconnect);

    if (!mysql_real_connect(mysql_, host.c_str(), user.c_str(),
                            password.c_str(), nullptr, port, nullptr, 0)) {
      if (host == "127.0.0.1" || host == "localhost") {
        std::cout << "[bronx db] connection to " << host << " failed (" << mysql_error(mysql_)
                  << "), trying host.containers.internal...\n";
        mysql_close(mysql_);
        mysql_ = mysql_init(nullptr);
        mysql_options(mysql_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
        mysql_options(mysql_, MYSQL_OPT_RECONNECT, &reconnect);
        if (mysql_real_connect(mysql_, "host.containers.internal", user.c_str(),
                               password.c_str(), nullptr, port, nullptr, 0)) {
          goto connected;
        }
      }
      std::cerr << "[bronx db] connection failed: " << mysql_error(mysql_)
                << "\n";
      return false;
    }

connected:

    // Ensure database exists with utf8mb4
    std::string create_db_sql = "CREATE DATABASE IF NOT EXISTS `" + dbname +
                                "` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci";
    if (mysql_query(mysql_, create_db_sql.c_str())) {
      std::cerr << "[bronx db] failed to create database: " << mysql_error(mysql_)
                << "\n";
      return false;
    }

    // Select database
    if (mysql_select_db(mysql_, dbname.c_str())) {
      std::cerr << "[bronx db] failed to select database: " << mysql_error(mysql_)
                << "\n";
      return false;
    }

    // Set connection character set to utf8mb4
    mysql_set_character_set(mysql_, "utf8mb4");

    // 1. Initialize DB tables and migrations
    if (!init_tables()) {
      std::cerr << "[bronx db] init_tables failed\n";
      return false;
    }

    // 2. Populate in-memory caches
    if (!init_cache()) {
      std::cerr << "[bronx db] init_cache failed\n";
      return false;
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
              << "           loaded " << arct_cache_.size() << " autoreacts.\n"
              << "           loaded " << ccmd_cache_.size() << " custom commands.\n";
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
        "user_afk",          "custom_commands",
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
