#pragma once
#include "../database.h"
#include "helpers.h"
#include "command_config.h"
#include "stoatpp/bot_module.h"
#include <algorithm>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bronx::cogs {

inline std::string truncate_str(const std::string &str, size_t max_len = 1900) {
  if (str.length() <= max_len)
    return str;
  return str.substr(0, max_len - 3) + "...";
}

inline void format_edited_diff(const std::string &old_text,
                               const std::string &new_text,
                               std::string &out_before,
                               std::string &out_after,
                               size_t max_full_len = 120,
                               size_t context_padding = 25) {
  if (old_text.empty() || new_text.empty() ||
      (old_text.size() <= max_full_len && new_text.size() <= max_full_len)) {
    out_before = truncate_str(old_text, 800);
    out_after  = truncate_str(new_text, 800);
    return;
  }

  size_t prefix_len = 0;
  size_t min_len = std::min(old_text.size(), new_text.size());
  while (prefix_len < min_len && old_text[prefix_len] == new_text[prefix_len]) {
    prefix_len++;
  }

  size_t suffix_len = 0;
  while (suffix_len < (min_len - prefix_len) &&
         old_text[old_text.size() - 1 - suffix_len] == new_text[new_text.size() - 1 - suffix_len]) {
    suffix_len++;
  }

  size_t start_idx = (prefix_len > context_padding) ? (prefix_len - context_padding) : 0;
  size_t old_end_idx = old_text.size() - ((suffix_len > context_padding) ? (suffix_len - context_padding) : 0);
  if (old_end_idx < start_idx) old_end_idx = old_text.size();

  size_t new_end_idx = new_text.size() - ((suffix_len > context_padding) ? (suffix_len - context_padding) : 0);
  if (new_end_idx < start_idx) new_end_idx = new_text.size();

  std::string old_snippet = old_text.substr(start_idx, old_end_idx - start_idx);
  std::string new_snippet = new_text.substr(start_idx, new_end_idx - start_idx);

  if (start_idx > 0) {
    old_snippet = "..." + old_snippet;
    new_snippet = "..." + new_snippet;
  }

  if (old_end_idx < old_text.size()) {
    old_snippet += "...";
  }
  if (new_end_idx < new_text.size()) {
    new_snippet += "...";
  }

  out_before = truncate_str(old_snippet, 800);
  out_after  = truncate_str(new_snippet, 800);
}

inline std::string format_log_placeholders(
    std::string text,
    const std::unordered_map<std::string, std::string> &placeholders) {
  auto replace_all = [](std::string &str, const std::string &from,
                        const std::string &to) {
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
      str.replace(start_pos, from.length(), to);
      start_pos += to.length();
    }
  };

  for (const auto &[key, val] : placeholders) {
    replace_all(text, "{" + key + "}", val);
  }
  return text;
}
inline std::string
reconstruct_message_text(stoatpp::cluster &bot, const nlohmann::json &msg_raw,
                         std::vector<std::string> &out_attachments) {
  std::string text = "";
  if (msg_raw.contains("content") && msg_raw["content"].is_string()) {
    text = msg_raw["content"].get<std::string>();
  }

  // Check standard attachments
  if (msg_raw.contains("attachments") && msg_raw["attachments"].is_array()) {
    for (const auto &att : msg_raw["attachments"]) {
      std::string att_id = "";
      if (att.is_string()) {
        att_id = att.get<std::string>();
      } else if (att.is_object()) {
        if (att.contains("_id") && att["_id"].is_string()) {
          att_id = att["_id"].get<std::string>();
        } else if (att.contains("id") && att["id"].is_string()) {
          att_id = att["id"].get<std::string>();
        }
      }
      if (!att_id.empty()) {
        out_attachments.push_back(att_id);
      }
    }
  }

  // Check embeds
  if (msg_raw.contains("embeds") && msg_raw["embeds"].is_array()) {
    for (const auto &emb : msg_raw["embeds"]) {
      if (!emb.is_object())
        continue;

      std::string embed_text = "";
      if (emb.contains("title") && emb["title"].is_string()) {
        embed_text += "> " + emb["title"].get<std::string>() + "\n";
      }
      if (emb.contains("description") && emb["description"].is_string()) {
        if (!embed_text.empty()) {
          embed_text += "> \n";
        }
        std::string desc = emb["description"].get<std::string>();
        size_t pos = 0;
        while (true) {
          size_t next = desc.find('\n', pos);
          std::string line = (next == std::string::npos)
                                 ? desc.substr(pos)
                                 : desc.substr(pos, next - pos);
          embed_text += "> " + line + "\n";
          if (next == std::string::npos)
            break;
          pos = next + 1;
        }
      }
      if (!embed_text.empty()) {
        if (!text.empty())
          text += "\n\n";
        text += embed_text;
      }

      // Extract images/media from embed to put in out_attachments
      std::string img_url = "";
      std::string media_id = "";

      if (emb.contains("media")) {
        if (emb["media"].is_string()) {
          media_id = emb["media"].get<std::string>();
        } else if (emb["media"].is_object()) {
          if (emb["media"].contains("_id") && emb["media"]["_id"].is_string()) {
            media_id = emb["media"]["_id"].get<std::string>();
          } else if (emb["media"].contains("id") &&
                     emb["media"]["id"].is_string()) {
            media_id = emb["media"]["id"].get<std::string>();
          }
        }
      }

      if (emb.contains("image") && emb["image"].is_object() &&
          emb["image"].contains("url") && emb["image"]["url"].is_string()) {
        img_url = emb["image"]["url"].get<std::string>();
      } else if (emb.contains("type") && emb["type"].is_string() &&
                 emb["type"].get<std::string>() == "Image" &&
                 emb.contains("url") && emb["url"].is_string()) {
        img_url = emb["url"].get<std::string>();
      }

      if (!media_id.empty()) {
        out_attachments.push_back(media_id);
      } else if (!img_url.empty()) {
        std::string uploaded_id = bot.rest().upload_from_url(img_url);
        if (!uploaded_id.empty()) {
          out_attachments.push_back(uploaded_id);
        }
      }
    }
  }

  return text;
}

inline void
dispatch_log(stoatpp::cluster &bot, const std::string &server_id,
             const std::string &event_type,
             const std::unordered_map<std::string, std::string> &placeholders,
             const std::function<std::pair<std::string, nlohmann::json>()>
                 &default_fallback,
             const std::vector<std::string> &log_attachments = {}) {
  if (!g_db)
    return;
  std::string log_chan = g_db->get_logging_channel(server_id, event_type);
  if (log_chan.empty())
    return;

  auto custom_opt = g_db->get_logging_embed(server_id, event_type);
  stoatpp::models::MessagePayload payload;

  if (custom_opt) {
    auto custom = *custom_opt;
    if (custom.message_type == "embed") {
      nlohmann::json emb_json = nlohmann::json::object();
      if (!custom.embed_json.empty()) {
        try {
          emb_json = nlohmann::json::parse(custom.embed_json);
        } catch (...) {
        }
      }

      if (!custom.content.empty()) {
        payload.content = format_log_placeholders(custom.content, placeholders);
      }

      nlohmann::json embed = nlohmann::json::object();
      if (emb_json.contains("title")) {
        embed["title"] = format_log_placeholders(
            emb_json["title"].get<std::string>(), placeholders);
      }
      if (emb_json.contains("description")) {
        embed["description"] = format_log_placeholders(
            emb_json["description"].get<std::string>(), placeholders);
      }
      if (emb_json.contains("color")) {
        std::string col = emb_json["color"].get<std::string>();
        if (col == "random") {
          char hex[8];
          snprintf(hex, sizeof(hex), "#%06x", rand() % 0xFFFFFF);
          col = hex;
        }
        embed["colour"] = col;
      }
      if (emb_json.contains("image")) {
        std::string img_url = emb_json["image"].get<std::string>();
        std::string desc = embed.contains("description")
                               ? embed["description"].get<std::string>()
                               : "";
        if (!desc.empty())
          desc += "\n";
        desc += "![](" + img_url + ")";
        embed["description"] = desc;
      }
      payload.embeds.push_back(embed);
    } else {
      payload.content = format_log_placeholders(custom.content, placeholders);
    }
  } else {
    auto [content, embed] = default_fallback();
    payload.content = content;
    payload.embeds.push_back(embed);
  }

  for (const auto &att : log_attachments) {
    payload.add_attachment(att);
  }

  for (auto &emb : payload.embeds) {
    if (emb.contains("description") && emb["description"].is_string()) {
      std::string desc = emb["description"].get<std::string>();
      if (desc.length() > 1900) {
        emb["description"] = desc.substr(0, 1897) + "...";
      }
    }
  }
  if (payload.content.empty()) {
    auto it = placeholders.find("jump_url");
    if (it != placeholders.end()) {
      payload.content = it->second;
    }
  }

  if (payload.content.length() > 1900) {
    payload.content = payload.content.substr(0, 1897) + "...";
  }

  // Run the webhook resolution and execution logic in a separate thread so it
  // doesn't block the gateway thread
  std::string server_id_copy = server_id;
  std::string event_type_copy = event_type;
  std::thread([&bot, server_id_copy, event_type_copy, log_chan, payload]() {
    bool sent_via_webhook = false;
    static std::unordered_set<std::string> g_no_webhook_channels;
    static std::mutex g_no_webhook_mutex;

    bool skip_webhook = false;
    {
      std::lock_guard<std::mutex> lock(g_no_webhook_mutex);
      if (g_no_webhook_channels.find(log_chan) != g_no_webhook_channels.end()) {
        skip_webhook = true;
      }
    }

    auto wh_opt =
        skip_webhook ? std::nullopt : g_db->get_logging_webhook(log_chan);
    std::string wh_id, wh_token;

    if (wh_opt) {
      wh_id = wh_opt->first;
      wh_token = wh_opt->second;
    } else if (!skip_webhook) {
      // Check if there is already a webhook named "Bronx Logger" in the channel
      auto get_res = bot.rest().get_channel_webhooks(log_chan);
      if (get_res.success()) {
        if (get_res.body.is_array() && !get_res.body.empty()) {
          for (const auto &item : get_res.body) {
            auto wh = stoatpp::models::Webhook::from_json(item);
            if (wh.name == "Bronx Logger" && wh.token) {
              wh_id = wh.id;
              wh_token = *wh.token;
              g_db->save_logging_webhook(log_chan, wh_id, wh_token);
              break;
            }
          }
        }
      } else if (get_res.status_code == 404) {
        // Channel does not exist! Evict configuration from DB
        std::cout << "[bronx logging] Evicting deleted log channel " << log_chan
                  << " for event " << event_type_copy << "\n";
        g_db->remove_logging_channel(server_id_copy, event_type_copy);
        return;
      } else if (get_res.status_code == 403) {
        // Missing ManageWebhooks permission — cache this so we don't spam 403
        // errors
        std::lock_guard<std::mutex> lock(g_no_webhook_mutex);
        g_no_webhook_channels.insert(log_chan);
      }

      if (wh_id.empty() && get_res.status_code != 403) {
        // No existing webhook found, create a new one
        auto create_res = bot.rest().create_webhook(log_chan, "Bronx Logger");
        if (create_res.success()) {
          auto wh = stoatpp::models::Webhook::from_json(create_res.body);
          if (wh.token) {
            wh_id = wh.id;
            wh_token = *wh.token;
            g_db->save_logging_webhook(log_chan, wh_id, wh_token);
          }
        } else if (create_res.status_code == 404) {
          // Channel does not exist! Evict configuration from DB
          std::cout << "[bronx logging] Evicting deleted log channel "
                    << log_chan << " for event " << event_type_copy << "\n";
          g_db->remove_logging_channel(server_id_copy, event_type_copy);
          return;
        } else if (create_res.status_code == 403) {
          // Missing ManageWebhooks permission — cache this so we don't spam 403
          // errors
          std::lock_guard<std::mutex> lock(g_no_webhook_mutex);
          g_no_webhook_channels.insert(log_chan);
        }
      }
    }

    if (!wh_id.empty() && !wh_token.empty()) {
      stoatpp::models::WebhookExecutePayload wh_payload;
      wh_payload.content = payload.content;
      wh_payload.username = "Bronx Logger";
      wh_payload.embeds = payload.embeds;

      auto exec_res = bot.rest().execute_webhook(wh_id, wh_token, wh_payload);
      if (exec_res.success()) {
        sent_via_webhook = true;
      } else if (exec_res.status_code == 404) {
        // Check if the channel is deleted or just the webhook
        auto ch_res = bot.rest().get_channel(log_chan);
        if (!ch_res.success() && ch_res.status_code == 404) {
          std::cout << "[bronx logging] Evicting deleted log channel "
                    << log_chan << " for event " << event_type_copy << "\n";
          g_db->remove_logging_channel(server_id_copy, event_type_copy);
          return;
        } else {
          // Webhook was deleted on the server, evict from DB cache
          g_db->remove_logging_webhook(log_chan);
        }
      }
    }

    if (!sent_via_webhook) {
      // Fallback to sending standard bot message
      bot.send_message(
          log_chan, payload,
          [&bot, server_id_copy, event_type_copy,
           log_chan](stoatpp::models::Message, bool success) {
            if (!success) {
              // Check if channel is deleted
              auto ch_res = bot.rest().get_channel(log_chan);
              if (!ch_res.success() && ch_res.status_code == 404) {
                std::cout << "[bronx logging] Evicting deleted log channel "
                          << log_chan << " for event " << event_type_copy
                          << "\n";
                g_db->remove_logging_channel(server_id_copy, event_type_copy);
              }
            }
          });
    }
  }).detach();
}

inline void
log_moderation_action(stoatpp::cluster &bot, const std::string &server_id,
                      const std::string &action, const std::string &mod_id,
                      const std::string &target_id, const std::string &reason,
                      const std::string &extra_details = "",
                      const std::string &channel_id = "") {
  // Persist to DB so .modlogs can query history
  if (g_db && !server_id.empty()) {
    g_db->record_moderation_action(server_id, action, mod_id, target_id, reason,
                                   extra_details, channel_id);
  }

  std::unordered_map<std::string, std::string> placeholders = {
      {"action", action},
      {"moderator", "<@" + mod_id + ">"},
      {"invokingUser", "<@" + mod_id + ">"},
      {"target", "<@" + target_id + ">"},
      {"affectedUser", "<@" + target_id + ">"},
      {"reason", reason.empty() ? "*No reason provided*" : reason},
      {"details", extra_details}};

  auto default_fallback = [action, placeholders]() {
    nlohmann::json embed = nlohmann::json::object();
    embed["title"] = "🛡️ Moderation Log: " + action;
    embed["colour"] = "#d12222"; // Premium dark red

    std::string desc = "• **Moderator:** " + placeholders.at("moderator") +
                       "\n"
                       "• **Target:** " +
                       placeholders.at("target") +
                       "\n"
                       "• **Reason:** " +
                       placeholders.at("reason") + "\n";
    std::string details = placeholders.at("details");
    if (!details.empty()) {
      desc += "• **Details:** " + details + "\n";
    }
    embed["description"] = desc;
    return std::make_pair("", embed);
  };

  dispatch_log(bot, server_id, "moderation", placeholders, default_fallback);
}

inline void
log_antiraid_action(stoatpp::cluster &bot, const std::string &server_id,
                    const std::string &action, const std::string &rule,
                    const std::string &user_id, const std::string &username,
                    const std::string &details) {
  std::unordered_map<std::string, std::string> placeholders = {
      {"action", action},
      {"rule", rule},
      {"user", "<@" + user_id + ">"},
      {"username", username},
      {"details", details}};

  auto default_fallback = [action, rule, placeholders]() {
    nlohmann::json embed = nlohmann::json::object();
    embed["title"] = "🛡️ AntiRaid Triggered: " + rule;
    embed["colour"] = "#d12222"; // Premium dark red

    std::string desc = "• **Action:** " + placeholders.at("action") +
                       "\n"
                       "• **User:** " +
                       placeholders.at("user") + " (" +
                       placeholders.at("username") +
                       ")\n"
                       "• **Details:** " +
                       placeholders.at("details") + "\n";
    embed["description"] = desc;
    return std::make_pair("", embed);
  };

  dispatch_log(bot, server_id, "antiraid", placeholders, default_fallback);
}

class LoggingModule : public stoatpp::bot_module {
private:
  std::unordered_map<std::string, stoatpp::events::Message> msg_cache_;
  std::deque<std::string> msg_ids_;
  std::mutex cache_mutex_;

  void cache_message(const stoatpp::events::Message &msg) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    msg_cache_[msg.id] = msg;
    msg_ids_.push_back(msg.id);
    if (msg_ids_.size() > 5000) {
      std::string oldest = msg_ids_.front();
      msg_ids_.pop_front();
      msg_cache_.erase(oldest);
    }
  }

  std::optional<stoatpp::events::Message>
  get_cached_message(const std::string &msg_id) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = msg_cache_.find(msg_id);
    if (it != msg_cache_.end())
      return it->second;
    return std::nullopt;
  }

  void evict_message(const std::string &msg_id) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    msg_cache_.erase(msg_id);
  }

public:
  static std::unordered_map<std::string, std::string>
  parse_pairs(const std::string &text) {
    std::unordered_map<std::string, std::string> pairs;
    std::vector<std::string> keys = {
        "title:", "description:", "desc:",   "colour:",
        "color:", "image:",       "content:"};

    struct KeyPos {
      std::string key;
      size_t pos;
    };
    std::vector<KeyPos> occurrences;

    for (const auto &key : keys) {
      size_t pos = 0;
      while ((pos = text.find(key, pos)) != std::string::npos) {
        if (pos == 0 || std::isspace(text[pos - 1])) {
          occurrences.push_back({key, pos});
        }
        pos += key.length();
      }
    }

    std::sort(occurrences.begin(), occurrences.end(),
              [](const KeyPos &a, const KeyPos &b) { return a.pos < b.pos; });

    for (size_t i = 0; i < occurrences.size(); ++i) {
      size_t start_val = occurrences[i].pos + occurrences[i].key.length();
      size_t end_val =
          (i + 1 < occurrences.size()) ? occurrences[i + 1].pos : text.length();

      std::string val = text.substr(start_val, end_val - start_val);
      val.erase(0, val.find_first_not_of(" \t\r\n"));
      val.erase(val.find_last_not_of(" \t\r\n") + 1);

      if (!val.empty() && val.back() == ',') {
        val.pop_back();
        val.erase(val.find_last_not_of(" \t\r\n") + 1);
      }

      std::string normalized_key = occurrences[i].key;
      normalized_key.pop_back(); // remove colon
      if (normalized_key == "desc")
        normalized_key = "description";
      if (normalized_key == "colour")
        normalized_key = "color";

      pairs[normalized_key] = val;
    }

    return pairs;
  }

  static std::string get_raw_trailing_args(const std::string &content,
                                           const std::string &prefix,
                                           const std::string &command_name,
                                           const std::string &subcommand) {
    std::string search1 = prefix + command_name;
    std::string lower_content = content;
    std::transform(lower_content.begin(), lower_content.end(),
                   lower_content.begin(), ::tolower);

    std::string lower_search1 = search1;
    std::transform(lower_search1.begin(), lower_search1.end(),
                   lower_search1.begin(), ::tolower);

    size_t pos = lower_content.find(lower_search1);
    if (pos == std::string::npos)
      return "";

    pos += lower_search1.length();
    while (pos < content.length() && std::isspace(content[pos])) {
      pos++;
    }

    std::string lower_sub = subcommand;
    std::transform(lower_sub.begin(), lower_sub.end(), lower_sub.begin(),
                   ::tolower);

    if (pos + lower_sub.length() <= content.length() &&
        lower_content.compare(pos, lower_sub.length(), lower_sub) == 0) {
      pos += lower_sub.length();
      if (pos < content.length() && std::isspace(content[pos])) {
        pos++;
      }
      return content.substr(pos);
    }

    return "";
  }

  static std::string get_raw_event_trailing_args(
      const std::string &content, const std::string &prefix,
      const std::string &command_name, const std::string &subcommand,
      const std::string &event_param) {
    std::string raw_sub =
        get_raw_trailing_args(content, prefix, command_name, subcommand);
    if (raw_sub.empty())
      return "";

    std::string lower_sub = raw_sub;
    std::transform(lower_sub.begin(), lower_sub.end(), lower_sub.begin(),
                   ::tolower);

    std::string lower_param = event_param;
    std::transform(lower_param.begin(), lower_param.end(), lower_param.begin(),
                   ::tolower);

    size_t pos = lower_sub.find(lower_param);
    if (pos == std::string::npos)
      return "";

    pos += lower_param.length();
    while (pos < raw_sub.length() && std::isspace(raw_sub[pos])) {
      pos++;
    }
    return raw_sub.substr(pos);
  }

public:
  void register_handlers(stoatpp::cluster &bot) override {
    // --- LOG COMMAND ---
    auto handle_log_cmd = [this](stoatpp::cluster &bot,
                                 const stoatpp::events::Message &msg,
                                 const std::vector<std::string> &args) {
      std::cout << "[bronx command] handling log...\n";
      if (msg.server_id.empty()) {
        bot.send_message(
            msg.channel_id,
            "" + bronx::emj_warning + " This command can only be used in a server channel.");
        return;
      }

      if (!g_db) {
        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Database is unavailable.");
        return;
      }

      // Check permissions
      auto server_opt = bot.get_server(msg.server_id);
      if (!server_opt) {
        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to resolve server details.");
        return;
      }

      std::string pfx =
          msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
      if (args.empty()) {
        bot.send_message(
            msg.channel_id,
            "📋 **Logging Module Commands:**\n"
            "• `" +
                pfx +
                "log aio` - Auto-setup a Logging category and channels.\n"
                "• `" +
                pfx +
                "log create <category>` - Create preset log category & "
                "channel.\n"
                "• `" +
                pfx +
                "log channel <event> <#channel|none>` - Route event to "
                "channel.\n"
                "• `" +
                pfx +
                "log list` - Show current logging config.\n"
                "• `" +
                pfx +
                "log type <event> <text|embed>` - Set log type.\n"
                "• `" +
                pfx +
                "log message <event> <text...>` - Customize log message body.\n"
                "• `" +
                pfx +
                "log edit <event> <key:val...>` - Customize embed fields.\n"
                "• `" +
                pfx +
                "log json <event> <raw_json>` - Directly import embed "
                "JSON.\n\n");
        bot.send_message(
            msg.channel_id,
            "**Valid Event Types:**\n"
            "- `delete` / `message_delete` (Placeholders: `{author}`, "
            "`{username}`, `{channel}`, `{content}`, `{jump_url}`)\n"
            "- `edit` / `message_edit` (Placeholders: `{author}`, "
            "`{username}`, `{channel}`, `{before}`, `{after}`, `{jump_url}`)\n"
            "- `join` / `member_join` (Placeholders: `{user}`, `{username}`, "
            "`{membercount}`)\n"
            "- `leave` / `member_leave` (Placeholders: `{user}`, `{username}`, "
            "`{membercount}`)\n"
            "- `mod` / `moderation` (Placeholders: `{action}`, `{moderator}`, "
            "`{target}`, `{reason}`, `{details}`)\n"
            "- `user` / `user_update` (Placeholders: `{user}`, `{oldUser}`, "
            "`{newUser}`)\n"
            "- `antiraid` (Placeholders: `{action}`, `{rule}`, `{user}`, "
            "`{username}`, `{details}`)\n"
            "- `role_create` (Placeholders: `{role}`, `{roleName}`, "
            "`{roleId}`)\n"
            "- `role_delete` (Placeholders: `{roleName}`, `{roleId}`)\n"
            "- `role_update` (Placeholders: `{role}`, `{roleName}`, "
            "`{roleId}`, `{details}`)\n"
            "- `channel_create` (Placeholders: `{channel}`, `{channelName}`, "
            "`{channelId}`)\n"
            "- `channel_delete` (Placeholders: `{channelName}`, "
            "`{channelId}`)\n"
            "- `channel_update` (Placeholders: `{channel}`, `{channelName}`, "
            "`{channelId}`, `{details}`)\n"
            "- `webhook_update` (Placeholders: `{webhookId}`, `{channel}`, "
            "`{action}`)\n"
            "- `reaction_add` (Placeholders: `{user}`, `{username}`, "
            "`{channel}`, `{messageId}`, `{emoji}`, `{jump_url}`)\n"
            "- `reaction_remove` (Placeholders: `{user}`, `{username}`, "
            "`{channel}`, `{messageId}`, `{emoji}`, `{jump_url}`)\n"
            "- `server_update` (Placeholders: `{server}`, `{details}`)\n"
            "- `member_update` (Placeholders: `{user}`, `{username}`, "
            "`{details}`)");
        return;
      }

      std::string subcommand = args[0];
      std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(),
                     ::tolower);

      auto execute_cmd = [this, &bot, msg, pfx, args, subcommand,
                          server_opt]() {
        if (subcommand == "create") {
          if (args.size() < 2) {
            bot.send_message(
                msg.channel_id,
                "usage: `" + pfx +
                    "log create <moderation|utility|messages|members>`");
            return;
          }
          std::string cat = args[1];
          std::transform(cat.begin(), cat.end(), cat.begin(), ::tolower);

          std::string ch_name = "";
          std::vector<std::string> events;

          if (cat == "moderation" || cat == "mod") {
            ch_name = "moderation-logs";
            events = {"moderation"};
          } else if (cat == "utility" || cat == "server") {
            ch_name = "server-logs";
            events = {"role_create",    "role_delete",    "role_update",
                      "channel_create", "channel_delete", "channel_update",
                      "webhook_update", "server_update"};
          } else if (cat == "messages" || cat == "message" || cat == "msg") {
            ch_name = "message-logs";
            events = {"message_edit", "message_delete", "reaction_add",
                      "reaction_remove"};
          } else if (cat == "members" || cat == "member") {
            ch_name = "member-logs";
            events = {"member_join", "member_leave", "user_update",
                      "member_update"};
          } else {
            bot.send_message(msg.channel_id,
                             "" + bronx::emj_warning + " Invalid category. Choose from: `moderation`, "
                             "`utility`, `messages`, `members`.");
            return;
          }

          std::string server_id = msg.server_id;
          std::thread([&bot, server_id, msg, ch_name, events, cat]() {
            bot.send_message(msg.channel_id,
                             "⚙️ Creating log channel **" + ch_name + "**...");

            auto res = bot.rest().create_channel(server_id, "Text", ch_name);
            if (!res.success()) {
              bot.send_message(msg.channel_id,
                               "" + bronx::emj_warning + " Failed to create channel `" + ch_name +
                                   "`: " + std::to_string(res.status_code) +
                                   " — " + res.error_message());
              return;
            }

            std::string channel_id = res.body["_id"].get<std::string>();

            // Route events
            for (const auto &ev : events) {
              g_db->save_logging_channel(server_id, ev, channel_id);
            }

            // Pre-create the webhook
            auto create_res =
                bot.rest().create_webhook(channel_id, "Bronx Logger");
            if (create_res.success()) {
              auto wh = stoatpp::models::Webhook::from_json(create_res.body);
              if (wh.token) {
                g_db->save_logging_webhook(channel_id, wh.id, *wh.token);
              }
            }

            std::string success_msg = "" + bronx::emj_yes + " **Log Category Created!**\n"
                                      "• Channel: <#" +
                                      channel_id +
                                      ">\n"
                                      "• Category: **" +
                                      cat +
                                      "**\n"
                                      "• Logged Events: ";
            for (size_t i = 0; i < events.size(); ++i) {
              success_msg +=
                  "`" + events[i] + "`" + (i == events.size() - 1 ? "" : ", ");
            }
            bot.send_message(msg.channel_id, success_msg);
          }).detach();
          return;
        }

        if (subcommand == "aio") {
          std::string server_id = msg.server_id;
          std::thread([&bot, server_id, msg]() {
            bot.send_message(msg.channel_id,
                             "⚙️ **All-In-One Setup Initiated**\nCreating "
                             "logging channels...");

            auto res_edits =
                bot.rest().create_channel(server_id, "Text", "edits");
            auto res_deletes =
                bot.rest().create_channel(server_id, "Text", "deletions");
            auto res_joins =
                bot.rest().create_channel(server_id, "Text", "joins-leaves");
            auto res_mod =
                bot.rest().create_channel(server_id, "Text", "moderation");

            // Collect and report individual failures
            std::vector<std::string> failures;
            if (!res_edits.success())
              failures.push_back(
                  "edits: " + std::to_string(res_edits.status_code) + " — " +
                  res_edits.error_message());
            if (!res_deletes.success())
              failures.push_back(
                  "deletions: " + std::to_string(res_deletes.status_code) +
                  " — " + res_deletes.error_message());
            if (!res_joins.success())
              failures.push_back(
                  "joins-leaves: " + std::to_string(res_joins.status_code) +
                  " — " + res_joins.error_message());
            if (!res_mod.success())
              failures.push_back(
                  "moderation: " + std::to_string(res_mod.status_code) + " — " +
                  res_mod.error_message());

            if (!failures.empty()) {
              std::string err_msg = "" + bronx::emj_warning + " Failed to create log channels:\n";
              for (const auto &f : failures) {
                err_msg += "• " + f + "\n";
              }
              bot.send_message(msg.channel_id, err_msg);
              return;
            }

            std::string edits_id = res_edits.body["_id"].get<std::string>();
            std::string deletes_id = res_deletes.body["_id"].get<std::string>();
            std::string joins_id = res_joins.body["_id"].get<std::string>();
            std::string mod_id = res_mod.body["_id"].get<std::string>();

            g_db->save_logging_channel(server_id, "message_edit", edits_id);
            g_db->save_logging_channel(server_id, "message_delete", deletes_id);
            g_db->save_logging_channel(server_id, "member_join", joins_id);
            g_db->save_logging_channel(server_id, "member_leave", joins_id);
            g_db->save_logging_channel(server_id, "moderation", mod_id);

            bot.send_message(msg.channel_id,
                             "⚙️ Creating Logging category grouping...");

            bool category_created = false;
            auto srv_res = bot.rest().get_server(server_id);
            if (srv_res.success()) {
              nlohmann::json categories = nlohmann::json::array();
              if (srv_res.body.contains("categories") &&
                  srv_res.body["categories"].is_array()) {
                categories = srv_res.body["categories"];
              }

              const std::string alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
              std::string cat_id = "";
              for (int i = 0; i < 26; ++i)
                cat_id += alphabet[rand() % alphabet.length()];

              nlohmann::json new_cat = nlohmann::json::object();
              new_cat["id"] = cat_id;
              new_cat["title"] = "Logging";
              new_cat["channels"] = {edits_id, deletes_id, joins_id, mod_id};
              categories.push_back(new_cat);

              nlohmann::json edit_fields = nlohmann::json::object();
              edit_fields["categories"] = categories;
              auto edit_res = bot.rest().edit_server(server_id, edit_fields);
              if (edit_res.success()) {
                category_created = true;
              }
            }

            std::string complete_msg = "" + bronx::emj_yes + " **All-In-One Setup Complete!**\n";
            if (category_created) {
              complete_msg += "• Created category **Logging**.\n";
            } else {
              complete_msg += "" + bronx::emj_warning + " *Note: I couldn't group the channels into a "
                              "'Logging' category (this action is restricted "
                              "for bots on this server).* \n";
            }
            complete_msg += "• Created and configured log channels:\n"
                            "  - <#" +
                            edits_id +
                            "> ➔ Message Edits\n"
                            "  - <#" +
                            deletes_id +
                            "> ➔ Message Deletions\n"
                            "  - <#" +
                            joins_id +
                            "> ➔ Joins & Leaves\n"
                            "  - <#" +
                            mod_id + "> ➔ Moderation Logs";
            bot.send_message(msg.channel_id, complete_msg);
          }).detach();
          return;
        }

        if (subcommand == "list") {
          auto config = g_db->get_server_logging_config(msg.server_id);
          if (config.empty()) {
            bot.send_message(
                msg.channel_id,
                "ℹ️ No event log channels configured for this server.");
            return;
          }

          std::string out = "📋 **Server Logging Configuration:**\n";
          for (const auto &[ev_type, chan_id] : config) {
            out += "• `" + ev_type + "` ➔ <#" + chan_id + ">\n";
          }
          bot.send_message(msg.channel_id, out);
          return;
        }

        auto send_edit_help = [&bot, msg, pfx]() {
          bot.send_message(
              msg.channel_id,
              "usage: `" + pfx +
                  "log edit <event_type> key:value ...`\n"
                  "keys: `title`, `description`, `color`, `image`, `content`\n"
                  "Use `none` to clear a field.\n\n"
                  "**Available Tokens by Event:**\n"
                  "• **Moderation (`mod`):** `{action}`, `{moderator}`, "
                  "`{target}`, `{reason}`, `{details}`\n"
                  "• **Message Delete (`delete`):** `{author}`, `{username}`, "
                  "`{channel}`, `{content}`\n"
                  "• **Message Edit (`edit`):** `{author}`, `{username}`, "
                  "`{channel}`, `{before}`, `{after}`\n"
                  "• **Join/Leave (`join`, `leave`):** `{user}`, `{username}`, "
                  "`{membercount}`\n"
                  "• **User Update (`user`):** `{user}`, `{oldUser}`, "
                  "`{newUser}`\n"
                  "• **AntiRaid (`antiraid`):** `{action}`, `{rule}`, "
                  "`{user}`, `{username}`, `{details}`\n"
                  "• **Roles (`role_create`, `role_delete`, `role_update`):** "
                  "`{role}`, `{roleName}`, `{roleId}`, `{details}`\n"
                  "• **Channels (`channel_create`, `channel_delete`, "
                  "`channel_update`):** `{channel}`, `{channelName}`, "
                  "`{channelId}`, `{details}`\n"
                  "• **Webhooks (`webhook_update`):** `{webhookId}`, "
                  "`{channel}`, `{action}`\n"
                  "• **Reactions (`reaction_add`, `reaction_remove`):** "
                  "`{user}`, `{username}`, `{channel}`, `{messageId}`, "
                  "`{emoji}`\n"
                  "• **Server Update (`server_update`):** `{server}`, "
                  "`{details}`\n"
                  "• **Member Update (`member_update`):** `{user}`, "
                  "`{username}`, `{details}`\n\n"
                  "**Examples:**\n"
                  "• `" +
                  pfx +
                  "log edit delete title:🗑️ Message Deleted color:#ff3333 "
                  "description:**Author:** {author}\\n**Content:** {content}`\n"
                  "• `" +
                  pfx +
                  "log edit mod title:🛡️ {action} color:#ffa500 "
                  "content:<@{target}> was moderated`");
        };

        auto send_json_help = [&bot, msg, pfx]() {
          bot.send_message(
              msg.channel_id,
              "usage: `" + pfx +
                  "log json <event_type> <raw_json_embed>`\n"
                  "Imports a full embed JSON to use for event logging. "
                  "Supported fields: `title`, `description`, `color`, `image`, "
                  "`content`.\n\n"
                  "**Available Tokens by Event:**\n"
                  "• **Moderation (`mod`):** `{action}`, `{moderator}`, "
                  "`{target}`, `{reason}`, `{details}`\n"
                  "• **Message Delete (`delete`):** `{author}`, `{username}`, "
                  "`{channel}`, `{content}`\n"
                  "• **Message Edit (`edit`):** `{author}`, `{username}`, "
                  "`{channel}`, `{before}`, `{after}`\n"
                  "• **Join/Leave (`join`, `leave`):** `{user}`, `{username}`, "
                  "`{membercount}`\n"
                  "• **User Update (`user`):** `{user}`, `{oldUser}`, "
                  "`{newUser}`\n"
                  "• **AntiRaid (`antiraid`):** `{action}`, `{rule}`, "
                  "`{user}`, `{username}`, `{details}`\n"
                  "• **Roles (`role_create`, `role_delete`, `role_update`):** "
                  "`{role}`, `{roleName}`, `{roleId}`, `{details}`\n"
                  "• **Channels (`channel_create`, `channel_delete`, "
                  "`channel_update`):** `{channel}`, `{channelName}`, "
                  "`{channelId}`, `{details}`\n"
                  "• **Webhooks (`webhook_update`):** `{webhookId}`, "
                  "`{channel}`, `{action}`\n"
                  "• **Reactions (`reaction_add`, `reaction_remove`):** "
                  "`{user}`, `{username}`, `{channel}`, `{messageId}`, "
                  "`{emoji}`\n"
                  "• **Server Update (`server_update`):** `{server}`, "
                  "`{details}`\n"
                  "• **Member Update (`member_update`):** `{user}`, "
                  "`{username}`, `{details}`\n\n"
                  "**Example:**\n"
                  "```json\n"
                  "{\n"
                  "  \"title\": \"🗑️ Message Deleted in {channel}\",\n"
                  "  \"description\": \"**Author:** {author} "
                  "({username})\\n**Content:**\\n{content}\",\n"
                  "  \"color\": \"#ff3333\",\n"
                  "  \"content\": \"Alert: message deleted by {author}!\"\n"
                  "}\n"
                  "```");
        };

        if (args.size() < 2) {
          if (subcommand == "edit") {
            send_edit_help();
            return;
          }
          if (subcommand == "json") {
            send_json_help();
            return;
          }
          bot.send_message(msg.channel_id, "usage: `" + pfx + "log " +
                                               subcommand +
                                               " <event_type> [args...]`");
          return;
        }

        std::string ev = args[1];
        std::transform(ev.begin(), ev.end(), ev.begin(), ::tolower);

        if (subcommand == "channel" && ev == "all") {
          if (args.size() < 3) {
            bot.send_message(msg.channel_id,
                             "usage: `" + pfx +
                                 "log channel all <#channel|none>`");
            return;
          }

          std::string chan_resp = args[2];
          std::vector<std::string> all_events = {
              "message_delete",  "message_edit",   "member_join",
              "member_leave",    "moderation",     "user_update",
              "antiraid",        "role_create",    "role_delete",
              "role_update",     "channel_create", "channel_delete",
              "channel_update",  "webhook_update", "reaction_add",
              "reaction_remove", "server_update",  "member_update"};

          if (chan_resp == "none" || chan_resp == "clear") {
            bool success = true;
            for (const auto &event_type : all_events) {
              if (!g_db->remove_logging_channel(msg.server_id, event_type)) {
                success = false;
              }
            }
            if (success) {
              bot.send_message(msg.channel_id,
                               "" + bronx::emj_yes + " Disabled logging for all events.");
            } else {
              bot.send_message(msg.channel_id,
                               "" + bronx::emj_warning + " Failed to disable logging for some events.");
            }
            return;
          }

          std::string target_channel = "";
          if (chan_resp.rfind("<#", 0) == 0 && chan_resp.back() == '>') {
            target_channel = chan_resp.substr(2, chan_resp.size() - 3);
          } else {
            auto server_channels_res =
                bot.rest().get_server_channels(msg.server_id);
            if (server_channels_res.success() &&
                server_channels_res.body.is_array()) {
              for (const auto &c_json : server_channels_res.body) {
                auto c = stoatpp::models::Channel::from_json(c_json);
                if (c.id == chan_resp || c.name == chan_resp) {
                  target_channel = c.id;
                  break;
                }
              }
            }
            if (target_channel.empty()) {
              target_channel = chan_resp;
            }
          }

          if (target_channel.empty()) {
            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid channel name or ID.");
            return;
          }

          bool success = true;
          for (const auto &event_type : all_events) {
            if (!g_db->save_logging_channel(msg.server_id, event_type,
                                            target_channel)) {
              success = false;
            }
          }

          if (success) {
            bot.send_message(
                msg.channel_id,
                "" + bronx::emj_yes + " Logging for all events enabled in <#" + target_channel +
                    ">.\n"
                    "" + bronx::emj_warning + " Warning: this will be spammy if ur server is active, "
                    "otherwise its okay i guess, just not best practice imo");
          } else {
            bot.send_message(
                msg.channel_id,
                "" + bronx::emj_warning + " Failed to save logging configuration for some events.");
          }
          return;
        }

        if (ev == "delete")
          ev = "message_delete";
        else if (ev == "edit")
          ev = "message_edit";
        else if (ev == "join")
          ev = "member_join";
        else if (ev == "leave")
          ev = "member_leave";
        else if (ev == "mod")
          ev = "moderation";
        else if (ev == "user_update" || ev == "user" || ev == "user_edit")
          ev = "user_update";
        else if (ev == "role_create" || ev == "rolecreate")
          ev = "role_create";
        else if (ev == "role_delete" || ev == "roledelete")
          ev = "role_delete";
        else if (ev == "role_update" || ev == "roleedit" || ev == "roleupdate")
          ev = "role_update";
        else if (ev == "channel_create" || ev == "channelcreate")
          ev = "channel_create";
        else if (ev == "channel_delete" || ev == "channeldelete")
          ev = "channel_delete";
        else if (ev == "channel_update" || ev == "channeledit" ||
                 ev == "channelupdate")
          ev = "channel_update";
        else if (ev == "webhook_update" || ev == "webhook" ||
                 ev == "webhookupdate")
          ev = "webhook_update";
        else if (ev == "reaction_add" || ev == "reactionadd" || ev == "react")
          ev = "reaction_add";
        else if (ev == "reaction_remove" || ev == "reactionremove" ||
                 ev == "unreact")
          ev = "reaction_remove";
        else if (ev == "server_update" || ev == "serverupdate" ||
                 ev == "server")
          ev = "server_update";
        else if (ev == "member_update" || ev == "memberupdate" ||
                 ev == "member")
          ev = "member_update";

        if (ev != "message_delete" && ev != "message_edit" &&
            ev != "member_join" && ev != "member_leave" && ev != "moderation" &&
            ev != "user_update" && ev != "antiraid" && ev != "role_create" &&
            ev != "role_delete" && ev != "role_update" &&
            ev != "channel_create" && ev != "channel_delete" &&
            ev != "channel_update" && ev != "webhook_update" &&
            ev != "reaction_add" && ev != "reaction_remove" &&
            ev != "server_update" && ev != "member_update") {
          bot.send_message(
              msg.channel_id,
              "" + bronx::emj_warning + " Invalid event type. Choose from: `delete`, `edit`, `join`, "
              "`leave`, `mod`, `user`, `antiraid`, `role_create`, "
              "`role_delete`, `role_update`, `channel_create`, "
              "`channel_delete`, `channel_update`, `webhook_update`, "
              "`reaction_add`, `reaction_remove`, `server_update`, "
              "`member_update`.");
          return;
        }

        if (subcommand == "channel") {
          if (args.size() < 3) {
            bot.send_message(msg.channel_id,
                             "usage: `" + pfx +
                                 "log channel <event_type> <#channel|none>`");
            return;
          }

          std::string chan_resp = args[2];
          if (chan_resp == "none" || chan_resp == "clear") {
            if (g_db->remove_logging_channel(msg.server_id, ev)) {
              bot.send_message(msg.channel_id,
                               "" + bronx::emj_yes + " Disabled logging for `" + ev + "`.");
            } else {
              bot.send_message(msg.channel_id,
                               "" + bronx::emj_warning + " Failed to update configuration.");
            }
            return;
          }

          std::string target_channel = "";
          if (chan_resp.rfind("<#", 0) == 0 && chan_resp.back() == '>') {
            target_channel = chan_resp.substr(2, chan_resp.size() - 3);
          } else {
            auto server_channels_res =
                bot.rest().get_server_channels(msg.server_id);
            if (server_channels_res.success() &&
                server_channels_res.body.is_array()) {
              for (const auto &c_json : server_channels_res.body) {
                auto c = stoatpp::models::Channel::from_json(c_json);
                if (c.id == chan_resp || c.name == chan_resp) {
                  target_channel = c.id;
                  break;
                }
              }
            }
            if (target_channel.empty()) {
              target_channel = chan_resp;
            }
          }

          if (target_channel.empty()) {
            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid channel name or ID.");
            return;
          }

          if (g_db->save_logging_channel(msg.server_id, ev, target_channel)) {
            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Logging for `" + ev +
                                                 "` enabled in <#" +
                                                 target_channel + ">.");
          } else {
            bot.send_message(msg.channel_id,
                             "" + bronx::emj_warning + " Failed to save logging configuration.");
          }
          return;
        }

        if (subcommand == "type") {
          if (args.size() < 3) {
            bot.send_message(msg.channel_id,
                             "usage: `" + pfx +
                                 "log type <event_type> <text|embed>`");
            return;
          }

          std::string type = args[2];
          std::transform(type.begin(), type.end(), type.begin(), ::tolower);
          if (type != "text" && type != "embed") {
            bot.send_message(msg.channel_id,
                             "" + bronx::emj_warning + " Type must be `text` or `embed`.");
            return;
          }

          auto le = g_db->get_logging_embed(msg.server_id, ev)
                        .value_or(Database::LoggingEmbed{msg.server_id, ev});
          le.message_type = type;

          if (g_db->save_logging_embed(le)) {
            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Logging type for `" + ev +
                                                 "` set to **" + type + "**.");
          } else {
            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save configuration.");
          }
          return;
        }

        if (subcommand == "message") {
          std::string text = get_raw_event_trailing_args(
              msg.content, pfx, "log", "message", args[1]);
          if (text.empty()) {
            bot.send_message(msg.channel_id,
                             "usage: `" + pfx +
                                 "log message <event_type> <text...>`");
            return;
          }

          auto le = g_db->get_logging_embed(msg.server_id, ev)
                        .value_or(Database::LoggingEmbed{msg.server_id, ev});
          le.content = text;

          if (g_db->save_logging_embed(le)) {
            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Custom message content for `" +
                                                 ev +
                                                 "` updated successfully.");
          } else {
            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save configuration.");
          }
          return;
        }

        if (subcommand == "edit") {
          std::string text = get_raw_event_trailing_args(
              msg.content, pfx, "log", "edit", args[1]);
          if (text.empty()) {
            send_edit_help();
            return;
          }

          auto pairs = parse_pairs(text);
          if (pairs.empty()) {
            bot.send_message(msg.channel_id,
                             "" + bronx::emj_warning + " No valid key-value pairs found. Format must be "
                             "`key:value` (e.g. `title:Hello color:random`).");
            return;
          }

          auto le = g_db->get_logging_embed(msg.server_id, ev)
                        .value_or(Database::LoggingEmbed{msg.server_id, ev});
          nlohmann::json emb = nlohmann::json::object();
          if (!le.embed_json.empty()) {
            try {
              emb = nlohmann::json::parse(le.embed_json);
            } catch (...) {
            }
          }

          for (const auto &[key, val] : pairs) {
            if (key == "content") {
              if (val == "none" || val == "null" || val == "clear") {
                le.content = "";
              } else {
                le.content = val;
              }
            } else if (val == "none" || val == "null" || val == "clear") {
              emb.erase(key);
            } else {
              emb[key] = val;
            }
          }

          le.embed_json = emb.dump();
          if (g_db->save_logging_embed(le)) {
            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Custom embed fields for `" +
                                                 ev +
                                                 "` updated successfully.");
          } else {
            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save configuration.");
          }
          return;
        }

        if (subcommand == "json") {
          std::string text = get_raw_event_trailing_args(
              msg.content, pfx, "log", "json", args[1]);
          if (text.empty()) {
            send_json_help();
            return;
          }

          nlohmann::json emb;
          try {
            emb = nlohmann::json::parse(text);
          } catch (const std::exception &e) {
            bot.send_message(msg.channel_id,
                             "" + bronx::emj_warning + " Invalid JSON format: " + std::string(e.what()));
            return;
          }

          auto le = g_db->get_logging_embed(msg.server_id, ev)
                        .value_or(Database::LoggingEmbed{msg.server_id, ev});

          if (emb.contains("content") && emb["content"].is_string()) {
            le.content = emb["content"].get<std::string>();
            emb.erase("content");
          }

          if (emb.contains("colour")) {
            emb["color"] = emb["colour"];
            emb.erase("colour");
          }

          le.embed_json = emb.dump();
          if (g_db->save_logging_embed(le)) {
            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Custom embed for `" + ev +
                                                 "` imported successfully.");
          } else {
            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save configuration.");
          }
          return;
        }

        bot.send_message(msg.channel_id,
                         "" + bronx::emj_warning + " Unknown subcommand. Use `aio`, `channel`, `list`, "
                         "`type`, `message`, `edit`, or `json`.");
      };

      // Check permissions
      bot.fetch_member(
          msg.server_id, msg.author.id,
          [this, &bot, msg, execute_cmd,
           server_opt](stoatpp::models::Member member, bool success) {
            if (!success) {
              bot.send_message(msg.channel_id,
                               "failed to verify your server member settings.");
              return;
            }
            if (!is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, member) &&
                !is_user_command_allowed(bot, msg.server_id, "log", "logging", member)) {
              bot.send_message(msg.channel_id,
                               "permission error: you need the Manage Server "
                               "permission to configure event logging.");
              return;
            }
            execute_cmd();
          });
    };

    bot.register_command(
        {"log",
         {"logging", "logs"},
         "configure server event log channels and templates.",
         "",
         {"<subcommand>", "[args...]"},
         handle_log_cmd,
         "Logging",
         0,
         0,
         {"aio", "create <category>", "channel <event> <#channel|none>", "list",
          "type <event> <text|embed>", "message <event> <text...>",
          "edit <event> <key:val...>", "json <event> <raw_json>"}});

    // --- GATEWAY MESSAGE CACHE ---
    bot.on_message(
        [this](const stoatpp::events::Message &msg) { cache_message(msg); });

    // --- GATEWAY EVENT: MESSAGE DELETE ---
    bot.on_message_delete(
        [this, &bot](const stoatpp::events::MessageDelete &e) {
          auto cached_opt = get_cached_message(e.id);
          std::string server_id = "";
          if (cached_opt) {
            server_id = cached_opt->server_id;
          } else {
            auto chan_opt = bot.get_channel(e.channel_id);
            if (chan_opt) {
              server_id = chan_opt->server.value_or("");
            }
          }

          if (server_id.empty() || !g_db)
            return;

          std::string author_id = cached_opt ? cached_opt->author.id : "";
          std::string username = cached_opt ? cached_opt->author.username : "";
          std::string deleted_content = cached_opt ? cached_opt->content : "";
          std::string trunc_deleted = truncate_str(deleted_content, 1500);

          std::unordered_map<std::string, std::string> placeholders = {
              {"author", "<@" + author_id + ">"},
              {"username", username},
              {"channel", "<#" + e.channel_id + ">"},
              {"content", trunc_deleted},
              {"jump_url", "https://app.revolt.chat/channel/" + e.channel_id + "/" + e.id}};

          auto default_fallback = [e, author_id, username, trunc_deleted]() {
            nlohmann::json embed = nlohmann::json::object();
            embed["title"] = "🗑️ Message Deleted";
            embed["colour"] = "#ff4444"; // Red

            std::string desc = "• **Channel:** <#" + e.channel_id + ">\n";
            if (!author_id.empty()) {
              desc += "• **Author:** <@" + author_id + "> (" + username +
                      ")\n"
                      "• **Content:**\n" +
                      trunc_deleted + "\n";
            } else {
              desc += "• **Message ID:** `" + e.id + "` *(not cached)*\n";
            }
            embed["description"] = truncate_str(desc, 1900);
            return std::make_pair("", embed);
          };

          dispatch_log(bot, server_id, "message_delete", placeholders,
                       default_fallback);
          evict_message(e.id);
        });

    // --- GATEWAY EVENT: MESSAGE UPDATE (EDIT) ---
    bot.on_message_update([this,
                           &bot](const stoatpp::events::MessageUpdate &e) {
      if (!e.data.contains("content") && !e.data.contains("embeds") &&
          !e.data.contains("attachments"))
        return;

      auto cached_opt = get_cached_message(e.id);
      std::string server_id = "";
      if (cached_opt) {
        server_id = cached_opt->server_id;
      } else {
        auto chan_opt = bot.get_channel(e.channel_id);
        if (chan_opt) {
          server_id = chan_opt->server.value_or("");
        }
      }

      if (server_id.empty() || !g_db)
        return;

      std::thread([this, &bot, e, cached_opt, server_id]() {
        nlohmann::json new_raw = nlohmann::json::object();
        if (cached_opt) {
          new_raw = cached_opt->raw;
        }
        for (auto it = e.data.begin(); it != e.data.end(); ++it) {
          new_raw[it.key()] = it.value();
        }

        std::vector<std::string> old_attachments;
        std::string old_content =
            cached_opt ? reconstruct_message_text(bot, cached_opt->raw,
                                                  old_attachments)
                       : "";

        std::vector<std::string> new_attachments;
        std::string new_content =
            reconstruct_message_text(bot, new_raw, new_attachments);

        if (cached_opt && old_content == new_content &&
            old_attachments == new_attachments)
          return;

        std::string author_id = cached_opt ? cached_opt->author.id : "";
        std::string username = cached_opt ? cached_opt->author.username : "";

        std::string trunc_old;
        std::string trunc_new;
        format_edited_diff(old_content, new_content, trunc_old, trunc_new);

        std::unordered_map<std::string, std::string> placeholders = {
            {"author", "<@" + author_id + ">"},
            {"username", username},
            {"channel", "<#" + e.channel_id + ">"},
            {"before", trunc_old},
            {"after", trunc_new},
            {"jump_url", "https://app.revolt.chat/channel/" + e.channel_id + "/" + e.id}};

        auto default_fallback = [e, author_id, trunc_old, trunc_new]() {
          nlohmann::json embed = nlohmann::json::object();
          embed["title"] = "✏️ Message Edited";
          embed["colour"] = "#ffaa00"; // Orange

          // Build a GitHub-style diff block: one - line for old, one + line for new
          auto make_diff_block = [](const std::string& before, const std::string& after) -> std::string {
            // Replace newlines inside each snippet so they stay on one logical line
            auto flatten = [](std::string s) -> std::string {
              std::string out;
              for (char c : s) out += (c == '\n') ? ' ' : c;
              return out;
            };
            std::string b = flatten(before);
            std::string a = flatten(after);
            if (b.size() > 900) b = b.substr(0, 897) + "...";
            if (a.size() > 900) a = a.substr(0, 897) + "...";
            return "```diff\n- " + b + "\n+ " + a + "\n```";
          };

          std::string desc = "• **Channel:** <#" + e.channel_id + ">\n";
          if (!author_id.empty()) {
            desc += "• **Author:** <@" + author_id + ">\n";
            desc += make_diff_block(trunc_old, trunc_new) + "\n";
          } else {
            desc += "• **Message ID:** `" + e.id + "` *(not cached)*\n";
            desc += make_diff_block("", trunc_new) + "\n";
          }
          embed["description"] = truncate_str(desc, 1900);
          return std::make_pair("", embed);
        };

        dispatch_log(bot, server_id, "message_edit", placeholders,
                     default_fallback, new_attachments);

        if (cached_opt) {
          auto updated_cached = *cached_opt;
          if (new_raw.contains("content") && new_raw["content"].is_string()) {
            updated_cached.content = new_raw["content"].get<std::string>();
          }
          updated_cached.raw = new_raw;
          std::lock_guard<std::mutex> lock(cache_mutex_);
          msg_cache_[e.id] = updated_cached;
        } else {
          stoatpp::events::Message msg;
          msg.id = e.id;
          msg.channel_id = e.channel_id;
          msg.server_id = server_id;
          if (new_raw.contains("content") && new_raw["content"].is_string()) {
            msg.content = new_raw["content"].get<std::string>();
          }
          if (e.data.contains("author")) {
            msg.author.id = e.data["author"].get<std::string>();
          }
          msg.raw = new_raw;
          cache_message(msg);
        }
      }).detach();
    });

    // --- GATEWAY EVENT: SERVER MEMBER JOIN ---
    bot.on_server_member_join(
        [this, &bot](const stoatpp::events::ServerMemberJoin &e) {
          if (e.user_id == bot.current_user().id)
            return;
          if (!g_db)
            return;
          bot.get_member_count(
              e.server_id, [this, &bot, e](int count, bool count_ok) {
                int final_count = count_ok ? count : 0;

                auto usr_opt = bot.get_user(e.user_id);
                std::string username = usr_opt ? usr_opt->username : "";

                std::unordered_map<std::string, std::string> placeholders = {
                    {"user", "<@" + e.user_id + ">"},
                    {"username", username},
                    {"membercount", std::to_string(final_count)}};

                auto default_fallback = [e, final_count]() {
                  nlohmann::json embed = nlohmann::json::object();
                  embed["title"] = "📥 Member Joined";
                  embed["colour"] = "#44ff44"; // Green

                  std::string desc = "• **User:** <@" + e.user_id +
                                     ">\n"
                                     "• **User ID:** `" +
                                     e.user_id + "`\n";
                  if (final_count > 0) {
                    desc += "• **Total Members:** " +
                            std::to_string(final_count) + "\n";
                  }
                  embed["description"] = desc;
                  return std::make_pair("", embed);
                };

                dispatch_log(bot, e.server_id, "member_join", placeholders,
                             default_fallback);
              });
        });

    // --- GATEWAY EVENT: SERVER MEMBER LEAVE ---
    bot.on_server_member_leave(
        [this, &bot](const stoatpp::events::ServerMemberLeave &e) {
          if (e.user_id == bot.current_user().id)
            return;
          if (!g_db)
            return;
          bot.get_member_count(
              e.server_id, [this, &bot, e](int count, bool count_ok) {
                int final_count = count_ok ? count : 0;

                auto usr_opt = bot.get_user(e.user_id);
                std::string username = usr_opt ? usr_opt->username : "";

                std::unordered_map<std::string, std::string> placeholders = {
                    {"user", "<@" + e.user_id + ">"},
                    {"username", username},
                    {"membercount", std::to_string(final_count)}};

                auto default_fallback = [e, final_count]() {
                  nlohmann::json embed = nlohmann::json::object();
                  embed["title"] = "📤 Member Left";
                  embed["colour"] = "#888888"; // Grey

                  std::string desc = "• **User ID:** `" + e.user_id + "`\n";
                  if (final_count > 0) {
                    desc += "• **Total Members:** " +
                            std::to_string(final_count) + "\n";
                  }
                  embed["description"] = desc;
                  return std::make_pair("", embed);
                };

                dispatch_log(bot, e.server_id, "member_leave", placeholders,
                             default_fallback);
              });
        });

    // --- GATEWAY EVENT: USER UPDATE (USERNAME CHANGE) ---
    bot.on_user_update([&bot](const stoatpp::events::UserUpdate &e) {
      if (!e.data.contains("username") || !g_db)
        return;
      std::string new_username = e.data["username"].get<std::string>();

      auto old_user_opt = bot.get_user(e.id);
      if (!old_user_opt)
        return;

      std::string old_username = old_user_opt->username;
      if (old_username == new_username)
        return;

      std::thread([&bot, user_id = e.id, old_username, new_username]() {
        auto cached_servers = bot.get_servers();
        for (const auto &srv : cached_servers) {
          std::string log_chan =
              g_db->get_logging_channel(srv.id, "user_update");
          if (log_chan.empty())
            continue;

          auto res = bot.rest().get_server_member(srv.id, user_id);
          if (res.success()) {
            std::unordered_map<std::string, std::string> placeholders = {
                {"user", "<@" + user_id + ">"},
                {"oldUser", old_username},
                {"newUser", new_username}};

            auto default_fallback = [user_id, old_username, new_username]() {
              nlohmann::json embed = nlohmann::json::object();
              embed["title"] = "👤 User Updated";
              embed["colour"] = "#00aaff";
              embed["description"] = "• **User:** <@" + user_id +
                                     ">\n"
                                     "• **Old Username:** " +
                                     old_username +
                                     "\n"
                                     "• **New Username:** " +
                                     new_username;
              return std::make_pair("", embed);
            };

            dispatch_log(bot, srv.id, "user_update", placeholders,
                         default_fallback);
          }
        }
      }).detach();
    });

    // --- GATEWAY EVENT: ROLE CREATE ---
    bot.on_server_role_create(
        [this, &bot](const stoatpp::events::ServerRoleCreate &e) {
          if (!g_db)
            return;

          std::unordered_map<std::string, std::string> placeholders = {
              {"role", "<%" + e.role_id + ">"},
              {"roleName", e.role.name},
              {"roleId", e.role_id}};

          auto default_fallback = [e]() {
            nlohmann::json embed = nlohmann::json::object();
            embed["title"] = "🆕 Role Created";
            embed["colour"] = "#44ff44"; // Green
            embed["description"] = "• **Role:** <%" + e.role_id +
                                   ">\n"
                                   "• **Name:** " +
                                   e.role.name +
                                   "\n"
                                   "• **ID:** `" +
                                   e.role_id + "`";
            return std::make_pair("", embed);
          };

          dispatch_log(bot, e.server_id, "role_create", placeholders,
                       default_fallback);
        });

    // --- GATEWAY EVENT: ROLE DELETE ---
    bot.on_server_role_delete(
        [this, &bot](const stoatpp::events::ServerRoleDelete &e) {
          if (!g_db)
            return;

          auto srv = bot.get_server(e.server_id);
          std::string role_name = "Unknown Role";
          if (srv) {
            for (const auto &r : srv->roles) {
              if (r.id == e.role_id) {
                role_name = r.name;
                break;
              }
            }
          }

          std::unordered_map<std::string, std::string> placeholders = {
              {"roleName", role_name}, {"roleId", e.role_id}};

          auto default_fallback = [e, role_name]() {
            nlohmann::json embed = nlohmann::json::object();
            embed["title"] = "🗑️ Role Deleted";
            embed["colour"] = "#ff4444"; // Red
            embed["description"] = "• **Name:** " + role_name +
                                   "\n"
                                   "• **ID:** `" +
                                   e.role_id + "`";
            return std::make_pair("", embed);
          };

          dispatch_log(bot, e.server_id, "role_delete", placeholders,
                       default_fallback);
        });

    // --- GATEWAY EVENT: ROLE UPDATE ---
    bot.on_server_role_update([this, &bot](
                                  const stoatpp::events::ServerRoleUpdate &e) {
      if (!g_db)
        return;

      auto srv = bot.get_server(e.server_id);
      std::string old_name = "";
      std::string new_name = "";
      std::string old_colour = "";
      std::string new_colour = "";

      if (srv) {
        for (const auto &r : srv->roles) {
          if (r.id == e.role_id) {
            old_name = r.name;
            if (r.colour)
              old_colour = *r.colour;
            break;
          }
        }
      }

      std::string details = "";
      if (e.data.contains("name")) {
        new_name = e.data["name"].get<std::string>();
        details += "• **Name Change:** " +
                   (old_name.empty() ? "" : old_name + " ➔ ") + new_name + "\n";
      } else {
        new_name = old_name;
      }

      if (e.data.contains("colour")) {
        if (e.data["colour"].is_null()) {
          new_colour = "none";
        } else if (e.data["colour"].is_string()) {
          new_colour = e.data["colour"].get<std::string>();
        }
        details += "• **Color Change:** " +
                   (old_colour.empty() ? "none" : old_colour) + " ➔ " +
                   new_colour + "\n";
      }

      std::unordered_map<std::string, std::string> placeholders = {
          {"role", "<%" + e.role_id + ">"},
          {"roleName", new_name},
          {"roleId", e.role_id},
          {"details", details}};

      auto default_fallback = [e, new_name, details]() {
        nlohmann::json embed = nlohmann::json::object();
        embed["title"] = "✏️ Role Updated";
        embed["colour"] = "#ffa500"; // Orange
        embed["description"] = "• **Role:** <%" + e.role_id +
                               ">\n"
                               "• **Name:** " +
                               new_name +
                               "\n"
                               "• **ID:** `" +
                               e.role_id + "`\n" + details;
        return std::make_pair("", embed);
      };

      dispatch_log(bot, e.server_id, "role_update", placeholders,
                   default_fallback);
    });

    // --- GATEWAY EVENT: CHANNEL CREATE ---
    bot.on_channel_create(
        [this, &bot](const stoatpp::events::ChannelCreate &e) {
          if (!g_db || !e.channel.server || e.channel.server->empty())
            return;
          std::string server_id = *e.channel.server;
          std::string channel_name = e.channel.name.value_or("unnamed");

          std::unordered_map<std::string, std::string> placeholders = {
              {"channel", "<#" + e.channel.id + ">"},
              {"channelName", channel_name},
              {"channelId", e.channel.id}};

          auto default_fallback = [e, channel_name]() {
            nlohmann::json embed = nlohmann::json::object();
            embed["title"] = "🆕 Channel Created";
            embed["colour"] = "#44ff44"; // Green
            embed["description"] = "• **Channel:** <#" + e.channel.id +
                                   ">\n"
                                   "• **Name:** " +
                                   channel_name +
                                   "\n"
                                   "• **ID:** `" +
                                   e.channel.id + "`";
            return std::make_pair("", embed);
          };

          dispatch_log(bot, server_id, "channel_create", placeholders,
                       default_fallback);
        });

    // --- GATEWAY EVENT: CHANNEL DELETE ---
    bot.on_channel_delete(
        [this, &bot](const stoatpp::events::ChannelDelete &e) {
          if (!g_db)
            return;

          auto cached = bot.get_channel(e.id);
          std::string server_id = "";
          std::string channel_name = "Unknown Channel";
          if (cached) {
            channel_name = cached->name.value_or("unnamed");
            server_id = cached->server.value_or("");
          }
          if (server_id.empty())
            return;

          std::unordered_map<std::string, std::string> placeholders = {
              {"channelName", channel_name}, {"channelId", e.id}};

          auto default_fallback = [e, channel_name]() {
            nlohmann::json embed = nlohmann::json::object();
            embed["title"] = "🗑️ Channel Deleted";
            embed["colour"] = "#ff4444"; // Red
            embed["description"] = "• **Name:** " + channel_name +
                                   "\n"
                                   "• **ID:** `" +
                                   e.id + "`";
            return std::make_pair("", embed);
          };

          dispatch_log(bot, server_id, "channel_delete", placeholders,
                       default_fallback);
        });

    // --- GATEWAY EVENT: CHANNEL UPDATE ---
    bot.on_channel_update([this,
                           &bot](const stoatpp::events::ChannelUpdate &e) {
      if (!g_db)
        return;

      auto cached = bot.get_channel(e.id);
      if (!cached || !cached->server || cached->server->empty())
        return;
      std::string server_id = *cached->server;

      std::string old_name = cached->name.value_or("");
      std::string new_name = old_name;

      std::string details = "";
      if (e.data.contains("name")) {
        new_name = e.data["name"].get<std::string>();
        details += "• **Name Change:** " + old_name + " ➔ " + new_name + "\n";
      }
      if (e.data.contains("description")) {
        details += "• **Description Updated**\n";
      }

      std::unordered_map<std::string, std::string> placeholders = {
          {"channel", "<#" + e.id + ">"},
          {"channelName", new_name},
          {"channelId", e.id},
          {"details", details}};

      auto default_fallback = [e, new_name, details]() {
        nlohmann::json embed = nlohmann::json::object();
        embed["title"] = "✏️ Channel Updated";
        embed["colour"] = "#ffa500"; // Orange
        embed["description"] = "• **Channel:** <#" + e.id +
                               ">\n"
                               "• **Name:** " +
                               new_name +
                               "\n"
                               "• **ID:** `" +
                               e.id + "`\n" + details;
        return std::make_pair("", embed);
      };

      dispatch_log(bot, server_id, "channel_update", placeholders,
                   default_fallback);
    });

    // --- GATEWAY EVENT: WEBHOOK UPDATE ---
    bot.on_webhook_update([this,
                           &bot](const stoatpp::events::WebhookUpdate &e) {
      if (!g_db)
        return;

      auto cached_chan = bot.get_channel(e.channel_id);
      if (!cached_chan || !cached_chan->server || cached_chan->server->empty())
        return;
      std::string server_id = *cached_chan->server;

      std::unordered_map<std::string, std::string> placeholders = {
          {"webhookId", e.webhook_id},
          {"channel", "<#" + e.channel_id + ">"},
          {"action", e.type}};

      auto default_fallback = [e]() {
        nlohmann::json embed = nlohmann::json::object();
        std::string title_str = "⚓ Webhook Updated";
        std::string color_str = "#ffa500";
        if (e.type == "create") {
          title_str = "⚓ Webhook Created";
          color_str = "#44ff44";
        } else if (e.type == "delete") {
          title_str = "⚓ Webhook Deleted";
          color_str = "#ff4444";
        }
        embed["title"] = title_str;
        embed["colour"] = color_str;
        embed["description"] = "• **Webhook ID:** `" + e.webhook_id +
                               "`\n"
                               "• **Channel:** <#" +
                               e.channel_id +
                               ">\n"
                               "• **Action:** " +
                               e.type;
        return std::make_pair("", embed);
      };

      dispatch_log(bot, server_id, "webhook_update", placeholders,
                   default_fallback);
    });

    // --- GATEWAY EVENT: MESSAGE REACTION ADD ---
    bot.on_message_react([this, &bot](const stoatpp::events::MessageReact &e) {
      if (!g_db)
        return;
      if (e.user_id == bot.current_user().id)
        return;

      auto chan_opt = bot.get_channel(e.channel_id);
      if (!chan_opt || !chan_opt->server || chan_opt->server->empty())
        return;
      std::string server_id = *chan_opt->server;

      auto usr_opt = bot.get_user(e.user_id);
      std::string username = usr_opt ? usr_opt->username : "";

      std::unordered_map<std::string, std::string> placeholders = {
          {"user", "<@" + e.user_id + ">"},
          {"username", username},
          {"channel", "<#" + e.channel_id + ">"},
          {"messageId", e.id},
          {"emoji", e.emoji_id},
          {"jump_url", "https://app.revolt.chat/channel/" + e.channel_id + "/" + e.id}};

      auto default_fallback = [e, username]() {
        nlohmann::json embed = nlohmann::json::object();
        embed["title"] = "➕ Reaction Added";
        embed["colour"] = "#44ff44"; // Green
        embed["description"] = "• **User:** <@" + e.user_id + "> (" + username +
                               ")\n"
                               "• **Channel:** <#" +
                               e.channel_id +
                               ">\n"
                               "• **Message ID:** `" +
                               e.id +
                               "`\n"
                               "• **Emoji:** " +
                               e.emoji_id;
        return std::make_pair("", embed);
      };

      dispatch_log(bot, server_id, "reaction_add", placeholders,
                   default_fallback);
    });

    // --- GATEWAY EVENT: MESSAGE REACTION REMOVE ---
    bot.on_message_unreact([this,
                            &bot](const stoatpp::events::MessageUnreact &e) {
      if (!g_db)
        return;
      if (e.user_id == bot.current_user().id)
        return;

      auto chan_opt = bot.get_channel(e.channel_id);
      if (!chan_opt || !chan_opt->server || chan_opt->server->empty())
        return;
      std::string server_id = *chan_opt->server;

      auto usr_opt = bot.get_user(e.user_id);
      std::string username = usr_opt ? usr_opt->username : "";

      std::unordered_map<std::string, std::string> placeholders = {
          {"user", "<@" + e.user_id + ">"},
          {"username", username},
          {"channel", "<#" + e.channel_id + ">"},
          {"messageId", e.id},
          {"emoji", e.emoji_id},
          {"jump_url", "https://app.revolt.chat/channel/" + e.channel_id + "/" + e.id}};

      auto default_fallback = [e, username]() {
        nlohmann::json embed = nlohmann::json::object();
        embed["title"] = "➖ Reaction Removed";
        embed["colour"] = "#ff4444"; // Red
        embed["description"] = "• **User:** <@" + e.user_id + "> (" + username +
                               ")\n"
                               "• **Channel:** <#" +
                               e.channel_id +
                               ">\n"
                               "• **Message ID:** `" +
                               e.id +
                               "`\n"
                               "• **Emoji:** " +
                               e.emoji_id;
        return std::make_pair("", embed);
      };

      dispatch_log(bot, server_id, "reaction_remove", placeholders,
                   default_fallback);
    });

    // --- GATEWAY EVENT: SERVER UPDATE ---
    bot.on_server_update([this, &bot](const stoatpp::events::ServerUpdate &e) {
      if (!g_db)
        return;

      auto srv = bot.get_server(e.id);
      std::string server_name = srv ? srv->name : "Server";

      std::string details = "";
      if (e.data.contains("name")) {
        std::string old_name = srv ? srv->name : "";
        std::string new_name = e.data["name"].get<std::string>();
        details += "• **Name:** " + (old_name.empty() ? "" : old_name + " ➔ ") +
                   new_name + "\n";
        server_name = new_name;
      }
      if (e.data.contains("description")) {
        std::string old_desc =
            srv && srv->description ? *srv->description : "none";
        std::string new_desc = e.data["description"].is_null()
                                   ? "none"
                                   : e.data["description"].get<std::string>();
        details += "• **Description:** " + old_desc + " ➔ " + new_desc + "\n";
      }
      if (e.data.contains("system_channel")) {
        std::string old_sc = "none";
        if (srv && srv->raw.contains("system_channel") &&
            !srv->raw["system_channel"].is_null()) {
          old_sc = srv->raw["system_channel"].get<std::string>();
        }
        std::string new_sc = e.data["system_channel"].is_null()
                                 ? "none"
                                 : e.data["system_channel"].get<std::string>();
        details += "• **System Channel:** " + old_sc + " ➔ " + new_sc + "\n";
      }

      if (details.empty()) {
        details = "• Server settings modified.";
      }

      std::unordered_map<std::string, std::string> placeholders = {
          {"server", server_name}, {"details", details}};

      auto default_fallback = [server_name, details]() {
        nlohmann::json embed = nlohmann::json::object();
        embed["title"] = "🖥️ Server Settings Updated";
        embed["colour"] = "#ffa500"; // Orange
        embed["description"] = "• **Server:** " + server_name + "\n" + details;
        return std::make_pair("", embed);
      };

      dispatch_log(bot, e.id, "server_update", placeholders, default_fallback);
    });

    // --- GATEWAY EVENT: SERVER MEMBER UPDATE ---
    bot.on_server_member_update(
        [this, &bot](const stoatpp::events::ServerMemberUpdate &e) {
          if (!g_db)
            return;

          auto usr_opt = bot.get_user(e.user_id);
          std::string username = usr_opt ? usr_opt->username : "";

          std::string details = "";

          // Nickname changes
          if (e.data.contains("nickname")) {
            std::string new_nick = e.data["nickname"].is_null()
                                       ? "none"
                                       : e.data["nickname"].get<std::string>();
            details += "• **Nickname:** ➔ " + new_nick + "\n";
          }
          for (const auto &field : e.clear) {
            if (field == "Nickname") {
              details += "• **Nickname:** removed\n";
            }
          }

          // Role diff — GitHub-style diff block
          if (e.data.contains("roles") && e.data["roles"].is_array()) {
            // Build new role set from event
            std::unordered_set<std::string> new_roles;
            for (const auto &r : e.data["roles"]) {
              if (r.is_string()) new_roles.insert(r.get<std::string>());
            }

            // Get old role set from cached member
            std::unordered_set<std::string> old_roles;
            auto cached_member = bot.get_member(e.server_id, e.user_id);
            if (cached_member) {
              for (const auto &r : cached_member->roles) old_roles.insert(r);
            }

            // Build role name lookup from server cache
            auto srv = bot.get_server(e.server_id);
            auto role_name = [&](const std::string &rid) -> std::string {
              if (srv) {
                for (const auto &r : srv->roles) {
                  if (r.id == rid) return r.name;
                }
              }
              return rid; // fallback to ID
            };

            std::vector<std::string> added, removed;
            for (const auto &r : new_roles) {
              if (!old_roles.count(r)) added.push_back(role_name(r));
            }
            for (const auto &r : old_roles) {
              if (!new_roles.count(r)) removed.push_back(role_name(r));
            }

            if (!added.empty() || !removed.empty()) {
              std::string diff = "```diff\n";
              for (const auto &r : added)   diff += "+ " + r + "\n";
              for (const auto &r : removed) diff += "- " + r + "\n";
              diff += "```";
              details += "• **Roles:**\n" + diff + "\n";
            } else {
              details += "• **Roles updated**\n"; // no old-state cached
            }
          }

          if (details.empty())
            return;

          std::unordered_map<std::string, std::string> placeholders = {
              {"user", "<@" + e.user_id + ">"},
              {"username", username},
              {"details", details}};

          auto default_fallback = [e, username, details]() {
            nlohmann::json embed = nlohmann::json::object();
            embed["title"] = "👥 Member Updated";
            embed["colour"] = "#ffa500"; // Orange
            embed["description"] = "• **Member:** <@" + e.user_id + "> (" +
                                   username + ")\n" + details;
            return std::make_pair("", embed);
          };

          dispatch_log(bot, e.server_id, "member_update", placeholders,
                       default_fallback);
        });
  }
};

} // namespace bronx::cogs

namespace bronx::helpers {

inline void ban_user_with_links(stoatpp::cluster& bot, const std::string& server_id, const std::string& user_id, const std::string& reason, const std::string& moderator_id, std::function<void(bool)> callback = nullptr) {
    bot.ban_user(server_id, user_id, reason, [&bot, server_id, user_id, reason, moderator_id, callback](bool success) {
        if (success) {
            if (g_db) {
                std::thread([&bot, server_id, user_id, reason, moderator_id]() {
                    auto linked_servers = g_db->get_linked_ban_servers(server_id);
                    for (const auto& linked_id : linked_servers) {
                        bot.ban_user(linked_id, user_id, "[Linked Ban Sync] " + reason, [&bot, linked_id, user_id, reason, moderator_id](bool sync_success) {
                            if (sync_success) {
                                bronx::cogs::log_moderation_action(bot, linked_id, "Ban", moderator_id, user_id, "[Linked Ban Sync] " + reason);
                            }
                        });
                    }
                }).detach();
            }
        }
        if (callback) callback(success);
    });
}

inline void unban_user_with_links(stoatpp::cluster& bot, const std::string& server_id, const std::string& user_id, const std::string& moderator_id, std::function<void(bool)> callback = nullptr) {
    bot.unban_user(server_id, user_id, [&bot, server_id, user_id, moderator_id, callback](bool success) {
        if (success) {
            if (g_db) {
                std::thread([&bot, server_id, user_id, moderator_id]() {
                    auto linked_servers = g_db->get_linked_ban_servers(server_id);
                    for (const auto& linked_id : linked_servers) {
                        bot.unban_user(linked_id, user_id, [&bot, linked_id, user_id, moderator_id](bool sync_success) {
                            if (sync_success) {
                                bronx::cogs::log_moderation_action(bot, linked_id, "Unban", moderator_id, user_id, "[Linked Unban Sync]");
                            }
                        });
                    }
                }).detach();
            }
        }
        if (callback) callback(success);
    });
}

} // namespace bronx::helpers

