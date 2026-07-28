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

  bool Database::recurring_channel_occupied(const std::string &channel_id) {
    std::shared_lock<std::shared_mutex> lock(rm_cache_mutex_);
    return rm_channel_index_.count(channel_id) > 0;
  }

  int Database::count_recurring_for_server(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(rm_cache_mutex_);
    auto it = rm_server_index_.find(server_id);
    if (it != rm_server_index_.end())
      return static_cast<int>(it->second.size());
    return 0;
  }

  std::vector<RecurringMessage>
  Database::get_recurring_messages_for_server(const std::string &server_id) {
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

  std::optional<RecurringMessage>
  Database::get_recurring_by_channel(const std::string &channel_id) {
    std::shared_lock<std::shared_mutex> lock(rm_cache_mutex_);
    auto it = rm_channel_index_.find(channel_id);
    if (it != rm_channel_index_.end()) {
      auto it2 = rm_cache_.find(it->second);
      if (it2 != rm_cache_.end())
        return it2->second;
    }
    return std::nullopt;
  }

  std::optional<RecurringMessage>
  Database::get_recurring_by_id(int id, const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(rm_cache_mutex_);
    auto it = rm_cache_.find(id);
    if (it != rm_cache_.end() && it->second.server_id == server_id)
      return it->second;
    return std::nullopt;
  }

  std::optional<RecurringMessage>
  Database::get_recurring_by_name(const std::string &server_id, const std::string &name) {
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

  std::vector<RecurringMessage> Database::get_due_recurring_messages(uint64_t now) {
    std::shared_lock<std::shared_mutex> lock(rm_cache_mutex_);
    std::vector<RecurringMessage> out;
    for (const auto &[id, rm] : rm_cache_) {
      if (rm.last_sent + rm.interval_seconds <= now) {
        out.push_back(rm);
      }
    }
    return out;
  }

  bool Database::add_recurring_message(RecurringMessage &rm) {
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

  bool Database::remove_recurring_message(int id, const std::string &server_id) {
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

  bool Database::update_recurring_message(const RecurringMessage &rm) {
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

  bool Database::update_recurring_last_sent(int id, uint64_t ts) {
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

  std::optional<RssFeed> Database::get_rss_by_id(int id, const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(rss_cache_mutex_);
    auto it = rss_cache_.find(id);
    if (it != rss_cache_.end() && it->second.server_id == server_id) {
      return it->second;
    }
    return std::nullopt;
  }

  std::optional<RssFeed> Database::get_rss_by_url(const std::string &server_id,
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

  std::vector<RssFeed> Database::get_rss_feeds_for_server(const std::string &server_id) {
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

  std::vector<RssFeed> Database::get_all_rss_feeds() {
    std::shared_lock<std::shared_mutex> lock(rss_cache_mutex_);
    std::vector<RssFeed> out;
    out.reserve(rss_cache_.size());
    for (const auto &[id, feed] : rss_cache_) {
      out.push_back(feed);
    }
    return out;
  }

  bool Database::add_rss_feed(RssFeed &feed) {
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

  bool Database::remove_rss_feed(int id, const std::string &server_id) {
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

  bool Database::update_rss_feed_channels(int id, const std::string &server_id,
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

  bool Database::update_rss_feed_check(int id, uint64_t last_checked,
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

  bool Database::update_rss_feed_interval(int id, int interval) {
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

  std::string Database::json_arr_str(const std::vector<std::string> &v) {
    nlohmann::json j = v;
    return j.dump();
  }

  std::vector<AutoresponderConfig>
  Database::get_autoresponders_for_server(const std::string &server_id) {
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
  Database::get_autoresponder_by_id(int id, const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(arsp_cache_mutex_);
    auto it = arsp_cache_.find(id);
    if (it != arsp_cache_.end() && it->second.server_id == server_id)
      return it->second;
    return std::nullopt;
  }

  std::optional<AutoresponderConfig>
  Database::get_autoresponder_by_name(const std::string &server_id,
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

  bool Database::add_autoresponder(AutoresponderConfig &c) {
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

  bool Database::remove_autoresponder(int id, const std::string &server_id) {
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

  bool Database::update_autoresponder(const AutoresponderConfig &c) {
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

  std::vector<AutoreactConfig>
  Database::get_autoreacts_for_server(const std::string &server_id) {
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
  Database::get_autoreact_by_id(int id, const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(arct_cache_mutex_);
    auto it = arct_cache_.find(id);
    if (it != arct_cache_.end() && it->second.server_id == server_id)
      return it->second;
    return std::nullopt;
  }

  std::optional<AutoreactConfig>
  Database::get_autoreact_by_name(const std::string &server_id, const std::string &name) {
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

  bool Database::add_autoreact(AutoreactConfig &c) {
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

  bool Database::remove_autoreact(int id, const std::string &server_id) {
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

  bool Database::update_autoreact(const AutoreactConfig &c) {
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

  bool Database::update_autoresponder_filter(int id, const std::string &server_id,
                                   const AutoScopeFilter &f) {
    auto opt = get_autoresponder_by_id(id, server_id);
    if (!opt)
      return false;
    opt->filter = f;
    return update_autoresponder(*opt);
  }

  bool Database::update_autoreact_filter(int id, const std::string &server_id,
                               const AutoScopeFilter &f) {
    auto opt = get_autoreact_by_id(id, server_id);
    if (!opt)
      return false;
    opt->filter = f;
    return update_autoreact(*opt);
  }

  std::vector<SocialSubscription> Database::get_all_social_subs() {
    std::shared_lock<std::shared_mutex> lock(social_sub_cache_mutex_);
    std::vector<SocialSubscription> out;
    out.reserve(social_sub_cache_.size());
    for (const auto &[id, sub] : social_sub_cache_)
      out.push_back(sub);
    return out;
  }

  std::vector<SocialSubscription>
  Database::get_social_subs_for_server(const std::string &server_id) {
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
  Database::get_social_sub_by_id(int id, const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(social_sub_cache_mutex_);
    auto it = social_sub_cache_.find(id);
    if (it != social_sub_cache_.end() && it->second.server_id == server_id)
      return it->second;
    return std::nullopt;
  }

  bool Database::add_social_sub(SocialSubscription &sub) {
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

  bool Database::remove_social_sub(int id, const std::string &server_id) {
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

  bool Database::update_social_sub_channels(int id, const std::string &server_id,
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

  bool Database::update_social_sub_events(int id, const std::string &server_id,
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

  bool Database::update_social_sub_check(int id, uint64_t last_checked,
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

  std::optional<SocialEmbedConfig>
  Database::get_social_embed(int sub_id, const std::string &event_type) {
    std::shared_lock<std::shared_mutex> lock(social_embed_cache_mutex_);
    std::string key = std::to_string(sub_id) + ":" + event_type;
    auto it = social_embed_cache_.find(key);
    if (it != social_embed_cache_.end())
      return it->second;
    return std::nullopt;
  }

  bool Database::save_social_embed(const SocialEmbedConfig &cfg) {
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

  bool Database::delete_social_embed(int sub_id, const std::string &event_type) {
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

} // namespace bronx
