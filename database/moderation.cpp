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

  std::optional<WelcomeLeaveConfig>
  Database::get_welcome_leave_config(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> wl_lock(wl_cache_mutex_);
    auto it = wl_cache_.find(server_id);
    if (it != wl_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool Database::save_welcome_leave_config(const WelcomeLeaveConfig &wl) {
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

  std::string Database::get_logging_channel(const std::string &server_id,
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
  Database::get_server_logging_config(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(log_cache_mutex_);
    auto it = log_cache_.find(server_id);
    if (it != log_cache_.end()) {
      return it->second;
    }
    return {};
  }

  std::optional<AntiRaidConfig>
  Database::get_antiraid_config(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(ar_cache_mutex_);
    auto it = ar_cache_.find(server_id);
    if (it != ar_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool Database::save_antiraid_config(const AntiRaidConfig &ar) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_srv = escape_string(ar.server_id);
    std::string esc_joins_act = escape_string(ar.joins_action);
    std::string esc_spam_act = escape_string(ar.spam_action);
    std::string esc_profile_act = escape_string(ar.default_profile_action);
    std::string esc_mentions_act = escape_string(ar.mentions_action);

    std::string query =
        "INSERT INTO antiraid_configs (server_id, joins_threshold_count, "
        "joins_threshold_seconds, joins_action, "
        "spam_threshold_count, spam_threshold_seconds, spam_action, "
        "default_profile_action, mentions_threshold_count, "
        "mentions_threshold_messages, mentions_action) VALUES ("
        "'" +
        esc_srv + "', " + std::to_string(ar.joins_threshold_count) + ", " +
        std::to_string(ar.joins_threshold_seconds) +
        ", "
        "'" +
        esc_joins_act + "', " + std::to_string(ar.spam_threshold_count) + ", " +
        std::to_string(ar.spam_threshold_seconds) +
        ", "
        "'" +
        esc_spam_act + "', '" + esc_profile_act + "', " +
        std::to_string(ar.mentions_threshold_count) + ", " +
        std::to_string(ar.mentions_threshold_messages) + ", '" +
        esc_mentions_act + "') "
        "ON DUPLICATE KEY UPDATE "
        "joins_threshold_count = VALUES(joins_threshold_count), "
        "joins_threshold_seconds = VALUES(joins_threshold_seconds), "
        "joins_action = VALUES(joins_action), "
        "spam_threshold_count = VALUES(spam_threshold_count), "
        "spam_threshold_seconds = VALUES(spam_threshold_seconds), "
        "spam_action = VALUES(spam_action), "
        "default_profile_action = VALUES(default_profile_action), "
        "mentions_threshold_count = VALUES(mentions_threshold_count), "
        "mentions_threshold_messages = VALUES(mentions_threshold_messages), "
        "mentions_action = VALUES(mentions_action)";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save antiraid config: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    std::lock_guard<std::shared_mutex> cache_lock(ar_cache_mutex_);
    ar_cache_[ar.server_id] = ar;
    return true;
  }

  bool Database::save_logging_channel(const std::string &server_id,
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

  bool Database::remove_logging_channel(const std::string &server_id,
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

  std::optional<LoggingEmbed> Database::get_logging_embed(const std::string &server_id,
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

  bool Database::save_logging_embed(const LoggingEmbed &le) {
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

  bool Database::remove_logging_embed(const std::string &server_id,
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
  Database::get_logging_webhook(const std::string &channel_id) {
    std::shared_lock<std::shared_mutex> lock(webhook_cache_mutex_);
    auto it = webhook_cache_.find(channel_id);
    if (it != webhook_cache_.end()) {
      return std::make_pair(it->second.id, it->second.token);
    }
    return std::nullopt;
  }

  bool Database::save_logging_webhook(const std::string &channel_id,
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

  bool Database::remove_logging_webhook(const std::string &channel_id) {
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

  bool Database::is_trusted(const std::string &server_id, const std::string &user_id,
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
  Database::get_trusted(const std::string &server_id) {
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

  bool Database::add_trusted(const std::string &server_id, const std::string &type,
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

  bool Database::remove_trusted(const std::string &server_id, const std::string &type,
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
  Database::get_restrictions(const std::string &server_id, const std::string &target_type,
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

  bool Database::add_restriction(const std::string &server_id,
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

  bool Database::remove_restriction(const std::string &server_id,
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

  bool Database::is_disabled(const std::string &server_id,
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
  Database::get_disabled_items(const std::string &server_id) {
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

  bool Database::disable_item(const std::string &server_id,
                    const std::string &target_type,
                    const std::string &target_name) {
    std::string esc_srv = escape_string(server_id);
    std::string esc_type = escape_string(target_type);
    std::string esc_name = escape_string(target_name);

    std::string query = "INSERT IGNORE INTO disabled_commands (server_id, "
                        "target_type, target_name) VALUES "
                        "('" +
                        esc_srv + "', '" + esc_type + "', '" + esc_name + "')";
    {
      std::lock_guard<std::mutex> db_lock(db_mutex_);
      if (mysql_query(mysql_, query.c_str())) {
        std::cerr << "[bronx db] failed to disable command/module: "
                  << mysql_error(mysql_) << "\n";
        return false;
      }
    }

    {
      std::unique_lock<std::shared_mutex> lock(disabled_cache_mutex_);
      disabled_cache_[server_id].insert(target_type + ":" + target_name);
    }
    return true;
  }

  bool Database::enable_item(const std::string &server_id, const std::string &target_type,
                   const std::string &target_name) {
    std::string esc_srv = escape_string(server_id);
    std::string esc_type = escape_string(target_type);
    std::string esc_name = escape_string(target_name);

    std::string query = "DELETE FROM disabled_commands WHERE server_id = '" +
                        esc_srv +
                        "' "
                        "AND target_type = '" +
                        esc_type + "' AND target_name = '" + esc_name + "'";
    {
      std::lock_guard<std::mutex> db_lock(db_mutex_);
      if (mysql_query(mysql_, query.c_str())) {
        std::cerr << "[bronx db] failed to enable command/module: "
                  << mysql_error(mysql_) << "\n";
        return false;
      }
    }

    {
      std::unique_lock<std::shared_mutex> lock(disabled_cache_mutex_);
      auto it = disabled_cache_.find(server_id);
      if (it != disabled_cache_.end()) {
        it->second.erase(target_type + ":" + target_name);
      }
    }
    return true;
  }

  bool Database::add_reminder(const std::string &user_id, const std::string &channel_id,
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

  std::vector<Reminder> Database::get_due_reminders(uint64_t current_timestamp) {
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

  std::vector<Reminder> Database::get_user_reminders(const std::string &user_id) {
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

  bool Database::delete_reminder(int id) {
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
  Database::get_action_message(const std::string &server_id,
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

  bool Database::save_action_message(const ActionMessageConfig &config) {
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

  bool Database::remove_action_message(const std::string &server_id,
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
  Database::list_action_messages(const std::string &server_id) {
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

  std::optional<JailConfig> Database::get_jail_config(const std::string &server_id) {
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

  bool Database::save_jail_config(const JailConfig &cfg) {
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

  std::optional<ActiveJail> Database::get_active_jail(const std::string &server_id,
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

  bool Database::add_active_jail(const ActiveJail &jail) {
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

  bool Database::remove_active_jail(const std::string &server_id,
                          const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "DELETE FROM active_jails WHERE server_id = '" +
                      escape_string(server_id) + "' AND user_id = '" +
                      escape_string(user_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::vector<ActiveJail> Database::get_expired_jails(uint64_t now_unix) {
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

  bool Database::record_moderation_action(const std::string &server_id,
                                const std::string &action,
                                const std::string &moderator_id,
                                const std::string &target_id,
                                const std::string &reason,
                                const std::string &extra_details,
                                const std::string &channel_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT INTO moderation_logs (server_id, action, moderator_id, "
        "target_id, reason, extra_details, channel_id) VALUES ('" +
        escape_string(server_id) + "', '" + escape_string(action) + "', '" +
        escape_string(moderator_id) + "', '" + escape_string(target_id) +
        "', '" + escape_string(reason) + "', '" + escape_string(extra_details) +
        "', " + (channel_id.empty() ? "NULL" : "'" + escape_string(channel_id) + "'") + ")";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool Database::record_command_usage(const std::string &server_id,
                            const std::string &command_name,
                            const std::string &user_id,
                            const std::string &channel_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT INTO command_usage_history (server_id, command_name, user_id, "
        "channel_id) VALUES ('" +
        escape_string(server_id) + "', '" + escape_string(command_name) +
        "', '" + escape_string(user_id) + "', '" + escape_string(channel_id) + "')";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::vector<CmdHistoryEntry> Database::get_server_command_history_by_user(const std::string &server_id,
                                                                   const std::string &user_id,
                                                                   int offset,
                                                                   int limit,
                                                                   int &total_count) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<CmdHistoryEntry> entries;
    if (!mysql_) {
      total_count = 0;
      return entries;
    }

    std::string esc_srv = escape_string(server_id);
    std::string esc_usr = escape_string(user_id);
    std::string where_clause = "WHERE server_id = '" + esc_srv + "' AND user_id = '" + esc_usr + "'";

    // 1. Get count
    std::string count_sql = "SELECT COUNT(*) FROM command_usage_history " + where_clause;
    if (mysql_query(mysql_, count_sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          total_count = std::stoi(row[0]);
        }
        mysql_free_result(res);
      }
    }

    // 2. Get entries
    std::string sql = "SELECT id, command_name, user_id, channel_id, UNIX_TIMESTAMP(created_at) FROM command_usage_history " +
                      where_clause + " ORDER BY id DESC LIMIT " + std::to_string(offset) + ", " + std::to_string(limit);

    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          CmdHistoryEntry e;
          e.id = row[0] ? std::stoi(row[0]) : 0;
          e.command_name = row[1] ? row[1] : "";
          e.user_id = row[2] ? row[2] : "";
          e.channel_id = row[3] ? row[3] : "";
          e.created_at = row[4] ? std::stoull(row[4]) : 0;
          entries.push_back(e);
        }
        mysql_free_result(res);
      }
    }

    return entries;
  }

  std::vector<CmdHistoryEntry> Database::get_server_command_history_by_command(const std::string &server_id,
                                                                      const std::string &command_name,
                                                                      int offset,
                                                                      int limit,
                                                                      int &total_count) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<CmdHistoryEntry> entries;
    if (!mysql_) {
      total_count = 0;
      return entries;
    }

    std::string esc_srv = escape_string(server_id);
    std::string esc_cmd = escape_string(command_name);
    std::string where_clause = "WHERE server_id = '" + esc_srv + "' AND command_name = '" + esc_cmd + "'";

    // 1. Get count
    std::string count_sql = "SELECT COUNT(*) FROM command_usage_history " + where_clause;
    if (mysql_query(mysql_, count_sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          total_count = std::stoi(row[0]);
        }
        mysql_free_result(res);
      }
    }

    // 2. Get entries
    std::string sql = "SELECT id, command_name, user_id, channel_id, UNIX_TIMESTAMP(created_at) FROM command_usage_history " +
                      where_clause + " ORDER BY id DESC LIMIT " + std::to_string(offset) + ", " + std::to_string(limit);

    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          CmdHistoryEntry e;
          e.id = row[0] ? std::stoi(row[0]) : 0;
          e.command_name = row[1] ? row[1] : "";
          e.user_id = row[2] ? row[2] : "";
          e.channel_id = row[3] ? row[3] : "";
          e.created_at = row[4] ? std::stoull(row[4]) : 0;
          entries.push_back(e);
        }
        mysql_free_result(res);
      }
    }

    return entries;
  }

  std::vector<ModHistoryEntry> Database::get_server_mod_history(const std::string &server_id,
                                                      const std::string &user_id,
                                                      int offset,
                                                      int limit,
                                                      int &total_count) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<ModHistoryEntry> entries;
    if (!mysql_) {
      total_count = 0;
      return entries;
    }

    std::string esc_srv = escape_string(server_id);
    std::string where_clause = "WHERE server_id = '" + esc_srv + "'";
    if (!user_id.empty()) {
      std::string esc_usr = escape_string(user_id);
      where_clause += " AND (moderator_id = '" + esc_usr + "' OR target_id = '" + esc_usr + "')";
    }

    // 1. Get count
    std::string count_sql = "SELECT COUNT(*) FROM moderation_logs " + where_clause;
    if (mysql_query(mysql_, count_sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          total_count = std::stoi(row[0]);
        }
        mysql_free_result(res);
      }
    }

    // 2. Get entries
    std::string sql = "SELECT id, action, moderator_id, target_id, COALESCE(reason,''), "
                      "COALESCE(extra_details,''), COALESCE(channel_id,''), "
                      "UNIX_TIMESTAMP(created_at) FROM moderation_logs " +
                      where_clause + " ORDER BY id DESC LIMIT " + std::to_string(offset) + ", " + std::to_string(limit);

    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          ModHistoryEntry e;
          e.id = row[0] ? std::stoi(row[0]) : 0;
          e.action = row[1] ? row[1] : "";
          e.moderator_id = row[2] ? row[2] : "";
          e.target_id = row[3] ? row[3] : "";
          e.reason = row[4] ? row[4] : "";
          e.extra_details = row[5] ? row[5] : "";
          e.channel_id = row[6] ? row[6] : "";
          e.created_at = row[7] ? std::stoull(row[7]) : 0;
          entries.push_back(e);
        }
        mysql_free_result(res);
      }
    }

    return entries;
  }

  bool Database::delete_moderation_action(const std::string &server_id, int id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "DELETE FROM moderation_logs WHERE server_id = '" +
                      escape_string(server_id) +
                      "' AND id = " + std::to_string(id);
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::vector<ModLogEntry> Database::get_mod_logs(const std::string &server_id,
                                        const std::string &target_id,
                                        int limit) {
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

  std::vector<ModLogEntry> Database::get_mod_actions(const std::string &server_id,
                                           const std::string &moderator_id,
                                           int limit) {
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

  int Database::get_warn_count(const std::string &server_id,
                               const std::string &target_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return 0;
    std::string sql = "SELECT COUNT(*) FROM moderation_logs WHERE server_id = '" +
                      escape_string(server_id) + "' AND target_id = '" +
                      escape_string(target_id) + "' AND action = 'Warn'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          int count = std::stoi(row[0]);
          mysql_free_result(res);
          return count;
        }
        mysql_free_result(res);
      }
    }
    return 0;
  }

  int Database::get_timeout_count(const std::string &server_id,
                                  const std::string &target_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return 0;
    std::string sql = "SELECT COUNT(*) FROM moderation_logs WHERE server_id = '" +
                      escape_string(server_id) + "' AND target_id = '" +
                      escape_string(target_id) + "' AND action = 'Timeout'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          int count = std::stoi(row[0]);
          mysql_free_result(res);
          return count;
        }
        mysql_free_result(res);
      }
    }
    return 0;
  }

  bool Database::reload_welcome_leave_config(const std::string &server_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_) return false;
    std::string q = "SELECT server_id, welcome_channel_id, welcome_type, welcome_content, welcome_embed, "
                    "leave_channel_id, leave_type, leave_content, leave_embed, autorole_role_id, "
                    "welcome_dm_type, welcome_dm_content, welcome_dm_embed FROM welcome_leave_configs WHERE server_id = '" + escape_string(server_id) + "'";
    if (mysql_query(mysql_, q.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        std::lock_guard<std::shared_mutex> cache_lock(wl_cache_mutex_);
        if (row) {
          WelcomeLeaveConfig wl;
          wl.server_id = row[0] ? row[0] : "";
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
          wl_cache_[server_id] = wl;
        } else {
          wl_cache_.erase(server_id);
        }
        mysql_free_result(res);
      }
    }
    return true;
  }

  bool Database::reload_logging_config(const std::string &server_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_) return false;
    std::string q = "SELECT event_type, channel_id FROM logging_configs WHERE server_id = '" + escape_string(server_id) + "'";
    if (mysql_query(mysql_, q.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        std::lock_guard<std::shared_mutex> cache_lock(log_cache_mutex_);
        log_cache_[server_id].clear();
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          log_cache_[server_id][row[0] ? row[0] : ""] = row[1] ? row[1] : "";
        }
        mysql_free_result(res);
      }
    }
    return true;
  }

  bool Database::reload_logging_embeds(const std::string &server_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_) return false;
    std::string q = "SELECT event_type, message_type, content, embed_json FROM logging_embeds WHERE server_id = '" + escape_string(server_id) + "'";
    if (mysql_query(mysql_, q.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        std::lock_guard<std::shared_mutex> cache_lock(log_embed_cache_mutex_);
        log_embed_cache_[server_id].clear();
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          LoggingEmbed le;
          le.server_id = server_id;
          le.event_type = row[0] ? row[0] : "";
          le.message_type = row[1] ? row[1] : "embed";
          le.content = row[2] ? row[2] : "";
          le.embed_json = row[3] ? row[3] : "";
          log_embed_cache_[server_id][le.event_type] = le;
        }
        mysql_free_result(res);
      }
    }
    return true;
  }

  bool Database::reload_antiraid_config(const std::string &server_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_) return false;
    std::string q = "SELECT server_id, joins_threshold_count, joins_threshold_seconds, "
                    "joins_action, spam_threshold_count, spam_threshold_seconds, "
                    "spam_action, default_profile_action, mentions_threshold_count, "
                    "mentions_threshold_messages, mentions_action FROM antiraid_configs WHERE server_id = '" + escape_string(server_id) + "'";
    if (mysql_query(mysql_, q.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        std::lock_guard<std::shared_mutex> cache_lock(ar_cache_mutex_);
        if (row) {
          AntiRaidConfig ar;
          ar.server_id = row[0] ? row[0] : "";
          ar.joins_threshold_count = row[1] ? std::stoi(row[1]) : 5;
          ar.joins_threshold_seconds = row[2] ? std::stoi(row[2]) : 60;
          ar.joins_action = row[3] ? row[3] : "none";
          ar.spam_threshold_count = row[4] ? std::stoi(row[4]) : 5;
          ar.spam_threshold_seconds = row[5] ? std::stoi(row[5]) : 2;
          ar.spam_action = row[6] ? row[6] : "none";
          ar.default_profile_action = row[7] ? row[7] : "none";
          ar.mentions_threshold_count = row[8] ? std::stoi(row[8]) : 10;
          ar.mentions_threshold_messages = row[9] ? std::stoi(row[9]) : 5;
          ar.mentions_action = row[10] ? row[10] : "none";
          ar_cache_[server_id] = ar;
        } else {
          ar_cache_.erase(server_id);
        }
        mysql_free_result(res);
      }
    }
    return true;
  }

  bool Database::reload_disabled_items(const std::string &server_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_) return false;
    std::string q = "SELECT target_type, target_name FROM disabled_commands WHERE server_id = '" + escape_string(server_id) + "'";
    if (mysql_query(mysql_, q.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        std::unique_lock<std::shared_mutex> lock(disabled_cache_mutex_);
        disabled_cache_[server_id].clear();
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          if (row[0] && row[1]) {
            disabled_cache_[server_id].insert(std::string(row[0]) + ":" + row[1]);
          }
        }
        mysql_free_result(res);
      }
    }
    return true;
  }

  std::optional<HoneypotConfig>
  Database::get_honeypot_config(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(hp_cache_mutex_);
    auto it = hp_cache_.find(server_id);
    if (it != hp_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool Database::save_honeypot_config(const HoneypotConfig &hp) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);

    std::string esc_srv = escape_string(hp.server_id);
    std::string esc_chan = escape_string(hp.channel_id);
    std::string esc_msg = escape_string(hp.honeypot_message);
    std::string esc_disc = escape_string(hp.disclaimer_message);
    std::string esc_act = escape_string(hp.action);

    std::string query =
        "INSERT INTO honeypot_configs (server_id, channel_id, honeypot_message, "
        "disclaimer_message, action, message_triggers, react_triggers, enabled) VALUES ("
        "'" + esc_srv + "', "
        "'" + esc_chan + "', "
        "'" + esc_msg + "', "
        "'" + esc_disc + "', "
        "'" + esc_act + "', "
        + std::to_string(hp.message_triggers ? 1 : 0) + ", "
        + std::to_string(hp.react_triggers ? 1 : 0) + ", "
        + std::to_string(hp.enabled ? 1 : 0) + ") "
        "ON DUPLICATE KEY UPDATE "
        "channel_id = VALUES(channel_id), "
        "honeypot_message = VALUES(honeypot_message), "
        "disclaimer_message = VALUES(disclaimer_message), "
        "action = VALUES(action), "
        "message_triggers = VALUES(message_triggers), "
        "react_triggers = VALUES(react_triggers), "
        "enabled = VALUES(enabled)";

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save honeypot config: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    std::lock_guard<std::shared_mutex> cache_lock(hp_cache_mutex_);
    hp_cache_[hp.server_id] = hp;
    return true;
  }

  bool Database::reload_honeypot_config(const std::string &server_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_) return false;
    std::string q = "SELECT server_id, channel_id, COALESCE(honeypot_message, ''), "
                    "COALESCE(disclaimer_message, ''), action, message_triggers, "
                    "react_triggers, enabled FROM honeypot_configs WHERE server_id = '" + escape_string(server_id) + "'";
    if (mysql_query(mysql_, q.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        std::lock_guard<std::shared_mutex> cache_lock(hp_cache_mutex_);
        if (row) {
          HoneypotConfig hp;
          hp.server_id = row[0] ? row[0] : "";
          hp.channel_id = row[1] ? row[1] : "";
          hp.honeypot_message = row[2] ? row[2] : "";
          hp.disclaimer_message = row[3] ? row[3] : "";
          hp.action = row[4] ? row[4] : "ban";
          hp.message_triggers = row[5] ? (std::stoi(row[5]) != 0) : true;
          hp.react_triggers = row[6] ? (std::stoi(row[6]) != 0) : false;
          hp.enabled = row[7] ? (std::stoi(row[7]) != 0) : false;
          hp_cache_[server_id] = hp;
        } else {
          hp_cache_.erase(server_id);
        }
        mysql_free_result(res);
      }
    }
    return true;
  }

  bool Database::add_ban_link(const std::string &guild_id, const std::string &target_guild_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_) return false;
    std::string query = "INSERT IGNORE INTO ban_links (guild_id, target_guild_id) VALUES ('" +
                        escape_string(guild_id) + "', '" + escape_string(target_guild_id) + "')";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to add ban link: " << mysql_error(mysql_) << "\n";
      return false;
    }
    return true;
  }

  bool Database::remove_ban_link(const std::string &guild_id, const std::string &target_guild_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_) return false;
    std::string query = "DELETE FROM ban_links WHERE (guild_id = '" + escape_string(guild_id) +
                        "' AND target_guild_id = '" + escape_string(target_guild_id) + "') OR "
                        "(guild_id = '" + escape_string(target_guild_id) +
                        "' AND target_guild_id = '" + escape_string(guild_id) + "')";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to remove ban link: " << mysql_error(mysql_) << "\n";
      return false;
    }
    return true;
  }

  std::vector<std::string> Database::get_linked_ban_servers(const std::string &guild_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::vector<std::string> servers;
    if (!mysql_) return servers;
    std::string query = "SELECT a.target_guild_id FROM ban_links a "
                        "INNER JOIN ban_links b ON a.guild_id = b.target_guild_id AND a.target_guild_id = b.guild_id "
                        "WHERE a.guild_id = '" + escape_string(guild_id) + "'";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to get linked ban servers: " << mysql_error(mysql_) << "\n";
      return servers;
    }
    MYSQL_RES *res = mysql_store_result(mysql_);
    if (res) {
      MYSQL_ROW row;
      while ((row = mysql_fetch_row(res))) {
        if (row[0]) {
          servers.push_back(row[0]);
        }
      }
      mysql_free_result(res);
    }
    return servers;
  }

  std::vector<std::string> Database::get_pending_ban_links(const std::string &guild_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::vector<std::string> servers;
    if (!mysql_) return servers;
    std::string query = "SELECT target_guild_id FROM ban_links WHERE guild_id = '" + escape_string(guild_id) + "' "
                        "AND target_guild_id NOT IN (SELECT guild_id FROM ban_links WHERE target_guild_id = '" + escape_string(guild_id) + "')";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to get pending ban links: " << mysql_error(mysql_) << "\n";
      return servers;
    }
    MYSQL_RES *res = mysql_store_result(mysql_);
    if (res) {
      MYSQL_ROW row;
      while ((row = mysql_fetch_row(res))) {
        if (row[0]) {
          servers.push_back(row[0]);
        }
      }
      mysql_free_result(res);
    }
    return servers;
  }

  std::vector<std::string> Database::get_incoming_ban_links(const std::string &guild_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::vector<std::string> servers;
    if (!mysql_) return servers;
    std::string query = "SELECT guild_id FROM ban_links WHERE target_guild_id = '" + escape_string(guild_id) + "' "
                        "AND guild_id NOT IN (SELECT target_guild_id FROM ban_links WHERE target_guild_id = '" + escape_string(guild_id) + "')";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to get incoming ban links: " << mysql_error(mysql_) << "\n";
      return servers;
    }
    MYSQL_RES *res = mysql_store_result(mysql_);
    if (res) {
      MYSQL_ROW row;
      while ((row = mysql_fetch_row(res))) {
        if (row[0]) {
          servers.push_back(row[0]);
        }
      }
      mysql_free_result(res);
    }
    return servers;
  }

} // namespace bronx
