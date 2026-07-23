#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "../database.h"
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <filesystem>
#include <thread>
#include <random>

#ifdef BRONX_CHART_ENABLED
#include "chart_renderer.h"
#include <leptonica/allheaders.h>
#endif

namespace bronx::cogs {

class LevelingModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {

        // Command: .rank [@user]
        bot.register_command({
            "rank",
            {"xp", "levelinfo"},
            "view leveling progress and rank card.",
            "[user]",
            {"[user]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                show_rank_card(bot, msg, args);
            },
            "Leveling"
        });

        // Command: .leaderboard
        bot.register_command({
            "leaderboard",
            {"lb", "levels"},
            "view the server XP leaderboard.",
            "",
            {},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id, "this command can only be used inside a server.");
                    return;
                }
                if (!g_db) {
                    bot.send_message(msg.channel_id, "database is currently unavailable.");
                    return;
                }

                auto cfg_opt = g_db->get_guild_leveling_config(msg.server_id);
                if (!cfg_opt || !cfg_opt->enabled) {
                    bot.send_message(msg.channel_id, "⚠️ Leveling is currently disabled in this server.");
                    return;
                }

                auto entries = g_db->get_server_xp_leaderboard(msg.server_id, 10);
                if (entries.empty()) {
                    bot.send_message(msg.channel_id, "ℹ️ No XP leaderboard records yet.");
                    return;
                }

                std::string out = "🏆 **XP Leaderboard** 🏆\n\n";
                int rank = 1;
                for (const auto& e : entries) {
                    std::string rank_prefix = "";
                    if (rank == 1) rank_prefix = "🥇 ";
                    else if (rank == 2) rank_prefix = "🥈 ";
                    else if (rank == 3) rank_prefix = "🥉 ";
                    else rank_prefix = std::to_string(rank) + ". ";

                    out += rank_prefix + "<@" + e.user_id + "> — **Level " + std::to_string(e.level) + "** (" + std::to_string(e.total_xp) + " XP)\n";
                    rank++;
                }
                bot.send_message(msg.channel_id, out);
            },
            "Leveling"
        });

        // Command: .level [subcommand] [args...]
        bot.register_command({
            "level",
            {"lvl"},
            "configure the server leveling settings or customize your rank card.",
            "[subcommand] [args...]",
            {"[subcommand]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id, "this command can only be used inside a server.");
                    return;
                }
                if (!g_db) {
                    bot.send_message(msg.channel_id, "database is currently unavailable.");
                    return;
                }

                bool is_admin_cmd = false;
                bool is_user_edit_cmd = false;
                if (!args.empty()) {
                    std::string sub = args[0];
                    std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
                    if (sub == "enable" || sub == "disable" || sub == "xprange" || sub == "role" || sub == "config") {
                        is_admin_cmd = true;
                    } else if (sub == "edit" || sub == "customize") {
                        is_user_edit_cmd = true;
                    }
                }

                if (is_user_edit_cmd) {
                    handle_level_edit_cmd(bot, msg, args);
                    return;
                }

                if (!is_admin_cmd) {
                    show_rank_card(bot, msg, args);
                    return;
                }

                // Check permissions (ManageServer or bot owner)
                auto server_opt = bot.get_server(msg.server_id);
                if (!server_opt) {
                    bot.send_message(msg.channel_id, "failed to fetch server info.");
                    return;
                }

                bool is_authorized = bot.is_owner(msg.author.id);
                if (!is_authorized) {
                    bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, server_opt, args](stoatpp::models::Member member, bool ok) {
                        if (!ok || !bot.has_permission(*server_opt, member, stoatpp::permissions::ManageServer)) {
                            bot.send_message(msg.channel_id, "❌ You do not have permissions to manage leveling settings (Manage Server required).");
                            return;
                        }
                        // Proceed to subcommand handler
                        handle_level_cmd(bot, msg, args);
                    });
                    return;
                }

                handle_level_cmd(bot, msg, args);
            },
            "Leveling"
        });

    }

private:
    static void handle_level_edit_cmd(stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
        if (!g_db) {
            bot.send_message(msg.channel_id, "database is currently unavailable.");
            return;
        }

        std::vector<std::string> prefixes = { bot.config().command_prefix };
        if (bot.config().prefix_resolver) {
            prefixes = bot.config().prefix_resolver(bot, msg);
        }
        std::string pfx = bot.config().command_prefix;
        for (const auto& p : prefixes) {
            if (msg.content.rfind(p, 0) == 0) { pfx = p; break; }
        }

        auto send_edit_help = [&bot, msg, pfx]() {
            bot.send_message(msg.channel_id,
                "**🎨 Personalize Your Rank Card**\n\n"
                "• `" + pfx + "level edit style <image|embed|text>` — Choose rank card display style.\n"
                "• `" + pfx + "level edit bgcolor <hex_color|none>` — Set background color (e.g., `#0e0e11`).\n"
                "• `" + pfx + "level edit bgimage <url|attachment|none>` — Set background image URL or upload one.\n"
                "• `" + pfx + "level edit activecolor <hex_color|none>` — Set progress bar color.\n"
                "• `" + pfx + "level edit dimcolor <hex_color|none>` — Set progress bar background color.\n"
                "• `" + pfx + "level edit textcolor <hex_color|none>` — Set text color.\n"
                "• `" + pfx + "level edit font <font_name|none>` — Set custom font (e.g. `monospace`, `serif`, `sans-serif`).\n"
                "• `" + pfx + "level edit preview` — Show current settings preview."
            );
        };

        if (args.size() < 2) {
            send_edit_help();
            return;
        }

        std::string action = args[1];
        std::transform(action.begin(), action.end(), action.begin(), ::tolower);

        // Fetch or create user rank card config
        Database::UserRankCard urc;
        auto urc_opt = g_db->get_user_rank_card(msg.author.id);
        if (urc_opt) {
            urc = *urc_opt;
        } else {
            urc.user_id = msg.author.id;
            urc.style = "image";
        }

        if (action == "preview") {
            std::string bg_color_str = urc.bg_color ? *urc.bg_color : "*Default*";
            std::string bg_image_str = urc.bg_image ? *urc.bg_image : "*Default*";
            std::string active_str = urc.bar_active ? *urc.bar_active : "*Default*";
            std::string dimmed_str = urc.bar_dimmed ? *urc.bar_dimmed : "*Default*";
            std::string text_str = urc.text_color ? *urc.text_color : "*Default*";
            std::string font_str = urc.font_name ? *urc.font_name : "*Default*";

            bot.send_message(msg.channel_id,
                "**🎨 Your Rank Card Customization**\n\n"
                "• **Style**: `" + urc.style + "`\n"
                "• **Background Color**: `" + bg_color_str + "`\n"
                "• **Background Image**: " + (urc.bg_image ? ("`" + bg_image_str + "`") : bg_image_str) + "\n"
                "• **Active Bar Color**: `" + active_str + "`\n"
                "• **Dimmed Bar Color**: `" + dimmed_str + "`\n"
                "• **Text Color**: `" + text_str + "`\n"
                "• **Font Family**: `" + font_str + "`"
            );
            return;
        }

        if (args.size() < 3 && action != "bgimage") {
            bot.send_message(msg.channel_id, "❌ Please specify a value. Use `" + pfx + "level edit` to see options.");
            return;
        }

        std::string val = args.size() >= 3 ? args[2] : "";
        std::string val_lower = val;
        std::transform(val_lower.begin(), val_lower.end(), val_lower.begin(), ::tolower);

        if (action == "style") {
            if (val_lower == "image" || val_lower == "embed" || val_lower == "text") {
                urc.style = val_lower;
                g_db->save_user_rank_card(urc);
                bot.send_message(msg.channel_id, "✅ Style updated to `" + val_lower + "`.");
            } else {
                bot.send_message(msg.channel_id, "❌ Invalid style. Choose from `image`, `embed`, or `text`.");
            }
        }
        else if (action == "bgcolor") {
            if (val_lower == "none" || val_lower == "default" || val_lower == "clear") {
                urc.bg_color = std::nullopt;
                g_db->save_user_rank_card(urc);
                bot.send_message(msg.channel_id, "✅ Background color reset to default.");
            } else {
                if (val[0] != '#' && val.length() == 6) val = "#" + val;
                chart::RGBA dummy;
                if (chart::parse_hex_color(val, dummy)) {
                    urc.bg_color = val;
                    g_db->save_user_rank_card(urc);
                    bot.send_message(msg.channel_id, "✅ Background color updated to `" + val + "`.");
                } else {
                    bot.send_message(msg.channel_id, "❌ Invalid hex color format. Example: `#0e0e11`.");
                }
            }
        }
        else if (action == "bgimage") {
            std::string img_url = val;
            if (msg.raw.contains("attachments") && msg.raw["attachments"].is_array() && !msg.raw["attachments"].empty()) {
                auto first_attach = msg.raw["attachments"][0];
                std::string tag = first_attach.value("tag", "");
                std::string id = first_attach.value("id", "");
                if (id.empty()) id = first_attach.value("_id", "");
                std::string filename = first_attach.value("filename", "");
                if (!tag.empty() && !id.empty()) {
                    img_url = bot.config().cdn_url + "/" + tag + "/" + id + "/" + filename;
                }
            }

            if (img_url.empty() || img_url == "none" || img_url == "default" || img_url == "clear") {
                urc.bg_image = std::nullopt;
                g_db->save_user_rank_card(urc);
                bot.send_message(msg.channel_id, "✅ Background image reset to default.");
            } else {
                std::string uploaded = helpers::upload_avatar(bot, img_url);
                if (!uploaded.empty()) {
                    size_t slash_pos = uploaded.rfind('/');
                    if (slash_pos != std::string::npos) {
                        uploaded = uploaded.substr(slash_pos + 1);
                    }
                    urc.bg_image = uploaded;
                    g_db->save_user_rank_card(urc);
                    bot.send_message(msg.channel_id, "✅ Background image set successfully.");
                } else {
                    bot.send_message(msg.channel_id, "❌ Failed to process background image.");
                }
            }
        }
        else if (action == "activecolor") {
            if (val_lower == "none" || val_lower == "default" || val_lower == "clear") {
                urc.bar_active = std::nullopt;
                g_db->save_user_rank_card(urc);
                bot.send_message(msg.channel_id, "✅ Active bar color reset to default.");
            } else {
                if (val[0] != '#' && val.length() == 6) val = "#" + val;
                chart::RGBA dummy;
                if (chart::parse_hex_color(val, dummy)) {
                    urc.bar_active = val;
                    g_db->save_user_rank_card(urc);
                    bot.send_message(msg.channel_id, "✅ Active bar color updated to `" + val + "`.");
                } else {
                    bot.send_message(msg.channel_id, "❌ Invalid hex color format.");
                }
            }
        }
        else if (action == "dimcolor") {
            if (val_lower == "none" || val_lower == "default" || val_lower == "clear") {
                urc.bar_dimmed = std::nullopt;
                g_db->save_user_rank_card(urc);
                bot.send_message(msg.channel_id, "✅ Dimmed bar color reset to default.");
            } else {
                if (val[0] != '#' && val.length() == 6) val = "#" + val;
                chart::RGBA dummy;
                if (chart::parse_hex_color(val, dummy)) {
                    urc.bar_dimmed = val;
                    g_db->save_user_rank_card(urc);
                    bot.send_message(msg.channel_id, "✅ Dimmed bar color updated to `" + val + "`.");
                } else {
                    bot.send_message(msg.channel_id, "❌ Invalid hex color format.");
                }
            }
        }
        else if (action == "textcolor") {
            if (val_lower == "none" || val_lower == "default" || val_lower == "clear") {
                urc.text_color = std::nullopt;
                g_db->save_user_rank_card(urc);
                bot.send_message(msg.channel_id, "✅ Text color reset to default.");
            } else {
                if (val[0] != '#' && val.length() == 6) val = "#" + val;
                chart::RGBA dummy;
                if (chart::parse_hex_color(val, dummy)) {
                    urc.text_color = val;
                    g_db->save_user_rank_card(urc);
                    bot.send_message(msg.channel_id, "✅ Text color updated to `" + val + "`.");
                } else {
                    bot.send_message(msg.channel_id, "❌ Invalid hex color format.");
                }
            }
        }
        else if (action == "font") {
            if (val_lower == "none" || val_lower == "default" || val_lower == "clear") {
                urc.font_name = std::nullopt;
                g_db->save_user_rank_card(urc);
                bot.send_message(msg.channel_id, "✅ Font family reset to default.");
            } else {
                urc.font_name = val;
                g_db->save_user_rank_card(urc);
                bot.send_message(msg.channel_id, "✅ Font family updated to `" + val + "`.");
            }
        } else {
            send_edit_help();
        }
    }

    static void show_rank_card(stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
        if (msg.server_id.empty()) {
            bot.send_message(msg.channel_id, "this command can only be used inside a server.");
            return;
        }
        if (!g_db) {
            bot.send_message(msg.channel_id, "database is currently unavailable.");
            return;
        }

        auto cfg_opt = g_db->get_guild_leveling_config(msg.server_id);
        if (!cfg_opt || !cfg_opt->enabled) {
            bot.send_message(msg.channel_id, "⚠️ Leveling is currently disabled in this server.");
            return;
        }

        std::string target_query = args.empty() ? msg.author.id : args[0];

        helpers::resolve_user(bot, msg.server_id, target_query, [msg, &bot](stoatpp::models::User target_user, stoatpp::models::Member, bool success) {
            if (!success) {
                bot.send_message(msg.channel_id, "could not resolve target user.");
                return;
            }

            auto xp_opt = g_db->get_user_xp(target_user.id, msg.server_id);
            if (!xp_opt) {
                bot.send_message(msg.channel_id, "ℹ️ " + target_user.username + " has no XP progress yet (needs to send messages first).");
                return;
            }

            uint32_t level = xp_opt->level;
            uint64_t current_xp = xp_opt->total_xp;
            uint64_t base_xp = Database::calculate_xp_for_level(level);
            uint64_t next_xp = Database::calculate_xp_for_level(level + 1);

            uint64_t xp_in_level = current_xp - base_xp;
            uint64_t xp_required_for_level = next_xp - base_xp;

            int rank = g_db->get_user_server_xp_rank(target_user.id, msg.server_id);
            auto leaderboard = g_db->get_server_xp_leaderboard(msg.server_id, 9999);
            int total_users = leaderboard.size();

            // Check style from user customization
            std::string style = "image";
            std::optional<Database::UserRankCard> urc_opt;
            if (g_db) {
                urc_opt = g_db->get_user_rank_card(target_user.id);
                if (urc_opt) {
                    style = urc_opt->style;
                }
            }

            int percentage = 0;
            if (xp_required_for_level > 0) {
                percentage = static_cast<int>((xp_in_level * 100) / xp_required_for_level);
            }

            // Generate a nice ASCII progress bar for text/embed style
            int filled = 0;
            if (xp_required_for_level > 0) {
                filled = static_cast<int>((xp_in_level * 10) / xp_required_for_level);
            }
            if (filled > 10) filled = 10;
            if (filled < 0) filled = 0;

            std::string bar = "[";
            for (int i = 0; i < 10; ++i) {
                if (i < filled) bar += "█";
                else bar += "░";
            }
            bar += "]";

            if (style == "text") {
                std::string out = "📊 **Rank progress for " + target_user.username + "**\n"
                                  "✨ **Level**: " + std::to_string(level) + "\n"
                                  "📈 **XP**: " + std::to_string(current_xp) + " / " + std::to_string(next_xp) + " (Level progress: " + std::to_string(percentage) + "%)\n" +
                                  bar + "\n"
                                  "🏆 **Rank**: #" + std::to_string(rank) + " of " + std::to_string(total_users);
                bot.send_message(msg.channel_id, out);
                return;
            }

            if (style == "embed") {
                stoatpp::models::MessagePayload payload;
                payload.content = " ";
                stoatpp::models::Embed embed;
                embed.set_title("📊 Rank progress for " + target_user.username);
                embed.set_colour(0x5865F2);
                
                std::string desc = "✨ **Level**: " + std::to_string(level) + "\n"
                                   "📈 **XP**: " + std::to_string(current_xp) + " / " + std::to_string(next_xp) + " (" + std::to_string(percentage) + "%)\n"
                                   "🏆 **Rank**: #" + std::to_string(rank) + " of " + std::to_string(total_users) + "\n\n" +
                                   bar;
                embed.set_description(desc);
                payload.add_embed(embed);
                bot.send_message(msg.channel_id, payload);
                return;
            }

#ifdef BRONX_CHART_ENABLED
            std::string user_id = target_user.id;
            std::string username = target_user.username;
            std::string avatar_url = target_user.avatar_url(bot.config().cdn_url);
            std::string server_id = msg.server_id;
            std::string channel_id = msg.channel_id;

            bot.begin_typing(channel_id);

            std::thread([&bot, server_id, channel_id, user_id, username, avatar_url, urc_opt]() {
                auto xp_opt = g_db->get_user_xp(user_id, server_id);
                if (!xp_opt) {
                    bot.send_message(channel_id, "ℹ️ " + username + " has no XP progress yet (needs to send messages first).");
                    return;
                }

                uint32_t level = xp_opt->level;
                uint64_t current_xp = xp_opt->total_xp;
                uint64_t base_xp = Database::calculate_xp_for_level(level);
                uint64_t next_xp = Database::calculate_xp_for_level(level + 1);

                int rank = g_db->get_user_server_xp_rank(user_id, server_id);
                auto leaderboard = g_db->get_server_xp_leaderboard(server_id, 9999);
                int total_users = leaderboard.size();

                // Download avatar to a temp file
                std::string temp_avatar_png = "";
                if (!avatar_url.empty() && avatar_url != "https://api.stoat.chat/assets/default_avatar.png") {
                    std::random_device rd;
                    std::mt19937 gen(rd());
                    std::uniform_int_distribution<> dis(100000, 999999);
                    std::string tmp_id = std::to_string(dis(gen));
                    std::string raw_avatar_path = "/tmp/avatar_raw_" + tmp_id;
                    std::string converted_avatar_png = "/tmp/avatar_" + tmp_id + ".png";

                    // Use shell curl to download the file
                    std::string dl_cmd = "curl -k -L -s -o " + helpers::shell_escape(raw_avatar_path) + " " + helpers::shell_escape(avatar_url);
                    int dl_status = std::system(dl_cmd.c_str());
                    if (dl_status == 0 && std::filesystem::exists(raw_avatar_path) && std::filesystem::file_size(raw_avatar_path) > 0) {
                        // Use Leptonica to load the raw image and write it as PNG format
                        Pix* pix = pixRead(raw_avatar_path.c_str());
                        if (pix) {
                            int write_status = pixWrite(converted_avatar_png.c_str(), pix, IFF_PNG);
                            pixDestroy(&pix);
                            if (write_status == 0 && std::filesystem::exists(converted_avatar_png) && std::filesystem::file_size(converted_avatar_png) > 0) {
                                temp_avatar_png = converted_avatar_png;
                            }
                        }
                    }
                    if (std::filesystem::exists(raw_avatar_path)) std::filesystem::remove(raw_avatar_path);
                }

                // Download custom background image to a temp file if customized
                std::string temp_bg_png = "";
                if (urc_opt && urc_opt->bg_image) {
                    std::string bg_raw = *urc_opt->bg_image;
                    std::string bg_url = "";
                    if (bg_raw.find("://") != std::string::npos) {
                        bg_url = bg_raw;
                    } else {
                        bg_url = bot.config().cdn_url + "/attachments/" + bg_raw;
                    }

                    std::random_device rd;
                    std::mt19937 gen(rd());
                    std::uniform_int_distribution<> dis(100000, 999999);
                    std::string tmp_id = std::to_string(dis(gen));
                    std::string raw_bg_path = "/tmp/bg_raw_" + tmp_id;
                    std::string converted_bg_png = "/tmp/bg_" + tmp_id + ".png";

                    // Use shell curl to download the file
                    std::string dl_cmd = "curl -k -L -s -o " + helpers::shell_escape(raw_bg_path) + " " + helpers::shell_escape(bg_url);
                    int dl_status = std::system(dl_cmd.c_str());
                    if (dl_status == 0 && std::filesystem::exists(raw_bg_path) && std::filesystem::file_size(raw_bg_path) > 0) {
                        Pix* pix = pixRead(raw_bg_path.c_str());
                        if (pix) {
                            int write_status = pixWrite(converted_bg_png.c_str(), pix, IFF_PNG);
                            pixDestroy(&pix);
                            if (write_status == 0 && std::filesystem::exists(converted_bg_png) && std::filesystem::file_size(converted_bg_png) > 0) {
                                temp_bg_png = converted_bg_png;
                            }
                        }
                    }
                    if (std::filesystem::exists(raw_bg_path)) std::filesystem::remove(raw_bg_path);
                }

                // Resolve customization hex values
                std::string bg_color_hex = (urc_opt && urc_opt->bg_color) ? *urc_opt->bg_color : "";
                std::string active_hex = (urc_opt && urc_opt->bar_active) ? *urc_opt->bar_active : "";
                std::string dimmed_hex = (urc_opt && urc_opt->bar_dimmed) ? *urc_opt->bar_dimmed : "";
                std::string text_hex = (urc_opt && urc_opt->text_color) ? *urc_opt->text_color : "";
                std::string font_family = (urc_opt && urc_opt->font_name) ? *urc_opt->font_name : "";

                // Render the rank card PNG
                std::string png_bytes = chart::render_rank_card(
                    username, level, rank, total_users, current_xp, next_xp, base_xp, temp_avatar_png,
                    temp_bg_png, bg_color_hex, active_hex, dimmed_hex, text_hex, font_family
                );

                // Clean up temp files
                if (!temp_avatar_png.empty() && std::filesystem::exists(temp_avatar_png)) {
                    std::filesystem::remove(temp_avatar_png);
                }
                if (!temp_bg_png.empty() && std::filesystem::exists(temp_bg_png)) {
                    std::filesystem::remove(temp_bg_png);
                }

                if (png_bytes.empty()) {
                    bot.send_message(channel_id, "❌ Failed to render rank card.");
                    return;
                }

                // Upload to Autumn
                std::string file_id = chart::upload_chart(bot, png_bytes, "rank_card.png");
                if (file_id.empty()) {
                    bot.send_message(channel_id, "❌ Failed to upload rank card to CDN.");
                    return;
                }

                // Send the rank card as attachment
                stoatpp::models::MessagePayload payload;
                payload.add_attachment(file_id);
                bot.send_message(channel_id, payload);
            }).detach();
#else
            std::string out = "📊 **Rank progress for " + target_user.username + "**\n"
                              "✨ **Level**: " + std::to_string(level) + "\n"
                              "📈 **XP**: " + std::to_string(current_xp) + " / " + std::to_string(next_xp) + " (Level progress: " + std::to_string(percentage) + "%)\n" +
                              bar + "\n"
                              "🏆 **Rank**: #" + std::to_string(rank) + " of " + std::to_string(total_users);

            bot.send_message(msg.channel_id, out);
#endif
        });
    }

    static void handle_level_cmd(stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
        auto to_lower = [](std::string s) {
            for (auto& c : s) c = std::tolower(c);
            return s;
        };

        auto server_opt = bot.get_server(msg.server_id);
        if (!server_opt) {
            bot.send_message(msg.channel_id, "failed to fetch server info.");
            return;
        }

        std::vector<std::string> prefixes = { bot.config().command_prefix };
        if (bot.config().prefix_resolver) {
            prefixes = bot.config().prefix_resolver(bot, msg);
        }
        std::string pfx = bot.config().command_prefix;
        for (const auto& p : prefixes) {
            if (msg.content.rfind(p, 0) == 0) { pfx = p; break; }
        }

        auto send_help = [&bot, msg, pfx]() {
            bot.send_message(msg.channel_id,
                "**⚙️ Server Leveling Settings Guide**\n\n"
                "• `" + pfx + "level enable` — Enable the leveling system.\n"
                "• `" + pfx + "level disable` — Disable the leveling system.\n"
                "• `" + pfx + "level xprange <min-max | single_value | -1 | 0>` — Set random XP gain range per message (default: 15-25).\n"
                "• `" + pfx + "level role add <level> <@role>` — Configure a role reward for a level.\n"
                "• `" + pfx + "level role remove <level>` — Delete a level role reward.\n"
                "• `" + pfx + "level role sticky <on|off>` — Keep or remove previous level reward roles (default: on).\n"
                "• `" + pfx + "level config preview` — Show current server config.\n"
                "• `" + pfx + "level config cooldown <seconds>` — Message cooldown for XP (default: 60s).\n"
                "• `" + pfx + "level config minchars <number>` — Min message characters to earn XP (default: 5).\n"
                "• `" + pfx + "level config announce <on|off>` — Toggle level-up announcements.\n"
                "• `" + pfx + "level config channel <#channel | here | none>` — Set level-up announcement channel.\n"
                "• `" + pfx + "level config message <message...>` — Custom message (tokens: `{name}`, `{mention}`, `{level}`).\n"
                "• `" + pfx + "level config reset` — Reset server leveling configuration.\n"
                "• `" + pfx + "level config resetdata` — Clear all XP database records for the server."
            );
        };

        if (args.empty()) {
            send_help();
            return;
        }

        std::string sub = args[0];
        std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

        // Ensure config row exists
        g_db->create_guild_leveling_config(msg.server_id);
        auto cfg_opt = g_db->get_guild_leveling_config(msg.server_id);
        if (!cfg_opt) {
            bot.send_message(msg.channel_id, "⚠️ Failed to resolve guild leveling config.");
            return;
        }
        auto cfg = *cfg_opt;

        if (sub == "enable") {
            cfg.enabled = true;
            if (g_db->update_guild_leveling_config(cfg)) {
                bot.send_message(msg.channel_id, "✅ Leveling system enabled!");
            } else {
                bot.send_message(msg.channel_id, "❌ Failed to update settings.");
            }
        }
        else if (sub == "disable") {
            cfg.enabled = false;
            if (g_db->update_guild_leveling_config(cfg)) {
                bot.send_message(msg.channel_id, "✅ Leveling system disabled!");
            } else {
                bot.send_message(msg.channel_id, "❌ Failed to update settings.");
            }
        }
        else if (sub == "xprange") {
            if (args.size() < 2) {
                bot.send_message(msg.channel_id, "usage: `" + pfx + "level xprange <min-max | single_value | -1 | 0>`");
                return;
            }
            std::string range_arg = args[1];
            if (range_arg == "-1" || range_arg == "0" || range_arg == "default") {
                cfg.min_xp_per_message = 15;
                cfg.max_xp_per_message = 25;
            } else {
                auto dash_pos = range_arg.find('-');
                if (dash_pos != std::string::npos) {
                    try {
                        int min_xp = std::stoi(range_arg.substr(0, dash_pos));
                        int max_xp = std::stoi(range_arg.substr(dash_pos + 1));
                        if (min_xp <= 0 || max_xp <= 0 || min_xp > max_xp) {
                            bot.send_message(msg.channel_id, "❌ Invalid XP range. Must be positive numbers, e.g. `10-20`.");
                            return;
                        }
                        cfg.min_xp_per_message = min_xp;
                        cfg.max_xp_per_message = max_xp;
                    } catch (...) {
                        bot.send_message(msg.channel_id, "❌ Invalid range format. Try `15-25`.");
                        return;
                    }
                } else {
                    try {
                        int xp = std::stoi(range_arg);
                        if (xp <= 0) {
                            bot.send_message(msg.channel_id, "❌ XP must be positive.");
                            return;
                        }
                        cfg.min_xp_per_message = xp;
                        cfg.max_xp_per_message = xp;
                    } catch (...) {
                        bot.send_message(msg.channel_id, "❌ Invalid XP range format.");
                        return;
                    }
                }
            }

            if (g_db->update_guild_leveling_config(cfg)) {
                bot.send_message(msg.channel_id, "✅ XP gain range set to **" + std::to_string(cfg.min_xp_per_message) + "-" + std::to_string(cfg.max_xp_per_message) + "** XP per message.");
            } else {
                bot.send_message(msg.channel_id, "❌ Failed to update settings.");
            }
        }
        else if (sub == "role") {
            if (args.size() < 2) {
                bot.send_message(msg.channel_id, "usage: `" + pfx + "level role <add|remove|sticky> [args...]`");
                return;
            }
            std::string role_sub = args[1];
            std::transform(role_sub.begin(), role_sub.end(), role_sub.begin(), ::tolower);

            if (role_sub == "add") {
                if (args.size() < 4) {
                    bot.send_message(msg.channel_id, "usage: `" + pfx + "level role add <level> <@role>`");
                    return;
                }
                uint32_t lvl = 1;
                try {
                    int l = std::stoi(args[2]);
                    if (l <= 0) {
                        bot.send_message(msg.channel_id, "❌ Level must be greater than 0.");
                        return;
                    }
                    lvl = static_cast<uint32_t>(l);
                } catch (...) {
                    bot.send_message(msg.channel_id, "❌ Invalid level number.");
                    return;
                }

                std::string role_query = args[3];
                std::string role_id = clean_role_query(role_query);

                // Verify the role exists in the server
                bool found = false;
                std::string role_name = "";
                for (const auto& r : server_opt->roles) {
                    if (r.id == role_id || to_lower(r.name) == to_lower(role_id)) {
                        role_id = r.id;
                        role_name = r.name;
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    bot.send_message(msg.channel_id, "❌ Could not find specified role in this server.");
                    return;
                }

                Database::LevelRole reward;
                reward.server_id = msg.server_id;
                reward.level = lvl;
                reward.role_id = role_id;
                reward.role_name = role_name;

                if (g_db->create_level_role(reward)) {
                    bot.send_message(msg.channel_id, "✅ Added level reward: reaching **Level " + std::to_string(lvl) + "** will award role `<@&" + role_id + ">`.");
                } else {
                    bot.send_message(msg.channel_id, "❌ Failed to add level role reward.");
                }
            }
            else if (role_sub == "remove") {
                if (args.size() < 3) {
                    bot.send_message(msg.channel_id, "usage: `" + pfx + "level role remove <level>`");
                    return;
                }
                uint32_t lvl = 1;
                try {
                    lvl = std::stoul(args[2]);
                } catch (...) {
                    bot.send_message(msg.channel_id, "❌ Invalid level.");
                    return;
                }

                if (g_db->delete_level_role(msg.server_id, lvl)) {
                    bot.send_message(msg.channel_id, "✅ Removed level role reward for level **" + std::to_string(lvl) + "**.");
                } else {
                    bot.send_message(msg.channel_id, "❌ Failed to delete level role reward.");
                }
            }
            else if (role_sub == "sticky") {
                if (args.size() < 3) {
                    bot.send_message(msg.channel_id, "usage: `" + pfx + "level role sticky <on|off>`");
                    return;
                }
                std::string sticky_arg = args[2];
                std::transform(sticky_arg.begin(), sticky_arg.end(), sticky_arg.begin(), ::tolower);
                if (sticky_arg == "on" || sticky_arg == "true" || sticky_arg == "1") {
                    cfg.sticky_roles = true;
                } else if (sticky_arg == "off" || sticky_arg == "false" || sticky_arg == "0") {
                    cfg.sticky_roles = false;
                } else {
                    bot.send_message(msg.channel_id, "❌ Invalid sticky value. Choose `on` or `off`.");
                    return;
                }

                if (g_db->update_guild_leveling_config(cfg)) {
                    bot.send_message(msg.channel_id, "✅ Role stickiness set to **" + std::string(cfg.sticky_roles ? "on" : "off") + "**. " + (cfg.sticky_roles ? "(previous level roles are kept)" : "(previous level roles are cleaned up on level-up)"));
                } else {
                    bot.send_message(msg.channel_id, "❌ Failed to update settings.");
                }
            }
        }
        else if (sub == "config") {
            if (args.size() < 2) {
                bot.send_message(msg.channel_id, "usage: `" + pfx + "level config <preview|cooldown|minchars|announce|channel|message|reset|resetdata>`");
                return;
            }
            std::string cfg_sub = args[1];
            std::transform(cfg_sub.begin(), cfg_sub.end(), cfg_sub.begin(), ::tolower);

            if (cfg_sub == "preview") {
                std::string chan_label = cfg.announcement_channel ? "<#" + *cfg.announcement_channel + ">" : "*Current channel*";
                std::string rewards_desc = "";
                auto roles = g_db->get_level_roles(msg.server_id);
                if (roles.empty()) {
                    rewards_desc = "*None*";
                } else {
                    for (const auto& r : roles) {
                        rewards_desc += "• Level " + std::to_string(r.level) + ": <@&" + r.role_id + ">\n";
                    }
                }

                std::string out = "⚙️ **Server Leveling Settings Preview**\n\n"
                                  "• **System Enabled**: " + std::string(cfg.enabled ? "✅ Yes" : "❌ No") + "\n"
                                  "• **XP gain range**: " + std::to_string(cfg.min_xp_per_message) + "-" + std::to_string(cfg.max_xp_per_message) + " XP\n"
                                  "• **XP Message Cooldown**: " + std::to_string(cfg.xp_cooldown_seconds) + " seconds\n"
                                  "• **Minimum character length**: " + std::to_string(cfg.min_message_chars) + " characters\n"
                                  "• **Announce Level-Ups**: " + std::string(cfg.announce_levelup ? "✅ Yes" : "❌ No") + "\n"
                                  "• **Announcement Channel**: " + chan_label + "\n"
                                  "• **Announcement Message Template**: `" + cfg.announcement_message + "`\n"
                                  "• **Sticky Role Rewards**: " + std::string(cfg.sticky_roles ? "✅ Yes (keep old)" : "❌ No (cleanup old)") + "\n\n"
                                  "🏆 **Level Role Rewards:**\n" + rewards_desc;

                bot.send_message(msg.channel_id, out);
            }
            else if (cfg_sub == "cooldown") {
                if (args.size() < 3) {
                    bot.send_message(msg.channel_id, "usage: `" + pfx + "level config cooldown <seconds>`");
                    return;
                }
                try {
                    int cd = std::stoi(args[2]);
                    if (cd < 0) {
                        bot.send_message(msg.channel_id, "❌ Cooldown cannot be negative.");
                        return;
                    }
                    cfg.xp_cooldown_seconds = cd;
                    if (g_db->update_guild_leveling_config(cfg)) {
                        bot.send_message(msg.channel_id, "✅ Cooldown updated to **" + std::to_string(cd) + "** seconds.");
                    } else {
                        bot.send_message(msg.channel_id, "❌ Failed to save changes.");
                    }
                } catch (...) {
                    bot.send_message(msg.channel_id, "❌ Invalid cooldown number.");
                }
            }
            else if (cfg_sub == "minchars") {
                if (args.size() < 3) {
                    bot.send_message(msg.channel_id, "usage: `" + pfx + "level config minchars <number>`");
                    return;
                }
                try {
                    int mc = std::stoi(args[2]);
                    if (mc < 0) {
                        bot.send_message(msg.channel_id, "❌ Minimum characters cannot be negative.");
                        return;
                    }
                    cfg.min_message_chars = mc;
                    if (g_db->update_guild_leveling_config(cfg)) {
                        bot.send_message(msg.channel_id, "✅ Minimum character length set to **" + std::to_string(mc) + "**.");
                    } else {
                        bot.send_message(msg.channel_id, "❌ Failed to save changes.");
                    }
                } catch (...) {
                    bot.send_message(msg.channel_id, "❌ Invalid characters count.");
                }
            }
            else if (cfg_sub == "announce") {
                if (args.size() < 3) {
                    bot.send_message(msg.channel_id, "usage: `" + pfx + "level config announce <on|off>`");
                    return;
                }
                std::string ann_arg = args[2];
                std::transform(ann_arg.begin(), ann_arg.end(), ann_arg.begin(), ::tolower);
                if (ann_arg == "on" || ann_arg == "true" || ann_arg == "1") {
                    cfg.announce_levelup = true;
                } else if (ann_arg == "off" || ann_arg == "false" || ann_arg == "0") {
                    cfg.announce_levelup = false;
                } else {
                    bot.send_message(msg.channel_id, "❌ Choose `on` or `off`.");
                    return;
                }

                if (g_db->update_guild_leveling_config(cfg)) {
                    bot.send_message(msg.channel_id, "✅ Level-up announcement set to **" + std::string(cfg.announce_levelup ? "enabled" : "disabled") + "**.");
                } else {
                    bot.send_message(msg.channel_id, "❌ Failed to save changes.");
                }
            }
            else if (cfg_sub == "channel") {
                if (args.size() < 3) {
                    bot.send_message(msg.channel_id, "usage: `" + pfx + "level config channel <#channel | here | none>`");
                    return;
                }
                std::string chan_arg = args[2];
                if (chan_arg == "none" || chan_arg == "clear") {
                    cfg.announcement_channel = std::nullopt;
                    bot.send_message(msg.channel_id, "✅ Level-up announcement channel set to default *Current channel*.");
                } else if (chan_arg == "here") {
                    cfg.announcement_channel = msg.channel_id;
                    bot.send_message(msg.channel_id, "✅ Level-up announcement channel set to current channel (<#" + msg.channel_id + ">).");
                } else {
                    if (chan_arg.rfind("<#", 0) == 0 && chan_arg.back() == '>') {
                        chan_arg = chan_arg.substr(2, chan_arg.size() - 3);
                    }
                    cfg.announcement_channel = chan_arg;
                    bot.send_message(msg.channel_id, "✅ Level-up announcement channel set to <#" + chan_arg + ">.");
                }

                g_db->update_guild_leveling_config(cfg);
            }
            else if (cfg_sub == "message") {
                if (args.size() < 3) {
                    bot.send_message(msg.channel_id, "usage: `" + pfx + "level config message <message...>`");
                    return;
                }
                size_t p = msg.content.find("message");
                if (p != std::string::npos) {
                    p += 7;
                    while (p < msg.content.size() && msg.content[p] == ' ') p++;
                }
                std::string val_msg = (p != std::string::npos && p < msg.content.size()) ? msg.content.substr(p) : "";
                if (val_msg.empty()) {
                    bot.send_message(msg.channel_id, "❌ Custom message cannot be empty.");
                    return;
                }

                cfg.announcement_message = val_msg;
                if (g_db->update_guild_leveling_config(cfg)) {
                    bot.send_message(msg.channel_id, "✅ Level-up custom message updated!");
                } else {
                    bot.send_message(msg.channel_id, "❌ Failed to save changes.");
                }
            }
            else if (cfg_sub == "reset") {
                Database::GuildLevelingConfig defaults;
                defaults.server_id = msg.server_id;
                if (g_db->update_guild_leveling_config(defaults)) {
                    bot.send_message(msg.channel_id, "✅ Leveling config reset to default settings.");
                } else {
                    bot.send_message(msg.channel_id, "❌ Failed to reset settings.");
                }
            }
            else if (cfg_sub == "resetdata") {
                if (g_db->reset_guild_xp(msg.server_id)) {
                    bot.send_message(msg.channel_id, "✅ Cleared all server members' XP data.");
                } else {
                    bot.send_message(msg.channel_id, "❌ Failed to clear XP data.");
                }
            }
        } else {
            send_help();
        }
    }
};

} // namespace bronx::cogs
