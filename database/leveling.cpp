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

  std::optional<UserRankCard>
  Database::get_user_rank_card(const std::string &user_id) {
    std::shared_lock<std::shared_mutex> lock(user_rank_card_cache_mutex_);
    auto it = user_rank_card_cache_.find(user_id);
    if (it != user_rank_card_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool Database::save_user_rank_card(const UserRankCard &urc) {
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

  uint32_t Database::calculate_level_from_xp(uint64_t xp) {
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

  uint64_t Database::calculate_xp_for_level(uint32_t level) {
    if (level <= 1)
      return 0;
    return static_cast<uint64_t>(100.0 * std::pow(level - 1, 1.5));
  }

  uint64_t Database::calculate_xp_for_next_level(uint32_t current_level) {
    return calculate_xp_for_level(current_level + 1);
  }

  std::optional<GuildLevelingConfig>
  Database::get_guild_leveling_config(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(leveling_config_cache_mutex_);
    auto it = leveling_config_cache_.find(server_id);
    if (it != leveling_config_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool Database::create_guild_leveling_config(const std::string &server_id) {
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

  bool Database::update_guild_leveling_config(const GuildLevelingConfig &cfg) {
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

  void Database::invalidate_guild_leveling_config_cache(const std::string &server_id) {
    std::lock_guard<std::shared_mutex> cache_lock(leveling_config_cache_mutex_);
    leveling_config_cache_.erase(server_id);
  }

  std::vector<LevelRole> Database::get_level_roles(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(level_roles_cache_mutex_);
    auto it = level_roles_cache_.find(server_id);
    if (it != level_roles_cache_.end())
      return it->second;
    return {};
  }

  std::optional<LevelRole> Database::get_level_role_at_level(const std::string &server_id,
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

  bool Database::create_level_role(const LevelRole &role) {
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

  bool Database::delete_level_role(const std::string &server_id, uint32_t level) {
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

  std::vector<XPBlacklistEntry> Database::get_xp_blacklist(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(xp_blacklist_cache_mutex_);
    auto it = xp_blacklist_cache_.find(server_id);
    if (it != xp_blacklist_cache_.end())
      return it->second;
    return {};
  }

  bool Database::add_xp_blacklist(const XPBlacklistEntry &entry) {
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

  bool Database::remove_xp_blacklist(const std::string &server_id,
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

  bool Database::is_xp_blocked(const std::string &server_id,
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

  std::optional<UserXP> Database::get_user_xp(const std::string &user_id,
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

  bool Database::add_xp(const std::string &user_id, uint64_t xp_amount,
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

  bool Database::reset_guild_xp(const std::string &server_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::string q = "DELETE FROM user_xp WHERE server_id = '" +
                    escape_string(server_id) + "'";
    return mysql_query(mysql_, q.c_str()) == 0;
  }

  bool Database::reset_user_guild_xp(const std::string &user_id,
                           const std::string &server_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::string q = "DELETE FROM user_xp WHERE user_id = '" +
                    escape_string(user_id) + "' AND server_id = '" +
                    escape_string(server_id) + "'";
    return mysql_query(mysql_, q.c_str()) == 0;
  }

  std::vector<UserXP> Database::get_server_xp_leaderboard(const std::string &server_id,
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

  int Database::get_user_server_xp_rank(const std::string &user_id,
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

} // namespace bronx
