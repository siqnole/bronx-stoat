#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include <vector>
#include <string>
#include <algorithm>
#include <iostream>

namespace bronx::cogs {

inline int parse_timeframe(const std::string& input, int default_days = 7) {
    if (input.empty()) return default_days;
    std::string clean = input;
    std::transform(clean.begin(), clean.end(), clean.begin(), [](unsigned char c) { return std::tolower(c); });
    
    try {
        if (clean.back() == 'd') {
            return std::max(1, std::stoi(clean.substr(0, clean.size() - 1)));
        }
        if (clean.back() == 'w') {
            return std::max(1, std::stoi(clean.substr(0, clean.size() - 1)) * 7);
        }
        if (clean.back() == 'm') {
            return std::max(1, std::stoi(clean.substr(0, clean.size() - 1)) * 30);
        }
        if (clean.back() == 'y') {
            return std::max(1, std::stoi(clean.substr(0, clean.size() - 1)) * 365);
        }
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
            "shows server telemetry: overview, top active users, channels, or global top lists.",
            "stats [overview/top/users/channels] [timeframe]",
            {"[subcommand]", "[timeframe]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (msg.server_id.empty() && !args.empty() && args[0] != "top" && args[0] != "t") {
                    bot.send_message(msg.channel_id, "⚠️ Non-global stats commands can only be used inside a server.");
                    return;
                }

                if (!g_db) {
                    bot.send_message(msg.channel_id, "database connection is currently unavailable.");
                    return;
                }

                std::string sub = "overview";
                std::string tf_str = "";
                bool global_top = false;

                if (!args.empty()) {
                    std::string first = args[0];
                    std::transform(first.begin(), first.end(), first.begin(), [](unsigned char c) { return std::tolower(c); });

                    if (first == "top" || first == "t") {
                        sub = "top";
                        // Check if next arg is a global qualifier
                        if (args.size() > 1) {
                            std::string second = args[1];
                            std::transform(second.begin(), second.end(), second.begin(), [](unsigned char c) { return std::tolower(c); });
                            if (second == "g" || second == "global" || second == "all") {
                                global_top = true;
                                if (args.size() > 2) tf_str = args[2];
                            } else {
                                tf_str = args[1];
                            }
                        }
                    } else if (first == "user" || first == "users" || first == "member" || first == "members" || first == "u" || first == "m") {
                        sub = "users";
                        if (args.size() > 1) tf_str = args[1];
                    } else if (first == "channel" || first == "channels" || first == "c") {
                        sub = "channels";
                        if (args.size() > 1) tf_str = args[1];
                    } else if (first == "overview" || first == "o") {
                        sub = "overview";
                        if (args.size() > 1) tf_str = args[1];
                    } else {
                        tf_str = args[0];
                    }
                }

                int days = parse_timeframe(tf_str, 7);

                stoatpp::models::MessagePayload payload;
                payload.content = " "; 
                stoatpp::models::Embed embed;
                embed.set_colour(0x5865F2);

                if (sub == "overview") {
                    auto totals = g_db->get_server_totals(msg.server_id, days);
                    embed.set_title("📈 bronx stats — overview (" + std::to_string(days) + "d)");
                    
                    std::string desc = 
                        "total messages: **" + std::to_string(totals.total_messages) + "**\n"
                        "active users: **" + std::to_string(totals.active_users) + "**\n"
                        "commands used: **" + std::to_string(totals.total_commands) + "**\n";
                    embed.set_description(desc);
                } 
                else if (sub == "top" && global_top) {
                    // Global leaderboard across all servers
                    auto top_srv = g_db->get_top_servers(days, 10);
                    auto top_usr = g_db->get_top_users_global(days, false, 10);
                    auto top_bot = g_db->get_top_users_global(days, true, 10);

                    embed.set_title("🌐 global stats leaderboard (" + std::to_string(days) + "d)");

                    std::string desc = "";

                    // 1. Top Servers
                    desc += "### 🌐 Top Active Servers\n";
                    if (top_srv.empty()) {
                        desc += "*No server activity recorded.*\n";
                    } else {
                        int rank = 1;
                        for (const auto& s : top_srv) {
                            std::string s_name = "Server " + s.guild_id;
                            auto s_opt = bot.get_server(s.guild_id);
                            if (s_opt) s_name = s_opt->name;
                            desc += std::to_string(rank) + ". **" + s_name + "** — " + std::to_string(s.message_count) + " msgs\n";
                            rank++;
                        }
                    }
                    desc += "\n";

                    // 2. Top Users
                    desc += "### 👤 Top Active Users\n";
                    if (top_usr.empty()) {
                        desc += "*No user activity recorded.*\n";
                    } else {
                        int rank = 1;
                        for (const auto& u : top_usr) {
                            desc += std::to_string(rank) + ". <@" + u.user_id + "> — " + std::to_string(u.message_count) + " msgs\n";
                            rank++;
                        }
                    }
                    desc += "\n";

                    // 3. Top Bots
                    desc += "### 🤖 Top Active Bots\n";
                    if (top_bot.empty()) {
                        desc += "*No bot activity recorded.*\n";
                    } else {
                        int rank = 1;
                        for (const auto& b : top_bot) {
                            desc += std::to_string(rank) + ". <@" + b.user_id + "> — " + std::to_string(b.message_count) + " msgs\n";
                            rank++;
                        }
                    }

                    embed.set_description(desc);
                }
                else if (sub == "top") {
                    // Server-scoped leaderboard (default)
                    if (msg.server_id.empty()) {
                        bot.send_message(msg.channel_id, "⚠️ run this inside a server, or use `.stats top g` for the global leaderboard.");
                        return;
                    }

                    auto top_usr  = g_db->get_top_users(msg.server_id, days, 10);
                    auto top_chan = g_db->get_top_channels(msg.server_id, days, 10);

                    auto srv_opt = bot.get_server(msg.server_id);
                    std::string srv_name = srv_opt ? srv_opt->name : "this server";
                    embed.set_title("🏆 top 10 — " + srv_name + " (" + std::to_string(days) + "d)");

                    std::string desc = "";

                    desc += "### 👤 Top Members\n";
                    if (top_usr.empty()) {
                        desc += "*No member activity recorded.*\n";
                    } else {
                        int rank = 1;
                        for (const auto& u : top_usr) {
                            desc += std::to_string(rank) + ". <@" + u.user_id + "> — " + std::to_string(u.message_count) + " msgs\n";
                            rank++;
                        }
                    }
                    desc += "\n";

                    desc += "### 💬 Top Channels\n";
                    if (top_chan.empty()) {
                        desc += "*No channel activity recorded.*\n";
                    } else {
                        int rank = 1;
                        for (const auto& c : top_chan) {
                            desc += std::to_string(rank) + ". <#" + c.channel_id + "> — " + std::to_string(c.message_count) + " msgs\n";
                            rank++;
                        }
                    }

                    desc += "\n-# use `.stats top g` to see the global leaderboard";
                    embed.set_description(desc);
                } 
                else if (sub == "users") {
                    auto top_users = g_db->get_top_users(msg.server_id, days, 10);
                    embed.set_title("👤 active members leaderboard (" + std::to_string(days) + "d)");
                    
                    std::string desc;
                    if (top_users.empty()) {
                        desc = "no member activity recorded in this period.";
                    } else {
                        int rank = 1;
                        for (const auto& u : top_users) {
                            desc += std::to_string(rank) + ". <@" + u.user_id + "> — " + std::to_string(u.message_count) + " messages\n";
                            rank++;
                        }
                    }
                    embed.set_description(desc);
                } 
                else if (sub == "channels") {
                    auto top_chans = g_db->get_top_channels(msg.server_id, days, 10);
                    embed.set_title("💬 active channels leaderboard (" + std::to_string(days) + "d)");
                    
                    std::string desc;
                    if (top_chans.empty()) {
                        desc = "no channel message telemetry recorded in this period.";
                    } else {
                        int rank = 1;
                        for (const auto& c : top_chans) {
                            desc += std::to_string(rank) + ". <#" + c.channel_id + "> — " + std::to_string(c.message_count) + " messages\n";
                            rank++;
                        }
                    }
                    embed.set_description(desc);
                }

                payload.add_embed(embed);
                bot.send_message(msg.channel_id, payload);
            },
            "Stats"
        });
    }
};

} // namespace bronx::cogs
