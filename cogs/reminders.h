#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "../database.h"
#include <chrono>
#include <thread>
#include <cctype>

namespace bronx::cogs {

class RemindersModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "remind",
            {"remindme", "reminder"},
            "set a reminder up to 2 years in the future.",
            "",
            {"<time>", "<message>"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (args.size() > 0 && args[0] == "list") {
                    auto rems = g_db->get_user_reminders(msg.author.id);
                    if (rems.empty()) {
                        bot.send_message(msg.channel_id, "you don't have any active reminders.");
                        return;
                    }

                    stoatpp::models::MessagePayload payload;
                    stoatpp::models::Embed embed;
                    embed.set_colour(0x5865F2);
                    embed.set_title("your reminders");
                    
                    std::string desc = "";
                    for (size_t i = 0; i < rems.size(); ++i) {
                        desc += "**" + std::to_string(i + 1) + ".** <t:" + std::to_string(rems[i].timestamp_due) + ":R> — " + rems[i].message + "\n";
                        if (desc.size() > 3000) {
                            desc += "... and more";
                            break;
                        }
                    }
                    embed.set_description(desc);
                    payload.add_embed(embed);
                    bot.send_message(msg.channel_id, payload);
                    return;
                }

                if (args.size() < 2) {
                    bot.send_message(msg.channel_id, "usage: `!remind <time> <message>` (e.g. `!remind 2h check oven`)\nor `!remind list` to view active reminders.");
                    return;
                }

                std::string time_str = args[0];
                uint64_t duration_sec = helpers::parse_duration(time_str);

                if (duration_sec == 0) {
                    bot.send_message(msg.channel_id, "invalid time format. examples: `10m`, `1h30m`, `2d`, `1y`");
                    return;
                }

                uint64_t max_duration = 31536000ULL * 2; // 2 years in seconds
                if (duration_sec > max_duration) {
                    bot.send_message(msg.channel_id, "reminders cannot be set for more than 2 years in the future.");
                    return;
                }

                std::string message_content = "";
                for (size_t i = 1; i < args.size(); ++i) {
                    message_content += args[i] + (i == args.size() - 1 ? "" : " ");
                }

                auto now_unix = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                auto due_unix = now_unix + duration_sec;

                if (g_db->add_reminder(msg.author.id, msg.channel_id, message_content, now_unix, due_unix)) {
                    bot.send_message(msg.channel_id, "⏰ okay, i'll remind you on <t:" + std::to_string(due_unix) + ":F>.");
                } else {
                    bot.send_message(msg.channel_id, "failed to save reminder to database.");
                }
            },
            "Utility"
        });

        bot.register_command({
            "reminders",
            {"remindlist", "reminderlist"},
            "list all your currently active reminders.",
            "",
            {},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                auto rems = g_db->get_user_reminders(msg.author.id);
                if (rems.empty()) {
                    bot.send_message(msg.channel_id, "you don't have any active reminders.");
                    return;
                }

                stoatpp::models::MessagePayload payload;
                stoatpp::models::Embed embed;
                embed.set_colour(0x5865F2);
                embed.set_title("your reminders");
                
                std::string desc = "";
                for (size_t i = 0; i < rems.size(); ++i) {
                    desc += "**" + std::to_string(i + 1) + ".** <t:" + std::to_string(rems[i].timestamp_due) + ":R> — " + rems[i].message + "\n";
                    if (desc.size() > 3000) {
                        desc += "... and more";
                        break;
                    }
                }
                embed.set_description(desc);
                payload.add_embed(embed);
                bot.send_message(msg.channel_id, payload);
            },
            "Utility"
        });

        std::thread([&bot]() {
            while (true) {
                auto now_unix = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                auto due_reminders = g_db->get_due_reminders(now_unix);
                for (const auto& r : due_reminders) {
                    stoatpp::models::MessagePayload payload;
                    payload.content = "<@" + r.user_id + "> reminder set on <t:" + std::to_string(r.timestamp_set) + ":f>:\n" + r.message;
                    
                    bot.send_message(r.channel_id, payload, [&bot, r, payload](stoatpp::models::Message, bool success) {
                        if (!success) {
                            // Channel might have been deleted, try to DM the user.
                            // rest client calls are synchronous, run in a background thread to avoid blocking callback dispatcher.
                            std::thread([&bot, r, payload]() {
                                auto dm_res = bot.rest().open_dm(r.user_id);
                                if (dm_res.success() && dm_res.body.is_object() && dm_res.body.contains("_id")) {
                                    std::string dm_channel_id = dm_res.body["_id"].get<std::string>();
                                    stoatpp::models::MessagePayload dm_payload = payload;
                                    dm_payload.content = "" + bronx::emj_warning + " *Original channel was deleted. Sending reminder to DMs:*\n" + dm_payload.content;
                                    bot.send_message(dm_channel_id, dm_payload);
                                }
                            }).detach();
                        }
                    });
                    g_db->delete_reminder(r.id);
                }
                std::this_thread::sleep_for(std::chrono::seconds(15));
            }
        }).detach();
    }
};

} // namespace bronx::cogs
