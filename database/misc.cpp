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

  std::optional<MasqueradeConfig>
  Database::get_masquerade_config(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(masquerade_cache_mutex_);
    auto it = masquerade_cache_.find(server_id);
    if (it != masquerade_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool Database::save_masquerade_config(const MasqueradeConfig &cfg) {
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

  bool Database::delete_masquerade_config(const std::string &server_id) {
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
  Database::get_user_masquerade(const std::string &user_id) {
    std::shared_lock<std::shared_mutex> lock(user_masq_cache_mutex_);
    auto it = user_masq_cache_.find(user_id);
    if (it != user_masq_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool Database::save_user_masquerade(const UserMasquerade &um) {
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

  std::optional<std::string>
  Database::get_user_timezone(const std::string &user_id) {
    std::shared_lock<std::shared_mutex> lock(user_timezones_cache_mutex_);
    auto it = user_timezones_cache_.find(user_id);
    if (it != user_timezones_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool Database::save_user_timezone(const std::string &user_id, const std::string &timezone) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_uid = escape_string(user_id);
    std::string esc_tz = escape_string(timezone);

    std::string query =
        "INSERT INTO user_timezones (user_id, timezone) "
        "VALUES ('" + esc_uid + "', '" + esc_tz + "') "
        "ON DUPLICATE KEY UPDATE timezone = '" + esc_tz + "'";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save user_timezone: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    {
      std::lock_guard<std::shared_mutex> cache_lock(user_timezones_cache_mutex_);
      user_timezones_cache_[user_id] = timezone;
    }
    return true;
  }

  bool Database::is_user_ignored(const std::string &user_id) {
    std::shared_lock<std::shared_mutex> lock(ignored_users_cache_mutex_);
    return ignored_users_cache_.find(user_id) != ignored_users_cache_.end();
  }

  bool Database::toggle_user_ignored(const std::string &user_id, bool &is_now_ignored) {
    bool ignored = is_user_ignored(user_id);

    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string esc_uid = escape_string(user_id);

    std::string query;
    if (ignored) {
      query = "DELETE FROM bot_ignored_users WHERE user_id = '" + esc_uid + "'";
    } else {
      query = "INSERT INTO bot_ignored_users (user_id) VALUES ('" + esc_uid + "')";
    }

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to toggle user ignored: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    {
      std::lock_guard<std::shared_mutex> cache_lock(ignored_users_cache_mutex_);
      if (ignored) {
        ignored_users_cache_.erase(user_id);
        is_now_ignored = false;
      } else {
        ignored_users_cache_.insert(user_id);
        is_now_ignored = true;
      }
    }
    return true;
  }

  bool Database::toggle_dashboard_allowed_user(const std::string &server_id, const std::string &user_id, bool &is_allowed) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_ || server_id.empty() || user_id.empty())
      return false;

    std::string esc_sid = escape_string(server_id);
    std::string esc_uid = escape_string(user_id);

    bool exists = false;
    std::string sel_query = "SELECT 1 FROM dashboard_allowed_users WHERE server_id = '" + esc_sid + "' AND user_id = '" + esc_uid + "'";
    if (mysql_query(mysql_, sel_query.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        if (mysql_num_rows(res) > 0) {
          exists = true;
        }
        mysql_free_result(res);
      }
    }

    std::string query;
    if (exists) {
      query = "DELETE FROM dashboard_allowed_users WHERE server_id = '" + esc_sid + "' AND user_id = '" + esc_uid + "'";
      is_allowed = false;
    } else {
      query = "INSERT INTO dashboard_allowed_users (server_id, user_id) VALUES ('" + esc_sid + "', '" + esc_uid + "')";
      is_allowed = true;
    }

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to toggle dashboard allowed user: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    return true;
  }

  bool Database::delete_user_masquerade(const std::string &user_id) {
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

  std::vector<Tupper> Database::get_user_tuppers(const std::string &user_id) {
    std::shared_lock<std::shared_mutex> lock(tupper_cache_mutex_);
    auto it = tupper_cache_.find(user_id);
    if (it != tupper_cache_.end()) {
      return it->second;
    }
    return {};
  }

  bool Database::add_tupper(const std::string &user_id, const std::string &name,
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

  bool Database::remove_tupper(const std::string &user_id, const std::string &name) {
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

  bool Database::update_tupper_avatar(const std::string &user_id, const std::string &name,
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

  bool Database::update_tupper_brackets(const std::string &user_id,
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

  bool Database::rename_tupper(const std::string &user_id, const std::string &old_name,
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

  bool Database::add_tupper_proxy(const std::string &message_id,
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

  std::optional<TupperProxy> Database::get_tupper_proxy(const std::string &message_id) {
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

  bool Database::delete_tupper_proxy(const std::string &message_id) {
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

  std::optional<CountingConfig>
  Database::get_counting_config(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lk(counting_cache_mutex_);
    auto it = counting_cache_.find(server_id);
    if (it != counting_cache_.end())
      return it->second;
    return std::nullopt;
  }

  bool Database::save_counting_config(const CountingConfig &cfg) {
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

  void Database::counting_increment_correct(const std::string &server_id,
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

  void Database::counting_increment_failed(const std::string &server_id,
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

  std::vector<CountingStats>
  Database::get_counting_leaderboard(const std::string &server_id, int limit) {
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

  CountingStats Database::get_counting_stats(const std::string &server_id,
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

  CountingHighlights Database::get_counting_highlights(const std::string &server_id) {
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

  std::optional<UserAFK> Database::get_user_afk(const std::string &user_id) {
    std::shared_lock<std::shared_mutex> lock(user_afk_cache_mutex_);
    auto it = user_afk_cache_.find(user_id);
    if (it != user_afk_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool Database::save_user_afk(const UserAFK &afk) {
    bool success;
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

  std::string Database::get_user_afk_preset(const std::string &user_id,
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

  bool Database::save_user_afk_preset(const std::string &user_id,
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

  bool Database::delete_user_afk_preset(const std::string &user_id,
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
  Database::get_user_afk_presets_list(const std::string &user_id) {
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

  std::optional<RobloxLink> Database::get_roblox_link(const std::string &user_id) {
    std::shared_lock<std::shared_mutex> lock(roblox_links_cache_mutex_);
    auto it = roblox_links_cache_.find(user_id);
    if (it != roblox_links_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  std::optional<RobloxLink>
  Database::get_roblox_link_by_roblox_username(const std::string &username) {
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

  std::optional<RobloxLink> Database::get_roblox_link_by_roblox_id(uint64_t roblox_id) {
    std::shared_lock<std::shared_mutex> lock(roblox_links_cache_mutex_);
    for (const auto &[_, link] : roblox_links_cache_) {
      if (link.roblox_id == roblox_id) {
        return link;
      }
    }
    return std::nullopt;
  }

  bool Database::save_roblox_link(const std::string &user_id, uint64_t roblox_id,
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

  bool Database::delete_roblox_link(const std::string &user_id) {
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

  bool Database::save_account_link(const std::string &stoat_id,
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

  std::optional<AccountLink> Database::get_account_link(const std::string &platform,
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

  std::unordered_map<std::string, std::string>
  Database::get_server_aliases(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(server_alias_cache_mutex_);
    auto it = server_alias_cache_.find(server_id);
    return (it != server_alias_cache_.end())
               ? it->second
               : std::unordered_map<std::string, std::string>{};
  }

  bool Database::set_server_alias(const std::string &server_id, const std::string &alias,
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

  bool Database::delete_server_alias(const std::string &server_id,
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

  std::unordered_map<std::string, std::string>
  Database::get_user_aliases(const std::string &user_id) {
    std::shared_lock<std::shared_mutex> lock(user_alias_cache_mutex_);
    auto it = user_alias_cache_.find(user_id);
    return (it != user_alias_cache_.end())
               ? it->second
               : std::unordered_map<std::string, std::string>{};
  }

  bool Database::set_user_alias(const std::string &user_id, const std::string &alias,
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

  bool Database::delete_user_alias(const std::string &user_id, const std::string &alias) {
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

  bool Database::get_user_aliases_disabled(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(
        server_alias_settings_cache_mutex_);
    auto it = server_alias_settings_cache_.find(server_id);
    return (it != server_alias_settings_cache_.end()) && it->second;
  }

  bool Database::set_user_aliases_disabled(const std::string &server_id, bool disabled) {
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

  std::string Database::resolve_alias(const std::string &server_id,
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

  std::vector<std::string> Database::parse_json_str_arr(const char *raw) {
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

  bool Database::filter_create_group(const std::string &server_id,
                           const std::string &name) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::string q = "INSERT IGNORE INTO word_filter_groups "
                    "(server_id, name, patterns, server_wide, "
                    "enabled_channels, disabled_channels) VALUES ('" +
                    escape_string(server_id) + "','" + escape_string(name) +
                    "','[]',1,'[]','[]')";
    return mysql_query(mysql_, q.c_str()) == 0;
  }

  bool Database::filter_delete_group(const std::string &server_id,
                           const std::string &name) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::string q = "DELETE FROM word_filter_groups WHERE server_id='" +
                    escape_string(server_id) + "' AND name='" +
                    escape_string(name) + "'";
    return mysql_query(mysql_, q.c_str()) == 0 &&
           mysql_affected_rows(mysql_) > 0;
  }

  std::vector<WordFilterGroup>
  Database::filter_list_groups(const std::string &server_id) {
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

  std::optional<WordFilterGroup> Database::filter_get_group(const std::string &server_id,
                                                  const std::string &name) {
    auto groups = filter_list_groups(server_id);
    for (auto &g : groups)
      if (g.name == name)
        return g;
    return std::nullopt;
  }

  bool Database::filter_add_pattern(const std::string &server_id, const std::string &name,
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

  bool Database::filter_remove_pattern(const std::string &server_id,
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

  bool Database::filter_toggle(const std::string &server_id, const std::string &name,
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

  bool Database::filter_group_active_in(const WordFilterGroup &g,
                                     const std::string &channel_id) {
    if (std::find(g.disabled_channels.begin(), g.disabled_channels.end(),
                  channel_id) != g.disabled_channels.end())
      return false;
    if (std::find(g.enabled_channels.begin(), g.enabled_channels.end(),
                  channel_id) != g.enabled_channels.end())
      return true;
    return g.server_wide;
  }

  WordFilterWhitelist Database::filter_get_whitelist(const std::string &server_id) {
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

  bool Database::filter_whitelist_add(const std::string &server_id,
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

  bool Database::filter_whitelist_remove(const std::string &server_id,
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

  bool Database::create_starboard(const std::string &server_id, const std::string &board_name, const std::string &channel_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string q = "INSERT INTO starboards (server_id, board_name, channel_id, threshold, emojis, allow_self, use_embed, message_content, embed_title, embed_description, delete_below_threshold) "
                    "VALUES ('" + escape_string(server_id) + "', '" + escape_string(board_name) + "', '" + escape_string(channel_id) + "', 3, '⭐', 1, 1, "
                    "'{emoji} **{count}** | {channel} (ID: {message_id})', '', '{content}\\n\\n[Jump to message]({jump_url})', 1) "
                    "ON DUPLICATE KEY UPDATE channel_id='" + escape_string(channel_id) + "'";
    if (mysql_query(mysql_, q.c_str())) {
      std::cerr << "[bronx db] create_starboard failed: " << mysql_error(mysql_) << "\n";
      return false;
    }
    std::lock_guard<std::shared_mutex> cache_lock(starboard_configs_cache_mutex_);
    auto& cfg = starboard_configs_cache_[server_id][board_name];
    cfg.server_id = server_id;
    cfg.board_name = board_name;
    cfg.channel_id = channel_id;
    cfg.threshold = 3;
    cfg.emojis = "⭐";
    cfg.allow_self = true;
    cfg.delete_below_threshold = true;
    cfg.use_embed = true;
    cfg.message_content = "{emoji} **{count}** | {channel} (ID: {message_id})";
    cfg.embed_title = "";
    cfg.embed_description = "{content}\n\n[Jump to message]({jump_url})";
    return true;
  }

  bool Database::delete_starboard(const std::string &server_id, const std::string &board_name) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string q = "DELETE FROM starboards WHERE server_id='" + escape_string(server_id) + "' AND board_name='" + escape_string(board_name) + "'";
    mysql_query(mysql_, q.c_str());
    
    std::string q2 = "DELETE FROM starboard_messages WHERE server_id='" + escape_string(server_id) + "' AND board_name='" + escape_string(board_name) + "'";
    mysql_query(mysql_, q2.c_str());

    std::lock_guard<std::shared_mutex> cache_lock(starboard_configs_cache_mutex_);
    starboard_configs_cache_[server_id].erase(board_name);
    
    std::lock_guard<std::shared_mutex> msg_cache_lock(starboard_messages_cache_mutex_);
    starboard_messages_cache_[server_id].erase(board_name);
    return true;
  }

  bool Database::set_starboard_threshold(const std::string &server_id, const std::string &board_name, int threshold) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string q = "UPDATE starboards SET threshold=" + std::to_string(threshold) + " WHERE server_id='" + escape_string(server_id) + "' AND board_name='" + escape_string(board_name) + "'";
    if (mysql_query(mysql_, q.c_str())) return false;
    
    std::lock_guard<std::shared_mutex> cache_lock(starboard_configs_cache_mutex_);
    starboard_configs_cache_[server_id][board_name].threshold = threshold;
    return true;
  }

  bool Database::set_starboard_channel(const std::string &server_id, const std::string &board_name, const std::string &channel_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string q = "UPDATE starboards SET channel_id='" + escape_string(channel_id) + "' WHERE server_id='" + escape_string(server_id) + "' AND board_name='" + escape_string(board_name) + "'";
    if (mysql_query(mysql_, q.c_str())) return false;
    
    std::lock_guard<std::shared_mutex> cache_lock(starboard_configs_cache_mutex_);
    starboard_configs_cache_[server_id][board_name].channel_id = channel_id;
    return true;
  }

  bool Database::set_starboard_emojis(const std::string &server_id, const std::string &board_name, const std::string &emojis) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string q = "UPDATE starboards SET emojis='" + escape_string(emojis) + "' WHERE server_id='" + escape_string(server_id) + "' AND board_name='" + escape_string(board_name) + "'";
    if (mysql_query(mysql_, q.c_str())) return false;
    
    std::lock_guard<std::shared_mutex> cache_lock(starboard_configs_cache_mutex_);
    starboard_configs_cache_[server_id][board_name].emojis = emojis;
    return true;
  }

  bool Database::set_starboard_self(const std::string &server_id, const std::string &board_name, bool allow_self) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string val = allow_self ? "1" : "0";
    std::string q = "UPDATE starboards SET allow_self=" + val + " WHERE server_id='" + escape_string(server_id) + "' AND board_name='" + escape_string(board_name) + "'";
    if (mysql_query(mysql_, q.c_str())) return false;
    
    std::lock_guard<std::shared_mutex> cache_lock(starboard_configs_cache_mutex_);
    starboard_configs_cache_[server_id][board_name].allow_self = allow_self;
    return true;
  }

  bool Database::set_starboard_delete_below_threshold(const std::string &server_id, const std::string &board_name, bool enabled) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string val = enabled ? "1" : "0";
    std::string q = "UPDATE starboards SET delete_below_threshold=" + val + " WHERE server_id='" + escape_string(server_id) + "' AND board_name='" + escape_string(board_name) + "'";
    if (mysql_query(mysql_, q.c_str())) return false;

    std::lock_guard<std::shared_mutex> cache_lock(starboard_configs_cache_mutex_);
    starboard_configs_cache_[server_id][board_name].delete_below_threshold = enabled;
    return true;
  }

  bool Database::set_starboard_message_config(const std::string &server_id, const std::string &board_name, const std::string &field, const std::string &value) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string col;
    if (field == "content") col = "message_content";
    else if (field == "title") col = "embed_title";
    else if (field == "description") col = "embed_description";
    else return false;

    std::string q = "UPDATE starboards SET " + col + "='" + escape_string(value) + "' WHERE server_id='" + escape_string(server_id) + "' AND board_name='" + escape_string(board_name) + "'";
    if (mysql_query(mysql_, q.c_str())) return false;

    std::lock_guard<std::shared_mutex> cache_lock(starboard_configs_cache_mutex_);
    if (field == "content") starboard_configs_cache_[server_id][board_name].message_content = value;
    else if (field == "title") starboard_configs_cache_[server_id][board_name].embed_title = value;
    else if (field == "description") starboard_configs_cache_[server_id][board_name].embed_description = value;
    return true;
  }

  bool Database::set_starboard_embed_enabled(const std::string &server_id, const std::string &board_name, bool enabled) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string val = enabled ? "1" : "0";
    std::string q = "UPDATE starboards SET use_embed=" + val + " WHERE server_id='" + escape_string(server_id) + "' AND board_name='" + escape_string(board_name) + "'";
    if (mysql_query(mysql_, q.c_str())) return false;

    std::lock_guard<std::shared_mutex> cache_lock(starboard_configs_cache_mutex_);
    starboard_configs_cache_[server_id][board_name].use_embed = enabled;
    return true;
  }

  std::vector<StarboardConfig> Database::get_starboards(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(starboard_configs_cache_mutex_);
    std::vector<StarboardConfig> res;
    auto it = starboard_configs_cache_.find(server_id);
    if (it != starboard_configs_cache_.end()) {
      for (const auto &[name, cfg] : it->second) {
        res.push_back(cfg);
      }
    }
    return res;
  }

  std::optional<StarboardConfig> Database::get_starboard(const std::string &server_id, const std::string &board_name) {
    std::shared_lock<std::shared_mutex> lock(starboard_configs_cache_mutex_);
    auto it = starboard_configs_cache_.find(server_id);
    if (it != starboard_configs_cache_.end()) {
      auto bit = it->second.find(board_name);
      if (bit != it->second.end()) {
        return bit->second;
      }
    }
    return std::nullopt;
  }

  bool Database::add_starboard_message(const std::string &server_id, const std::string &board_name, const std::string &orig_msg_id, const std::string &star_msg_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string q = "INSERT INTO starboard_messages (server_id, board_name, original_message_id, starboard_message_id) VALUES ('" +
                    escape_string(server_id) + "','" + escape_string(board_name) + "','" + escape_string(orig_msg_id) + "','" + escape_string(star_msg_id) + "') "
                    "ON DUPLICATE KEY UPDATE starboard_message_id='" + escape_string(star_msg_id) + "'";
    if (mysql_query(mysql_, q.c_str())) return false;
    
    std::lock_guard<std::shared_mutex> cache_lock(starboard_messages_cache_mutex_);
    starboard_messages_cache_[server_id][board_name][orig_msg_id] = star_msg_id;
    return true;
  }

  std::string Database::get_starboard_message(const std::string &server_id, const std::string &board_name, const std::string &orig_msg_id) {
    std::shared_lock<std::shared_mutex> lock(starboard_messages_cache_mutex_);
    auto it = starboard_messages_cache_.find(server_id);
    if (it != starboard_messages_cache_.end()) {
      auto bit = it->second.find(board_name);
      if (bit != it->second.end()) {
        auto mit = bit->second.find(orig_msg_id);
        if (mit != bit->second.end()) {
          return mit->second;
        }
      }
    }
    return "";
  }

  bool Database::delete_starboard_message(const std::string &server_id, const std::string &board_name, const std::string &orig_msg_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::string q = "DELETE FROM starboard_messages WHERE server_id='" + escape_string(server_id) + "' AND board_name='" + escape_string(board_name) + "' AND original_message_id='" + escape_string(orig_msg_id) + "'";
    if (mysql_query(mysql_, q.c_str())) return false;
    
    std::lock_guard<std::shared_mutex> cache_lock(starboard_messages_cache_mutex_);
    auto it = starboard_messages_cache_.find(server_id);
    if (it != starboard_messages_cache_.end()) {
      auto bit = it->second.find(board_name);
      if (bit != it->second.end()) {
        bit->second.erase(orig_msg_id);
      }
    }
    return true;
  }

  std::optional<LastfmLink> Database::get_lastfm_link(const std::string &user_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return std::nullopt;

    std::string esc_user_id = escape_string(user_id);
    std::string query = "SELECT lastfm_username FROM lastfm_links WHERE user_id = '" + esc_user_id + "'";
    if (mysql_query(mysql_, query.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          LastfmLink link;
          link.user_id = user_id;
          link.lastfm_username = row[0] ? row[0] : "";
          mysql_free_result(res);
          return link;
        }
        mysql_free_result(res);
      }
    }
    return std::nullopt;
  }

  bool Database::save_lastfm_link(const std::string &user_id,
                        const std::string &lastfm_username) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string esc_user_id = escape_string(user_id);
    std::string esc_username = escape_string(lastfm_username);

    std::string query =
        "INSERT INTO lastfm_links (user_id, lastfm_username) "
        "VALUES ('" + esc_user_id + "', '" + esc_username + "') "
        "ON DUPLICATE KEY UPDATE lastfm_username = '" + esc_username + "'";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save lastfm link: "
                << mysql_error(mysql_) << "\n";
      return false;
    }
    return true;
  }

  bool Database::delete_lastfm_link(const std::string &user_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string esc_user_id = escape_string(user_id);
    std::string query = "DELETE FROM lastfm_links WHERE user_id = '" + esc_user_id + "'";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to delete lastfm link: "
                << mysql_error(mysql_) << "\n";
      return false;
    }
    return true;
  }

  std::vector<LastfmLink> Database::get_all_lastfm_links() {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::vector<LastfmLink> out;
    if (!mysql_)
      return out;

    std::string query = "SELECT user_id, lastfm_username FROM lastfm_links";
    if (mysql_query(mysql_, query.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          LastfmLink link;
          link.user_id = row[0] ? row[0] : "";
          link.lastfm_username = row[1] ? row[1] : "";
          out.push_back(std::move(link));
        }
        mysql_free_result(res);
      }
    }
    return out;
  }

  bool Database::reload_prefixes(const std::string &server_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_) return false;
    std::string q = "SELECT prefixes, default_enabled FROM prefixes WHERE target_type = 'server' AND target_id = '" + escape_string(server_id) + "'";
    if (mysql_query(mysql_, q.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        std::lock_guard<std::shared_mutex> pfx_lock(cache_mutex_);
        std::lock_guard<std::shared_mutex> def_pfx_lock(pfx_default_cache_mutex_);
        if (row) {
          std::vector<std::string> pfxs;
          std::string raw_pfx = row[0] ? row[0] : "[\".\"]";
          try {
            auto parsed = nlohmann::json::parse(raw_pfx);
            if (parsed.is_array()) {
              for (const auto &p : parsed) pfxs.push_back(p.get<std::string>());
            }
          } catch (...) {}
          cache_["server:" + server_id] = pfxs;
          pfx_default_cache_[server_id] = row[1] && std::string(row[1]) == "1";
        } else {
          cache_.erase("server:" + server_id);
          pfx_default_cache_.erase(server_id);
        }
        mysql_free_result(res);
      }
    }
    return true;
  }

  bool Database::reload_autoresponders(const std::string &server_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_) return false;
    std::string q = "SELECT id, name, keyword, match_type, message_type, content, embed_json, allow_roles, allow_users, allow_channels, ignore_roles, ignore_users, ignore_channels FROM autoresponders WHERE server_id = '" + escape_string(server_id) + "'";
    if (mysql_query(mysql_, q.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        std::lock_guard<std::shared_mutex> cache_lock(arsp_cache_mutex_);
        auto &idx = arsp_server_index_[server_id];
        for (int id : idx) {
          arsp_cache_.erase(id);
        }
        idx.clear();

        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          AutoresponderConfig c;
          c.id = row[0] ? std::stoi(row[0]) : 0;
          c.server_id = server_id;
          c.name = row[1] ? row[1] : "";
          c.keyword = row[2] ? row[2] : "";
          c.match_type = row[3] ? row[3] : "contains";
          c.message_type = row[4] ? row[4] : "text";
          c.content = row[5] ? row[5] : "";
          c.embed_json = row[6] ? row[6] : "";
          
          arsp_cache_[c.id] = c;
          idx.push_back(c.id);
        }
        mysql_free_result(res);
      }
    }
    return true;
  }

  bool Database::reload_autoreacts(const std::string &server_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_) return false;
    std::string q = "SELECT id, name, keyword, match_type, emojis FROM autoreacts WHERE server_id = '" + escape_string(server_id) + "'";
    if (mysql_query(mysql_, q.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        std::lock_guard<std::shared_mutex> cache_lock(arct_cache_mutex_);
        auto &idx = arct_server_index_[server_id];
        for (int id : idx) {
          arct_cache_.erase(id);
        }
        idx.clear();

        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          AutoreactConfig c;
          c.id = row[0] ? std::stoi(row[0]) : 0;
          c.server_id = server_id;
          c.name = row[1] ? row[1] : "";
          c.keyword = row[2] ? row[2] : "";
          c.match_type = row[3] ? row[3] : "contains";
          c.emojis = row[4] ? row[4] : "";
          
          arct_cache_[c.id] = c;
          idx.push_back(c.id);
        }
        mysql_free_result(res);
      }
    }
    return true;
  }

} // namespace bronx
