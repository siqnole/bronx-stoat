#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include <regex>
#include <thread>
#include <httplib.h>

namespace bronx::cogs {

class EmojiModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
        auto callback = [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
            std::cout << "[bronx command] handling emoji steal...\n";
            if (msg.server_id.empty()) {
                bot.send_reply(msg, "this command can only be used within a server.", true);
                return;
            }

            if (args.empty()) {
                std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                bot.send_reply(msg, "usage: `" + pfx + "emoji <emoji1> [emoji2] ...`\n"
                                    "examples:\n"
                                    "• `" + pfx + "emoji <:smile:01...>`\n"
                                    "• `" + pfx + "steal <:smile:01...> <:heart:02...>`", true);
                return;
            }

            struct TargetEmoji {
                std::string name;
                std::string id;
                bool animated = false;
                std::string status = "pending...";
            };
            std::vector<TargetEmoji> targets;
            std::regex single_emoji_regex(R"(^(?:<a?:([a-zA-Z0-9_]{1,32}):([0-9a-zA-Z]{26})>|:([0-9a-zA-Z]{26}):)$)");
            std::smatch match;

            for (const auto& arg : args) {
                if (std::regex_match(arg, match, single_emoji_regex)) {
                    TargetEmoji te;
                    if (!match[3].str().empty()) {
                        te.id = match[3].str();
                        te.name = ""; // Resolve dynamically
                        te.animated = false;
                    } else {
                        te.name = match[1].str();
                        te.id = match[2].str();
                        te.animated = !match[0].str().empty() && match[0].str()[1] == 'a';
                    }
                    targets.push_back(te);
                } else {
                    if (!targets.empty() && !arg.empty()) {
                        std::string clean_name = "";
                        for (char c : arg) {
                            if (std::isalnum(c) || c == '_') {
                                clean_name += c;
                            }
                        }
                        if (!clean_name.empty()) {
                            // Enforce Revolt emoji name length limit of 32 characters
                            if (clean_name.length() > 32) {
                                clean_name = clean_name.substr(0, 32);
                            }
                            targets.back().name = clean_name;
                        }
                    }
                }
            }

            if (targets.empty()) {
                bot.send_reply(msg, "no valid custom emojis found to steal.", true);
                return;
            }

            // Send initial progress embed
            auto render_payload = [](const std::vector<TargetEmoji>& tgts, int current, int total) {
                stoatpp::models::MessagePayload payload;
                nlohmann::json embed = nlohmann::json::object();
                embed["title"] = "emoji stealer progress";
                
                int success_count = 0;
                int failed_count = 0;
                for (const auto& t : tgts) {
                    if (t.status == "success!") success_count++;
                    else if (t.status != "pending..." && t.status != "processing...") failed_count++;
                }

                if (current == total) {
                    embed["colour"] = failed_count > 0 ? "#E74C3C" : "#2ECC71";
                } else {
                    embed["colour"] = "#F1C40F";
                }

                // Progress bar generator
                int percentage = (total == 0) ? 0 : (current * 100) / total;
                int filled = percentage / 10;
                std::string bar = "[";
                for (int i = 0; i < 10; ++i) {
                    if (i < filled) bar += "█";
                    else bar += "░";
                }
                bar += "] " + std::to_string(percentage) + "%";

                std::string desc = "**progress**: " + bar + "\n\n";
                for (const auto& t : tgts) {
                    std::string prefix_icon = "";
                    if (t.name.empty()) {
                        prefix_icon = ":" + t.id + ":";
                    } else {
                        prefix_icon = (t.animated ? "<a:" : "<:") + t.name + ":" + t.id + ">";
                    }
                    desc += "• " + prefix_icon + " `" + (t.name.empty() ? t.id : t.name) + "` ➔ " + t.status + "\n";
                }
                embed["description"] = desc;
                payload.embeds.push_back(embed);
                payload.content = " ";
                return payload;
            };

            bot.send_message(msg.channel_id, render_payload(targets, 0, targets.size()), [&bot, targets, msg, render_payload](stoatpp::models::Message status_msg, bool ok) mutable {
                if (!ok) return;

                // Run processing in a detached thread
                std::thread([&bot, targets, status_msg, render_payload, msg]() mutable {
                    int completed = 0;
                    for (size_t idx = 0; idx < targets.size(); ++idx) {
                        targets[idx].status = "processing...";
                        bot.edit_message(status_msg.channel, status_msg.id, render_payload(targets, completed, targets.size()));

                        std::string emoji_id = targets[idx].id;
                        if (targets[idx].name.empty()) {
                            auto emoji_res = bot.rest().get_custom_emoji(emoji_id);
                            if (emoji_res.success()) {
                                targets[idx].name = emoji_res.body.value("name", "stolen_emoji");
                                targets[idx].animated = emoji_res.body.value("animated", false);
                            } else {
                                targets[idx].name = "emoji_" + emoji_id.substr(0, 6);
                            }
                        }
                        std::string emoji_name = targets[idx].name;

                        // Make HTTP GET call to download emoji from CDN
                        httplib::Client cli(bot.config().cdn_url);
                        cli.set_connection_timeout(std::chrono::milliseconds(5000));
                        cli.set_read_timeout(std::chrono::milliseconds(5000));
                        cli.set_follow_location(true);

                        auto download_res = cli.Get("/emojis/" + emoji_id);
                        if (download_res) {
                            if (download_res->status == 200) {
                                std::vector<uint8_t> data(download_res->body.begin(), download_res->body.end());
                                std::string mime_type = download_res->get_header_value("Content-Type");
                                if (mime_type.empty()) {
                                    mime_type = targets[idx].animated ? "image/gif" : "image/png";
                                }

                                std::string ext = (mime_type == "image/gif") ? ".gif" : ".png";
                                std::string filename = emoji_name + ext;

                                 // Upload directly to Autumn CDN /emojis endpoint
                                httplib::Client upload_cli(bot.config().cdn_url);
                                upload_cli.set_connection_timeout(std::chrono::milliseconds(10000));
                                upload_cli.set_read_timeout(std::chrono::milliseconds(10000));
                                upload_cli.set_follow_location(true);

                                httplib::Headers upload_headers;
                                if (bot.config().token_type == stoatpp::TokenType::BOT) {
                                    upload_headers.emplace("X-Bot-Token", bot.token());
                                } else {
                                    upload_headers.emplace("X-Session-Token", bot.token());
                                }
                                for (const auto& [k, v] : bot.config().extra_headers) {
                                    upload_headers.emplace(k, v);
                                }

                                std::string file_content(data.begin(), data.end());
                                httplib::MultipartFormDataItems upload_items = {
                                    { "file", file_content, filename, mime_type }
                                };

                                auto upload_res = upload_cli.Post("/emojis", upload_headers, upload_items);
                                if (upload_res) {
                                    if (upload_res->status == 200 || upload_res->status == 201) {
                                        nlohmann::json upload_json;
                                        try {
                                            upload_json = nlohmann::json::parse(upload_res->body);
                                        } catch (...) {
                                            upload_json = nlohmann::json::object();
                                        }

                                        if (upload_json.contains("id")) {
                                            std::string new_file_id = upload_json["id"];
                                            
                                            // Create custom emoji on target server
                                            auto create_res = bot.rest().create_custom_emoji(emoji_name, new_file_id, msg.server_id);
                                            if (create_res.success()) {
                                                targets[idx].status = "success!";
                                            } else {
                                                targets[idx].status = "failed (create error: " + create_res.error_message() + ")";
                                            }
                                        } else {
                                            targets[idx].status = "failed (upload body missing id: " + upload_res->body + ")";
                                        }
                                    } else {
                                        targets[idx].status = "failed (upload status: " + std::to_string(upload_res->status) + ", body: " + upload_res->body + ")";
                                    }
                                } else {
                                    auto upload_err = upload_res.error();
                                    targets[idx].status = "failed (upload network error: " + std::to_string(static_cast<int>(upload_err)) + ")";
                                }
                            } else {
                                targets[idx].status = "failed (download status: " + std::to_string(download_res->status) + ")";
                            }
                        } else {
                            auto err = download_res.error();
                            targets[idx].status = "failed (download error code: " + std::to_string(static_cast<int>(err)) + ")";
                        }

                        completed++;
                        bot.edit_message(status_msg.channel, status_msg.id, render_payload(targets, completed, targets.size()));
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    }
                }).detach();
            });
        };

        bot.register_command({
            "emoji",
            {"steal"},
            "steal custom emojis from other servers and add them to this server.",
            "",
            {"<emoji1>", "[emoji2]..."},
            callback,
            "Emoji"
        });
    }
};

} // namespace bronx::cogs
