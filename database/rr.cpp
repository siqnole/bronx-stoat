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

  bool Database::add_rr_message(const std::string &message_id,
                      const std::string &channel_id,
                      const std::string &server_id,
                      const std::string &modal_name,
                      const std::string &type) {
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

  bool Database::update_rr_message_type(const std::string &message_id,
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

  bool Database::update_rr_embed_json(const std::string &message_id,
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

  bool Database::add_rr_rule(const std::string &message_id, const std::string &emoji_id,
                   const std::string &role_id,
                   const std::string &rule_type) {
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
        bool found;
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

  bool Database::remove_rr_rule(const std::string &message_id,
                      const std::string &emoji_id,
                      const std::string &role_id) {
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

  bool Database::delete_rr_message(const std::string &message_id) {
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

  std::optional<RRMessage> Database::get_rr_message(const std::string &message_id) {
    std::shared_lock<std::shared_mutex> lock(rr_cache_mutex_);
    auto it = rr_cache_.find(message_id);
    if (it != rr_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  std::string Database::get_message_id_by_modal_name(const std::string &server_id,
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
  Database::get_message_ids_by_modal(const std::string &server_id,
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

  std::vector<ModalInfo> Database::get_server_modals(const std::string &server_id) {
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

  bool Database::clear_rr_rules(const std::string &message_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_ || message_id.empty())
      return false;
    std::string esc_msg = escape_string(message_id);
    std::string query = "DELETE FROM rr_rules WHERE message_id = '" + esc_msg + "'";
    return mysql_query(mysql_, query.c_str()) == 0;
  }

} // namespace bronx
