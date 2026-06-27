#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"

namespace bronx::cogs {

class ModerationModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "ban",
            {},
            "ban a member from the server.",
            "",
            {"<@user|id>", "[reason]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling ban...\n";
                if (args.empty()) {
                    bot.send_message(msg.channel_id, "usage: `!ban <@user|id> [reason]`");
                    return;
                }

                std::string query = args[0];
                std::string reason = args.size() > 1 ? args[1] : "no reason provided";

                helpers::resolve_user(bot, msg.server_id, query, [&bot, msg, reason](stoatpp::models::User target_user, stoatpp::models::Member target_member, bool success) {
                    if (!success) {
                        bot.send_message(msg.channel_id, "could not resolve target user for ban.");
                        return;
                    }

                    bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, target_member, reason, target_user](stoatpp::models::Member exec_member, bool exec_ok) {
                        if (!exec_ok) {
                            bot.send_message(msg.channel_id, "failed to verify executor member settings.");
                            return;
                        }

                        bot.fetch_member(msg.server_id, bot.current_user().id, [&bot, msg, target_member, reason, target_user, exec_member](stoatpp::models::Member bot_member, bool bot_ok) {
                            if (!bot_ok) {
                                bot.send_message(msg.channel_id, "failed to verify bot member settings.");
                                  return;
                            }

                            if (!helpers::check_hierarchy(bot, msg.server_id, exec_member, target_member)) {
                                bot.send_message(msg.channel_id, "hierarchy error: you cannot moderate this user.");
                                return;
                            }

                            if (!helpers::check_hierarchy(bot, msg.server_id, bot_member, target_member)) {
                                bot.send_message(msg.channel_id, "hierarchy error: i do not have high enough position to moderate this user.");
                                return;
                            }

                            bot.ban_user(msg.server_id, target_user.id, reason, [&bot, msg, target_user](bool action_success) {
                                if (action_success) {
                                    bot.send_message(msg.channel_id, "successfully banned **" + target_user.username + "**.");
                                } else {
                                    bot.send_message(msg.channel_id, "failed to ban user. do i have target permissions?");
                                }
                            });
                        });
                    });
                });
            }
        });

        bot.register_command({
            "kick",
            {},
            "kick a member from the server.",
            "",
            {"<@user|id>"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling kick...\n";
                if (args.empty()) {
                    bot.send_message(msg.channel_id, "usage: `!kick <@user|id>`");
                    return;
                }

                std::string query = args[0];

                helpers::resolve_user(bot, msg.server_id, query, [&bot, msg](stoatpp::models::User target_user, stoatpp::models::Member target_member, bool success) {
                    if (!success) {
                        bot.send_message(msg.channel_id, "could not resolve target user for kick.");
                        return;
                    }

                    bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, target_member, target_user](stoatpp::models::Member exec_member, bool exec_ok) {
                        if (!exec_ok) {
                            bot.send_message(msg.channel_id, "failed to verify executor member settings.");
                            return;
                        }

                        bot.fetch_member(msg.server_id, bot.current_user().id, [&bot, msg, target_member, target_user, exec_member](stoatpp::models::Member bot_member, bool bot_ok) {
                            if (!bot_ok) {
                                bot.send_message(msg.channel_id, "failed to verify bot member settings.");
                                return;
                            }

                            if (!helpers::check_hierarchy(bot, msg.server_id, exec_member, target_member)) {
                                bot.send_message(msg.channel_id, "hierarchy error: you cannot moderate this user.");
                                return;
                            }

                            if (!helpers::check_hierarchy(bot, msg.server_id, bot_member, target_member)) {
                                bot.send_message(msg.channel_id, "hierarchy error: i do not have high enough position to moderate this user.");
                                return;
                            }

                            bot.kick_member(msg.server_id, target_user.id, [&bot, msg, target_user](bool action_success) {
                                if (action_success) {
                                    bot.send_message(msg.channel_id, "successfully kicked **" + target_user.username + "**.");
                                } else {
                                    bot.send_message(msg.channel_id, "failed to kick user.");
                                }
                            });
                        });
                    });
                });
            }
        });

        bot.register_command({
            "timeout",
            {},
            "timeout a member (mute/restrict).",
            "",
            {"<@user|id>", "<seconds>"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling timeout...\n";
                if (args.size() < 2) {
                    bot.send_message(msg.channel_id, "usage: `!timeout <@user|id> <seconds>`");
                    return;
                }

                std::string query = args[0];
                int seconds = 0;
                try {
                    seconds = std::stoi(args[1]);
                } catch (...) {
                    bot.send_message(msg.channel_id, "invalid duration. please specify duration in seconds.");
                    return;
                }

                helpers::resolve_user(bot, msg.server_id, query, [&bot, msg, seconds](stoatpp::models::User target_user, stoatpp::models::Member target_member, bool success) {
                    if (!success) {
                        bot.send_message(msg.channel_id, "could not resolve target user.");
                        return;
                    }

                    bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, target_member, target_user, seconds](stoatpp::models::Member exec_member, bool exec_ok) {
                        if (!exec_ok) {
                            bot.send_message(msg.channel_id, "failed to verify executor member settings.");
                            return;
                        }

                        bot.fetch_member(msg.server_id, bot.current_user().id, [&bot, msg, target_member, target_user, exec_member, seconds](stoatpp::models::Member bot_member, bool bot_ok) {
                            if (!bot_ok) {
                                bot.send_message(msg.channel_id, "failed to verify bot member settings.");
                                return;
                            }

                            if (!helpers::check_hierarchy(bot, msg.server_id, exec_member, target_member)) {
                                bot.send_message(msg.channel_id, "hierarchy error: you cannot moderate this user.");
                                return;
                            }

                            if (!helpers::check_hierarchy(bot, msg.server_id, bot_member, target_member)) {
                                bot.send_message(msg.channel_id, "hierarchy error: i do not have high enough position to moderate this user.");
                                return;
                            }

                            std::string iso_duration = helpers::get_future_iso_timestamp(seconds);
                            bot.timeout_member(msg.server_id, target_user.id, iso_duration, [&bot, msg, target_user, seconds](bool action_success) {
                                if (action_success) {
                                    bot.send_message(msg.channel_id, "successfully timed out **" + target_user.username + "** for " + std::to_string(seconds) + " seconds.");
                                } else {
                                    bot.send_message(msg.channel_id, "failed to set timeout.");
                                }
                            });
                        });
                    });
                });
            }
        });

        bot.register_command({
            "purge",
            {},
            "bulk delete messages in the channel.",
            "",
            {"[amount]", "[@user]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling purge...\n";
                int amount = 10;
                std::string user_query = "";
                
                if (!args.empty()) {
                    try {
                        size_t pos;
                        int val = std::stoi(args[0], &pos);
                        if (pos == args[0].size()) {
                            amount = val;
                            if (args.size() > 1) {
                                user_query = args[1];
                            }
                        } else {
                            user_query = args[0];
                            if (args.size() > 1) {
                                try {
                                    amount = std::stoi(args[1]);
                                } catch (...) {}
                            }
                        }
                    } catch (...) {
                        user_query = args[0];
                        if (args.size() > 1) {
                            try {
                                amount = std::stoi(args[1]);
                            } catch (...) {}
                        }
                    }
                }

                if (amount < 1 || amount > 100) {
                    bot.send_message(msg.channel_id, "amount must be between 1 and 100.");
                    return;
                }

                auto proceed_purge = [&bot, msg, amount](const std::string& target_user_id) {
                    stoatpp::models::MessageQuery query;
                    query.limit = target_user_id.empty() ? amount : 100;

                    std::thread([&bot, msg, amount, target_user_id, query]() {
                        auto res = bot.rest().get_messages(msg.channel_id, query);
                        if (!res.success() || !res.body.is_array()) {
                            bot.send_message(msg.channel_id, "failed to fetch messages for purge.");
                            return;
                        }

                        std::vector<std::string> ids;
                        for (const auto& item : res.body) {
                            auto m = stoatpp::models::Message::from_json(item);
                            if (target_user_id.empty() || m.author == target_user_id) {
                                ids.push_back(m.id);
                                if (!target_user_id.empty() && ids.size() >= static_cast<size_t>(amount)) {
                                    break;
                                }
                            }
                        }

                        if (ids.empty()) {
                            bot.send_message(msg.channel_id, "no messages found to delete.");
                            return;
                        }

                        auto del_res = bot.rest().delete_messages_bulk(msg.channel_id, ids);
                        if (del_res.success()) {
                            std::cout << "[bronx command] successfully purged " << ids.size() << " messages.\n";
                            stoatpp::models::MessagePayload confirm_payload;
                            confirm_payload.content = "successfully purged " + std::to_string(ids.size()) + " messages.";
                            confirm_payload.delete_after = 5;
                            bot.send_message(msg.channel_id, confirm_payload);
                        } else {
                            bot.send_message(msg.channel_id, "failed to bulk delete messages: " + del_res.error_message());
                        }
                    }).detach();
                };

                if (!user_query.empty()) {
                    helpers::resolve_user(bot, msg.server_id, user_query, [proceed_purge, &bot, msg](stoatpp::models::User user, stoatpp::models::Member member, bool success) {
                        if (!success) {
                            bot.send_message(msg.channel_id, "could not resolve target user for purge.");
                            return;
                        }
                        proceed_purge(user.id);
                    });
                } else {
                    proceed_purge("");
                }
            }
        });
    }
};

} // namespace bronx::cogs
