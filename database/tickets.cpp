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

  std::optional<TicketConfig>
  Database::get_ticket_config(const std::string &server_id,
                    const std::string &name) {
    std::shared_lock<std::shared_mutex> lock(ticket_config_cache_mutex_);
    std::string key = server_id + ":" + name;
    auto it = ticket_config_cache_.find(key);
    if (it != ticket_config_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool Database::save_ticket_config(const TicketConfig &config) {
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

  bool Database::delete_ticket_config(const std::string &server_id, const std::string &name) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string esc_srv = escape_string(server_id);
    std::string esc_name = escape_string(name);
    std::string query =
        "DELETE FROM ticket_configs WHERE server_id = '" + esc_srv + "' AND name = '" + esc_name + "'";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to delete ticket config: " << mysql_error(mysql_)
                << "\n";
      return false;
    }

    {
      std::lock_guard<std::shared_mutex> cache_lock(ticket_config_cache_mutex_);
      std::string key = server_id + ":" + name;
      ticket_config_cache_.erase(key);
    }
    return true;
  }

  std::optional<Ticket> Database::get_ticket(const std::string &channel_id) {
    std::shared_lock<std::shared_mutex> lock(ticket_cache_mutex_);
    auto it = ticket_cache_.find(channel_id);
    if (it != ticket_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool Database::save_ticket(const Ticket &ticket) {
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

  bool Database::delete_ticket(const std::string &channel_id) {
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

  std::vector<Ticket> Database::get_server_tickets(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(ticket_cache_mutex_);
    std::vector<Ticket> results;
    for (const auto &[ch_id, t] : ticket_cache_) {
      if (t.server_id == server_id) {
        results.push_back(t);
      }
    }
    return results;
  }

  std::optional<Ticket> Database::get_user_open_ticket(const std::string &server_id,
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
  Database::get_ticket_configs_for_server(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(ticket_config_cache_mutex_);
    std::vector<TicketConfig> results;
    for (const auto &[key, cfg] : ticket_config_cache_) {
      if (cfg.server_id == server_id) {
        results.push_back(cfg);
      }
    }
    return results;
  }

  std::vector<TicketConfig> Database::get_all_ticket_configs() {
    std::shared_lock<std::shared_mutex> lock(ticket_config_cache_mutex_);
    std::vector<TicketConfig> results;
    for (const auto &[key, cfg] : ticket_config_cache_) {
      results.push_back(cfg);
    }
    return results;
  }

  std::vector<Ticket> Database::get_all_tickets() {
    std::shared_lock<std::shared_mutex> lock(ticket_cache_mutex_);
    std::vector<Ticket> results;
    for (const auto &[ch_id, t] : ticket_cache_) {
      results.push_back(t);
    }
    return results;
  }

  std::optional<ApplicationPanel> Database::get_application_panel(const std::string &server_id, const std::string &name) {
    std::shared_lock<std::shared_mutex> lock(app_panel_cache_mutex_);
    std::string key = server_id + ":" + name;
    auto it = app_panel_cache_.find(key);
    if (it != app_panel_cache_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool Database::save_application_panel(const ApplicationPanel &panel) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string esc_srv = escape_string(panel.server_id);
    std::string esc_name = escape_string(panel.name);
    std::string esc_cmd = escape_string(panel.trigger_command);
    std::string esc_emoji = escape_string(panel.trigger_emoji);
    std::string esc_m_chan = escape_string(panel.modal_channel_id);
    std::string esc_m_msg = escape_string(panel.modal_message_id);
    std::string esc_content = escape_string(panel.modal_content);
    std::string esc_embed = escape_string(panel.modal_embed_json);
    std::string esc_o_chan = escape_string(panel.output_channel_id);
    std::string esc_mode = escape_string(panel.mode);
    std::string esc_roles = escape_string(panel.view_roles);
    std::string esc_users = escape_string(panel.view_users);
    std::string esc_perms = escape_string(panel.view_permissions);
    std::string esc_m_role = escape_string(panel.member_role_id);
    std::string esc_scheme = escape_string(panel.naming_scheme);
    std::string esc_welcome = escape_string(panel.welcome_content);
    std::string esc_close = escape_string(panel.close_emoji);
    int req_role = panel.require_role ? 1 : 0;
    std::string esc_req_role_id = escape_string(panel.require_role_id);
    int give_role = panel.role_to_give ? 1 : 0;
    std::string esc_give_role_id = escape_string(panel.role_to_give_id);
    std::string esc_end_msg = escape_string(panel.end_message);

    nlohmann::json q_arr = nlohmann::json::array();
    for (const auto &q : panel.questions) {
      q_arr.push_back(q);
    }
    std::string esc_questions = escape_string(q_arr.dump());
    int counter = panel.app_counter;

    std::string query =
        "INSERT INTO application_panels (server_id, name, trigger_command, "
        "trigger_emoji, modal_channel_id, modal_message_id, modal_content, "
        "modal_embed_json, output_channel_id, mode, view_roles, view_users, "
        "view_permissions, member_role_id, naming_scheme, welcome_content, "
        "close_emoji, require_role, require_role_id, questions, app_counter, "
        "role_to_give, role_to_give_id, end_message) "
        "VALUES ('" +
        esc_srv + "', '" + esc_name + "', '" + esc_cmd + "', '" + esc_emoji +
        "', '" + esc_m_chan + "', '" + esc_m_msg + "', '" + esc_content + "', '" +
        esc_embed + "', '" + esc_o_chan + "', '" + esc_mode + "', '" + esc_roles +
        "', '" + esc_users + "', '" + esc_perms + "', '" + esc_m_role + "', '" +
        esc_scheme + "', '" + esc_welcome + "', '" + esc_close + "', " +
        std::to_string(req_role) + ", '" + esc_req_role_id + "', '" + esc_questions +
        "', " + std::to_string(counter) + ", " + std::to_string(give_role) + ", '" +
        esc_give_role_id + "', '" + esc_end_msg + "') "
        "ON DUPLICATE KEY UPDATE "
        "trigger_command = '" + esc_cmd + "', "
        "trigger_emoji = '" + esc_emoji + "', "
        "modal_channel_id = '" + esc_m_chan + "', "
        "modal_message_id = '" + esc_m_msg + "', "
        "modal_content = '" + esc_content + "', "
        "modal_embed_json = '" + esc_embed + "', "
        "output_channel_id = '" + esc_o_chan + "', "
        "mode = '" + esc_mode + "', "
        "view_roles = '" + esc_roles + "', "
        "view_users = '" + esc_users + "', "
        "view_permissions = '" + esc_perms + "', "
        "member_role_id = '" + esc_m_role + "', "
        "naming_scheme = '" + esc_scheme + "', "
        "welcome_content = '" + esc_welcome + "', "
        "close_emoji = '" + esc_close + "', "
        "require_role = " + std::to_string(req_role) + ", "
        "require_role_id = '" + esc_req_role_id + "', "
        "role_to_give = " + std::to_string(give_role) + ", "
        "role_to_give_id = '" + esc_give_role_id + "', "
        "end_message = '" + esc_end_msg + "', "
        "questions = '" + esc_questions + "', "
        "app_counter = " + std::to_string(counter);

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save application panel: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    {
      std::lock_guard<std::shared_mutex> cache_lock(app_panel_cache_mutex_);
      std::string key = panel.server_id + ":" + panel.name;
      app_panel_cache_[key] = panel;
    }
    return true;
  }

  bool Database::delete_application_panel(const std::string &server_id, const std::string &name) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string esc_srv = escape_string(server_id);
    std::string esc_name = escape_string(name);
    std::string query =
        "DELETE FROM application_panels WHERE server_id = '" + esc_srv + "' AND name = '" + esc_name + "'";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to delete application panel: " << mysql_error(mysql_)
                << "\n";
      return false;
    }

    {
      std::lock_guard<std::shared_mutex> cache_lock(app_panel_cache_mutex_);
      std::string key = server_id + ":" + name;
      app_panel_cache_.erase(key);
    }
    return true;
  }

  std::vector<ApplicationPanel> Database::get_application_panels_for_server(const std::string &server_id) {
    std::shared_lock<std::shared_mutex> lock(app_panel_cache_mutex_);
    std::vector<ApplicationPanel> results;
    for (const auto &[key, cfg] : app_panel_cache_) {
      if (cfg.server_id == server_id) {
        results.push_back(cfg);
      }
    }
    return results;
  }

  std::vector<ApplicationPanel> Database::get_all_application_panels() {
    std::shared_lock<std::shared_mutex> lock(app_panel_cache_mutex_);
    std::vector<ApplicationPanel> results;
    for (const auto &[key, cfg] : app_panel_cache_) {
      results.push_back(cfg);
    }
    return results;
  }

  bool Database::save_application_submission(ApplicationSubmission &sub) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string esc_srv = escape_string(sub.server_id);
    std::string esc_panel = escape_string(sub.panel_name);
    std::string esc_user = escape_string(sub.user_id);
    std::string esc_answers = escape_string(sub.answers_json);
    std::string esc_status = escape_string(sub.status);
    std::string esc_out_msg = escape_string(sub.output_message_id);
    std::string esc_t_chan = escape_string(sub.ticket_channel_id);

    std::string query;
    if (sub.id == 0) {
      query =
          "INSERT INTO application_submissions (server_id, panel_name, user_id, "
          "submitted_at, answers_json, status, output_message_id, "
          "ticket_channel_id) "
          "VALUES ('" +
          esc_srv + "', '" + esc_panel + "', '" + esc_user + "', " +
          std::to_string(sub.submitted_at) + ", '" + esc_answers + "', '" +
          esc_status + "', '" + esc_out_msg + "', '" + esc_t_chan + "')";
    } else {
      query =
          "UPDATE application_submissions SET server_id = '" + esc_srv + "', "
          "panel_name = '" + esc_panel + "', user_id = '" + esc_user + "', "
          "submitted_at = " + std::to_string(sub.submitted_at) + ", "
          "answers_json = '" + esc_answers + "', status = '" + esc_status + "', "
          "output_message_id = '" + esc_out_msg + "', ticket_channel_id = '" +
          esc_t_chan + "' WHERE id = " + std::to_string(sub.id);
    }

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save application submission: "
                << mysql_error(mysql_) << "\n";
      return false;
    }

    if (sub.id == 0) {
      sub.id = static_cast<int>(mysql_insert_id(mysql_));
    }
    return true;
  }

  std::optional<ApplicationSubmission> Database::get_application_submission_by_message_id(const std::string &message_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_ || message_id.empty())
      return std::nullopt;

    std::string query =
        "SELECT id, server_id, panel_name, user_id, submitted_at, answers_json, "
        "status, output_message_id, ticket_channel_id FROM application_submissions "
        "WHERE output_message_id = '" +
        escape_string(message_id) + "'";
    if (mysql_query(mysql_, query.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        std::optional<ApplicationSubmission> sub_opt;
        if ((row = mysql_fetch_row(res))) {
          ApplicationSubmission sub;
          sub.id = row[0] ? std::stoi(row[0]) : 0;
          sub.server_id = row[1] ? row[1] : "";
          sub.panel_name = row[2] ? row[2] : "";
          sub.user_id = row[3] ? row[3] : "";
          sub.submitted_at = row[4] ? std::stoull(row[4]) : 0;
          sub.answers_json = row[5] ? row[5] : "";
          sub.status = row[6] ? row[6] : "pending";
          sub.output_message_id = row[7] ? row[7] : "";
          sub.ticket_channel_id = row[8] ? row[8] : "";
          sub_opt = sub;
        }
        mysql_free_result(res);
        return sub_opt;
      }
    }
    return std::nullopt;
  }

  std::optional<ApplicationSubmission> Database::get_application_submission_by_id(int id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return std::nullopt;

    std::string query =
        "SELECT id, server_id, panel_name, user_id, submitted_at, answers_json, "
        "status, output_message_id, ticket_channel_id FROM application_submissions "
        "WHERE id = " +
        std::to_string(id);
    if (mysql_query(mysql_, query.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        std::optional<ApplicationSubmission> sub_opt;
        if ((row = mysql_fetch_row(res))) {
          ApplicationSubmission sub;
          sub.id = row[0] ? std::stoi(row[0]) : 0;
          sub.server_id = row[1] ? row[1] : "";
          sub.panel_name = row[2] ? row[2] : "";
          sub.user_id = row[3] ? row[3] : "";
          sub.submitted_at = row[4] ? std::stoull(row[4]) : 0;
          sub.answers_json = row[5] ? row[5] : "";
          sub.status = row[6] ? row[6] : "pending";
          sub.output_message_id = row[7] ? row[7] : "";
          sub.ticket_channel_id = row[8] ? row[8] : "";
          sub_opt = sub;
        }
        mysql_free_result(res);
        return sub_opt;
      }
    }
    return std::nullopt;
  }

  std::vector<ApplicationSubmission> Database::get_application_submissions(const std::string &server_id, const std::string &panel_name, int limit) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::vector<ApplicationSubmission> results;
    if (!mysql_)
      return results;

    std::string query =
        "SELECT id, server_id, panel_name, user_id, submitted_at, answers_json, "
        "status, output_message_id, ticket_channel_id FROM application_submissions "
        "WHERE server_id = '" +
        escape_string(server_id) + "'";
    if (!panel_name.empty()) {
      query += " AND panel_name = '" + escape_string(panel_name) + "'";
    }
    query += " ORDER BY id DESC LIMIT " + std::to_string(limit);

    if (mysql_query(mysql_, query.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          ApplicationSubmission sub;
          sub.id = row[0] ? std::stoi(row[0]) : 0;
          sub.server_id = row[1] ? row[1] : "";
          sub.panel_name = row[2] ? row[2] : "";
          sub.user_id = row[3] ? row[3] : "";
          sub.submitted_at = row[4] ? std::stoull(row[4]) : 0;
          sub.answers_json = row[5] ? row[5] : "";
          sub.status = row[6] ? row[6] : "pending";
          sub.output_message_id = row[7] ? row[7] : "";
          sub.ticket_channel_id = row[8] ? row[8] : "";
          results.push_back(sub);
        }
        mysql_free_result(res);
      }
    }
    return results;
  }

  std::vector<ApplicationSubmission> Database::get_user_application_submissions(const std::string &user_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::vector<ApplicationSubmission> results;
    if (!mysql_)
      return results;

    std::string query =
        "SELECT id, server_id, panel_name, user_id, submitted_at, answers_json, "
        "status, output_message_id, ticket_channel_id FROM application_submissions "
        "WHERE user_id = '" +
        escape_string(user_id) + "' ORDER BY id DESC";
    if (mysql_query(mysql_, query.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          ApplicationSubmission sub;
          sub.id = row[0] ? std::stoi(row[0]) : 0;
          sub.server_id = row[1] ? row[1] : "";
          sub.panel_name = row[2] ? row[2] : "";
          sub.user_id = row[3] ? row[3] : "";
          sub.submitted_at = row[4] ? std::stoull(row[4]) : 0;
          sub.answers_json = row[5] ? row[5] : "";
          sub.status = row[6] ? row[6] : "pending";
          sub.output_message_id = row[7] ? row[7] : "";
          sub.ticket_channel_id = row[8] ? row[8] : "";
          results.push_back(sub);
        }
        mysql_free_result(res);
      }
    }
    return results;
  }

  bool Database::update_application_submission_status(int id, const std::string &status) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string query = "UPDATE application_submissions SET status = '" + escape_string(status) + "' WHERE id = " + std::to_string(id);
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to update application submission status: " << mysql_error(mysql_) << "\n";
      return false;
    }
    return true;
  }

  std::optional<ServerListing> Database::get_server_listing(const std::string &server_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_ || server_id.empty())
      return std::nullopt;

    std::string query =
        "SELECT server_id, name, mode, tags, description, embed_json, invite_url, "
        "advertising_channel_id, status, last_bumped_at, created_at, bump_count "
        "FROM server_listings WHERE server_id = '" +
        escape_string(server_id) + "'";

    if (mysql_query(mysql_, query.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        std::optional<ServerListing> listing_opt;
        if ((row = mysql_fetch_row(res))) {
          ServerListing listing;
          listing.server_id = row[0] ? row[0] : "";
          listing.name = row[1] ? row[1] : "";
          listing.mode = row[2] ? row[2] : "disabled";
          listing.tags = row[3] ? row[3] : "";
          listing.description = row[4] ? row[4] : "";
          listing.embed_json = row[5] ? row[5] : "";
          listing.invite_url = row[6] ? row[6] : "";
          listing.advertising_channel_id = row[7] ? row[7] : "";
          listing.status = row[8] ? row[8] : "pending";
          listing.last_bumped_at = row[9] ? std::stoull(row[9]) : 0;
          listing.created_at = row[10] ? std::stoull(row[10]) : 0;
          listing.bump_count = row[11] ? std::stoi(row[11]) : 0;
          listing_opt = listing;
        }
        mysql_free_result(res);
        return listing_opt;
      }
    }
    return std::nullopt;
  }

  bool Database::save_server_listing(const ServerListing &listing) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string esc_srv = escape_string(listing.server_id);
    std::string esc_name = escape_string(listing.name);
    std::string esc_mode = escape_string(listing.mode);
    std::string esc_tags = escape_string(listing.tags);
    std::string esc_desc = escape_string(listing.description);
    std::string esc_embed = escape_string(listing.embed_json);
    std::string esc_invite = escape_string(listing.invite_url);
    std::string esc_ad_chan = escape_string(listing.advertising_channel_id);
    std::string esc_status = escape_string(listing.status);

    std::string query =
        "INSERT INTO server_listings (server_id, name, mode, tags, description, "
        "embed_json, invite_url, advertising_channel_id, status, last_bumped_at, "
        "created_at, bump_count) "
        "VALUES ('" +
        esc_srv + "', '" + esc_name + "', '" + esc_mode + "', '" + esc_tags +
        "', '" + esc_desc + "', '" + esc_embed + "', '" + esc_invite + "', '" +
        esc_ad_chan + "', '" + esc_status + "', " +
        std::to_string(listing.last_bumped_at) + ", " +
        std::to_string(listing.created_at) + ", " +
        std::to_string(listing.bump_count) + ") "
        "ON DUPLICATE KEY UPDATE "
        "name = '" + esc_name + "', "
        "mode = '" + esc_mode + "', "
        "tags = '" + esc_tags + "', "
        "description = '" + esc_desc + "', "
        "embed_json = '" + esc_embed + "', "
        "invite_url = '" + esc_invite + "', "
        "advertising_channel_id = '" + esc_ad_chan + "', "
        "status = '" + esc_status + "', "
        "last_bumped_at = " + std::to_string(listing.last_bumped_at) + ", "
        "bump_count = " + std::to_string(listing.bump_count);

    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to save server listing: "
                << mysql_error(mysql_) << "\n";
      return false;
    }
    return true;
  }

  bool Database::delete_server_listing(const std::string &server_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string query =
        "DELETE FROM server_listings WHERE server_id = '" + escape_string(server_id) + "'";
    if (mysql_query(mysql_, query.c_str())) {
      std::cerr << "[bronx db] failed to delete server listing: " << mysql_error(mysql_)
                << "\n";
      return false;
    }
    return true;
  }

  std::vector<ServerListing> Database::get_approved_server_listings() {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::vector<ServerListing> results;
    if (!mysql_)
      return results;

    std::string query =
        "SELECT server_id, name, mode, tags, description, embed_json, invite_url, "
        "advertising_channel_id, status, last_bumped_at, created_at, bump_count "
        "FROM server_listings WHERE status = 'approved' ORDER BY last_bumped_at DESC";

    if (mysql_query(mysql_, query.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          ServerListing listing;
          listing.server_id = row[0] ? row[0] : "";
          listing.name = row[1] ? row[1] : "";
          listing.mode = row[2] ? row[2] : "disabled";
          listing.tags = row[3] ? row[3] : "";
          listing.description = row[4] ? row[4] : "";
          listing.embed_json = row[5] ? row[5] : "";
          listing.invite_url = row[6] ? row[6] : "";
          listing.advertising_channel_id = row[7] ? row[7] : "";
          listing.status = row[8] ? row[8] : "pending";
          listing.last_bumped_at = row[9] ? std::stoull(row[9]) : 0;
          listing.created_at = row[10] ? std::stoull(row[10]) : 0;
          listing.bump_count = row[11] ? std::stoi(row[11]) : 0;
          results.push_back(listing);
        }
        mysql_free_result(res);
      }
    }
    return results;
  }

  std::vector<ServerListing> Database::get_all_server_listings() {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::vector<ServerListing> results;
    if (!mysql_)
      return results;

    std::string query =
        "SELECT server_id, name, mode, tags, description, embed_json, invite_url, "
        "advertising_channel_id, status, last_bumped_at, created_at, bump_count "
        "FROM server_listings ORDER BY created_at DESC";

    if (mysql_query(mysql_, query.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          ServerListing listing;
          listing.server_id = row[0] ? row[0] : "";
          listing.name = row[1] ? row[1] : "";
          listing.mode = row[2] ? row[2] : "disabled";
          listing.tags = row[3] ? row[3] : "";
          listing.description = row[4] ? row[4] : "";
          listing.embed_json = row[5] ? row[5] : "";
          listing.invite_url = row[6] ? row[6] : "";
          listing.advertising_channel_id = row[7] ? row[7] : "";
          listing.status = row[8] ? row[8] : "pending";
          listing.last_bumped_at = row[9] ? std::stoull(row[9]) : 0;
          listing.created_at = row[10] ? std::stoull(row[10]) : 0;
          listing.bump_count = row[11] ? std::stoi(row[11]) : 0;
          results.push_back(listing);
        }
        mysql_free_result(res);
      }
    }
    return results;
  }

  std::optional<ApplicationSubmission> Database::get_application_submission_by_channel_id(const std::string &channel_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_ || channel_id.empty())
      return std::nullopt;
    std::string query =
        "SELECT id, server_id, panel_name, user_id, submitted_at, answers_json, "
        "status, output_message_id, ticket_channel_id FROM application_submissions "
        "WHERE ticket_channel_id = '" +
        escape_string(channel_id) + "'";
    if (mysql_query(mysql_, query.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        std::optional<ApplicationSubmission> sub_opt;
        if ((row = mysql_fetch_row(res))) {
          ApplicationSubmission sub;
          sub.id = row[0] ? std::stoi(row[0]) : 0;
          sub.server_id = row[1] ? row[1] : "";
          sub.panel_name = row[2] ? row[2] : "";
          sub.user_id = row[3] ? row[3] : "";
          sub.submitted_at = row[4] ? std::stoull(row[4]) : 0;
          sub.answers_json = row[5] ? row[5] : "";
          sub.status = row[6] ? row[6] : "pending";
          sub.output_message_id = row[7] ? row[7] : "";
          sub.ticket_channel_id = row[8] ? row[8] : "";
          sub_opt = sub;
        }
        mysql_free_result(res);
        return sub_opt;
      }
    }
    return std::nullopt;
  }

} // namespace bronx
