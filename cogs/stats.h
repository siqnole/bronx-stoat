#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include <vector>
#include <string>
#include <algorithm>
#include <iostream>

namespace bronx::cogs {

class StatsModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "stats",
            {"serverstats", "telemetry"},
            "shows server telemetry: overview, top message senders, or top commands.",
            "stats [overview/top/commands] [days]",
            {"[subcommand]", "[days]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id, "this command can only be used inside a server.");
                    return;
                }

                if (!g_db) {
                    bot.send_message(msg.channel_id, "database connection is currently unavailable.");
                    return;
                }

                std::string sub = "overview";
                int days = 7;

                if (!args.empty()) {
                    if (args[0] == "top" || args[0] == "commands" || args[0] == "overview") {
                        sub = args[0];
                        if (args.size() > 1) {
                            try {
                                int d = std::stoi(args[1]);
                                if (d > 0 && d <= 365) days = d;
                            } catch (...) {}
                        }
                    } else {
                        try {
                            int d = std::stoi(args[0]);
                            if (d > 0 && d <= 365) days = d;
                        } catch (...) {}
                    }
                }

                stoatpp::models::MessagePayload payload;
                payload.content = " "; // Prevent empty content Revolt validation error

                stoatpp::models::Embed embed;
                embed.set_colour(0x5865F2);

                if (sub == "overview") {
                    auto totals = g_db->get_server_totals(msg.server_id, days);
                    embed.set_title("bronx stats — overview (" + std::to_string(days) + "d)");
                    
                    std::string desc = 
                        "total messages: **" + std::to_string(totals.total_messages) + "**\n"
                        "active users: **" + std::to_string(totals.active_users) + "**\n"
                        "commands used: **" + std::to_string(totals.total_commands) + "**\n";
                    embed.set_description(desc);
                } 
                else if (sub == "top") {
                    auto top_users = g_db->get_top_users(msg.server_id, days, 10);
                    embed.set_title("bronx stats — top users (" + std::to_string(days) + "d)");
                    
                    std::string desc;
                    if (top_users.empty()) {
                        desc = "no user activity recorded in this period.";
                    } else {
                        int rank = 1;
                        for (const auto& u : top_users) {
                            desc += std::to_string(rank) + ". <@" + u.user_id + "> — " + std::to_string(u.message_count) + " messages\n";
                            rank++;
                        }
                    }
                    embed.set_description(desc);
                } 
                else if (sub == "commands") {
                    auto top_cmds = g_db->get_top_commands(msg.server_id, days, 10);
                    embed.set_title("bronx stats — top commands (" + std::to_string(days) + "d)");
                    
                    std::string desc;
                    if (top_cmds.empty()) {
                        desc = "no command usage recorded in this period.";
                    } else {
                        int rank = 1;
                        for (const auto& c : top_cmds) {
                            desc += std::to_string(rank) + ". " + c.command_name + " — " + std::to_string(c.use_count) + " hits\n";
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
