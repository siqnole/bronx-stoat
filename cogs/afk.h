#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "../database.h"
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_set>

namespace bronx::cogs {

#include "nlohmann/json.hpp"
#include <cstdlib>

struct AFKTemplate {
    std::string type = "text"; // "text" or "embed"
    std::string content = "{user} is AFK: {reason} ({time} ago)";
    std::string title;
    std::string description;
    std::string color;
    std::string image;

    static AFKTemplate parse(const std::string& raw) {
        AFKTemplate tmpl;
        if (raw.empty()) {
            return tmpl;
        }
        try {
            auto j = nlohmann::json::parse(raw);
            if (j.is_object()) {
                if (j.contains("type")) tmpl.type = j["type"].get<std::string>();
                if (j.contains("content")) tmpl.content = j["content"].get<std::string>();
                if (j.contains("embed") && j["embed"].is_object()) {
                    auto je = j["embed"];
                    if (je.contains("title")) tmpl.title = je["title"].get<std::string>();
                    if (je.contains("description")) tmpl.description = je["description"].get<std::string>();
                    if (je.contains("color")) tmpl.color = je["color"].get<std::string>();
                    if (je.contains("image")) tmpl.image = je["image"].get<std::string>();
                }
                return tmpl;
            }
        } catch (...) {}

        tmpl.type = "text";
        tmpl.content = raw;
        return tmpl;
    }

    std::string dump() const {
        nlohmann::json j;
        j["type"] = type;
        j["content"] = content;
        nlohmann::json je = nlohmann::json::object();
        if (!title.empty()) je["title"] = title;
        if (!description.empty()) je["description"] = description;
        if (!color.empty()) je["color"] = color;
        if (!image.empty()) je["image"] = image;
        j["embed"] = je;
        return j.dump();
    }
};

inline std::string format_afk_placeholders(std::string text, const std::string& user_id, const std::string& reason, const std::string& time_str) {
    size_t pos;
    while ((pos = text.find("{user}")) != std::string::npos) {
        text.replace(pos, 6, "<@" + user_id + ">");
    }
    while ((pos = text.find("{reason}")) != std::string::npos) {
        text.replace(pos, 8, reason);
    }
    while ((pos = text.find("{time}")) != std::string::npos) {
        text.replace(pos, 6, time_str);
    }
    return text;
}

inline std::string format_elapsed_time(int64_t elapsed_seconds) {
    if (elapsed_seconds < 60) {
        return std::to_string(elapsed_seconds) + "s";
    }
    int64_t minutes = elapsed_seconds / 60;
    if (minutes < 60) {
        return std::to_string(minutes) + "m";
    }
    int64_t hours = minutes / 60;
    if (hours < 24) {
        return std::to_string(hours) + "h " + std::to_string(minutes % 60) + "m";
    }
    int64_t days = hours / 24;
    return std::to_string(days) + "d " + std::to_string(hours % 24) + "h";
}

class AFKModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "afk",
            {},
            "set your afk status, customize template, or clear afk.",
            "",
            {"[reason|setup|type|message|title|desc|color|image|json|test|template|clear]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                bool is_subcommand = false;
                if (!args.empty()) {
                    std::string sub = args[0];
                    std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
                    if (sub == "setup" || sub == "clear" || sub == "type" || 
                        sub == "message" || sub == "content" || sub == "edit" || 
                        sub == "title" || sub == "description" || sub == "desc" || 
                        sub == "color" || sub == "image" || sub == "json" || 
                        sub == "test" || sub == "template" || sub == "status") {
                        is_subcommand = true;
                    }
                }

                if (is_subcommand) {
                    std::string sub = args[0];
                    std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

                    if (sub == "setup") {
                        bot.send_message(msg.channel_id, "💬 **AFK Customization Setup Wizard**\nLet's configure your custom AFK message.\n\n**Step 1/6**: Choose the template type. Reply with `text` or `embed`.");
                        bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30), [&bot, msg](std::optional<stoatpp::events::Message> rep1) {
                            if (!rep1) {
                                bot.send_message(msg.channel_id, "⏱️ Setup timed out. AFK setup canceled.");
                                return;
                            }
                            
                            std::string type_resp = rep1->content;
                            std::transform(type_resp.begin(), type_resp.end(), type_resp.begin(), ::tolower);
                            if (type_resp == "cancel") {
                                bot.send_message(msg.channel_id, "🚫 AFK setup canceled.");
                                return;
                            }
                            if (type_resp != "text" && type_resp != "embed") {
                                bot.send_message(msg.channel_id, "⚠️ Invalid type (must be `text` or `embed`). AFK setup canceled.");
                                return;
                            }

                            if (type_resp == "text") {
                                bot.send_message(msg.channel_id, "💬 **AFK Customization Setup Wizard**\n**Step 2/2**: Type your AFK message content.\n*Available tokens: `{user}`, `{reason}`, `{time}`*");
                                bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(45), [&bot, msg, type_resp](std::optional<stoatpp::events::Message> rep2) {
                                    if (!rep2) {
                                        bot.send_message(msg.channel_id, "⏱️ Setup timed out. AFK setup canceled.");
                                        return;
                                    }
                                    std::string content_resp = rep2->content;
                                    if (content_resp == "cancel") {
                                        bot.send_message(msg.channel_id, "🚫 AFK setup canceled.");
                                        return;
                                    }
                                    
                                    AFKTemplate tmpl;
                                    tmpl.type = type_resp;
                                    tmpl.content = content_resp;

                                    auto afk_opt = g_db->get_user_afk(msg.author.id);
                                    Database::UserAFK afk;
                                    if (afk_opt) afk = *afk_opt;
                                    afk.user_id = msg.author.id;
                                    afk.custom_template = tmpl.dump();

                                    if (g_db->save_user_afk(afk)) {
                                        bot.send_message(msg.channel_id, "✅ **AFK Setup Complete!** Your AFK template has been saved.");
                                    } else {
                                        bot.send_message(msg.channel_id, "⚠️ Failed to save AFK template.");
                                    }
                                });
                                return;
                            }

                            // Embed setup flow
                            bot.send_message(msg.channel_id, "💬 **AFK Customization Setup Wizard**\n**Step 2/6**: Type the embed **title** (or reply `skip`).\n*Available tokens: `{user}`, `{reason}`, `{time}`*");
                            bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(60), [&bot, msg, type_resp](std::optional<stoatpp::events::Message> rep2) {
                                if (!rep2) {
                                    bot.send_message(msg.channel_id, "⏱️ Setup timed out. AFK setup canceled.");
                                    return;
                                }
                                std::string title_resp = rep2->content;
                                if (title_resp == "cancel") {
                                    bot.send_message(msg.channel_id, "🚫 AFK setup canceled.");
                                    return;
                                }

                                AFKTemplate tmpl;
                                tmpl.type = type_resp;
                                if (title_resp != "skip") tmpl.title = title_resp;

                                bot.send_message(msg.channel_id, "💬 **AFK Customization Setup Wizard**\n**Step 3/6**: Type the embed **description** (or reply `skip`).\n*Available tokens: `{user}`, `{reason}`, `{time}`*");
                                bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(90), [&bot, msg, tmpl](std::optional<stoatpp::events::Message> rep3) {
                                    if (!rep3) {
                                        bot.send_message(msg.channel_id, "⏱️ Setup timed out. AFK setup canceled.");
                                        return;
                                    }
                                    std::string desc_resp = rep3->content;
                                    if (desc_resp == "cancel") {
                                        bot.send_message(msg.channel_id, "🚫 AFK setup canceled.");
                                        return;
                                    }
                                    auto current_tmpl = tmpl;
                                    if (desc_resp != "skip") current_tmpl.description = desc_resp;

                                    bot.send_message(msg.channel_id, "💬 **AFK Customization Setup Wizard**\n**Step 4/6**: Type the embed **color hex** (e.g. `#5865f2` or `random`, or reply `skip`).");
                                    bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30), [&bot, msg, current_tmpl](std::optional<stoatpp::events::Message> rep4) {
                                        if (!rep4) {
                                            bot.send_message(msg.channel_id, "⏱️ Setup timed out. AFK setup canceled.");
                                            return;
                                        }
                                        std::string color_resp = rep4->content;
                                        if (color_resp == "cancel") {
                                            bot.send_message(msg.channel_id, "🚫 AFK setup canceled.");
                                            return;
                                        }
                                        auto next_tmpl = current_tmpl;
                                        if (color_resp != "skip") next_tmpl.color = color_resp;

                                        bot.send_message(msg.channel_id, "💬 **AFK Customization Setup Wizard**\n**Step 5/6**: Type the embed **image URL** (or reply `skip`).");
                                        bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(45), [&bot, msg, next_tmpl](std::optional<stoatpp::events::Message> rep5) {
                                            if (!rep5) {
                                                bot.send_message(msg.channel_id, "⏱️ Setup timed out. AFK setup canceled.");
                                                return;
                                            }
                                            std::string img_resp = rep5->content;
                                            if (img_resp == "cancel") {
                                                bot.send_message(msg.channel_id, "🚫 AFK setup canceled.");
                                                return;
                                            }
                                            auto last_tmpl = next_tmpl;
                                            if (img_resp != "skip") last_tmpl.image = img_resp;

                                            bot.send_message(msg.channel_id, "💬 **AFK Customization Setup Wizard**\n**Step 6/6**: Type the **message content/text** to show outside the embed (or reply `skip`).\n*Available tokens: `{user}`, `{reason}`, `{time}`*");
                                            bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(45), [&bot, msg, last_tmpl](std::optional<stoatpp::events::Message> rep6) {
                                                if (!rep6) {
                                                    bot.send_message(msg.channel_id, "⏱️ Setup timed out. AFK setup canceled.");
                                                    return;
                                                }
                                                std::string text_resp = rep6->content;
                                                if (text_resp == "cancel") {
                                                    bot.send_message(msg.channel_id, "🚫 AFK setup canceled.");
                                                    return;
                                                }
                                                auto final_tmpl = last_tmpl;
                                                if (text_resp != "skip") {
                                                    final_tmpl.content = text_resp;
                                                } else {
                                                    final_tmpl.content = "";
                                                }

                                                auto afk_opt = g_db->get_user_afk(msg.author.id);
                                                Database::UserAFK afk;
                                                if (afk_opt) afk = *afk_opt;
                                                afk.user_id = msg.author.id;
                                                afk.custom_template = final_tmpl.dump();

                                                if (g_db->save_user_afk(afk)) {
                                                    bot.send_message(msg.channel_id, "✅ **AFK Setup Complete!** Your custom embed AFK template has been saved.");
                                                } else {
                                                    bot.send_message(msg.channel_id, "⚠️ Failed to save AFK template.");
                                                }
                                            });
                                        });
                                    });
                                });
                            });
                        });
                        return;
                    }

                    if (sub == "clear") {
                        if (args.size() == 1) {
                            // Clear own AFK
                            auto afk_opt = g_db->get_user_afk(msg.author.id);
                            if (afk_opt && afk_opt->is_afk) {
                                Database::UserAFK afk = *afk_opt;
                                afk.is_afk = false;
                                afk.reason = "";
                                afk.since = 0;
                                if (g_db->save_user_afk(afk)) {
                                    bot.send_message(msg.channel_id, "✅ Welcome back <@" + msg.author.id + ">, I removed your AFK.");
                                } else {
                                    bot.send_message(msg.channel_id, "⚠️ Failed to clear AFK status.");
                                }
                            } else {
                                bot.send_message(msg.channel_id, "ℹ️ You are not AFK.");
                            }
                            return;
                        } else {
                            // Clear someone else's AFK reason (moderator action)
                            auto s_opt = bot.get_server(msg.server_id);
                            if (!s_opt) {
                                bot.send_message(msg.channel_id, "⚠️ This action can only be performed in a server.");
                                return;
                            }

                            bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, s_opt, args](stoatpp::models::Member executor_mbr, bool ok) {
                                if (!ok) return;
                                bool is_mod = (s_opt->owner == msg.author.id) ||
                                              bot.has_permission(*s_opt, executor_mbr, stoatpp::permissions::KickMembers) ||
                                              bot.has_permission(*s_opt, executor_mbr, stoatpp::permissions::ManageMessages);
                                if (!is_mod) {
                                    bot.send_message(msg.channel_id, "⚠️ you do not have permission to clear other users' AFK status.");
                                    return;
                                }

                                std::string query = args[1];
                                helpers::resolve_user(bot, msg.server_id, query, [&bot, msg](stoatpp::models::User target_user, stoatpp::models::Member target_member, bool success) {
                                    if (!success) {
                                        bot.send_message(msg.channel_id, "⚠️ Could not resolve target user.");
                                        return;
                                    }

                                    auto target_afk_opt = g_db->get_user_afk(target_user.id);
                                    if (!target_afk_opt || !target_afk_opt->is_afk) {
                                        bot.send_message(msg.channel_id, "ℹ️ " + target_user.username + " is not AFK.");
                                        return;
                                    }

                                    Database::UserAFK afk = *target_afk_opt;
                                    afk.reason = ""; // Clear custom reason
                                    if (g_db->save_user_afk(afk)) {
                                        bot.send_message(msg.channel_id, "✅ Cleared AFK reason for **" + target_user.username + "**. They are still AFK but their custom reason has been reset.");
                                    } else {
                                        bot.send_message(msg.channel_id, "⚠️ Failed to clear AFK reason.");
                                    }
                                });
                            });
                            return;
                        }
                    }

                    if (sub == "type") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "ℹ️ Usage: `.afk type <text|embed>`");
                            return;
                        }
                        std::string t = args[1];
                        std::transform(t.begin(), t.end(), t.begin(), ::tolower);
                        if (t != "text" && t != "embed") {
                            bot.send_message(msg.channel_id, "⚠️ Invalid type. Must be `text` or `embed`.");
                            return;
                        }
                        auto afk_opt = g_db->get_user_afk(msg.author.id);
                        Database::UserAFK afk;
                        if (afk_opt) afk = *afk_opt;
                        afk.user_id = msg.author.id;
                        AFKTemplate tmpl = AFKTemplate::parse(afk.custom_template);
                        tmpl.type = t;
                        afk.custom_template = tmpl.dump();
                        if (g_db->save_user_afk(afk)) {
                            bot.send_message(msg.channel_id, "✅ AFK template type updated to `" + t + "`.");
                        } else {
                            bot.send_message(msg.channel_id, "⚠️ Failed to update template.");
                        }
                        return;
                    }

                    if (sub == "message" || sub == "content" || sub == "edit") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "ℹ️ Usage: `.afk message <text content>`");
                            return;
                        }
                        std::string content = "";
                        for (size_t i = 1; i < args.size(); ++i) {
                            if (i > 1) content += " ";
                            content += args[i];
                        }
                        auto afk_opt = g_db->get_user_afk(msg.author.id);
                        Database::UserAFK afk;
                        if (afk_opt) afk = *afk_opt;
                        afk.user_id = msg.author.id;
                        AFKTemplate tmpl = AFKTemplate::parse(afk.custom_template);
                        tmpl.content = content;
                        afk.custom_template = tmpl.dump();
                        if (g_db->save_user_afk(afk)) {
                            bot.send_message(msg.channel_id, "✅ AFK content/text updated.");
                        } else {
                            bot.send_message(msg.channel_id, "⚠️ Failed to update template.");
                        }
                        return;
                    }

                    if (sub == "title") {
                        std::string title = "";
                        for (size_t i = 1; i < args.size(); ++i) {
                            if (i > 1) title += " ";
                            title += args[i];
                        }
                        auto afk_opt = g_db->get_user_afk(msg.author.id);
                        Database::UserAFK afk;
                        if (afk_opt) afk = *afk_opt;
                        afk.user_id = msg.author.id;
                        AFKTemplate tmpl = AFKTemplate::parse(afk.custom_template);
                        tmpl.title = title;
                        afk.custom_template = tmpl.dump();
                        if (g_db->save_user_afk(afk)) {
                            bot.send_message(msg.channel_id, title.empty() ? "✅ Embed title cleared." : "✅ Embed title updated.");
                        } else {
                            bot.send_message(msg.channel_id, "⚠️ Failed to update template.");
                        }
                        return;
                    }

                    if (sub == "description" || sub == "desc") {
                        std::string desc_text = "";
                        for (size_t i = 1; i < args.size(); ++i) {
                            if (i > 1) desc_text += " ";
                            desc_text += args[i];
                        }
                        auto afk_opt = g_db->get_user_afk(msg.author.id);
                        Database::UserAFK afk;
                        if (afk_opt) afk = *afk_opt;
                        afk.user_id = msg.author.id;
                        AFKTemplate tmpl = AFKTemplate::parse(afk.custom_template);
                        tmpl.description = desc_text;
                        afk.custom_template = tmpl.dump();
                        if (g_db->save_user_afk(afk)) {
                            bot.send_message(msg.channel_id, desc_text.empty() ? "✅ Embed description cleared." : "✅ Embed description updated.");
                        } else {
                            bot.send_message(msg.channel_id, "⚠️ Failed to update template.");
                        }
                        return;
                    }

                    if (sub == "color") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "ℹ️ Usage: `.afk color <#hex|random|none>`");
                            return;
                        }
                        std::string col = args[1];
                        if (col == "none" || col == "clear") col = "";
                        auto afk_opt = g_db->get_user_afk(msg.author.id);
                        Database::UserAFK afk;
                        if (afk_opt) afk = *afk_opt;
                        afk.user_id = msg.author.id;
                        AFKTemplate tmpl = AFKTemplate::parse(afk.custom_template);
                        tmpl.color = col;
                        afk.custom_template = tmpl.dump();
                        if (g_db->save_user_afk(afk)) {
                            bot.send_message(msg.channel_id, col.empty() ? "✅ Embed color cleared." : "✅ Embed color updated.");
                        } else {
                            bot.send_message(msg.channel_id, "⚠️ Failed to update template.");
                        }
                        return;
                    }

                    if (sub == "image") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "ℹ️ Usage: `.afk image <url|none>`");
                            return;
                        }
                        std::string img = args[1];
                        if (img == "none" || img == "clear") img = "";
                        auto afk_opt = g_db->get_user_afk(msg.author.id);
                        Database::UserAFK afk;
                        if (afk_opt) afk = *afk_opt;
                        afk.user_id = msg.author.id;
                        AFKTemplate tmpl = AFKTemplate::parse(afk.custom_template);
                        tmpl.image = img;
                        afk.custom_template = tmpl.dump();
                        if (g_db->save_user_afk(afk)) {
                            bot.send_message(msg.channel_id, img.empty() ? "✅ Embed image cleared." : "✅ Embed image updated.");
                        } else {
                            bot.send_message(msg.channel_id, "⚠️ Failed to update template.");
                        }
                        return;
                    }

                    if (sub == "json") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "ℹ️ Usage: `.afk json <raw_json>`");
                            return;
                        }
                        std::string raw_json = "";
                        for (size_t i = 1; i < args.size(); ++i) {
                            if (i > 1) raw_json += " ";
                            raw_json += args[i];
                        }
                        try {
                            auto parsed = nlohmann::json::parse(raw_json);
                            if (!parsed.is_object()) throw std::runtime_error("must be an object");
                        } catch (const std::exception& ex) {
                            bot.send_message(msg.channel_id, "⚠️ Invalid JSON format: " + std::string(ex.what()));
                            return;
                        }
                        auto afk_opt = g_db->get_user_afk(msg.author.id);
                        Database::UserAFK afk;
                        if (afk_opt) afk = *afk_opt;
                        afk.user_id = msg.author.id;
                        afk.custom_template = raw_json;
                        if (g_db->save_user_afk(afk)) {
                            bot.send_message(msg.channel_id, "✅ AFK JSON template updated.");
                        } else {
                            bot.send_message(msg.channel_id, "⚠️ Failed to update template.");
                        }
                        return;
                    }

                    if (sub == "test") {
                        auto afk_opt = g_db->get_user_afk(msg.author.id);
                        Database::UserAFK afk;
                        if (afk_opt) afk = *afk_opt;
                        
                        std::string reason = afk.reason.empty() ? "Test Reason" : afk.reason;
                        int64_t elapsed = afk.since == 0 ? 300 : (std::time(nullptr) - afk.since);
                        std::string time_str = format_elapsed_time(elapsed);

                        AFKTemplate tmpl = AFKTemplate::parse(afk.custom_template);

                        if (tmpl.type == "embed") {
                            stoatpp::models::MessagePayload payload;
                            payload.content = format_afk_placeholders(tmpl.content, msg.author.id, reason, time_str);
                            if (payload.content.empty()) payload.content = " ";

                            nlohmann::json embed = nlohmann::json::object();
                            if (!tmpl.title.empty()) {
                                embed["title"] = format_afk_placeholders(tmpl.title, msg.author.id, reason, time_str);
                            }
                            if (!tmpl.description.empty()) {
                                embed["description"] = format_afk_placeholders(tmpl.description, msg.author.id, reason, time_str);
                            }
                            if (!tmpl.color.empty()) {
                                std::string col = tmpl.color;
                                if (col == "random") col = "#ff9900";
                                embed["colour"] = col;
                            }
                            if (!tmpl.image.empty()) {
                                std::string img_url = format_afk_placeholders(tmpl.image, msg.author.id, reason, time_str);
                                std::string media_id = bot.rest().upload_from_url(img_url);
                                if (!media_id.empty()) {
                                    embed["media"] = media_id;
                                } else {
                                    std::string desc = embed.contains("description") ? embed["description"].get<std::string>() : "";
                                    if (!desc.empty()) desc += "\n";
                                    desc += "![](" + img_url + ")";
                                    embed["description"] = desc;
                                }
                            }
                            if (!embed.empty()) {
                                payload.embeds.push_back(embed);
                            }
                            bot.send_message(msg.channel_id, payload);
                        } else {
                            std::string out = format_afk_placeholders(tmpl.content, msg.author.id, reason, time_str);
                            bot.send_message(msg.channel_id, out);
                        }
                        return;
                    }

                    if (sub == "template" || sub == "status") {
                        auto afk_opt = g_db->get_user_afk(msg.author.id);
                        Database::UserAFK afk;
                        if (afk_opt) afk = *afk_opt;
                        AFKTemplate tmpl = AFKTemplate::parse(afk.custom_template);
                        
                        std::string desc = "ℹ️ **Your AFK Template Configuration**:\n"
                                           "• **Type**: `" + tmpl.type + "`\n"
                                           "• **Content/Text**: `" + (tmpl.content.empty() ? "(none)" : tmpl.content) + "`\n";
                        if (tmpl.type == "embed") {
                            desc += "• **Embed Title**: `" + (tmpl.title.empty() ? "(none)" : tmpl.title) + "`\n"
                                    "• **Embed Description**: `" + (tmpl.description.empty() ? "(none)" : tmpl.description) + "`\n"
                                    "• **Embed Color**: `" + (tmpl.color.empty() ? "(none)" : tmpl.color) + "`\n"
                                    "• **Embed Image URL**: `" + (tmpl.image.empty() ? "(none)" : tmpl.image) + "`\n";
                        }
                        desc += "\nUse `.afk setup` to run the step-by-step wizard, or `.afk test` to preview it.\n"
                                "You can also use: `.afk type <text|embed>`, `.afk message <text>`, `.afk title <title>`, etc.";
                        bot.send_message(msg.channel_id, desc);
                        return;
                    }
                }

                // Regular .afk [reason] logic
                std::string reason = "AFK";
                if (!args.empty()) {
                    reason = "";
                    for (size_t i = 0; i < args.size(); ++i) {
                        if (i > 0) reason += " ";
                        reason += args[i];
                    }
                }

                auto afk_opt = g_db->get_user_afk(msg.author.id);
                Database::UserAFK afk;
                if (afk_opt) afk = *afk_opt;
                afk.user_id = msg.author.id;
                afk.is_afk = true;
                afk.reason = reason;
                afk.since = std::time(nullptr);

                if (g_db->save_user_afk(afk)) {
                    bot.send_message(msg.channel_id, "✅ <@" + msg.author.id + "> is now AFK: " + reason);
                } else {
                    bot.send_message(msg.channel_id, "⚠️ Failed to set AFK status.");
                }
            },
            "Utility"
        });

        bot.register_command({
            "unafk",
            {},
            "clear your afk status, or clear another user's afk status (mods).",
            "",
            {"[user]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (args.empty()) {
                    // Clear own AFK
                    auto afk_opt = g_db->get_user_afk(msg.author.id);
                    if (afk_opt && afk_opt->is_afk) {
                        Database::UserAFK afk = *afk_opt;
                        afk.is_afk = false;
                        afk.reason = "";
                        afk.since = 0;
                        if (g_db->save_user_afk(afk)) {
                            bot.send_message(msg.channel_id, "✅ Welcome back <@" + msg.author.id + ">, I removed your AFK.");
                        } else {
                            bot.send_message(msg.channel_id, "⚠️ Failed to clear AFK status.");
                        }
                    } else {
                        bot.send_message(msg.channel_id, "ℹ️ You are not AFK.");
                    }
                    return;
                }

                // Clear someone else's AFK status entirely
                auto s_opt = bot.get_server(msg.server_id);
                if (!s_opt) {
                    bot.send_message(msg.channel_id, "⚠️ This action can only be performed in a server.");
                    return;
                }

                bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, s_opt, args](stoatpp::models::Member executor_mbr, bool ok) {
                    if (!ok) return;
                    bool is_mod = (s_opt->owner == msg.author.id) ||
                                  bot.has_permission(*s_opt, executor_mbr, stoatpp::permissions::KickMembers) ||
                                  bot.has_permission(*s_opt, executor_mbr, stoatpp::permissions::ManageMessages);
                    if (!is_mod) {
                        bot.send_message(msg.channel_id, "⚠️ you do not have permission to clear other users' AFK status.");
                        return;
                    }

                    std::string query = args[0];
                    helpers::resolve_user(bot, msg.server_id, query, [&bot, msg](stoatpp::models::User target_user, stoatpp::models::Member target_member, bool success) {
                        if (!success) {
                            bot.send_message(msg.channel_id, "⚠️ Could not resolve target user.");
                            return;
                        }

                        auto target_afk_opt = g_db->get_user_afk(target_user.id);
                        if (!target_afk_opt || !target_afk_opt->is_afk) {
                            bot.send_message(msg.channel_id, "ℹ️ " + target_user.username + " is not AFK.");
                            return;
                        }

                        Database::UserAFK afk = *target_afk_opt;
                        afk.is_afk = false;
                        afk.reason = "";
                        afk.since = 0;
                        if (g_db->save_user_afk(afk)) {
                            bot.send_message(msg.channel_id, "✅ Welcomed back **" + target_user.username + "**. Removed their AFK status.");
                        } else {
                            bot.send_message(msg.channel_id, "⚠️ Failed to clear AFK status.");
                        }
                    });
                });
            },
            "Moderation"
        });

        // ── on_message listener for AFK checks ───────────────────────────────
        bot.on_message([&bot](const stoatpp::events::Message& msg) {
            if (msg.author.id.empty() || msg.author.id == bot.current_user().id) {
                return;
            }

            // 1. Remove AFK status if the sender is AFK
            auto author_afk_opt = g_db->get_user_afk(msg.author.id);
            if (author_afk_opt && author_afk_opt->is_afk) {
                // Don't auto-remove if they just typed the command to set/edit/clear afk
                // We resolve custom prefixes to see if it starts with one of them
                std::vector<std::string> prefixes;
                prefixes.push_back("<@" + bot.current_user().id + "> ");
                prefixes.push_back("<@" + bot.current_user().id + ">");
                if (!msg.server_id.empty()) {
                    auto server_pfx = g_db->get_prefixes("server", msg.server_id);
                    prefixes.insert(prefixes.end(), server_pfx.begin(), server_pfx.end());
                }
                auto user_pfx = g_db->get_prefixes("user", msg.author.id);
                prefixes.insert(prefixes.end(), user_pfx.begin(), user_pfx.end());
                if (msg.server_id.empty() || g_db->get_prefix_default_enabled(msg.server_id)) {
                    prefixes.push_back(bot.config().command_prefix);
                }

                bool is_afk_command = false;
                for (const auto& p : prefixes) {
                    if (msg.content.rfind(p, 0) == 0) {
                        std::string cmd_part = msg.content.substr(p.size());
                        // Trim leading spaces
                        size_t first = cmd_part.find_first_not_of(" \t\r\n");
                        if (first != std::string::npos) {
                            cmd_part = cmd_part.substr(first);
                        }
                        // Check if it starts with "afk" or "unafk"
                        if (cmd_part.rfind("afk", 0) == 0 || cmd_part.rfind("unafk", 0) == 0) {
                            is_afk_command = true;
                            break;
                        }
                    }
                }

                // Also check if it's very recent (e.g. less than 3 seconds) to prevent accidental immediate trigger
                int64_t diff = std::time(nullptr) - author_afk_opt->since;
                if (!is_afk_command && diff > 2) {
                    Database::UserAFK afk = *author_afk_opt;
                    afk.is_afk = false;
                    afk.reason = "";
                    afk.since = 0;
                    if (g_db->save_user_afk(afk)) {
                        std::string time_spent = format_elapsed_time(diff);
                        bot.send_message(msg.channel_id, "👋 Welcome back <@" + msg.author.id + ">! I removed your AFK (Time away: " + time_spent + ").");
                    }
                }
            }

            // 2. Response to mentions of AFK users
            std::unordered_set<std::string> handled_mentions;
            for (const auto& mentioned_id : msg.mentions) {
                if (mentioned_id == msg.author.id || mentioned_id == bot.current_user().id) {
                    continue;
                }
                if (handled_mentions.count(mentioned_id)) {
                    continue;
                }
                handled_mentions.insert(mentioned_id);

                auto target_afk_opt = g_db->get_user_afk(mentioned_id);
                if (target_afk_opt && target_afk_opt->is_afk) {
                    std::string reason = target_afk_opt->reason.empty() ? "AFK" : target_afk_opt->reason;
                    int64_t elapsed = std::time(nullptr) - target_afk_opt->since;
                    std::string time_str = format_elapsed_time(elapsed);

                    AFKTemplate tmpl = AFKTemplate::parse(target_afk_opt->custom_template);

                    if (tmpl.type == "embed") {
                        stoatpp::models::MessagePayload payload;
                        payload.content = format_afk_placeholders(tmpl.content, mentioned_id, reason, time_str);
                        if (payload.content.empty()) payload.content = " ";

                        nlohmann::json embed = nlohmann::json::object();
                        if (!tmpl.title.empty()) {
                            embed["title"] = format_afk_placeholders(tmpl.title, mentioned_id, reason, time_str);
                        }
                        if (!tmpl.description.empty()) {
                            embed["description"] = format_afk_placeholders(tmpl.description, mentioned_id, reason, time_str);
                        }
                        if (!tmpl.color.empty()) {
                            std::string col = tmpl.color;
                            if (col == "random") {
                                char hex[8];
                                snprintf(hex, sizeof(hex), "#%06x", rand() % 0xFFFFFF);
                                col = hex;
                            }
                            embed["colour"] = col;
                        }
                        if (!tmpl.image.empty()) {
                            std::string img_url = format_afk_placeholders(tmpl.image, mentioned_id, reason, time_str);
                            std::string media_id = bot.rest().upload_from_url(img_url);
                            if (!media_id.empty()) {
                                embed["media"] = media_id;
                            } else {
                                std::string desc = embed.contains("description") ? embed["description"].get<std::string>() : "";
                                if (!desc.empty()) desc += "\n";
                                desc += "![](" + img_url + ")";
                                embed["description"] = desc;
                            }
                        }
                        if (!embed.empty()) {
                            payload.embeds.push_back(embed);
                        }
                        bot.send_message(msg.channel_id, payload);
                    } else {
                        std::string out = format_afk_placeholders(tmpl.content, mentioned_id, reason, time_str);
                        bot.send_message(msg.channel_id, out);
                    }
                }
            }
        });
    }
};

} // namespace bronx::cogs
