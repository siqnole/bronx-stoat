#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include <vector>
#include <string>
#include <algorithm>
#include <iostream>
#include <thread>

#ifdef BRONX_CHART_ENABLED
#include "chart_renderer.h"
#endif

namespace bronx::cogs {

inline int parse_timeframe(const std::string& input, int default_days = 7) {
    if (input.empty()) return default_days;
    std::string clean = input;
    std::transform(clean.begin(), clean.end(), clean.begin(), [](unsigned char c) { return std::tolower(c); });
    
    try {
        if (clean.back() == 'd') return std::max(1, std::stoi(clean.substr(0, clean.size() - 1)));
        if (clean.back() == 'w') return std::max(1, std::stoi(clean.substr(0, clean.size() - 1)) * 7);
        if (clean.back() == 'm') return std::max(1, std::stoi(clean.substr(0, clean.size() - 1)) * 30);
        if (clean.back() == 'y') return std::max(1, std::stoi(clean.substr(0, clean.size() - 1)) * 365);
        int days = std::stoi(clean);
        if (days > 0) return days;
    } catch (...) {}
    return default_days;
}

class StatsModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "stats",
            {"serverstats", "telemetry"},
            "shows server/user telemetry: overview, top active users, channels, chart, global top lists, or your own stats.",
            "stats [overview/top/users/channels/chart/me] [g/global] [timeframe]",
            {"[subcommand]", "[global/timeframe]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                bool include_bots = false;
                std::vector<std::string> clean_args;
                for (const auto& arg : args) {
                    std::string lower_arg = arg;
                    std::transform(lower_arg.begin(), lower_arg.end(), lower_arg.begin(), [](unsigned char c) { return std::tolower(c); });
                    if (lower_arg == "bots" || lower_arg == "bot" || lower_arg == "b") {
                        include_bots = true;
                    } else {
                        clean_args.push_back(arg);
                    }
                }

                if (!g_db) {
                    bot.send_message(msg.channel_id, "database connection is currently unavailable.");
                    return;
                }

                bool is_me = false;
                bool me_global = false;
                std::string tf_str = "";

                if (!clean_args.empty()) {
                    std::string first = clean_args[0];
                    std::transform(first.begin(), first.end(), first.begin(), [](unsigned char c) { return std::tolower(c); });
                    if (first == "me") {
                        is_me = true;
                        if (clean_args.size() > 1) {
                            std::string second = clean_args[1];
                            std::transform(second.begin(), second.end(), second.begin(), [](unsigned char c) { return std::tolower(c); });
                            if (second == "g" || second == "global") {
                                me_global = true;
                                if (clean_args.size() > 2) tf_str = clean_args[2];
                            } else {
                                tf_str = clean_args[1];
                            }
                        }
                    }
                }

                if (is_me) {
                    int days = parse_timeframe(tf_str, 7);
                    if (me_global) {
                        auto stats = g_db->get_user_global_stats(msg.author.id, days);
                        
                        stoatpp::models::MessagePayload payload;
                        payload.content = " ";
                        stoatpp::models::Embed embed;
                        embed.set_colour(0x5865F2);
                        embed.set_title("🌐 global stats for " + msg.author.username + " (" + std::to_string(days) + "d)");
                        
                        std::string desc;
                        if (stats.empty()) {
                            desc = "*No global activity recorded for you in this period.*";
                        } else {
                            int rank = 1;
                            int total_msgs = 0;
                            int total_cmds = 0;
                            std::string server_list;
                            for (const auto& s : stats) {
                                std::string s_name = "Server " + s.guild_id;
                                auto s_opt = bot.get_server(s.guild_id);
                                if (s_opt) s_name = s_opt->name;
                                server_list += std::to_string(rank++) + ". **" + s_name + "** — " 
                                    + std::to_string(s.messages) + " msgs, " 
                                    + std::to_string(s.commands_used) + " cmds\n";
                                total_msgs += s.messages;
                                total_cmds += s.commands_used;
                            }
                            desc += "**Total Activity:** " + std::to_string(total_msgs) + " messages, " 
                                + std::to_string(total_cmds) + " commands used\n\n"
                                + "### 🌐 Breakdown by Server\n" + server_list;
                        }
                        embed.set_description(desc);
                        payload.add_embed(embed);
                        bot.send_message(msg.channel_id, payload);
                        return;
                    } else {
                        if (msg.server_id.empty()) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Channel-level stats can only be viewed inside a server.");
                            return;
                        }
                        
                        auto stats = g_db->get_user_channel_stats(msg.server_id, msg.author.id, days);
                        
                        auto srv_opt = bot.get_server(msg.server_id);
                        std::string srv_name = srv_opt ? srv_opt->name : "this server";
                        
                        stoatpp::models::MessagePayload payload;
                        payload.content = " ";
                        stoatpp::models::Embed embed;
                        embed.set_colour(0x5865F2);
                        embed.set_title("📊 your stats — " + srv_name + " (" + std::to_string(days) + "d)");
                        
                        std::string desc;
                        if (stats.empty()) {
                            desc = "*No activity recorded for you in this server during this period.*";
                        } else {
                            int rank = 1;
                            int total_msgs = 0;
                            int total_cmds = 0;
                            std::string channel_list;
                            for (const auto& c : stats) {
                                channel_list += std::to_string(rank++) + ". <#" + c.channel_id + "> — " 
                                    + std::to_string(c.messages) + " msgs, " 
                                    + std::to_string(c.commands_used) + " cmds\n";
                                total_msgs += c.messages;
                                total_cmds += c.commands_used;
                            }
                            desc += "**Total Activity:** " + std::to_string(total_msgs) + " messages, " 
                                + std::to_string(total_cmds) + " commands used\n\n"
                                + "### 💬 Breakdown by Channel\n" + channel_list;
                        }
                        embed.set_description(desc);
                        payload.add_embed(embed);
                        bot.send_message(msg.channel_id, payload);
                        return;
                    }
                }

                bool is_cmd_arg = (!clean_args.empty() && (clean_args[0] == "command" || clean_args[0] == "commands" || clean_args[0] == "cmd" || clean_args[0] == "cmds"));
                bool is_global_arg = (clean_args.size() > 1 && (clean_args[1] == "g" || clean_args[1] == "global" || clean_args[1] == "all"));

                if (msg.server_id.empty() && !clean_args.empty() && clean_args[0] != "top" && clean_args[0] != "t" && !(is_cmd_arg && is_global_arg)) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Non-global stats commands can only be used inside a server.");
                    return;
                }

                std::string sub = "overview";
                bool global_top = false;

                if (!clean_args.empty()) {
                    std::string first = clean_args[0];
                    std::transform(first.begin(), first.end(), first.begin(), [](unsigned char c) { return std::tolower(c); });

                    if (first == "top" || first == "t") {
                        sub = "top";
                        if (clean_args.size() > 1) {
                            std::string second = clean_args[1];
                            std::transform(second.begin(), second.end(), second.begin(), [](unsigned char c) { return std::tolower(c); });
                            if (second == "g" || second == "global" || second == "all") {
                                global_top = true;
                                if (clean_args.size() > 2) tf_str = clean_args[2];
                            } else {
                                tf_str = clean_args[1];
                            }
                        }
                    } else if (first == "user" || first == "users" || first == "member" || first == "members" || first == "u" || first == "m") {
                        sub = "users";
                        if (clean_args.size() > 1) tf_str = clean_args[1];
                    } else if (first == "channel" || first == "channels" || first == "c") {
                        sub = "channels";
                        if (clean_args.size() > 1) tf_str = clean_args[1];
                    } else if (first == "command" || first == "commands" || first == "cmd" || first == "cmds") {
                        sub = "commands";
                        if (clean_args.size() > 1) {
                            std::string second = clean_args[1];
                            std::transform(second.begin(), second.end(), second.begin(), [](unsigned char c) { return std::tolower(c); });
                            if (second == "g" || second == "global" || second == "all") {
                                global_top = true;
                                if (clean_args.size() > 2) tf_str = clean_args[2];
                            } else {
                                tf_str = clean_args[1];
                            }
                        }
                    } else if (first == "overview" || first == "o") {
                        sub = "overview";
                        if (clean_args.size() > 1) tf_str = clean_args[1];
                    } else if (first == "chart" || first == "graph" || first == "viz") {
                        sub = "chart";
                        if (clean_args.size() > 1) tf_str = clean_args[1];
                        if (clean_args.size() > 2) tf_str = clean_args[2];  // allow: .stats chart line 7d
                    } else {
                        tf_str = clean_args[0];
                    }
                }


                int days = parse_timeframe(tf_str, 7);

                // ── chart subcommand ────────────────────────────────────────
                if (sub == "chart") {
#ifdef BRONX_CHART_ENABLED
                    if (msg.server_id.empty()) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " chart stats can only be used inside a server.");
                        return;
                    }

                    // Determine chart type from clean_args
                    std::string chart_type = "line";  // default
                    if (clean_args.size() > 1) {
                        std::string ct = clean_args[1];
                        std::transform(ct.begin(), ct.end(), ct.begin(), [](unsigned char c) { return std::tolower(c); });
                        if (ct == "heatmap" || ct == "heat" || ct == "h") chart_type = "heatmap";
                        else if (ct == "commands" || ct == "cmds")        chart_type = "commands";
                        else if (ct == "bars" || ct == "bar")             chart_type = "bars";
                    }

                    auto srv_opt = bot.get_server(msg.server_id);
                    std::string srv_name = srv_opt ? srv_opt->name : "server";

                    // Run chart generation + upload on background thread so we don't block gateway
                    std::string server_id_copy = msg.server_id;
                    std::string channel_id_copy = msg.channel_id;

                    bot.send_message(msg.channel_id, "⏳ rendering chart...", [&bot, server_id_copy, channel_id_copy, days, chart_type, srv_name, include_bots](stoatpp::models::Message sent_msg, bool success) {
                        if (!success) return;
                        std::string status_msg_id = sent_msg.id;
                        std::thread([&bot, server_id_copy, channel_id_copy, status_msg_id, days, chart_type, srv_name, include_bots]() {
                            try {
                                std::string png;

                                if (chart_type == "heatmap") {
                                    auto matrix = g_db->get_hourly_heatmap(server_id_copy, days, include_bots);
                                    std::vector<std::string> days_lbl = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
                                    std::vector<std::string> hrs_lbl;
                                    for (int h = 0; h < 24; ++h) {
                                        char buf[4]; snprintf(buf, sizeof(buf), "%dh", h);
                                        hrs_lbl.push_back(buf);
                                    }
                                    png = chart::render_heatmap(
                                        "activity heatmap — " + srv_name + " (" + std::to_string(days) + "d)",
                                        days_lbl, hrs_lbl, matrix, 700);

                                } else if (chart_type == "commands") {
                                    auto top_cmds = g_db->get_top_commands(server_id_copy, days, 10);
                                    std::vector<chart::BarItem> items;
                                    for (auto& c : top_cmds)
                                        items.push_back({c.command_name, c.use_count});
                                    png = chart::render_horizontal_bar_chart(
                                        "top commands — " + srv_name + " (" + std::to_string(days) + "d)",
                                        items);

                                } else if (chart_type == "bars") {
                                    auto top_users = g_db->get_top_users(server_id_copy, days, include_bots, 10);
                                    std::vector<chart::BarItem> items;
                                    for (auto& u : top_users) {
                                        std::string display = "User " + u.user_id.substr(0, 6);
                                        auto u_opt = bot.get_user(u.user_id);
                                        if (u_opt) {
                                            display = u_opt->username;
                                            if (!u_opt->discriminator.empty() && u_opt->discriminator != "0000") {
                                                display += "#" + u_opt->discriminator;
                                            } else {
                                                display += "#0000";
                                            }
                                        }
                                        items.push_back({display, u.message_count});
                                    }
                                    png = chart::render_horizontal_bar_chart(
                                        "top members — " + srv_name + " (" + std::to_string(days) + "d)",
                                        items);

                                } else {
                                    // Default: line chart — daily message trend
                                    auto series_data = g_db->get_daily_message_series(server_id_copy, days, include_bots);
                                    std::vector<std::string> labels;
                                    std::vector<double> values;
                                    for (auto& [date, count] : series_data) {
                                        labels.push_back(date);
                                        values.push_back(static_cast<double>(count));
                                    }
                                    if (labels.empty()) {
                                        bot.edit_message(channel_id_copy, status_msg_id, "no message data found for this period.");
                                        return;
                                    }
                                    chart::LineSeries ls{"messages", chart::COL_ACCENT, values};
                                    png = chart::render_line_chart(
                                        "daily messages — " + srv_name + " (" + std::to_string(days) + "d)",
                                        labels, {ls});
                                }

                                if (png.empty()) {
                                    bot.edit_message(channel_id_copy, status_msg_id, "" + bronx::emj_warning + " chart rendering failed.");
                                    return;
                                }

                                std::string autumn_id = chart::upload_chart(bot, png, chart_type + "_chart.png");
                                if (autumn_id.empty()) {
                                    bot.edit_message(channel_id_copy, status_msg_id, "" + bronx::emj_warning + " failed to upload chart to autumn.");
                                    return;
                                }

                                stoatpp::models::MessagePayload payload;
                                payload.content = " ";
                                stoatpp::models::Embed embed;
                                embed.set_colour(0xb4a7d6);
                                embed.set_title("📊 " + chart_type + " chart — " + srv_name);
                                embed.set_image(autumn_id);
                                payload.add_embed(embed);
                                bot.edit_message(channel_id_copy, status_msg_id, payload);

                            } catch (const std::exception& ex) {
                                std::cerr << "[bronx chart] exception: " << ex.what() << "\n";
                                bot.edit_message(channel_id_copy, status_msg_id, "" + bronx::emj_warning + " chart error: " + std::string(ex.what()));
                            }
                        }).detach();
                    });
                    return;
#else
                    bot.send_message(msg.channel_id,
                        "" + bronx::emj_warning + " chart rendering is not enabled in this build (Cairo not found at compile time).");
                    return;
#endif
                }

                // ── all other subcommands send a text embed ─────────────────
                stoatpp::models::MessagePayload payload;
                payload.content = " "; 
                stoatpp::models::Embed embed;
                embed.set_colour(0x5865F2);

                if (sub == "overview") {
                    auto totals = g_db->get_server_totals(msg.server_id, days, include_bots);
                    embed.set_title("📈 bronx stats — overview (" + std::to_string(days) + "d)");
                    
                    std::string desc = 
                        "total messages: **" + std::to_string(totals.total_messages) + "**\n"
                        "active users: **" + std::to_string(totals.active_users) + "**\n"
                        "commands used: **" + std::to_string(totals.total_commands) + "**\n"
                        "\n###### use `.stats chart` to visualize message trends";
                    embed.set_description(desc);
                } 
                else if (sub == "top" && global_top) {
                    auto top_srv = g_db->get_top_servers(days, include_bots, 10);
                    auto top_usr = g_db->get_top_users_global(days, false, 10);
                    auto top_bot = g_db->get_top_users_global(days, true, 10);

                    embed.set_title("🌐 global stats leaderboard (" + std::to_string(days) + "d)");

                    std::string desc;
                    desc += "### 🌐 Top Active Servers\n";
                    if (top_srv.empty()) {
                        desc += "*No server activity recorded.*\n";
                    } else {
                        int rank = 1;
                        for (const auto& s : top_srv) {
                            std::string s_name = "Server " + s.guild_id;
                            auto s_opt = bot.get_server(s.guild_id);
                            if (s_opt) s_name = s_opt->name;
                            desc += std::to_string(rank++) + ". **" + s_name + "** — " + std::to_string(s.message_count) + " msgs\n";
                        }
                    }
                    desc += "\n";

                    desc += "### 👤 Top Active Users\n";
                    if (top_usr.empty()) {
                        desc += "*No user activity recorded.*\n";
                    } else {
                        int rank = 1;
                        for (const auto& u : top_usr)
                            desc += std::to_string(rank++) + ". <@" + u.user_id + "> — " + std::to_string(u.message_count) + " msgs\n";
                    }
                    desc += "\n";

                    if (include_bots) {
                        desc += "### 🤖 Top Active Bots\n";
                        if (top_bot.empty()) {
                            desc += "*No bot activity recorded.*\n";
                        } else {
                            int rank = 1;
                            for (const auto& b : top_bot)
                                desc += std::to_string(rank++) + ". <@" + b.user_id + "> — " + std::to_string(b.message_count) + " msgs\n";
                        }
                    }
                    embed.set_description(desc);
                }
                else if (sub == "top") {
                    if (msg.server_id.empty()) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " run this inside a server, or use `.stats top g` for the global leaderboard.");
                        return;
                    }

                    auto top_usr  = g_db->get_top_users(msg.server_id, days, include_bots, 10);
                    auto top_chan = g_db->get_top_channels(msg.server_id, days, 10);

                    auto srv_opt = bot.get_server(msg.server_id);
                    std::string srv_name = srv_opt ? srv_opt->name : "this server";
                    embed.set_title("🏆 top 10 — " + srv_name + " (" + std::to_string(days) + "d)");

                    std::string desc;
                    desc += "### 👤 Top Members\n";
                    if (top_usr.empty()) {
                        desc += "*No member activity recorded.*\n";
                    } else {
                        int rank = 1;
                        for (const auto& u : top_usr)
                            desc += std::to_string(rank++) + ". <@" + u.user_id + "> — " + std::to_string(u.message_count) + " msgs\n";
                    }
                    desc += "\n";

                    desc += "### 💬 Top Channels\n";
                    if (top_chan.empty()) {
                        desc += "*No channel activity recorded.*\n";
                    } else {
                        int rank = 1;
                        for (const auto& c : top_chan)
                            desc += std::to_string(rank++) + ". <#" + c.channel_id + "> — " + std::to_string(c.message_count) + " msgs\n";
                    }
                    desc += "\n###### use `.stats top g` for the global leaderboard · `.stats chart` to visualize";
                    embed.set_description(desc);
                } 
                else if (sub == "users") {
#ifdef BRONX_CHART_ENABLED
                    auto srv_opt = bot.get_server(msg.server_id);
                    std::string srv_name = srv_opt ? srv_opt->name : "server";
                    std::string server_id_copy = msg.server_id;
                    std::string channel_id_copy = msg.channel_id;

                    bot.send_message(msg.channel_id, "⏳ rendering members chart...", [&bot, server_id_copy, channel_id_copy, days, srv_name, include_bots](stoatpp::models::Message sent_msg, bool success) {
                        if (!success) return;
                        std::string status_msg_id = sent_msg.id;
                        std::thread([&bot, server_id_copy, channel_id_copy, status_msg_id, days, srv_name, include_bots]() {
                            try {
                                auto top_users = g_db->get_top_users(server_id_copy, days, include_bots, 10);
                                if (top_users.empty()) {
                                    bot.edit_message(channel_id_copy, status_msg_id, "no member activity recorded in this period.");
                                    return;
                                }

                                std::vector<chart::BarItem> items;
                                std::string text_desc = "";
                                int rank = 1;
                                for (const auto& u : top_users) {
                                    std::string display = "User " + u.user_id.substr(0, 6);
                                    auto u_opt = bot.get_user(u.user_id);
                                    if (u_opt) {
                                        display = u_opt->username;
                                        if (!u_opt->discriminator.empty() && u_opt->discriminator != "0000") {
                                            display += "#" + u_opt->discriminator;
                                        } else {
                                            display += "#0000";
                                        }
                                    }
                                    items.push_back({display, u.message_count});
                                    text_desc += std::to_string(rank++) + ". <@" + u.user_id + "> — " + std::to_string(u.message_count) + " messages\n";
                                }

                                std::string png = chart::render_horizontal_bar_chart(
                                    "active members — " + srv_name + " (" + std::to_string(days) + "d)",
                                    items);

                                std::string autumn_id = chart::upload_chart(bot, png, "members_chart.png");

                                stoatpp::models::MessagePayload payload;
                                payload.content = " ";
                                stoatpp::models::Embed embed;
                                embed.set_colour(0x5865F2);
                                embed.set_title("👤 active members leaderboard (" + std::to_string(days) + "d)");
                                embed.set_description(text_desc);
                                if (!autumn_id.empty()) {
                                    embed.set_image(autumn_id);
                                }
                                payload.add_embed(embed);
                                bot.edit_message(channel_id_copy, status_msg_id, payload);
                            } catch (const std::exception& ex) {
                                bot.edit_message(channel_id_copy, status_msg_id, "" + bronx::emj_warning + " members chart error: " + std::string(ex.what()));
                            }
                        }).detach();
                    });
                    return;
#else
                    auto top_users = g_db->get_top_users(msg.server_id, days, include_bots, 10);
                    embed.set_title("👤 active members leaderboard (" + std::to_string(days) + "d)");
                    std::string desc;
                    if (top_users.empty()) {
                        desc = "no member activity recorded in this period.";
                    } else {
                        int rank = 1;
                        for (const auto& u : top_users)
                            desc += std::to_string(rank++) + ". <@" + u.user_id + "> — " + std::to_string(u.message_count) + " messages\n";
                    }
                    embed.set_description(desc);
#endif
                } 
                else if (sub == "channels") {
#ifdef BRONX_CHART_ENABLED
                    auto srv_opt = bot.get_server(msg.server_id);
                    std::string srv_name = srv_opt ? srv_opt->name : "server";
                    std::string server_id_copy = msg.server_id;
                    std::string channel_id_copy = msg.channel_id;

                    bot.send_message(msg.channel_id, "⏳ rendering channels chart...", [&bot, server_id_copy, channel_id_copy, days, srv_name](stoatpp::models::Message sent_msg, bool success) {
                        if (!success) return;
                        std::string status_msg_id = sent_msg.id;
                        std::thread([&bot, server_id_copy, channel_id_copy, status_msg_id, days, srv_name]() {
                            try {
                                auto top_chans = g_db->get_top_channels(server_id_copy, days, 10);
                                if (top_chans.empty()) {
                                    bot.edit_message(channel_id_copy, status_msg_id, "no channel message telemetry recorded in this period.");
                                    return;
                                }

                                std::vector<chart::PieSlice> slices;
                                std::string text_desc = "";
                                int rank = 1;
                                for (const auto& c : top_chans) {
                                    std::string ch_name = "channel";
                                    auto ch_opt = bot.get_channel(c.channel_id);
                                    if (ch_opt) ch_name = ch_opt->name.value_or("channel");
                                    slices.push_back({ch_name, static_cast<double>(c.message_count), {0,0,0,0}});
                                    text_desc += std::to_string(rank++) + ". <#" + c.channel_id + "> — " + std::to_string(c.message_count) + " messages\n";
                                }

                                std::string png = chart::render_pie_chart(
                                    "channel activity — " + srv_name + " (" + std::to_string(days) + "d)",
                                    slices);

                                std::string autumn_id = chart::upload_chart(bot, png, "channels_pie_chart.png");

                                stoatpp::models::MessagePayload payload;
                                payload.content = " ";
                                stoatpp::models::Embed embed;
                                embed.set_colour(0x5865F2);
                                embed.set_title("💬 active channels leaderboard (" + std::to_string(days) + "d)");
                                embed.set_description(text_desc);
                                if (!autumn_id.empty()) {
                                    embed.set_image(autumn_id);
                                }
                                payload.add_embed(embed);
                                bot.edit_message(channel_id_copy, status_msg_id, payload);
                            } catch (const std::exception& ex) {
                                bot.edit_message(channel_id_copy, status_msg_id, "" + bronx::emj_warning + " channels chart error: " + std::string(ex.what()));
                            }
                        }).detach();
                    });
                    return;
#else
                    auto top_chans = g_db->get_top_channels(msg.server_id, days, 10);
                    embed.set_title("💬 active channels leaderboard (" + std::to_string(days) + "d)");
                    std::string desc;
                    if (top_chans.empty()) {
                        desc = "no channel message telemetry recorded in this period.";
                    } else {
                        int rank = 1;
                        for (const auto& c : top_chans)
                            desc += std::to_string(rank++) + ". <#" + c.channel_id + "> — " + std::to_string(c.message_count) + " messages\n";
                    }
                    embed.set_description(desc);
#endif
                }
                else if (sub == "commands") {
#ifdef BRONX_CHART_ENABLED
                    if (!global_top && msg.server_id.empty()) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Server command stats can only be used inside a server. Use `.stats command g` for global command stats.");
                        return;
                    }

                    auto srv_opt = bot.get_server(msg.server_id);
                    std::string srv_name = srv_opt ? srv_opt->name : "server";
                    std::string server_id_copy = msg.server_id;
                    std::string channel_id_copy = msg.channel_id;

                    std::string status_text = global_top ? "⏳ rendering global commands pie chart..." : "⏳ rendering commands pie chart...";

                    bot.send_message(msg.channel_id, status_text, [&bot, server_id_copy, channel_id_copy, days, srv_name, global_top](stoatpp::models::Message sent_msg, bool success) {
                        if (!success) return;
                        std::string status_msg_id = sent_msg.id;
                        std::thread([&bot, server_id_copy, channel_id_copy, status_msg_id, days, srv_name, global_top]() {
                            try {
                                std::vector<Database::TopCmdEntry> top_cmds;
                                if (global_top) {
                                    top_cmds = g_db->get_top_commands_global(days, 10);
                                } else {
                                    top_cmds = g_db->get_top_commands(server_id_copy, days, 10);
                                }

                                if (top_cmds.empty()) {
                                    bot.edit_message(channel_id_copy, status_msg_id, "no command usage telemetry recorded in this period.");
                                    return;
                                }

                                std::vector<chart::PieSlice> slices;
                                std::string text_desc = "";
                                int rank = 1;
                                for (const auto& c : top_cmds) {
                                    slices.push_back({"." + c.command_name, static_cast<double>(c.use_count), {0,0,0,0}});
                                    text_desc += std::to_string(rank++) + ". **." + c.command_name + "** — " + std::to_string(c.use_count) + " uses\n";
                                }

                                std::string title_str = global_top ?
                                    ("most used commands (global) (" + std::to_string(days) + "d)") :
                                    ("most used commands — " + srv_name + " (" + std::to_string(days) + "d)");

                                std::string png = chart::render_pie_chart(title_str, slices);

                                std::string autumn_id = chart::upload_chart(bot, png, "commands_pie_chart.png");

                                stoatpp::models::MessagePayload payload;
                                payload.content = " ";
                                stoatpp::models::Embed embed;
                                embed.set_colour(0x5865F2);
                                embed.set_title(global_top ?
                                    ("⚙️ most used commands (global) (" + std::to_string(days) + "d)") :
                                    ("⚙️ most used commands — " + srv_name + " (" + std::to_string(days) + "d)"));
                                
                                std::string desc = text_desc;
                                if (!global_top) {
                                    desc += "\n###### use `.stats command g` for global command stats";
                                }
                                embed.set_description(desc);

                                if (!autumn_id.empty()) {
                                    embed.set_image(autumn_id);
                                }
                                payload.add_embed(embed);
                                bot.edit_message(channel_id_copy, status_msg_id, payload);
                            } catch (const std::exception& ex) {
                                bot.edit_message(channel_id_copy, status_msg_id, "" + bronx::emj_warning + " commands chart error: " + std::string(ex.what()));
                            }
                        }).detach();
                    });
                    return;
#else
                    if (!global_top && msg.server_id.empty()) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Server command stats can only be used inside a server. Use `.stats command g` for global command stats.");
                        return;
                    }

                    std::vector<Database::TopCmdEntry> top_cmds;
                    if (global_top) {
                        top_cmds = g_db->get_top_commands_global(days, 10);
                    } else {
                        top_cmds = g_db->get_top_commands(msg.server_id, days, 10);
                    }

                    auto srv_opt = bot.get_server(msg.server_id);
                    std::string srv_name = srv_opt ? srv_opt->name : "this server";

                    embed.set_title(global_top ?
                        ("⚙️ most used commands (global) (" + std::to_string(days) + "d)") :
                        ("⚙️ most used commands — " + srv_name + " (" + std::to_string(days) + "d)"));
                    std::string desc;
                    if (top_cmds.empty()) {
                        desc = "no command usage telemetry recorded in this period.";
                    } else {
                        int rank = 1;
                        for (const auto& c : top_cmds)
                            desc += std::to_string(rank++) + ". **." + c.command_name + "** — " + std::to_string(c.use_count) + " uses\n";
                    }
                    if (!global_top) {
                        desc += "\n###### use `.stats command g` for global command stats";
                    }
                    embed.set_description(desc);
#endif
                }

                payload.add_embed(embed);
                bot.send_message(msg.channel_id, payload);
            },
            "Stats"
        });

        bot.register_command({
            "cmdstats",
            {"commandstats", "topcommands", "topcmds"},
            "shows most used commands in the server or globally with a pie chart.",
            "cmdstats [g/global] [timeframe]",
            {"[global/timeframe]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::vector<std::string> forwarded_args = {"command"};
                forwarded_args.insert(forwarded_args.end(), args.begin(), args.end());
                auto cmd_opt = bot.get_commands();
                for (const auto& c : cmd_opt) {
                    if (c.name == "stats") {
                        if (c.callback) {
                            c.callback(bot, msg, forwarded_args);
                        }
                        break;
                    }
                }
            },
            "Stats"
        });
    }
};



} // namespace bronx::cogs
