#include "database.h"
#include <mysql.h>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <string>
#include <vector>

namespace bronx {

bool Database::init_cache() {
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
        RRRule rule;
        rule.message_id = msg_id;
        rule.emoji_id = row[1];
        rule.role_id = row[2];
        rule.rule_type = row[3] ? row[3] : "add";

        auto it = rr_cache_.find(msg_id);
        if (it != rr_cache_.end()) {
          it->second.rules.push_back(rule);
        }
      }
      mysql_free_result(rr_rule_res);
    }

    // Load welcome_leave_configs
    std::string select_wl =
        "SELECT server_id, welcome_channel_id, welcome_type, welcome_content, "
        "welcome_embed, leave_channel_id, leave_type, leave_content, "
        "leave_embed, autorole_role_id, welcome_dm_type, welcome_dm_content, "
        "welcome_dm_embed FROM welcome_leave_configs";
    if (mysql_query(mysql_, select_wl.c_str()) == 0) {
      MYSQL_RES *wl_res = mysql_store_result(mysql_);
      if (wl_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> wl_lock(wl_cache_mutex_);
        while ((row = mysql_fetch_row(wl_res))) {
          WelcomeLeaveConfig wl;
          wl.server_id = row[0];
          if (row[1]) wl.welcome_channel_id = row[1];
          wl.welcome_type = row[2] ? row[2] : "text";
          if (row[3]) wl.welcome_content = row[3];
          if (row[4]) wl.welcome_embed = row[4];
          if (row[5]) wl.leave_channel_id = row[5];
          wl.leave_type = row[6] ? row[6] : "text";
          if (row[7]) wl.leave_content = row[7];
          if (row[8]) wl.leave_embed = row[8];
          if (row[9]) wl.autorole_role_id = row[9];
          wl.welcome_dm_type = row[10] ? row[10] : "none";
          if (row[11]) wl.welcome_dm_content = row[11];
          if (row[12]) wl.welcome_dm_embed = row[12];
          wl_cache_[wl.server_id] = wl;
        }
        mysql_free_result(wl_res);
      }
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
          std::string e_type = row[1];
          std::string c_id = row[2];
          log_cache_[s_id][e_type] = c_id;
        }
        mysql_free_result(log_res);
      }
    }

    // Load logging_embeds
    std::string select_log_embed = "SELECT server_id, event_type, "
                                   "message_type, content, embed_json FROM "
                                   "logging_embeds";
    if (mysql_query(mysql_, select_log_embed.c_str()) == 0) {
      MYSQL_RES *le_res = mysql_store_result(mysql_);
      if (le_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> le_lock(log_embed_cache_mutex_);
        while ((row = mysql_fetch_row(le_res))) {
          LoggingEmbed le;
          le.server_id = row[0];
          le.event_type = row[1];
          le.message_type = row[2] ? row[2] : "embed";
          if (row[3]) le.content = row[3];
          if (row[4]) le.embed_json = row[4];
          log_embed_cache_[le.server_id][le.event_type] = le;
        }
        mysql_free_result(le_res);
      }
    }

    // Load logging_webhooks
    std::string select_log_wh =
        "SELECT channel_id, webhook_id, webhook_token FROM logging_webhooks";
    if (mysql_query(mysql_, select_log_wh.c_str()) == 0) {
      MYSQL_RES *wh_res = mysql_store_result(mysql_);
      if (wh_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> wh_lock(webhook_cache_mutex_);
        while ((row = mysql_fetch_row(wh_res))) {
          std::string c_id = row[0];
          WebhookCacheEntry entry;
          entry.webhook_id = row[1];
          entry.webhook_token = row[2];
          webhook_cache_[c_id] = entry;
        }
        mysql_free_result(wh_res);
      }
    }

    // Load trusted_entities
    std::string select_trusted =
        "SELECT server_id, entity_type, entity_id FROM trusted_entities";
    if (mysql_query(mysql_, select_trusted.c_str()) == 0) {
      MYSQL_RES *t_res = mysql_store_result(mysql_);
      if (t_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> t_lock(trusted_cache_mutex_);
        while ((row = mysql_fetch_row(t_res))) {
          std::string s_id = row[0];
          std::string type = row[1];
          std::string id = row[2];
          trusted_cache_[s_id].insert(type + ":" + id);
        }
        mysql_free_result(t_res);
      }
    }

    // Load command_restrictions
    std::string select_restrictions =
        "SELECT server_id, target_type, target_name, restriction_type, "
        "entity_type, entity_id FROM command_restrictions";
    if (mysql_query(mysql_, select_restrictions.c_str()) == 0) {
      MYSQL_RES *r_res = mysql_store_result(mysql_);
      if (r_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> r_lock(restrictions_cache_mutex_);
        while ((row = mysql_fetch_row(r_res))) {
          RestrictionEntry e;
          e.server_id = row[0];
          e.target_type = row[1];
          e.target_name = row[2];
          e.restriction_type = row[3];
          e.entity_type = row[4];
          e.entity_id = row[5];
          restrictions_cache_[e.server_id].push_back(e);
        }
        mysql_free_result(r_res);
      }
    }

    // Load disabled_commands
    std::string select_disabled =
        "SELECT server_id, target_type, target_name FROM disabled_commands";
    if (mysql_query(mysql_, select_disabled.c_str()) == 0) {
      MYSQL_RES *d_res = mysql_store_result(mysql_);
      if (d_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> d_lock(disabled_cache_mutex_);
        while ((row = mysql_fetch_row(d_res))) {
          std::string s_id = row[0];
          std::string type = row[1];
          std::string name = row[2];
          disabled_cache_[s_id].insert(type + ":" + name);
        }
        mysql_free_result(d_res);
      }
    }

    // Load action_message_configs
    std::string select_action_msg =
        "SELECT server_id, action_type, message_type, content, embed_json FROM "
        "action_message_configs";
    if (mysql_query(mysql_, select_action_msg.c_str()) == 0) {
      MYSQL_RES *am_res = mysql_store_result(mysql_);
      if (am_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> am_lock(action_msg_cache_mutex_);
        while ((row = mysql_fetch_row(am_res))) {
          ActionMessageConfig amc;
          amc.server_id = row[0];
          amc.action_type = row[1];
          amc.message_type = row[2] ? row[2] : "text";
          if (row[3]) amc.content = row[3];
          if (row[4]) amc.embed_json = row[4];
          action_msg_cache_[amc.server_id][amc.action_type] = amc;
        }
        mysql_free_result(am_res);
      }
    }

    // Load antiraid_configs
    std::string select_ar =
        "SELECT server_id, joins_threshold_count, joins_threshold_seconds, "
        "joins_action, spam_threshold_count, spam_threshold_seconds, "
        "spam_action, default_profile_action, mentions_threshold_count, "
        "mentions_threshold_messages, mentions_action FROM antiraid_configs";
    if (mysql_query(mysql_, select_ar.c_str()) == 0) {
      MYSQL_RES *ar_res = mysql_store_result(mysql_);
      if (ar_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> ar_lock(ar_cache_mutex_);
        while ((row = mysql_fetch_row(ar_res))) {
          AntiRaidConfig cfg;
          cfg.server_id = row[0];
          cfg.joins_threshold_count = std::stoi(row[1]);
          cfg.joins_threshold_seconds = std::stoi(row[2]);
          cfg.joins_action = row[3] ? row[3] : "none";
          cfg.spam_threshold_count = std::stoi(row[4]);
          cfg.spam_threshold_seconds = std::stoi(row[5]);
          cfg.spam_action = row[6] ? row[6] : "none";
          cfg.default_profile_action = row[7] ? row[7] : "none";
          if (row[8]) cfg.mentions_threshold_count = std::stoi(row[8]);
          if (row[9]) cfg.mentions_threshold_messages = std::stoi(row[9]);
          if (row[10]) cfg.mentions_action = row[10];
          ar_cache_[cfg.server_id] = cfg;
        }
        mysql_free_result(ar_res);
      }
    }

    // Load honeypot_configs
    std::string select_hp =
        "SELECT server_id, channel_id, honeypot_message, disclaimer_message, "
        "action, message_triggers, react_triggers, enabled FROM honeypot_configs";
    if (mysql_query(mysql_, select_hp.c_str()) == 0) {
      MYSQL_RES *hp_res = mysql_store_result(mysql_);
      if (hp_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> hp_lock(hp_cache_mutex_);
        while ((row = mysql_fetch_row(hp_res))) {
          HoneypotConfig hp;
          hp.server_id = row[0];
          if (row[1]) hp.channel_id = row[1];
          if (row[2]) hp.honeypot_message = row[2];
          if (row[3]) hp.disclaimer_message = row[3];
          if (row[4]) hp.action = row[4];
          hp.message_triggers = row[5] ? (std::stoi(row[5]) != 0) : true;
          hp.react_triggers = row[6] ? (std::stoi(row[6]) != 0) : false;
          hp.enabled = row[7] ? (std::stoi(row[7]) != 0) : false;
          hp_cache_[hp.server_id] = hp;
        }
        mysql_free_result(hp_res);
      }
    }

    // Load recurring_messages
    std::string select_rm =
        "SELECT id, server_id, channel_id, name, message_type, content, "
        "embed_json, interval_seconds, last_sent, owner_tokens FROM "
        "recurring_messages";
    if (mysql_query(mysql_, select_rm.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> rm_lock(rm_cache_mutex_);
        while ((row = mysql_fetch_row(res))) {
          RecurringMessage rm;
          rm.id = std::stoi(row[0]);
          rm.server_id = row[1];
          rm.channel_id = row[2];
          rm.name = row[3];
          rm.message_type = row[4] ? row[4] : "text";
          if (row[5]) rm.content = row[5];
          if (row[6]) rm.embed_json = row[6];
          rm.interval_seconds = std::stoul(row[7]);
          rm.last_sent = std::stoull(row[8]);
          rm.owner_tokens = row[9] ? (std::stoi(row[9]) != 0) : false;

          rm_cache_[rm.id] = rm;
          rm_server_index_[rm.server_id].push_back(rm.id);
          rm_channel_index_[rm.channel_id] = rm.id;
        }
        mysql_free_result(res);
      }
    }

    // Load rss_feeds
    std::string select_rss =
        "SELECT id, server_id, url, channels, last_checked, seen_items, "
        "check_interval FROM rss_feeds";
    if (mysql_query(mysql_, select_rss.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> rss_lock(rss_cache_mutex_);
        while ((row = mysql_fetch_row(res))) {
          RssFeed feed;
          feed.id = std::stoi(row[0]);
          feed.server_id = row[1];
          feed.url = row[2];
          feed.channels = row[3] ? row[3] : "";
          feed.last_checked = std::stoull(row[4]);
          feed.seen_items = row[5] ? row[5] : "[]";
          feed.check_interval = row[6] ? std::stoul(row[6]) : 1200;

          rss_cache_[feed.id] = feed;
          rss_server_index_[feed.server_id].push_back(feed.id);
        }
        mysql_free_result(res);
      }
    }

    // Load ticket_configs
    std::string select_tc =
        "SELECT server_id, name, modal_channel_id, modal_message_id, "
        "modal_content, modal_embed_json, reaction_emoji, "
        "input_creation_enabled, naming_scheme, category_id, view_roles, "
        "view_users, view_permissions, ticket_welcome_content, "
        "ticket_welcome_embed_json, ticket_close_emoji, ticket_close_content, "
        "member_role_id, ticket_counter FROM ticket_configs";
    if (mysql_query(mysql_, select_tc.c_str()) == 0) {
      MYSQL_RES *tc_res = mysql_store_result(mysql_);
      if (tc_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> tc_lock(ticket_config_cache_mutex_);
        while ((row = mysql_fetch_row(tc_res))) {
          TicketConfig tc;
          tc.server_id = row[0];
          tc.name = row[1] ? row[1] : "default";
          if (row[2]) tc.modal_channel_id = row[2];
          if (row[3]) tc.modal_message_id = row[3];
          if (row[4]) tc.modal_content = row[4];
          if (row[5]) tc.modal_embed_json = row[5];
          tc.reaction_emoji = row[6] ? row[6] : "📩";
          tc.input_creation_enabled = row[7] ? (std::stoi(row[7]) != 0) : false;
          tc.naming_scheme = row[8] ? row[8] : "ticket-{number}";
          if (row[9]) tc.category_id = row[9];
          if (row[10]) tc.view_roles = row[10];
          if (row[11]) tc.view_users = row[11];
          if (row[12]) tc.view_permissions = row[12];
          if (row[13]) tc.ticket_welcome_content = row[13];
          if (row[14]) tc.ticket_welcome_embed_json = row[14];
          tc.ticket_close_emoji = row[15] ? row[15] : "🔒";
          if (row[16]) tc.ticket_close_content = row[16];
          if (row[17]) tc.member_role_id = row[17];
          tc.ticket_counter = row[18] ? std::stoi(row[18]) : 0;
          ticket_config_cache_[tc.server_id][tc.name] = tc;
        }
        mysql_free_result(tc_res);
      }
    }

    // Load active tickets
    std::string select_t =
        "SELECT channel_id, server_id, user_id, ticket_number, "
        "welcome_message_id, status, panel_name FROM tickets";
    if (mysql_query(mysql_, select_t.c_str()) == 0) {
      MYSQL_RES *t_res = mysql_store_result(mysql_);
      if (t_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> t_lock(ticket_cache_mutex_);
        while ((row = mysql_fetch_row(t_res))) {
          Ticket t;
          t.channel_id = row[0];
          t.server_id = row[1];
          t.user_id = row[2];
          t.ticket_number = std::stoi(row[3]);
          if (row[4]) t.welcome_message_id = row[4];
          t.status = row[5] ? row[5] : "open";
          t.panel_name = row[6] ? row[6] : "default";
          ticket_cache_[t.channel_id] = t;
        }
        mysql_free_result(t_res);
      }
    }

    // Load application_panels
    std::string select_app_panels =
        "SELECT server_id, name, trigger_command, trigger_emoji, modal_channel_id, "
        "modal_message_id, modal_content, modal_embed_json, output_channel_id, mode, "
        "view_roles, view_users, view_permissions, member_role_id, naming_scheme, "
        "welcome_content, close_emoji, require_role, require_role_id, questions, "
        "app_counter, role_to_give, role_to_give_id, end_message FROM application_panels";
    if (mysql_query(mysql_, select_app_panels.c_str()) == 0) {
      MYSQL_RES *ap_res = mysql_store_result(mysql_);
      if (ap_res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> ap_lock(app_panel_cache_mutex_);
        while ((row = mysql_fetch_row(ap_res))) {
          ApplicationPanel ap;
          std::string s_id = row[0] ? row[0] : "";
          std::string p_name = row[1] ? row[1] : "";
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
          afk.since = row[4] ? std::stoull(row[4]) : 0;
          afk.active_preset = row[5] ? row[5] : "default";
          user_afk_cache_[afk.user_id] = afk;
        }
        mysql_free_result(res);
      }
    }

    // Load user_afk_presets
    std::string select_presets = "SELECT user_id, preset_name, template_json FROM user_afk_presets";
    if (mysql_query(mysql_, select_presets.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> lock(user_afk_presets_cache_mutex_);
        while ((row = mysql_fetch_row(res))) {
          std::string uid = row[0] ? row[0] : "";
          std::string pname = row[1] ? row[1] : "";
          std::string tjson = row[2] ? row[2] : "";
          if (!uid.empty() && !pname.empty()) {
            user_afk_presets_cache_[uid][pname] = tjson;
          }
        }
        mysql_free_result(res);
      }
    }

    // Load roblox_links
    std::string select_roblox = "SELECT user_id, roblox_id, roblox_username FROM roblox_links";
    if (mysql_query(mysql_, select_roblox.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> lock(roblox_links_cache_mutex_);
        while ((row = mysql_fetch_row(res))) {
          RobloxLink link;
          link.user_id = row[0] ? row[0] : "";
          link.roblox_id = row[1] ? std::stoull(row[1]) : 0;
          link.roblox_username = row[2] ? row[2] : "";
          roblox_links_cache_[link.user_id] = link;
        }
        mysql_free_result(res);
      }
    }

    // Helper lambda for loading auto JSON arrays
    auto load_json_arr = [](const char *raw) -> std::vector<std::string> {
      if (!raw)
        return {};
      try {
        auto j = nlohmann::json::parse(raw);
        if (j.is_array())
          return j.get<std::vector<std::string>>();
      } catch (...) {
      }
      return {};
    };

    if (mysql_query(mysql_,
                    "SELECT id,server_id,name,keyword,match_type,message_type,"
                    "content,embed_json,allow_roles,allow_users,allow_channels,"
                    "ignore_roles,ignore_users,ignore_channels,actions "
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
          c.actions = row[14] ? row[14] : "";
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

    // Load masquerade_configs
    std::string select_masq =
        "SELECT server_id, avatar, colour, name, disabled, disable_user, "
        "disable_user_name, disable_user_avatar, disable_user_colour FROM "
        "masquerade_configs";
    if (mysql_query(mysql_, select_masq.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> lock(masquerade_cache_mutex_);
        while ((row = mysql_fetch_row(res))) {
          MasqueradeConfig cfg;
          cfg.server_id = row[0] ? row[0] : "";
          if (row[1]) cfg.avatar = row[1];
          if (row[2]) cfg.colour = row[2];
          if (row[3]) cfg.name = row[3];
          cfg.disabled = row[4] ? (std::stoi(row[4]) != 0) : false;
          cfg.disable_user = row[5] ? (std::stoi(row[5]) != 0) : false;
          cfg.disable_user_name = row[6] ? (std::stoi(row[6]) != 0) : false;
          cfg.disable_user_avatar = row[7] ? (std::stoi(row[7]) != 0) : false;
          cfg.disable_user_colour = row[8] ? (std::stoi(row[8]) != 0) : false;
          masquerade_cache_[cfg.server_id] = cfg;
        }
        mysql_free_result(res);
      }
    }

    // Load user_masquerades
    std::string select_user_masq = "SELECT user_id, avatar, colour, name FROM user_masquerades";
    if (mysql_query(mysql_, select_user_masq.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> lock(user_masq_cache_mutex_);
        while ((row = mysql_fetch_row(res))) {
          UserMasquerade um;
          um.user_id = row[0] ? row[0] : "";
          if (row[1]) um.avatar = row[1];
          if (row[2]) um.colour = row[2];
          if (row[3]) um.name = row[3];
          user_masq_cache_[um.user_id] = um;
        }
        mysql_free_result(res);
      }
    }

    // Load user_timezones
    std::string select_user_tz = "SELECT user_id, timezone FROM user_timezones";
    if (mysql_query(mysql_, select_user_tz.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> lock(user_timezones_cache_mutex_);
        while ((row = mysql_fetch_row(res))) {
          std::string uid = row[0] ? row[0] : "";
          std::string tz = row[1] ? row[1] : "";
          if (!uid.empty() && !tz.empty()) {
            user_timezones_cache_[uid] = tz;
          }
        }
        mysql_free_result(res);
      }
    }

    // Load user_languages
    std::string select_user_lang = "SELECT user_id, language FROM user_languages";
    if (mysql_query(mysql_, select_user_lang.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> lock(user_languages_cache_mutex_);
        while ((row = mysql_fetch_row(res))) {
          std::string uid = row[0] ? row[0] : "";
          std::string lang = row[1] ? row[1] : "";
          if (!uid.empty() && !lang.empty()) {
            user_languages_cache_[uid] = lang;
          }
        }
        mysql_free_result(res);
      }
    }

    // Load server_languages
    std::string select_srv_lang = "SELECT server_id, language FROM server_languages";
    if (mysql_query(mysql_, select_srv_lang.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> lock(server_languages_cache_mutex_);
        while ((row = mysql_fetch_row(res))) {
          std::string sid = row[0] ? row[0] : "";
          std::string lang = row[1] ? row[1] : "";
          if (!sid.empty() && !lang.empty()) {
            server_languages_cache_[sid] = lang;
          }
        }
        mysql_free_result(res);
      }
    }

    // Load bot_ignored_users
    std::string select_bot_ignored = "SELECT user_id FROM bot_ignored_users";
    if (mysql_query(mysql_, select_bot_ignored.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> lock(ignored_users_cache_mutex_);
        while ((row = mysql_fetch_row(res))) {
          std::string uid = row[0] ? row[0] : "";
          if (!uid.empty()) {
            ignored_users_cache_.insert(uid);
          }
        }
        mysql_free_result(res);
      }
    }

    // Load user_rank_card
    std::string select_urc = "SELECT user_id, style, bg_color, bg_image, bar_active, bar_dimmed, text_color, font_name FROM user_rank_card";
    if (mysql_query(mysql_, select_urc.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> lock(user_rank_card_cache_mutex_);
        while ((row = mysql_fetch_row(res))) {
          UserRankCard urc;
          urc.user_id = row[0] ? row[0] : "";
          urc.style = row[1] ? row[1] : "image";
          if (row[2]) urc.bg_color = row[2];
          if (row[3]) urc.bg_image = row[3];
          if (row[4]) urc.bar_active = row[4];
          if (row[5]) urc.bar_dimmed = row[5];
          if (row[6]) urc.text_color = row[6];
          if (row[7]) urc.font_name = row[7];
          user_rank_card_cache_[urc.user_id] = urc;
        }
        mysql_free_result(res);
      }
    }

    // Load tuppers
    std::string select_tuppers = "SELECT id, user_id, name, avatar_url, prefix, suffix FROM tuppers";
    if (mysql_query(mysql_, select_tuppers.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        std::lock_guard<std::shared_mutex> lock(tupper_cache_mutex_);
        while ((row = mysql_fetch_row(res))) {
          Tupper t;
          t.id = row[0] ? std::stoi(row[0]) : 0;
          t.user_id = row[1] ? row[1] : "";
          t.name = row[2] ? row[2] : "";
          t.avatar_url = row[3] ? row[3] : "";
          t.prefix = row[4] ? row[4] : "";
          t.suffix = row[5] ? row[5] : "";
          tupper_cache_[t.user_id].push_back(t);
        }
        mysql_free_result(res);
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

    reload_custom_commands("");

    return true;
}

} // namespace bronx
