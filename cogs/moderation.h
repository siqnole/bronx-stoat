#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include <chrono>

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
            },
            "Moderation",
            stoatpp::permissions::BanMembers
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
            },
            "Moderation",
            stoatpp::permissions::KickMembers
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
                                     auto now_unix = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                                     auto expires_unix = now_unix + seconds;
                                     bot.send_message(msg.channel_id, "successfully timed out **" + target_user.username + "** until " + stoatpp::utils::format_timestamp(expires_unix, "R") + ".");
                                 } else {
                                     bot.send_message(msg.channel_id, "failed to set timeout.");
                                 }
                             });
                        });
                    });
                });
            },
            "Moderation",
            stoatpp::permissions::TimeoutMembers
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
            },
            "Moderation",
            stoatpp::permissions::ManageMessages
        });
        bot.register_command({
            "pin",
            {},
            "pin a message in this channel. reply to a message or provide its id.",
            "",
            {"[message_id]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling pin...\n";

                // Prefer a message ID supplied as an argument, otherwise use the replied-to message
                std::string target_id;
                if (!args.empty()) {
                    target_id = args[0];
                } else if (!msg.replies.empty()) {
                    target_id = msg.replies[0];
                }

                if (target_id.empty()) {
                    bot.send_message(msg.channel_id, "usage: reply to a message or provide a message id: `pin <message_id>`");
                    return;
                }

                bot.pin_message(msg.channel_id, target_id, [&bot, msg, target_id](bool success) {
                    if (success) {
                        bot.send_message(msg.channel_id, "📌 message pinned.");
                    } else {
                        bot.send_message(msg.channel_id, "failed to pin message. do i have the manage messages permission?");
                    }
                });
            },
            "Moderation",
            stoatpp::permissions::ManageMessages
        });

        bot.register_command({
            "unpin",
            {},
            "unpin a message in this channel. reply to a message or provide its id.",
            "",
            {"[message_id]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling unpin...\n";

                std::string target_id;
                if (!args.empty()) {
                    target_id = args[0];
                } else if (!msg.replies.empty()) {
                    target_id = msg.replies[0];
                }

                if (target_id.empty()) {
                    bot.send_message(msg.channel_id, "usage: reply to a message or provide a message id: `unpin <message_id>`");
                    return;
                }

                bot.unpin_message(msg.channel_id, target_id, [&bot, msg, target_id](bool success) {
                    if (success) {
                        bot.send_message(msg.channel_id, "📌 message unpinned.");
                    } else {
                        bot.send_message(msg.channel_id, "failed to unpin message. do i have the manage messages permission?");
                    }
                });
            },
            "Moderation",
            stoatpp::permissions::ManageMessages
        });

        bot.register_command({
            "cleanup",
            {"cu"},
            "delete bot messages and command invocations.",
            "",
            {"[amount]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling cleanup...\n";

                int amount = 100;
                bool has_limit = false;
                if (!args.empty()) {
                    try {
                        amount = std::stoi(args[0]);
                        has_limit = true;
                    } catch (...) {
                        bot.send_message(msg.channel_id, "amount must be a valid number.");
                        return;
                    }
                }

                if (amount < 1 || amount > 100) {
                    bot.send_message(msg.channel_id, "amount must be between 1 and 100.");
                    return;
                }

                std::vector<std::string> prefixes = { bot.config().command_prefix };
                if (bot.config().prefix_resolver) {
                    prefixes = bot.config().prefix_resolver(bot, msg);
                }

                auto starts_with_prefix = [](const std::string& content, const std::vector<std::string>& pfxs) {
                    for (const auto& p : pfxs) {
                        if (content.rfind(p, 0) == 0) return true;
                    }
                    return false;
                };

                stoatpp::models::MessageQuery query;
                query.limit = 100;

                std::thread([&bot, msg, amount, has_limit, prefixes, starts_with_prefix, query]() {
                    auto res = bot.rest().get_messages(msg.channel_id, query);
                    if (!res.success() || !res.body.is_array()) {
                        bot.send_message(msg.channel_id, "failed to fetch messages for cleanup.");
                        return;
                    }

                    std::vector<stoatpp::models::Message> messages;
                    for (const auto& item : res.body) {
                        messages.push_back(stoatpp::models::Message::from_json(item));
                    }

                    std::vector<std::string> ids_to_delete;
                    ids_to_delete.push_back(msg.id); // delete the cleanup command itself

                    int bot_msgs_deleted = 0;
                    for (size_t i = 0; i < messages.size(); ++i) {
                        const auto& m = messages[i];
                        if (m.id == msg.id) continue;

                        if (m.author == bot.current_user().id) {
                            if (has_limit && bot_msgs_deleted >= amount) {
                                continue;
                            }

                            ids_to_delete.push_back(m.id);
                            bot_msgs_deleted++;

                            // Check replies first
                            if (!m.replies.empty()) {
                                ids_to_delete.push_back(m.replies[0]);
                            } else if (i + 1 < messages.size()) {
                                const auto& prev = messages[i + 1];
                                if (prev.content && starts_with_prefix(*prev.content, prefixes)) {
                                    ids_to_delete.push_back(prev.id);
                                }
                            }
                        }
                    }

                    // Deduplicate IDs
                    std::sort(ids_to_delete.begin(), ids_to_delete.end());
                    ids_to_delete.erase(std::unique(ids_to_delete.begin(), ids_to_delete.end()), ids_to_delete.end());

                    if (ids_to_delete.empty()) {
                        bot.send_message(msg.channel_id, "no messages found to clean up.");
                        return;
                    }

                    auto del_res = bot.rest().delete_messages_bulk(msg.channel_id, ids_to_delete);
                    if (del_res.success()) {
                        std::cout << "[bronx command] successfully cleaned up " << ids_to_delete.size() << " messages.\n";
                        stoatpp::models::MessagePayload confirm_payload;
                        confirm_payload.content = "successfully cleaned up " + std::to_string(ids_to_delete.size()) + " messages.";
                        confirm_payload.delete_after = 5;
                        bot.send_message(msg.channel_id, confirm_payload);
                    } else {
                        bot.send_message(msg.channel_id, "failed to delete messages: " + del_res.error_message());
                    }
                }).detach();
            },
            "Moderation",
            stoatpp::permissions::ManageMessages
        });
    }
};

} // namespace bronx::cogs
