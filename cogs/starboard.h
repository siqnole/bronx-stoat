#pragma once
#include "helpers.h"
#include "stoatpp/bot_module.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace bronx::starboard_helpers {
inline std::string clean_emoji(const std::string &emoji_arg) {
  std::string cleaned = emoji_arg;
  std::string pua = "\xee\x83\xa6";
  size_t pos;
  while ((pos = cleaned.find(pua)) != std::string::npos) {
    cleaned.erase(pos, pua.length());
  }
  if (cleaned.size() >= 28 && cleaned.front() == ':' && cleaned.back() == ':') {
    std::string inner = cleaned.substr(1, cleaned.size() - 2);
    if (inner.size() == 26 &&
        inner.find_first_not_of(
            "0123456789ABCDEFGHJKMNPQRSTVWXYZabcdefghjkmnpqrstvwxyz") ==
            std::string::npos) {
      return inner;
    }
  }
  return cleaned;
}

inline std::string display_emoji(const std::string &raw_emoji) {
  if (raw_emoji.size() == 26 &&
      raw_emoji.find_first_not_of(
          "0123456789ABCDEFGHJKMNPQRSTVWXYZabcdefghjkmnpqrstvwxyz") ==
          std::string::npos) {
    return ":" + raw_emoji + ":";
  }
  return raw_emoji;
}

// Helper: expand {emojis}, {emojis|template}, {emojis||sep}, {emojis|template|sep}
// emoji_counts: vector of (display_emoji, per-emoji count), zeroes already filtered by caller
inline std::string expand_emojis_token(
    const std::string &res_in,
    const std::vector<std::pair<std::string, int>> &emoji_counts) {
  std::string res = res_in;
  size_t p = 0;
  while ((p = res.find("{emojis", p)) != std::string::npos) {
    // Find closing } tracking brace depth so nested {emoji}/{count} don't confuse us
    int depth = 1;
    size_t q = p + 1;
    while (q < res.size() && depth > 0) {
      if (res[q] == '{') ++depth;
      else if (res[q] == '}') --depth;
      ++q;
    }
    if (depth != 0) { ++p; continue; } // malformed, skip

    // interior = everything between "{emojis" and "}"
    std::string interior = res.substr(p + 7, q - p - 8);

    // Split interior on '|' that are NOT inside {}
    std::vector<std::string> parts;
    std::string cur;
    int d = 0;
    for (char c : interior) {
      if      (c == '{') { ++d; cur += c; }
      else if (c == '}') { --d; cur += c; }
      else if (c == '|' && d == 0) { parts.push_back(cur); cur = ""; }
      else { cur += c; }
    }
    parts.push_back(cur);

    // Resolve template and separator
    // parts[0] always empty (the bit before the first | or the whole thing if no |)
    std::string entry_tmpl = "{emoji} {count}"; // default
    std::string sep        = " ";               // default
    if (parts.size() == 2) {
      // {emojis|template}  — one pipe: custom template, default sep
      if (!parts[1].empty()) entry_tmpl = parts[1];
    } else if (parts.size() >= 3) {
      // {emojis|template|sep}  — two pipes
      if (!parts[1].empty()) entry_tmpl = parts[1];
      sep = parts[2]; // sep may be empty string intentionally
    }

    // Build replacement by expanding entry_tmpl for each emoji
    std::string replacement;
    bool first = true;
    for (const auto &[em, cnt] : emoji_counts) {
      if (cnt == 0) continue;
      if (!first) replacement += sep;
      first = false;
      std::string entry = entry_tmpl;
      size_t ep;
      while ((ep = entry.find("{emoji}")) != std::string::npos)
        entry.replace(ep, 7, em);
      while ((ep = entry.find("{count}")) != std::string::npos)
        entry.replace(ep, 7, std::to_string(cnt));
      replacement += entry;
    }

    res.replace(p, q - p, replacement);
    p += replacement.size();
  }
  return res;
}

inline std::string resolve_tokens(const std::string &pattern,
                                  const stoatpp::models::Message &msg,
                                  const std::string &emoji, int count,
                                  const std::string &username,
                                  const std::string &image_url = "",
                                  const std::vector<std::pair<std::string,int>> &emoji_counts = {}) {
  std::string res = pattern;
  size_t pos;

  // Unescape literal "\n" to real newlines
  while ((pos = res.find("\\n")) != std::string::npos) {
    res.replace(pos, 2, "\n");
  }

  // {username}
  while ((pos = res.find("{username}")) != std::string::npos) {
    res.replace(pos, 10, username);
  }

  // {user}
  while ((pos = res.find("{user}")) != std::string::npos) {
    res.replace(pos, 6, "<@" + msg.author + ">");
  }

  // {channel}
  while ((pos = res.find("{channel}")) != std::string::npos) {
    res.replace(pos, 9, "<#" + msg.channel + ">");
  }

  // {count}
  while ((pos = res.find("{count}")) != std::string::npos) {
    res.replace(pos, 7, std::to_string(count));
  }

  // {emoji}
  while ((pos = res.find("{emoji}")) != std::string::npos) {
    res.replace(pos, 7, emoji);
  }

  // {emojis} / {emojis|template|sep}
  res = expand_emojis_token(res, emoji_counts);

  // {message_id}
  while ((pos = res.find("{message_id}")) != std::string::npos) {
    res.replace(pos, 12, msg.id);
  }

  // {jump_url}
  std::string jump =
      "https://app.revolt.chat/channel/" + msg.channel + "/" + msg.id;
  while ((pos = res.find("{jump_url}")) != std::string::npos) {
    res.replace(pos, 10, jump);
  }

  // {content}
  std::string content_str = msg.content ? *msg.content : "";
  while ((pos = res.find("{content}")) != std::string::npos) {
    res.replace(pos, 9, content_str);
  }

  // {message}
  while ((pos = res.find("{message}")) != std::string::npos) {
    res.replace(pos, 9, content_str);
  }

  // {image}
  while ((pos = res.find("{image}")) != std::string::npos) {
    res.replace(pos, 7, image_url);
  }

  return res;
}

inline std::string resolve_tokens(const std::string &pattern,
                                  const stoatpp::events::Message &msg,
                                  const std::string &emoji, int count,
                                  const std::string &username,
                                  const std::string &image_url = "",
                                  const std::vector<std::pair<std::string,int>> &emoji_counts = {}) {
  std::string res = pattern;
  size_t pos;

  // Unescape literal "\n" to real newlines
  while ((pos = res.find("\\n")) != std::string::npos) {
    res.replace(pos, 2, "\n");
  }

  // {username}
  while ((pos = res.find("{username}")) != std::string::npos) {
    res.replace(pos, 10, username);
  }

  // {user}
  while ((pos = res.find("{user}")) != std::string::npos) {
    res.replace(pos, 6, "<@" + msg.author.id + ">");
  }

  // {channel}
  while ((pos = res.find("{channel}")) != std::string::npos) {
    res.replace(pos, 9, "<#" + msg.channel_id + ">");
  }

  // {count}
  while ((pos = res.find("{count}")) != std::string::npos) {
    res.replace(pos, 7, std::to_string(count));
  }

  // {emoji}
  while ((pos = res.find("{emoji}")) != std::string::npos) {
    res.replace(pos, 7, emoji);
  }

  // {emojis} / {emojis|template|sep}
  res = expand_emojis_token(res, emoji_counts);

  // {message_id}
  while ((pos = res.find("{message_id}")) != std::string::npos) {
    res.replace(pos, 12, msg.id);
  }

  // {jump_url}
  std::string jump =
      "https://app.revolt.chat/channel/" + msg.channel_id + "/" + msg.id;
  while ((pos = res.find("{jump_url}")) != std::string::npos) {
    res.replace(pos, 10, jump);
  }

  // {content}
  while ((pos = res.find("{content}")) != std::string::npos) {
    res.replace(pos, 9, msg.content);
  }

  // {message}
  while ((pos = res.find("{message}")) != std::string::npos) {
    res.replace(pos, 9, msg.content);
  }

  // {image}
  while ((pos = res.find("{image}")) != std::string::npos) {
    res.replace(pos, 7, image_url);
  }

  return res;
}

inline void handle_reaction_change(stoatpp::cluster &bot,
                                   const std::string &channel_id_val,
                                   const std::string &message_id_val) {
  std::thread([bot_ptr = &bot, channel_id = channel_id_val,
               message_id = message_id_val]() {
    try {
      if (!g_db)
        return;

      auto res = bot_ptr->rest().get_message(channel_id, message_id);
      if (!res.success())
        return;

      auto msg = stoatpp::models::Message::from_json(res.body);

      // Resolve server ID
      std::string server_id = "";
      if (msg.raw.contains("server") && msg.raw["server"].is_string()) {
        server_id = msg.raw["server"].get<std::string>();
      } else {
        auto chan = bot_ptr->get_channel(channel_id);
        if (chan && chan->server) {
          server_id = *chan->server;
        }
      }
      if (server_id.empty())
        return;

      // Fetch all starboard boards for this server
      auto boards = g_db->get_starboards(server_id);
      if (boards.empty())
        return;

      for (const auto &board : boards) {
        if (board.channel_id.empty())
          continue; // Skip unconfigured boards

        // Parse configured emojis (comma-separated)
        std::vector<std::string> allowed_emojis;
        std::stringstream ss(board.emojis);
        std::string item;
        while (std::getline(ss, item, ',')) {
          if (!item.empty()) {
            allowed_emojis.push_back(item);
          }
        }

        // Count unique reactors (global) + per-emoji counts for {emojis} token
        std::unordered_set<std::string> unique_reactors;
        std::vector<std::pair<std::string,int>> emoji_counts; // (display_emoji, count)
        if (msg.raw.contains("reactions") && msg.raw["reactions"].is_object()) {
          for (const auto &emoji : allowed_emojis) {
            std::string cleaned = clean_emoji(emoji);
            std::string disp    = display_emoji(emoji);
            std::unordered_set<std::string> per_emoji_reactors;

            for (const auto &key : {cleaned, disp}) {
              if (msg.raw["reactions"].contains(key) &&
                  msg.raw["reactions"][key].is_array()) {
                for (const auto &user_id_val : msg.raw["reactions"][key]) {
                  if (user_id_val.is_string()) {
                    std::string reactor_id = user_id_val.get<std::string>();
                    if (!board.allow_self && reactor_id == msg.author) {
                      continue; // Skip self reaction
                    }
                    per_emoji_reactors.insert(reactor_id);
                    unique_reactors.insert(reactor_id);
                  }
                }
              }
            }

            int per_count = static_cast<int>(per_emoji_reactors.size());
            if (per_count > 0) // only include emojis with at least 1 reaction
              emoji_counts.push_back({disp, per_count});
          }
        }

        int count = static_cast<int>(unique_reactors.size());

        // Check if message is already starboarded
        std::string starboard_msg_id = g_db->get_starboard_message(
            server_id, board.board_name, message_id);

        // Format display emoji (first configured, for legacy {emoji} token)
        std::string display_em =
            allowed_emojis.empty() ? "⭐" : display_emoji(allowed_emojis[0]);

        bool should_post =
            (count >= board.threshold) ||
            (!starboard_msg_id.empty() && !board.delete_below_threshold);

        if (should_post) {
          bot_ptr->fetch_user(msg.author, [bot_ptr, board, server_id,
                                           message_id, starboard_msg_id,
                                           display_em, count, emoji_counts, msg](
                                              stoatpp::models::User author_user,
                                              bool user_ok) mutable {
            std::string username =
                user_ok ? author_user.username : "Unknown User";
            std::string avatar_url =
                user_ok ? author_user.avatar_url(bot_ptr->config().autumn_url)
                        : "";

            std::vector<std::string> attachment_ids;
            if (msg.raw.contains("attachments") &&
                msg.raw["attachments"].is_array()) {
              for (const auto &att : msg.raw["attachments"]) {
                if (att.is_object()) {
                  if (att.contains("_id") && att["_id"].is_string()) {
                    attachment_ids.push_back(att["_id"].get<std::string>());
                  } else if (att.contains("id") && att["id"].is_string()) {
                    attachment_ids.push_back(att["id"].get<std::string>());
                  }
                }
              }
            }

            std::string image_url = "";
            if (!attachment_ids.empty()) {
              image_url = bot_ptr->config().autumn_url + "/attachments/" + attachment_ids[0];
            }

            bool is_image_token_used = false;
            if (board.message_content.find("{image}") != std::string::npos) {
              is_image_token_used = true;
            }
            if (board.use_embed) {
              if (board.embed_title.find("{image}") != std::string::npos ||
                  board.embed_description.find("{image}") != std::string::npos) {
                is_image_token_used = true;
              }
            }

            // Build the payload
            stoatpp::models::MessagePayload payload;
            payload.content = resolve_tokens(board.message_content, msg,
                                             display_em, count, username, image_url, emoji_counts);

            if (board.use_embed) {
              // Build embed
              nlohmann::json embed = nlohmann::json::object();
              embed["colour"] = "#FFD700"; // gold color

              std::string emb_title = resolve_tokens(
                  board.embed_title, msg, display_em, count, username, image_url, emoji_counts);
              if (!emb_title.empty()) {
                embed["title"] = emb_title;
              }

              std::string emb_desc = resolve_tokens(
                  board.embed_description, msg, display_em, count, username, image_url, emoji_counts);
              if (!emb_desc.empty()) {
                embed["description"] = emb_desc;
              }

              // Add original footer
              nlohmann::json footer = nlohmann::json::object();
              footer["text"] = "Original: " + message_id;
              embed["footer"] = footer;

              nlohmann::json author_json = nlohmann::json::object();
              author_json["name"] = username;
              if (!avatar_url.empty()) {
                author_json["icon_url"] = avatar_url;
              }
              embed["author"] = author_json;

              if (!attachment_ids.empty()) {
                if (!is_image_token_used) {
                  embed["media"] = attachment_ids[0];
                  for (size_t i = 1; i < attachment_ids.size(); ++i) {
                    payload.attachments.push_back(attachment_ids[i]);
                  }
                } else {
                  for (size_t i = 1; i < attachment_ids.size(); ++i) {
                    payload.attachments.push_back(attachment_ids[i]);
                  }
                }
              }
              payload.embeds.push_back(embed);
            } else {
              if (!attachment_ids.empty()) {
                if (!is_image_token_used) {
                  for (const auto &att_id : attachment_ids) {
                    payload.attachments.push_back(att_id);
                  }
                } else {
                  for (size_t i = 1; i < attachment_ids.size(); ++i) {
                    payload.attachments.push_back(attachment_ids[i]);
                  }
                }
              }
            }

            if (starboard_msg_id.empty()) {
              // Send new message
              bot_ptr->send_message(
                  board.channel_id, payload,
                  [bot_ptr, server_id, board, message_id,
                   payload](stoatpp::models::Message res, bool ok) mutable {
                    if (ok) {
                      g_db->add_starboard_message(server_id, board.board_name,
                                                  message_id, res.id);
                    } else if (!payload.attachments.empty()) {
                      payload.attachments.clear();
                      bot_ptr->send_message(
                          board.channel_id, payload,
                          [server_id, board, message_id](
                              stoatpp::models::Message res2, bool ok2) {
                            if (ok2) {
                              g_db->add_starboard_message(server_id,
                                                          board.board_name,
                                                          message_id, res2.id);
                            }
                          });
                    }
                  });
            } else {
              // Edit existing message. If it fails, retry without attachments.
              // If that still fails, delete mapping and resend.
              bot_ptr->edit_message(
                  board.channel_id, starboard_msg_id, payload,
                  [bot_ptr, board, server_id, message_id, starboard_msg_id,
                   payload](stoatpp::models::Message res, bool ok) mutable {
                    if (!ok) {
                      if (!payload.attachments.empty()) {
                        payload.attachments.clear();
                        bot_ptr->edit_message(
                            board.channel_id, starboard_msg_id, payload,
                            [bot_ptr, board, server_id, message_id,
                             payload](stoatpp::models::Message res2,
                                      bool ok2) mutable {
                              if (!ok2) {
                                g_db->delete_starboard_message(
                                    server_id, board.board_name, message_id);
                                bot_ptr->send_message(
                                    board.channel_id, payload,
                                    [server_id, board,
                                     message_id](stoatpp::models::Message res3,
                                                 bool ok3) {
                                      if (ok3) {
                                        g_db->add_starboard_message(
                                            server_id, board.board_name,
                                            message_id, res3.id);
                                      }
                                    });
                              }
                            });
                      } else {
                        g_db->delete_starboard_message(
                            server_id, board.board_name, message_id);
                        bot_ptr->send_message(
                            board.channel_id, payload,
                            [server_id, board, message_id](
                                stoatpp::models::Message res3, bool ok3) {
                              if (ok3) {
                                g_db->add_starboard_message(
                                    server_id, board.board_name, message_id,
                                    res3.id);
                              }
                            });
                      }
                    }
                  });
            }
          });
        } else {
          // Count is below threshold
          if (!starboard_msg_id.empty()) {
            // Delete message and remove from database
            bot_ptr->delete_message(board.channel_id, starboard_msg_id);
            g_db->delete_starboard_message(server_id, board.board_name,
                                           message_id);
          }
        }
      }
    } catch (const std::exception &ex) {
      std::cout << "[bronx starboard error] Network/exception in "
                   "handle_reaction_change: "
                << ex.what() << "\n";
    } catch (...) {
      std::cout << "[bronx starboard error] Unknown exception in "
                   "handle_reaction_change\n";
    }
  }).detach();
}
} // namespace bronx::starboard_helpers

namespace bronx::cogs {

class StarboardModule : public stoatpp::bot_module {
public:
  void register_handlers(stoatpp::cluster &bot) override {
    bot.register_command(
        {"board",
         {"starboard"},
         "configure starboard settings (subcommands: list, create, delete, "
         "threshold, channel, emoji, self, message, preview, config).",
         "",
         {"[subcommand]", "[args...]"},
         [](stoatpp::cluster &bot, const stoatpp::events::Message &msg,
            const std::vector<std::string> &args) {
           if (msg.server_id.empty()) {
             bot.send_message(
                 msg.channel_id,
                 "" + bronx::emj_no + " this command can only be used inside a server.");
             return;
           }

           if (!g_db) {
             bot.send_message(msg.channel_id,
                              "" + bronx::emj_no + " database connection is not initialized.");
             return;
           }

           std::string pfx =
               msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
           if (args.empty()) {
             bot.send_message(
                 msg.channel_id,
                 "**starboard command usage:**\n"
                 "• `" +
                     pfx +
                     "board list` — list all starboard boards on the server\n"
                     "• `" +
                     pfx +
                     "board create [board]` — create a new board (defaults to "
                     "`default`)\n"
                     "• `" +
                     pfx +
                     "board delete [board]` — delete a board (defaults to "
                     "`default`)\n"
                     "• `" +
                     pfx +
                     "board [board] threshold <number>` — set threshold for "
                     "posts\n"
                     "• `" +
                     pfx +
                     "board [board] channel <#channel>` — set target channel "
                     "for posts\n"
                     "• `" +
                     pfx +
                     "board [board] emoji <emojis...>` — set allowed emoji(s) "
                     "for the board\n"
                     "• `" +
                     pfx +
                     "board [board] self <on|off>` — configure if "
                     "self-reactions count\n"
                     "• `" +
                     pfx +
                     "board [board] delete <on|off>` — configure if posts "
                     "going below threshold are deleted (default on)\n"
                     "• `" +
                     pfx +
                     "board [board] message` — view templates and list tokens\n"
                     "• `" +
                     pfx +
                     "board [board] message <key> <template>` — modify "
                     "content, title, description or toggle embed\n"
                     "• `" +
                     pfx +
                     "board [board] preview` — send a preview using the "
                     "command message\n"
                     "• `" +
                     pfx + "board [board] config` — show board settings");
             return;
           }

           // Parse board name and action
           std::string subcommand = "";
           std::string board_name = "default";
           std::vector<std::string> sub_args;

           if (args[0] == "list") {
             subcommand = "list";
           } else if (args[0] == "create") {
             subcommand = "create";
             if (args.size() > 1) {
               board_name = args[1];
             }
           } else if (args[0] == "delete") {
             if (args.size() > 1 &&
                 (args[1] == "on" || args[1] == "off" || args[1] == "true" ||
                  args[1] == "false" || args[1] == "1" || args[1] == "0")) {
               subcommand = "toggle_delete";
               board_name = "default";
               sub_args.push_back(args[1]);
             } else {
               subcommand = "delete";
               if (args.size() > 1) {
                 board_name = args[1];
               }
             }
           } else if (args[0] == "threshold" || args[0] == "channel" ||
                      args[0] == "emoji" || args[0] == "self" ||
                      args[0] == "config" || args[0] == "message" ||
                      args[0] == "preview") {
             subcommand = args[0];
             for (size_t i = 1; i < args.size(); ++i) {
               sub_args.push_back(args[i]);
             }
           } else {
             board_name = args[0];
             if (args.size() > 1) {
               if (args[1] == "delete") {
                 if (args.size() > 2 &&
                     (args[2] == "on" || args[2] == "off" ||
                      args[2] == "true" || args[2] == "false" ||
                      args[2] == "1" || args[2] == "0")) {
                   subcommand = "toggle_delete";
                   for (size_t i = 2; i < args.size(); ++i) {
                     sub_args.push_back(args[i]);
                   }
                 } else {
                   subcommand = "delete";
                 }
               } else if (args[1] == "threshold" || args[1] == "channel" ||
                          args[1] == "emoji" || args[1] == "self" ||
                          args[1] == "config" || args[1] == "message" ||
                          args[1] == "preview") {
                 subcommand = args[1];
                 for (size_t i = 2; i < args.size(); ++i) {
                   sub_args.push_back(args[i]);
                 }
               } else {
                 subcommand = "config";
               }
             } else {
               subcommand = "config";
             }
           }

           if (subcommand == "list") {
             auto list = g_db->get_starboards(msg.server_id);
             if (list.empty()) {
               bot.send_message(
                   msg.channel_id,
                   "ℹ️ No starboard boards configured on this server.");
               return;
             }

             stoatpp::models::MessagePayload payload;
             nlohmann::json embed = nlohmann::json::object();
             embed["title"] = "⭐ Starboard Boards List";
             embed["colour"] = "#FFD700";

             std::string desc =
                 "Here are the starboard boards configured on this server:\n\n";
             for (const auto &tc : list) {
               desc += "• **" + tc.board_name + "** — Channel: " +
                       (tc.channel_id.empty() ? "*None*"
                                              : "<#" + tc.channel_id + ">") +
                       " | Threshold: `" + std::to_string(tc.threshold) + "`\n";
             }
             embed["description"] = desc;
             payload.embeds.push_back(embed);
             bot.send_message(msg.channel_id, payload);
             return;
           }

           if (subcommand == "create") {
             if (g_db->create_starboard(msg.server_id, board_name)) {
               bot.send_message(msg.channel_id,
                                "" + bronx::emj_yes + " successfully created starboard board **" +
                                    board_name + "**!");
             } else {
               bot.send_message(msg.channel_id, "" + bronx::emj_no + " failed to create board.");
             }
             return;
           }

           if (subcommand == "delete") {
             if (g_db->delete_starboard(msg.server_id, board_name)) {
               bot.send_message(msg.channel_id,
                                "" + bronx::emj_yes + " successfully deleted starboard board **" +
                                    board_name + "**.");
             } else {
               bot.send_message(msg.channel_id, "" + bronx::emj_no + " failed to delete board.");
             }
             return;
           }

           // Verify if board exists
           auto board_opt = g_db->get_starboard(msg.server_id, board_name);
           if (!board_opt) {
             bot.send_message(msg.channel_id,
                              "" + bronx::emj_no + " starboard board `" + board_name +
                                  "` does not exist. Use `" + pfx +
                                  "board create " + board_name + "` first.");
             return;
           }

           if (subcommand == "threshold") {
             if (sub_args.empty()) {
               bot.send_message(
                   msg.channel_id,
                   "board **" + board_name + "** threshold is currently: **" +
                       std::to_string(board_opt->threshold) + "**.");
               return;
             }

             int val = 0;
             try {
               val = std::stoi(sub_args[0]);
             } catch (...) {
               bot.send_message(msg.channel_id,
                                "" + bronx::emj_no + " invalid threshold value. Must be a valid "
                                "positive number.");
               return;
             }

             if (val <= 0) {
               bot.send_message(msg.channel_id,
                                "" + bronx::emj_no + " threshold must be greater than 0.");
               return;
             }

             if (g_db->set_starboard_threshold(msg.server_id, board_name,
                                               val)) {
               bot.send_message(msg.channel_id,
                                "" + bronx::emj_yes + " successfully set threshold for board **" +
                                    board_name + "** to **" +
                                    std::to_string(val) + "**.");
             } else {
               bot.send_message(msg.channel_id,
                                "" + bronx::emj_no + " failed to update threshold.");
             }
             return;
           }

           if (subcommand == "channel") {
             if (sub_args.empty()) {
               bot.send_message(msg.channel_id,
                                "board **" + board_name +
                                    "** channel is currently: " +
                                    (board_opt->channel_id.empty()
                                         ? "_None_"
                                         : "<#" + board_opt->channel_id + ">") +
                                    ".");
               return;
             }

             std::string target_channel = sub_args[0];
             if (target_channel.rfind("<#", 0) == 0 &&
                 target_channel.back() == '>') {
               target_channel =
                   target_channel.substr(2, target_channel.size() - 3);
             }

             if (g_db->set_starboard_channel(msg.server_id, board_name,
                                             target_channel)) {
               bot.send_message(msg.channel_id,
                                "" + bronx::emj_yes + " successfully set channel for board **" +
                                    board_name + "** to <#" + target_channel +
                                    ">.");
             } else {
               bot.send_message(msg.channel_id, "" + bronx::emj_no + " failed to update channel.");
             }
             return;
           }

           if (subcommand == "emoji") {
             if (sub_args.empty()) {
               bot.send_message(
                   msg.channel_id,
                   "board **" + board_name +
                       "** emojis are currently: " + board_opt->emojis + ".");
               return;
             }

             std::string joined;
             for (size_t i = 0; i < sub_args.size(); ++i) {
               if (i > 0)
                 joined += ",";
               joined += starboard_helpers::clean_emoji(sub_args[i]);
             }

             if (g_db->set_starboard_emojis(msg.server_id, board_name,
                                            joined)) {
               std::string display_list;
               std::stringstream ss(joined);
               std::string item;
               while (std::getline(ss, item, ',')) {
                 if (!display_list.empty())
                   display_list += ", ";
                 display_list += starboard_helpers::display_emoji(item);
               }
               bot.send_message(msg.channel_id,
                                "" + bronx::emj_yes + " successfully set emojis for board **" +
                                    board_name + "** to: " + display_list);
             } else {
               bot.send_message(msg.channel_id, "" + bronx::emj_no + " failed to update emojis.");
             }
             return;
           }

           if (subcommand == "self") {
             if (sub_args.empty()) {
               bot.send_message(
                   msg.channel_id,
                   "board **" + board_name +
                       "** self-reactions are currently: **" +
                       (board_opt->allow_self ? "allowed" : "disallowed") +
                       "**.");
               return;
             }

             std::string val = sub_args[0];
             std::transform(val.begin(), val.end(), val.begin(),
                            [](unsigned char c) { return std::tolower(c); });

             bool allow = true;
             if (val == "off" || val == "false" || val == "0" || val == "no" ||
                 val == "disabled") {
               allow = false;
             } else if (val == "on" || val == "true" || val == "1" ||
                        val == "yes" || val == "enabled") {
               allow = true;
             } else {
               bot.send_message(msg.channel_id,
                                "" + bronx::emj_no + " invalid self setting. Use `on` or `off`.");
               return;
             }

             if (g_db->set_starboard_self(msg.server_id, board_name, allow)) {
               bot.send_message(
                   msg.channel_id,
                   "" + bronx::emj_yes + " successfully set self-reactions for board **" +
                       board_name + "** to **" + (allow ? "on" : "off") +
                       "**.");
             } else {
               bot.send_message(msg.channel_id,
                                "" + bronx::emj_no + " failed to update self-reactions setting.");
             }
             return;
           }

           if (subcommand == "toggle_delete") {
             if (sub_args.empty()) {
               bot.send_message(
                   msg.channel_id,
                   "board **" + board_name +
                       "** delete-below-threshold is currently: **" +
                       (board_opt->delete_below_threshold ? "on" : "off") +
                       "**.");
               return;
             }

             std::string val = sub_args[0];
             std::transform(val.begin(), val.end(), val.begin(),
                            [](unsigned char c) { return std::tolower(c); });

             bool enable = true;
             if (val == "off" || val == "false" || val == "0" || val == "no" ||
                 val == "disabled") {
               enable = false;
             } else if (val == "on" || val == "true" || val == "1" ||
                        val == "yes" || val == "enabled") {
               enable = true;
             } else {
               bot.send_message(
                   msg.channel_id,
                   "" + bronx::emj_no + " invalid delete setting. Use `on` or `off`.");
               return;
             }

             if (g_db->set_starboard_delete_below_threshold(
                     msg.server_id, board_name, enable)) {
               bot.send_message(
                   msg.channel_id,
                   "" + bronx::emj_yes + " successfully set delete-below-threshold for board **" +
                       board_name + "** to **" + (enable ? "on" : "off") +
                       "**.");
             } else {
               bot.send_message(
                   msg.channel_id,
                   "" + bronx::emj_no + " failed to update delete-below-threshold setting.");
             }
             return;
           }

           if (subcommand == "message") {
             if (sub_args.empty()) {
               stoatpp::models::MessagePayload payload;
               nlohmann::json embed = nlohmann::json::object();
               embed["title"] = "board `" + board_name + "`";
               embed["colour"] = "#FFD700";
               std::string desc =
                   "Use `" + pfx + "board " + board_name +
                   " message <key> <template>` to modify.\n"
                   "**Keys**:\n"
                   "• `content` — The message content text outside the embed\n"
                   "• `title` — The embed title\n"
                   "• `description` — The embed description\n"
                   "• `embed` — `on` or `off` (enable/disable embed)\n"
                   "• `clear` — Reset all template values to defaults\n\n"
                   "**Available Tokens**:\n"
                   "• `{user}` — Mention of the original message author\n"
                   "• `{username}` — Plaintext username (works in titles)\n"
                   "• `{channel}` — Mention of the original channel\n"
                   "• `{count}` — Total unique reactors across all emojis\n"
                   "• `{emoji}` — First configured emoji (legacy)\n"
                   "• `{emojis}` — All emojis with counts, space-separated\n"
                   "  e.g. `:heart: 4 :star: 6`\n"
                   "• `{emojis|template}` — Custom per-entry format\n"
                   "  uses `{emoji}` and `{count}` inside the template\n"
                   "  e.g. `{emojis|{emoji} **{count}**}` → `:heart: **4** :star: **6**`\n"
                   "• `{emojis||sep}` — Custom separator, default template\n"
                   "  e.g. `{emojis||\\n}` → one emoji per line\n"
                   "• `{emojis|template|sep}` — Both custom\n"
                   "  e.g. `{emojis|**{count}** {emoji}| • }` → `**4** :heart: • **6** :star:`\n"
                   "• `{message_id}` — Original message ID\n"
                   "• `{jump_url}` — URL linking to the original message\n"
                   "• `{content}` — Content of the original message\n\n"
                   "**Current Configuration**:\n"
                   "• **Embed Enabled**: " +
                   std::string(board_opt->use_embed ? "Yes" : "No") +
                   "\n"
                   "• **Content Template**:\n```\n" +
                   board_opt->message_content +
                   "\n```\n"
                   "• **Embed Title Template**:\n```\n" +
                   (board_opt->embed_title.empty() ? "(none)"
                                                   : board_opt->embed_title) +
                   "\n```\n"
                   "• **Embed Description Template**:\n```\n" +
                   board_opt->embed_description + "\n```";
               embed["description"] = desc;
               payload.embeds.push_back(embed);
               bot.send_message(msg.channel_id, payload);
               return;
             }

             std::string key = sub_args[0];
             std::transform(key.begin(), key.end(), key.begin(),
                            [](unsigned char c) { return std::tolower(c); });

             if (key == "clear" || key == "reset") {
               bool ok1 = g_db->set_starboard_message_config(
                   msg.server_id, board_name, "content",
                   "{emoji} **{count}** | {channel} (ID: {message_id})");
               bool ok2 = g_db->set_starboard_message_config(
                   msg.server_id, board_name, "title", "");
               bool ok3 = g_db->set_starboard_message_config(
                   msg.server_id, board_name, "description",
                   "{content}\n\n[Jump to message]({jump_url})");
               bool ok4 = g_db->set_starboard_embed_enabled(msg.server_id,
                                                            board_name, true);
               if (ok1 && ok2 && ok3 && ok4) {
                 bot.send_message(msg.channel_id,
                                  "" + bronx::emj_yes + " successfully reset all templates to "
                                  "default settings for board **" +
                                      board_name + "**.");
               } else {
                 bot.send_message(msg.channel_id,
                                  "" + bronx::emj_no + " failed to reset templates.");
               }
               return;
             }

             if (key == "embed") {
               if (sub_args.size() < 2) {
                 bot.send_message(msg.channel_id,
                                  "" + bronx::emj_no + " Usage: `" + pfx + "board " + board_name +
                                      " message embed <on|off>`");
                 return;
               }
               std::string val = sub_args[1];
               std::transform(val.begin(), val.end(), val.begin(),
                              [](unsigned char c) { return std::tolower(c); });
               bool enable = true;
               if (val == "off" || val == "false" || val == "0" ||
                   val == "no" || val == "disabled") {
                 enable = false;
               }
               if (g_db->set_starboard_embed_enabled(msg.server_id, board_name,
                                                     enable)) {
                 bot.send_message(
                     msg.channel_id,
                     "" + bronx::emj_yes + " successfully " +
                         std::string(enable ? "enabled" : "disabled") +
                         " embed for board **" + board_name + "**.");
               } else {
                 bot.send_message(msg.channel_id,
                                  "" + bronx::emj_no + " failed to update embed setting.");
               }
               return;
             }

             if (key == "content" || key == "title" || key == "description") {
               if (sub_args.size() < 2) {
                 bot.send_message(msg.channel_id,
                                  "" + bronx::emj_no + " Usage: `" + pfx + "board " + board_name +
                                      " message " + key + " <template>`");
                 return;
               }

               std::string value;
               size_t words_to_skip = (args[0] == board_name) ? 4 : 3;
               size_t idx = 0;
               size_t word_count = 0;
               bool in_whitespace = true;

               while (idx < msg.content.length() &&
                      word_count < words_to_skip) {
                 char c = msg.content[idx];
                 bool is_ws = (c == ' ' || c == '\t' || c == '\r' || c == '\n');
                 if (in_whitespace) {
                   if (!is_ws) {
                     in_whitespace = false;
                     word_count++;
                   }
                 } else {
                   if (is_ws) {
                     in_whitespace = true;
                   }
                 }
                 if (word_count < words_to_skip) {
                   idx++;
                 }
               }

               while (idx < msg.content.length()) {
                 char c = msg.content[idx];
                 bool is_ws = (c == ' ' || c == '\t' || c == '\r' || c == '\n');
                 if (is_ws) {
                   break;
                 }
                 idx++;
               }

               while (idx < msg.content.length()) {
                 char c = msg.content[idx];
                 if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
                   break;
                 }
                 idx++;
               }

               if (idx < msg.content.length()) {
                 value = msg.content.substr(idx);
               }

               if (value == "clear" || value == "none") {
                 value = "";
               } else {
                 size_t npos = 0;
                 while ((npos = value.find("\\n", npos)) != std::string::npos) {
                   value.replace(npos, 2, "\n");
                   npos += 1;
                 }
               }

               if (g_db->set_starboard_message_config(msg.server_id, board_name,
                                                      key, value)) {
                 bot.send_message(msg.channel_id,
                                  "" + bronx::emj_yes + " successfully updated message template `" +
                                      key + "` for board **" + board_name +
                                      "**.");
               } else {
                 bot.send_message(msg.channel_id,
                                  "" + bronx::emj_no + " failed to update message template.");
               }
               return;
             }

             bot.send_message(
                 msg.channel_id,
                 "" + bronx::emj_no + " invalid message key. Valid keys are: `content`, `title`, "
                 "`description`, `embed`, `clear`.");
             return;
           }

           if (subcommand == "preview") {
             // Parse emojis
             std::vector<std::string> allowed_emojis;
             {
               std::stringstream ss(board_opt->emojis);
               std::string item;
               while (std::getline(ss, item, ',')) {
                 if (!item.empty()) allowed_emojis.push_back(item);
               }
             }
             std::string display_em =
                 allowed_emojis.empty()
                     ? "⭐"
                     : starboard_helpers::display_emoji(allowed_emojis[0]);

             // Build mock emoji_counts for preview (each emoji shown with count 5)
             std::vector<std::pair<std::string,int>> preview_emoji_counts;
             for (const auto &e : allowed_emojis)
               preview_emoji_counts.push_back({starboard_helpers::display_emoji(e), 5});

             bot.fetch_user(
                 msg.author.id,
                 [&bot, msg, display_em, board_opt, preview_emoji_counts](
                     stoatpp::models::User author_user, bool user_ok) mutable {
                   std::string username =
                       user_ok ? author_user.username : "Unknown User";
                   std::string avatar_url =
                       user_ok ? author_user.avatar_url(bot.config().autumn_url)
                               : "";

                   std::vector<std::string> attachment_ids;
                   if (msg.raw.contains("attachments") &&
                       msg.raw["attachments"].is_array()) {
                     for (const auto &att : msg.raw["attachments"]) {
                       if (att.is_object()) {
                         if (att.contains("_id") && att["_id"].is_string()) {
                           attachment_ids.push_back(att["_id"].get<std::string>());
                         } else if (att.contains("id") && att["id"].is_string()) {
                           attachment_ids.push_back(att["id"].get<std::string>());
                         }
                       }
                     }
                   }

                   std::string image_url = "";
                   if (!attachment_ids.empty()) {
                     image_url = bot.config().autumn_url + "/attachments/" + attachment_ids[0];
                   }

                   bool is_image_token_used = false;
                   if (board_opt->message_content.find("{image}") != std::string::npos) {
                     is_image_token_used = true;
                   }
                   if (board_opt->use_embed) {
                     if (board_opt->embed_title.find("{image}") != std::string::npos ||
                         board_opt->embed_description.find("{image}") != std::string::npos) {
                       is_image_token_used = true;
                     }
                   }

                   // Generate preview payload using the command message
                   stoatpp::models::MessagePayload payload;
                   payload.content = starboard_helpers::resolve_tokens(
                       board_opt->message_content, msg, display_em, 5,
                       username, image_url, preview_emoji_counts);

                   if (board_opt->use_embed) {
                     nlohmann::json embed = nlohmann::json::object();
                     embed["colour"] = "#FFD700";

                     std::string emb_title = starboard_helpers::resolve_tokens(
                         board_opt->embed_title, msg, display_em, 5, username, image_url, preview_emoji_counts);
                     if (!emb_title.empty()) {
                       embed["title"] = emb_title;
                     }

                     std::string emb_desc = starboard_helpers::resolve_tokens(
                         board_opt->embed_description, msg, display_em, 5,
                         username, image_url, preview_emoji_counts);
                     if (!emb_desc.empty()) {
                       embed["description"] = emb_desc;
                     }

                     nlohmann::json footer = nlohmann::json::object();
                     footer["text"] = "Original: " + msg.id;
                     embed["footer"] = footer;

                     nlohmann::json author_json = nlohmann::json::object();
                     author_json["name"] = username;
                     if (!avatar_url.empty()) {
                       author_json["icon_url"] = avatar_url;
                     }
                     embed["author"] = author_json;

                     if (!attachment_ids.empty()) {
                       if (!is_image_token_used) {
                         embed["media"] = attachment_ids[0];
                         for (size_t i = 1; i < attachment_ids.size(); ++i) {
                           payload.attachments.push_back(attachment_ids[i]);
                         }
                       } else {
                         for (size_t i = 1; i < attachment_ids.size(); ++i) {
                           payload.attachments.push_back(attachment_ids[i]);
                         }
                       }
                     }
                     payload.embeds.push_back(embed);
                   } else {
                     if (!attachment_ids.empty()) {
                       if (!is_image_token_used) {
                         for (const auto &att_id : attachment_ids) {
                           payload.attachments.push_back(att_id);
                         }
                       } else {
                         for (size_t i = 1; i < attachment_ids.size(); ++i) {
                           payload.attachments.push_back(attachment_ids[i]);
                         }
                       }
                     }
                   }

                   bot.send_message(msg.channel_id, payload);
                 });
             return;
           }

           if (subcommand == "config") {
             std::string display_list;
             std::stringstream ss(board_opt->emojis);
             std::string item;
             while (std::getline(ss, item, ',')) {
               if (!display_list.empty())
                 display_list += ", ";
               display_list += starboard_helpers::display_emoji(item);
             }

             stoatpp::models::MessagePayload payload;
             nlohmann::json embed = nlohmann::json::object();
             embed["title"] = "⭐ board settings: " + board_name;
             embed["colour"] = "#FFD700";

             std::string desc = "";
             desc += "• **Channel**: " +
                     (board_opt->channel_id.empty()
                          ? "_None_"
                          : "<#" + board_opt->channel_id + ">") +
                     "\n";
             desc += "• **Threshold**: **" +
                     std::to_string(board_opt->threshold) + "** reactions\n";
             desc += "• **Emojis**: " +
                     (display_list.empty() ? "_None_" : display_list) + "\n";
             desc += "• **Count Self Reactions**: **" +
                     std::string(board_opt->allow_self ? "Yes" : "No") + "**\n";
             desc +=
                 "• **Delete Below Threshold**: **" +
                 std::string(board_opt->delete_below_threshold ? "Yes" : "No") +
                 "**\n\n";
             desc += "**Template Configuration**:\n";
             desc += "• **Embed Enabled**: **" +
                     std::string(board_opt->use_embed ? "Yes" : "No") + "**\n";
             desc += "• **Content Template**: `" + board_opt->message_content +
                     "`\n";
             desc += "• **Embed Title Template**: `" +
                     (board_opt->embed_title.empty() ? "_None_"
                                                     : board_opt->embed_title) +
                     "`\n";
             desc += "• **Embed Description Template**: `" +
                     board_opt->embed_description + "`\n";

             embed["description"] = desc;
             payload.embeds.push_back(embed);
             bot.send_message(msg.channel_id, payload);
             return;
           }
         },
         "Starboard",
         stoatpp::permissions::ManageChannels, // Required permission
         0,
         {"list", "create [board]", "delete [board]",
          "[board] threshold <number>", "[board] channel <#channel>",
          "[board] emoji <emojis...>", "[board] self <on/off>",
          "[board] delete <on/off>", "[board] message",
          "[board] message <key> <val>", "[board] preview", "[board] config"}});

    // Gateway event handling for Starboards
    bot.on_message_react([&bot](const stoatpp::events::MessageReact &e) {
      try {
        starboard_helpers::handle_reaction_change(bot, e.channel_id, e.id);
      } catch (const std::exception &err) {
        std::cout << "[bronx starboard error] Exception in on_message_react: "
                  << err.what() << "\n";
      }
    });

    bot.on_message_unreact([&bot](const stoatpp::events::MessageUnreact &e) {
      try {
        starboard_helpers::handle_reaction_change(bot, e.channel_id, e.id);
      } catch (const std::exception &err) {
        std::cout << "[bronx starboard error] Exception in on_message_unreact: "
                  << err.what() << "\n";
      }
    });
  }
};

} // namespace bronx::cogs
