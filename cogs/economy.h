#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "games.h"
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <cstdio>

namespace bronx::cogs {

class EconomyModule : public stoatpp::bot_module {
private:
    std::string resolve_user_id(const std::string& arg) {
        if (arg.size() > 3 && arg.front() == '<' && arg[1] == '@' && arg.back() == '>') {
            return arg.substr(2, arg.size() - 3);
        }
        return arg;
    }

    std::string format_cooldown(int seconds) {
        if (seconds >= 3600) {
            int hours = seconds / 3600;
            int mins = (seconds % 3600) / 60;
            return std::to_string(hours) + "h " + std::to_string(mins) + "m";
        }
        if (seconds >= 60) {
            return std::to_string(seconds / 60) + "m " + std::to_string(seconds % 60) + "s";
        }
        return std::to_string(seconds) + "s";
    }

public:
    void register_handlers(stoatpp::cluster& bot) override {
        // 1. balance command
        bot.register_command({
            "balance",
            {"bal", "money"},
            "checks your wallet and bank balance.",
            "balance [user]",
            {"[user]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) {
                    bot.send_message(msg.channel_id, "database connection is currently unavailable.");
                    return;
                }

                std::string target_uid = msg.author.id;
                if (!args.empty()) {
                    target_uid = resolve_user_id(args[0]);
                }

                auto u = g_db->get_economy_user(target_uid);
                if (!u) {
                    bot.send_message(msg.channel_id, "could not retrieve user data.");
                    return;
                }

                stoatpp::models::MessagePayload payload;
                payload.content = " ";
                stoatpp::models::Embed embed;
                embed.set_colour(0x5865F2);
                
                std::string target_mention = (target_uid == msg.author.id) ? "your" : "<@" + target_uid + ">'s";
                embed.set_title("bronx economy — " + target_mention + " balance");

                std::string desc = 
                    "wallet: **" + std::to_string(u->wallet) + "**\n"
                    "bank: **" + std::to_string(u->bank) + "** / **" + std::to_string(u->bank_limit) + "**\n"
                    "networth: **" + std::to_string(u->wallet + u->bank) + "**\n"
                    "prestige: **" + std::to_string(u->prestige) + "**\n";
                embed.set_description(desc);

                payload.add_embed(embed);
                bot.send_message(msg.channel_id, payload);
            },
            "Economy"
        });

        // 2. beg command
        bot.register_command({
            "beg",
            {},
            "begs for some coins.",
            "beg",
            {},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<int64_t> dist(50, 250);
                int64_t amount = dist(gen);

                auto u = g_db->get_economy_user(msg.author.id);
                double multiplier = 1.0;
                if (u) {
                    multiplier += (u->prestige * 0.15);
                }
                amount = static_cast<int64_t>(amount * multiplier);

                g_db->update_balances(msg.author.id, amount, 0);

                bot.send_message(msg.channel_id, "you begged and received **" + std::to_string(amount) + "** coins.");
            },
            "Economy",
            0,
            45
        });

        // 3. work command
        bot.register_command({
            "work",
            {},
            "works a shift to earn coins.",
            "work",
            {},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                std::vector<std::string> jobs = {
                    "software engineer", "fast food cook", "barista", "mechanic", 
                    "streamer", "plumber", "delivery driver", "designer"
                };
                
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<size_t> job_dist(0, jobs.size() - 1);
                std::uniform_int_distribution<int64_t> pay_dist(500, 1500);

                std::string job = jobs[job_dist(gen)];
                int64_t pay = pay_dist(gen);

                auto u = g_db->get_economy_user(msg.author.id);
                double multiplier = 1.0;
                if (u) {
                    multiplier += (u->prestige * 0.15);
                }
                pay = static_cast<int64_t>(pay * multiplier);

                g_db->update_balances(msg.author.id, pay, 0);

                bot.send_message(msg.channel_id, "you worked as a **" + job + "** and earned **" + std::to_string(pay) + "** coins.");
            },
            "Economy",
            0,
            3600
        });

        // 4. pay command
        bot.register_command({
            "pay",
            {"give", "transfer"},
            "gives coins from your wallet to another user.",
            "pay <user> <amount>",
            {"<user>", "<amount>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                if (args.size() < 2) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `pay <user> <amount>`.");
                    return;
                }

                std::string target_uid = resolve_user_id(args[0]);
                if (target_uid == msg.author.id) {
                    bot.send_message(msg.channel_id, "you cannot pay yourself.");
                    return;
                }

                int64_t amount = 0;
                try {
                    amount = std::stoll(args[1]);
                } catch (...) {}

                if (amount <= 0) {
                    bot.send_message(msg.channel_id, "please provide a valid positive amount.");
                    return;
                }

                auto self = g_db->get_economy_user(msg.author.id);
                if (!self || self->wallet < amount) {
                    bot.send_message(msg.channel_id, "you do not have enough coins in your wallet.");
                    return;
                }

                g_db->update_balances(msg.author.id, -amount, 0);
                g_db->update_balances(target_uid, amount, 0);

                bot.send_message(msg.channel_id, "you paid **" + std::to_string(amount) + "** coins to <@" + target_uid + ">.");
            },
            "Economy"
        });

        // 5. deposit command
        bot.register_command({
            "deposit",
            {"dep"},
            "deposits coins from your wallet into your bank.",
            "deposit <amount/all>",
            {"<amount>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                if (args.empty()) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `deposit <amount/all>`.");
                    return;
                }

                auto self = g_db->get_economy_user(msg.author.id);
                if (!self) return;

                int64_t amount = 0;
                if (args[0] == "all" || args[0] == "max") {
                    amount = self->wallet;
                } else {
                    try {
                        amount = std::stoll(args[0]);
                    } catch (...) {}
                }

                if (amount <= 0) {
                    bot.send_message(msg.channel_id, "please provide a valid positive amount.");
                    return;
                }

                if (self->wallet < amount) {
                    bot.send_message(msg.channel_id, "you do not have enough coins in your wallet.");
                    return;
                }

                int64_t space_remaining = self->bank_limit - self->bank;
                if (space_remaining <= 0) {
                    bot.send_message(msg.channel_id, "your bank is already full.");
                    return;
                }

                if (amount > space_remaining) {
                    amount = space_remaining;
                }

                g_db->update_balances(msg.author.id, -amount, amount);
                bot.send_message(msg.channel_id, "you deposited **" + std::to_string(amount) + "** coins into your bank.");
            },
            "Economy"
        });

        // 6. withdraw command
        bot.register_command({
            "withdraw",
            {"with"},
            "withdraws coins from your bank into your wallet.",
            "withdraw <amount/all>",
            {"<amount>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                if (args.empty()) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `withdraw <amount/all>`.");
                    return;
                }

                auto self = g_db->get_economy_user(msg.author.id);
                if (!self) return;

                int64_t amount = 0;
                if (args[0] == "all" || args[0] == "max") {
                    amount = self->bank;
                } else {
                    try {
                        amount = std::stoll(args[0]);
                    } catch (...) {}
                }

                if (amount <= 0) {
                    bot.send_message(msg.channel_id, "please provide a valid positive amount.");
                    return;
                }

                if (self->bank < amount) {
                    bot.send_message(msg.channel_id, "you do not have enough coins in your bank.");
                    return;
                }

                g_db->update_balances(msg.author.id, amount, -amount);
                bot.send_message(msg.channel_id, "you withdrew **" + std::to_string(amount) + "** coins from your bank.");
            },
            "Economy"
        });

        // 7. rob command
        bot.register_command({
            "rob",
            {"steal"},
            "steals coins from another user's wallet.",
            "rob <user>",
            {"<user>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                if (args.empty()) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `rob <user>`.");
                    return;
                }

                std::string target_uid = resolve_user_id(args[0]);
                if (target_uid == msg.author.id) {
                    bot.send_message(msg.channel_id, "you cannot rob yourself.");
                    return;
                }

                auto target = g_db->get_economy_user(target_uid);
                if (!target || target->wallet < 100) {
                    bot.send_message(msg.channel_id, "it is not worth robbing them; they have less than 100 coins in their wallet.");
                    return;
                }

                auto self = g_db->get_economy_user(msg.author.id);
                if (!self) return;

                // 1. Check Uno Reverse Card trap
                if (g_db->is_item_active(target_uid, "reverse_card")) {
                    g_db->remove_user_economy_item(target_uid, "reverse_card", 1);

                    std::random_device rd;
                    std::mt19937 gen(rd());
                    std::uniform_int_distribution<int> success_dist(0, 1);
                    bool rev_success = (success_dist(gen) == 1);

                    if (rev_success) {
                        std::uniform_real_distribution<double> percent_dist(0.20, 0.60);
                        double percent = percent_dist(gen);
                        int64_t stolen = static_cast<int64_t>(self->wallet * percent);
                        if (stolen < 1) stolen = 1;

                        g_db->update_balances(target_uid, stolen, 0);
                        g_db->update_balances(msg.author.id, -stolen, 0);

                        bot.send_message(msg.channel_id, "💥 **GOTCHA!** <@" + target_uid + "> activated an **Uno Reverse Card**! The robbery backfired and <@" + target_uid + "> robbed **" + std::to_string(stolen) + "** coins from <@" + msg.author.id + ">!");
                    } else {
                        std::uniform_real_distribution<double> fine_dist(0.10, 0.20);
                        double percent = fine_dist(gen);
                        int64_t fine = static_cast<int64_t>(target->wallet * percent);
                        if (fine < 100) fine = 100;
                        if (target->wallet < fine) fine = target->wallet;

                        g_db->update_balances(target_uid, -fine, 0);
                        g_db->update_balances(msg.author.id, fine, 0);

                        bot.send_message(msg.channel_id, "💥 **GOTCHA!** <@" + target_uid + "> activated an **Uno Reverse Card**! However, their counter-attack failed and they were fined **" + std::to_string(fine) + "** coins to <@" + msg.author.id + ">!");
                    }
                    return;
                }

                // 2. Check Robbery Guard protection
                if (g_db->is_item_active(target_uid, "robbery_guard")) {
                    bot.send_message(msg.channel_id, "🛡️ **Robbery Blocked!** <@" + target_uid + "> is protected by an active **Robbery Guard**.");
                    return;
                }

                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<int> success_dist(0, 1);
                bool success = (success_dist(gen) == 1);

                if (success) {
                    std::uniform_real_distribution<double> percent_dist(0.20, 0.60);
                    double percent = percent_dist(gen);
                    int64_t stolen = static_cast<int64_t>(target->wallet * percent);
                    if (stolen < 1) stolen = 1;

                    g_db->update_balances(msg.author.id, stolen, 0);
                    g_db->update_balances(target_uid, -stolen, 0);

                    bot.send_message(msg.channel_id, "you successfully robbed <@" + target_uid + "> and took **" + std::to_string(stolen) + "** coins!");
                } else {
                    std::uniform_real_distribution<double> fine_dist(0.10, 0.20);
                    double percent = fine_dist(gen);
                    int64_t fine = static_cast<int64_t>(self->wallet * percent);
                    if (fine < 100) fine = 100;
                    if (self->wallet < fine) {
                        fine = self->wallet; // can't take more than they have
                    }

                    g_db->update_balances(msg.author.id, -fine, 0);
                    g_db->update_balances(target_uid, fine, 0);

                    bot.send_message(msg.channel_id, "you were caught robbing <@" + target_uid + "> and were fined **" + std::to_string(fine) + "** coins!");
                }
            },
            "Economy",
            0,
            7200
        });

        // 8. leaderboard command
        bot.register_command({
            "leaderboard",
            {"lb", "rich"},
            "shows the richest users in the server.",
            "leaderboard [server/global]",
            {"[scope]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                bool is_global = (!args.empty() && (args[0] == "global" || args[0] == "all" || args[0] == "g"));

                std::vector<Database::TopEcoEntry> top_entries;

                if (is_global || msg.server_id.empty()) {
                    top_entries = g_db->get_top_economy_users(10);
                } else {
                    // Fetch candidate top economy users
                    auto candidate_top = g_db->get_top_economy_users(250);

                    // Fetch server members list
                    std::unordered_set<std::string> server_member_ids;
                    auto res = bot.rest().get_server_members(msg.server_id);
                    if (res.success()) {
                        nlohmann::json members_arr;
                        if (res.body.is_array()) {
                            members_arr = res.body;
                        } else if (res.body.is_object() && res.body.contains("members") && res.body["members"].is_array()) {
                            members_arr = res.body["members"];
                        }
                        for (const auto& item : members_arr) {
                            auto m = stoatpp::models::Member::from_json(item);
                            if (!m.id.empty()) {
                                server_member_ids.insert(m.id);
                            }
                        }
                    }

                    for (const auto& entry : candidate_top) {
                        if (top_entries.size() >= 10) break;

                        bool is_in_server = false;
                        if (!server_member_ids.empty()) {
                            is_in_server = (server_member_ids.count(entry.user_id) > 0);
                        } else {
                            auto mem_opt = bot.get_member(msg.server_id, entry.user_id);
                            if (mem_opt) {
                                is_in_server = true;
                            } else {
                                auto r_mem = bot.rest().get_server_member(msg.server_id, entry.user_id);
                                is_in_server = r_mem.success();
                            }
                        }

                        if (is_in_server) {
                            top_entries.push_back(entry);
                        }
                    }
                }

                stoatpp::models::MessagePayload payload;
                payload.content = " ";
                stoatpp::models::Embed embed;
                embed.set_colour(0x5865F2);
                embed.set_title(is_global ? "bronx economy — global leaderboard" : "bronx economy — server leaderboard");

                std::string desc;
                if (top_entries.empty()) {
                    desc = "no user balance records found for this server.";
                } else {
                    int rank = 1;
                    for (const auto& u : top_entries) {
                        desc += std::to_string(rank) + ". <@" + u.user_id + "> — **" + std::to_string(u.networth) + "** coins\n";
                        rank++;
                    }
                    if (!is_global) {
                        desc += "\n*Tip: Use `.lb global` to view global rankings.*";
                    }
                }
                embed.set_description(desc);

                payload.add_embed(embed);
                bot.send_message(msg.channel_id, payload);
            },
            "Economy"
        });


        // 9. coinflip command
        bot.register_command({
            "coinflip",
            {"cf", "flip"},
            "flips a coin and bets on the outcome.",
            "coinflip [amount/fun] [choice/user]",
            {"[amount]", "[choice]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                int64_t bet = 0;
                std::string second_arg = "";
                bool is_duel = false;
                std::string target_uid = "";
                std::string choice = "heads";

                auto parse_bet = [&](const std::string& str, int64_t wallet) -> std::pair<bool, int64_t> {
                    if (str == "all" || str == "max") return {true, wallet};
                    if (str == "half") return {true, wallet / 2};
                    try {
                        size_t idx;
                        int64_t val = std::stoll(str, &idx);
                        if (idx == str.size()) return {true, val};
                    } catch(...) {}
                    return {false, 0};
                };

                auto self = g_db->get_economy_user(msg.author.id);
                int64_t wallet_bal = self ? self->wallet : 0;

                if (!args.empty()) {
                    auto [is_num, parsed_bet] = parse_bet(args[0], wallet_bal);
                    if (is_num) {
                        bet = parsed_bet;
                        if (args.size() > 1) {
                            second_arg = args[1];
                        }
                    } else {
                        bet = 0;
                        second_arg = args[0];
                    }
                }

                if (second_arg.size() > 3 && second_arg.front() == '<' && second_arg[1] == '@' && second_arg.back() == '>') {
                    is_duel = true;
                    target_uid = second_arg.substr(2, second_arg.size() - 3);
                } else if (second_arg.size() == 26 && second_arg.find_first_not_of("0123456789ABCDEFGHJKMNPQRSTVWXYZabcdefghjkmnpqrstvwxyz") == std::string::npos) {
                    is_duel = true;
                    target_uid = second_arg;
                }

                if (is_duel) {
                    if (bronx::cogs::GamesModule::instance) {
                        bronx::cogs::GamesModule::instance->create_challenge(bot, "coinflip", msg.author.id, target_uid, msg.channel_id, bet);
                    } else {
                        bot.send_message(msg.channel_id, "games module is not loaded.");
                    }
                    return;
                }

                if (bet > 0 && bet < 10) {
                    bot.send_message(msg.channel_id, "minimum bet is 10 coins.");
                    return;
                }

                if (bet > 0 && wallet_bal < bet) {
                    bot.send_message(msg.channel_id, "you do not have enough coins in your wallet.");
                    return;
                }

                if (!second_arg.empty()) {
                    choice = second_arg;
                    std::transform(choice.begin(), choice.end(), choice.begin(), ::tolower);
                }

                if (choice != "heads" && choice != "tails" && choice != "h" && choice != "t") {
                    bot.send_message(msg.channel_id, "please choose heads or tails.");
                    return;
                }

                bool guess_heads = (choice == "heads" || choice == "h");
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> dis(0, 1);
                bool heads = (dis(gen) == 0);

                bool won = (heads == guess_heads);

                if (bet > 0) {
                    int64_t delta = won ? bet : -bet;
                    g_db->update_balances(msg.author.id, delta, 0);
                }

                stoatpp::models::MessagePayload payload;
                payload.content = " ";
                stoatpp::models::Embed embed;
                embed.set_colour(won ? 0x2ECC71 : 0xE74C3C);
                embed.set_title("bronx economy — coinflip");

                std::string result_str = heads ? "heads" : "tails";
                std::string desc = "the coin landed on **" + result_str + "**!\n\n";
                if (won) {
                    if (bet > 0) {
                        desc += "you won **" + std::to_string(bet) + "** coins!";
                    } else {
                        desc += "you won (for fun)!";
                    }
                } else {
                    if (bet > 0) {
                        desc += "you lost **" + std::to_string(bet) + "** coins.";
                    } else {
                        desc += "you lost (for fun).";
                    }
                }
                embed.set_description(desc);

                payload.add_embed(embed);
                bot.send_message(msg.channel_id, payload);
            },
            "Economy",
            0,
            5
        });

        // 10. slots command
        bot.register_command({
            "slots",
            {"sl", "slot"},
            "spins the slot machine.",
            "slots <amount>",
            {"<amount>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                if (args.empty()) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `slots <amount>`.");
                    return;
                }

                auto self = g_db->get_economy_user(msg.author.id);
                if (!self) return;

                int64_t bet = 0;
                if (args[0] == "all" || args[0] == "max") {
                    bet = self->wallet;
                } else if (args[0] == "half") {
                    bet = self->wallet / 2;
                } else {
                    try {
                        bet = std::stoll(args[0]);
                    } catch (...) {}
                }

                if (bet < 10) {
                    bot.send_message(msg.channel_id, "minimum bet is 10 coins.");
                    return;
                }

                if (self->wallet < bet) {
                    bot.send_message(msg.channel_id, "you do not have enough coins in your wallet.");
                    return;
                }

                std::vector<std::string> symbols = {"🍒", "🍋", "🍊", "🍇", "💎", "7️⃣"};
                std::vector<int> weights = {35, 30, 20, 10, 4, 1};

                std::random_device rd;
                std::mt19937 gen(rd());
                std::discrete_distribution<> dis(weights.begin(), weights.end());

                std::string s1 = symbols[dis(gen)];
                std::string s2 = symbols[dis(gen)];
                std::string s3 = symbols[dis(gen)];

                int64_t winnings = 0;
                std::string outcome_text;

                if (s1 == s2 && s2 == s3) {
                    if (s1 == "7️⃣") {
                        winnings = bet * 50;
                        outcome_text = "jackpot!";
                    } else if (s1 == "💎") {
                        winnings = bet * 20;
                        outcome_text = "triple diamonds!";
                    } else if (s1 == "🍇") {
                        winnings = bet * 10;
                        outcome_text = "triple grapes!";
                    } else if (s1 == "🍊") {
                        winnings = bet * 5;
                        outcome_text = "triple oranges!";
                    } else if (s1 == "🍋") {
                        winnings = bet * 2;
                        outcome_text = "triple lemons!";
                    } else {
                        winnings = static_cast<int64_t>(bet * 1.5);
                        outcome_text = "triple cherries!";
                    }
                } else if (s1 == s2 || s2 == s3 || s1 == s3) {
                    winnings = -bet / 2;
                    outcome_text = "double match! you returned half your bet.";
                } else {
                    winnings = -bet;
                    outcome_text = "no match.";
                }

                g_db->update_balances(msg.author.id, winnings, 0);

                stoatpp::models::MessagePayload payload;
                payload.content = " ";
                stoatpp::models::Embed embed;
                embed.set_colour((winnings >= 0) ? 0x2ECC71 : 0xE74C3C);
                embed.set_title("bronx economy — slots");

                std::string desc = 
                    "[ " + s1 + " | " + s2 + " | " + s3 + " ]\n\n" +
                    outcome_text + "\n\n";

                if (winnings > 0) {
                    desc += "you won **" + std::to_string(winnings) + "** coins!";
                } else if (winnings < 0) {
                    desc += "you lost **" + std::to_string(-winnings) + "** coins.";
                } else {
                    desc += "you broke even.";
                }

                embed.set_description(desc);
                payload.add_embed(embed);
                bot.send_message(msg.channel_id, payload);
            },
            "Economy",
            0,
            5
        });

        // 11. hustle command
        bot.register_command({
            "hustle",
            {},
            "does a Bronx side hustle to earn some quick coins.",
            "hustle",
            {},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                std::vector<std::string> hustles = {
                    "flipped a pair of deadstock Jordans and made **%d** coins.",
                    "sold loosey cigarettes outside the 161st St subway station and made **%d** coins.",
                    "distributed your fire mixtape on Grand Concourse and got **%d** coins in tips.",
                    "sold bootleg DVDs on Fordham Road and earned **%d** coins.",
                    "washed windshields at a red light in the South Bronx and earned **%d** coins.",
                    "ran a street-side shaved ice stand during a heatwave and made **%d** coins.",
                    "did a gig delivering bacon-egg-and-cheese sandwiches on a moped and made **%d** coins.",
                    "helped someone move a couch up a 5th-floor walk-up in Tremont and earned **%d** coins."
                };

                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<size_t> hustle_dist(0, hustles.size() - 1);
                std::uniform_int_distribution<int64_t> pay_dist(300, 900);

                std::string hustle_tmpl = hustles[hustle_dist(gen)];
                int64_t pay = pay_dist(gen);

                auto u = g_db->get_economy_user(msg.author.id);
                double multiplier = 1.0;
                if (u) {
                    multiplier += (u->prestige * 0.15);
                }
                pay = static_cast<int64_t>(pay * multiplier);

                g_db->update_balances(msg.author.id, pay, 0);

                char buf[512];
                snprintf(buf, sizeof(buf), hustle_tmpl.c_str(), pay);
                bot.send_message(msg.channel_id, "you " + std::string(buf));
            },
            "Economy",
            0,
            1800
        });

        // 12. crime command
        bot.register_command({
            "crime",
            {"rob-bank", "heist"},
            "commits a crime in the Bronx streets for a high payout, but with a risk of getting caught.",
            "crime",
            {},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                auto self = g_db->get_economy_user(msg.author.id);
                if (!self) return;

                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<int> success_dist(0, 99);
                bool success = (success_dist(gen) < 60);

                double multiplier = 1.0 + (self->prestige * 0.15);

                if (success) {
                    std::vector<std::string> success_scenarios = {
                        "you successfully pickpocketed a tourist at Yankee Stadium and ran away with **%d** coins!",
                        "you bypassed a security camera and hotwired an old sedan, selling it to a chop shop for **%d** coins!",
                        "you successfully snuck into a fancy rooftop party in Manhattan and swiped a wallet with **%d** coins!",
                        "you set up a fake shell game on the sidewalk and hustled some gullible marks out of **%d** coins!",
                        "you hacked into a vending machine using a paperclip and retrieved **%d** coins!"
                    };
                    std::uniform_int_distribution<size_t> sc_dist(0, success_scenarios.size() - 1);
                    std::uniform_int_distribution<int64_t> pay_dist(1000, 3000);

                    std::string sc_tmpl = success_scenarios[sc_dist(gen)];
                    int64_t pay = static_cast<int64_t>(pay_dist(gen) * multiplier);

                    g_db->update_balances(msg.author.id, pay, 0);

                    char buf[512];
                    snprintf(buf, sizeof(buf), sc_tmpl.c_str(), pay);
                    bot.send_message(msg.channel_id, std::string(buf));
                } else {
                    std::vector<std::string> fail_scenarios = {
                        "you tried to shoplift a bodega but the owner chased you out with a broom! you lost **%d** coins in the escape.",
                        "you attempted to hotwire a car but the car alarm went off, drawing the police! you paid a fine of **%d** coins.",
                        "you tried to swipe a handbag at Grand Concourse, but a bystander tackled you! you had to pay **%d** coins to settle the situation.",
                        "you tried to sell bootleg merchandise, but undercover officers confiscated your stock and fined you **%d** coins."
                    };
                    std::uniform_int_distribution<size_t> fail_dist(0, fail_scenarios.size() - 1);
                    std::uniform_int_distribution<int64_t> fine_dist(500, 1500);

                    std::string fail_tmpl = fail_scenarios[fail_dist(gen)];
                    int64_t fine = fine_dist(gen);
                    if (self->wallet < fine) {
                        fine = self->wallet;
                    }

                    g_db->update_balances(msg.author.id, -fine, 0);

                    char buf[512];
                    snprintf(buf, sizeof(buf), fail_tmpl.c_str(), fine);
                    bot.send_message(msg.channel_id, std::string(buf));
                }
            },
            "Economy",
            0,
            3600
        });

        // 13. search command
        bot.register_command({
            "search",
            {"scavenge", "find"},
            "searches a nearby Bronx spot for spare change.",
            "search",
            {},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                std::vector<std::string> locations = {
                    "under the sofa cushions of the local community center",
                    "inside a subway station trash can",
                    "around the seats of a Bronx bus",
                    "in the grass at Pelham Bay Park",
                    "under a sewer grate on 161st St",
                    "behind a vending machine in a laundromat",
                    "inside the pockets of an old winter jacket you haven't worn in years"
                };

                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<size_t> loc_dist(0, locations.size() - 1);
                std::uniform_int_distribution<int64_t> pay_dist(100, 400);

                std::string loc = locations[loc_dist(gen)];
                int64_t pay = pay_dist(gen);

                auto u = g_db->get_economy_user(msg.author.id);
                double multiplier = 1.0;
                if (u) {
                    multiplier += (u->prestige * 0.15);
                }
                pay = static_cast<int64_t>(pay * multiplier);

                g_db->update_balances(msg.author.id, pay, 0);

                bot.send_message(msg.channel_id, "you searched **" + loc + "** and found **" + std::to_string(pay) + "** coins!");
            },
            "Economy",
            0,
            600
        });

        // 14. highlow command
        bot.register_command({
            "highlow",
            {"hl"},
            "bets coins on a game of high-low.",
            "highlow <amount>",
            {"<amount>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                if (args.empty()) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `highlow <amount>`.");
                    return;
                }

                auto self = g_db->get_economy_user(msg.author.id);
                if (!self) return;

                int64_t bet = 0;
                if (args[0] == "all" || args[0] == "max") {
                    bet = self->wallet;
                } else if (args[0] == "half") {
                    bet = self->wallet / 2;
                } else {
                    try {
                        bet = std::stoll(args[0]);
                    } catch (...) {}
                }

                if (bet < 10) {
                    bot.send_message(msg.channel_id, "minimum bet is 10 coins.");
                    return;
                }

                if (self->wallet < bet) {
                    bot.send_message(msg.channel_id, "you do not have enough coins in your wallet.");
                    return;
                }

                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<int> num_dist(1, 100);
                int secret = num_dist(gen);
                int hint = num_dist(gen);
                while (hint == secret) {
                    hint = num_dist(gen);
                }

                bot.send_message(msg.channel_id, "🔍 **High-Low Game**\n"
                                                 "I am thinking of a secret number between **1** and **100**.\n"
                                                 "The hint is: **" + std::to_string(hint) + "**\n"
                                                 "Is the secret number **higher** or **lower** than the hint? Type `higher` (`h`) or `lower` (`l`).\n"
                                                 "You have 30 seconds!");

                bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30), [this, &bot, msg, secret, hint, bet](std::optional<stoatpp::events::Message> reply) {
                    if (!g_db) return;
                    if (!reply) {
                        g_db->update_balances(msg.author.id, -bet, 0);
                        bot.send_message(msg.channel_id, "⏱️ <@" + msg.author.id + "> took too long to answer and lost their bet of **" + std::to_string(bet) + "** coins.");
                        return;
                    }

                    std::string answer = reply->content;
                    std::transform(answer.begin(), answer.end(), answer.begin(), ::tolower);

                    if (answer != "higher" && answer != "h" && answer != "lower" && answer != "l") {
                        g_db->update_balances(msg.author.id, -bet, 0);
                        bot.send_message(msg.channel_id, "❌ Invalid guess! You lost your bet of **" + std::to_string(bet) + "** coins.");
                        return;
                    }

                    bool guess_higher = (answer == "higher" || answer == "h");
                    bool actual_higher = (secret > hint);
                    bool won = (guess_higher == actual_higher);

                    stoatpp::models::MessagePayload payload;
                    payload.content = " ";
                    stoatpp::models::Embed embed;
                    embed.set_colour(won ? 0x2ECC71 : 0xE74C3C);
                    embed.set_title("🔍 High-Low Result");

                    std::string desc = "The secret number was **" + std::to_string(secret) + "** (Hint was **" + std::to_string(hint) + "**).\n\n";
                    if (won) {
                        g_db->update_balances(msg.author.id, bet, 0);
                        desc += "🎉 Correct! You won **" + std::to_string(bet) + "** coins!";
                    } else {
                        g_db->update_balances(msg.author.id, -bet, 0);
                        desc += "❌ Incorrect! You lost **" + std::to_string(bet) + "** coins.";
                    }
                    embed.set_description(desc);
                    payload.add_embed(embed);
                    bot.send_message(msg.channel_id, payload);
                });
            },
            "Economy",
            0,
            10
        });

        // 15. prestige command
        bot.register_command({
            "prestige",
            {},
            "resets your balances for a permanent multiplier and bank capacity boost.",
            "prestige",
            {},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                auto self = g_db->get_economy_user(msg.author.id);
                if (!self) return;

                int64_t networth = self->wallet + self->bank;
                int64_t required = 250000 * (self->prestige + 1);

                if (networth < required) {
                    bot.send_message(msg.channel_id, "❌ **Prestige Requirements Not Met**\n"
                                                     "You need a net worth of **" + std::to_string(required) + "** coins to prestige to level **" + std::to_string(self->prestige + 1) + "**.\n"
                                                     "Your current net worth is **" + std::to_string(networth) + "** coins.");
                    return;
                }

                bot.send_message(msg.channel_id, "⚠️ **Confirm Prestige Level " + std::to_string(self->prestige + 1) + "**\n"
                                                 "Prestiging resets your **wallet** and **bank** balance to **0**!\n"
                                                 "In return, you will gain:\n"
                                                 "- Permanent **+15%** earnings multiplier (new total: **+" + std::to_string((self->prestige + 1) * 15) + "%**)\n"
                                                 "- Expanded bank limit of **" + std::to_string(10000 + 50000 * (self->prestige + 1)) + "** coins\n\n"
                                                 "Type `confirm` within 30 seconds to proceed.");

                bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30), [this, &bot, msg, self](std::optional<stoatpp::events::Message> reply) {
                    if (!g_db) return;
                    if (!reply) {
                        bot.send_message(msg.channel_id, "⏱️ Prestige confirmation timed out. Action cancelled.");
                        return;
                    }

                    std::string answer = reply->content;
                    std::transform(answer.begin(), answer.end(), answer.begin(), ::tolower);

                    if (answer == "confirm") {
                        auto current = g_db->get_economy_user(msg.author.id);
                        if (!current) return;
                        
                        int64_t current_networth = current->wallet + current->bank;
                        int64_t req = 250000 * (current->prestige + 1);
                        if (current_networth < req) {
                            bot.send_message(msg.channel_id, "❌ Your net worth has changed and you no longer meet the prestige requirement.");
                            return;
                        }

                        int64_t new_limit = 10000 + 50000 * (current->prestige + 1);
                        if (g_db->prestige_user(msg.author.id, new_limit)) {
                            int new_prestige = current->prestige + 1;
                            bot.send_message(msg.channel_id, "🎉 **Prestige Success!** 🎉\n"
                                                             "Congratulations <@" + msg.author.id + ">, you have prestiged to level **" + std::to_string(new_prestige) + "**!\n"
                                                             "Your new bank limit is **" + std::to_string(new_limit) + "** coins, and your earnings are boosted by **+" + std::to_string(new_prestige * 15) + "%**!");
                        } else {
                            bot.send_message(msg.channel_id, "❌ An error occurred while prestiging. Please try again.");
                        }
                    } else {
                        bot.send_message(msg.channel_id, "❌ Confirmation failed. Prestige cancelled.");
                    }
                });
            },
            "Economy"
        });

        // 16. inventory command
        bot.register_command({
            "inventory",
            {"inv", "items", "bag"},
            "shows your inventory including event items, general economy items, and fishing gear.",
            "inventory [user]",
            {"[user]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                std::string target_uid = msg.author.id;
                if (!args.empty()) {
                    target_uid = resolve_user_id(args[0]);
                }

                auto eco = g_db->get_economy_user(target_uid);
                auto fish = g_db->get_fishing_user(target_uid);
                auto user_items = g_db->get_user_economy_items(target_uid);

                stoatpp::models::MessagePayload payload;
                payload.content = " ";
                stoatpp::models::Embed embed;
                embed.set_colour(0x5865F2);
                embed.set_title("🎒 Bronx Inventory — <@" + target_uid + ">");

                std::string desc = "";

                // 🎫 Section 1: Event Items
                desc += "🎫 **EVENT ITEMS & CURRENCY**\n";
                int event_tokens = fish ? fish->event_tokens : 0;
                desc += "• Event Tokens: **" + std::to_string(event_tokens) + "** 🎫\n";

                auto user_pets = g_db->get_user_pets(target_uid);
                if (!user_pets.empty()) {
                    desc += "• Event / Companion Pets Unlocked: **" + std::to_string(user_pets.size()) + "** pets (`.fish pets`)\n";
                }
                desc += "\n";

                // 🎒 Section 2: General Economy Items
                desc += "🎒 **GENERAL ECONOMY ITEMS & PROTECTIONS**\n";
                if (user_items.empty()) {
                    desc += "*No general items owned yet. Visit `.shop` to buy items!*\n";
                } else {
                    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();

                    for (const auto& item : user_items) {
                        if (item.item_id == "robbery_guard") {
                            std::string status = (item.active_until > static_cast<uint64_t>(now)) ?
                                ("🟢 **ACTIVE** (" + format_cooldown(item.active_until - now) + " remaining)") : "⚪ Inactive (Use `.use robbery_guard`)";
                            desc += "• 🛡️ **Robbery Guard**: " + status + "\n";
                        } else if (item.item_id == "reverse_card") {
                            std::string status = (item.active_until > static_cast<uint64_t>(now)) ?
                                ("🟢 **ACTIVE** (" + format_cooldown(item.active_until - now) + " remaining)") : "⚪ Inactive (Use `.use reverse_card`)";
                            desc += "• 💥 **Uno Reverse Card**: " + status + "\n";
                        } else if (item.item_id == "item_detector") {
                            desc += "• 🔍 **Item Detector**: **" + std::to_string(item.uses_left) + "** uses remaining (Qty: " + std::to_string(item.quantity) + ") — Use `.detect <user>`\n";
                        } else {
                            desc += "• 📦 `" + item.item_id + "`: Quantity **" + std::to_string(item.quantity) + "**\n";
                        }
                    }
                }
                desc += "\n";

                // 🎣 Section 3: Fishing Gear & Fish Bucket
                desc += "🎣 **FISHING GEAR & BUCKET**\n";
                if (fish) {
                    desc += "• Rod: **" + fish->rod_id + "** | Bait: **" + fish->bait_id + "**\n";
                    desc += "• Active Pet: **" + (fish->active_pet_id == "none" ? "None" : fish->active_pet_id) + "**\n";
                    desc += "• Total Catches: **" + std::to_string(fish->total_catches) + "** | Fishing Tier: **P" + std::to_string(fish->fish_prestige) + "**\n";

                    auto user_fish = g_db->get_user_fish_inventory(target_uid);
                    desc += "• Bucket Inventory: **" + std::to_string(user_fish.size()) + "** fish in bucket (`.fish bucket`)\n";
                } else {
                    desc += "*No fishing data available.*\n";
                }

                embed.set_description(desc);
                payload.add_embed(embed);
                bot.send_message(msg.channel_id, payload);
            },
            "Economy"
        });

        // 17. generalshop command
        bot.register_command({
            "generalshop",
            {"shop", "store", "market"},
            "opens the Bronx General Economy Item Shop.",
            "generalshop",
            {},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                stoatpp::models::MessagePayload payload;
                payload.content = " ";
                stoatpp::models::Embed embed;
                embed.set_colour(0x2ECC71);
                embed.set_title("🛒 Bronx General Economy Shop");

                std::string desc = "Use `.buy <item_id> [quantity]` to purchase an item, and `.use <item_id>` to activate it!\n\n";

                desc += "🛡️ **`robbery_guard` — Robbery Guard**\n"
                        "   Price: **5,000** coins | Duration: **24 Hours**\n"
                        "   *Prevents anyone from robbing you for 24 hours while active.*\n\n";

                desc += "💥 **`reverse_card` — Uno Reverse Card**\n"
                        "   Price: **15,000** coins | Duration: **24 Hours**\n"
                        "   *Gotcha trap! Anyone robbing you within 24 hours gets robbed back instead.*\n\n";

                desc += "🔍 **`item_detector` — Item Detector**\n"
                        "   Price: **2,500** coins | Charges: **5 Uses**\n"
                        "   *Scans a target user to see if they have active protections or traps (`.detect <user>`).*\n\n";

                auto custom_items = g_db->get_custom_shop_items();
                if (!custom_items.empty()) {
                    desc += "✨ **CUSTOM SHOP ITEMS:**\n";
                    for (const auto& item : custom_items) {
                        desc += "• `" + item.item_id + "` — **" + item.name + "** (" + std::to_string(item.cost) + " coins)\n"
                                "   " + item.description + "\n";
                    }
                }

                desc += "*Tip: Check your items with `.inventory`!*";

                embed.set_description(desc);
                payload.add_embed(embed);
                bot.send_message(msg.channel_id, payload);
            },
            "Economy"
        });

        // 18. buyitem command
        bot.register_command({
            "buyitem",
            {"buy"},
            "buys an item from the general shop.",
            "buy <item_id> [quantity]",
            {"<item_id>", "[quantity]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                if (args.empty()) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `buy <item_id> [quantity]`.");
                    return;
                }

                std::string item_id = args[0];
                std::transform(item_id.begin(), item_id.end(), item_id.begin(), ::tolower);

                int qty = 1;
                if (args.size() > 1) {
                    try { qty = std::stoi(args[1]); } catch (...) {}
                }
                if (qty <= 0) qty = 1;

                int64_t unit_cost = 0;
                int default_uses = 0;
                int duration = 0;
                std::string item_name = item_id;

                if (item_id == "robbery_guard" || item_id == "guard" || item_id == "shield") {
                    item_id = "robbery_guard";
                    item_name = "Robbery Guard";
                    unit_cost = 5000;
                    duration = 86400;
                } else if (item_id == "reverse_card" || item_id == "reverse" || item_id == "uno_reverse") {
                    item_id = "reverse_card";
                    item_name = "Uno Reverse Card";
                    unit_cost = 15000;
                    duration = 86400;
                } else if (item_id == "item_detector" || item_id == "detector" || item_id == "scanner") {
                    item_id = "item_detector";
                    item_name = "Item Detector";
                    unit_cost = 2500;
                    default_uses = 5 * qty;
                } else {
                    auto custom = g_db->get_custom_shop_items();
                    bool found = false;
                    for (const auto& ci : custom) {
                        if (ci.item_id == item_id) {
                            item_name = ci.name;
                            unit_cost = ci.cost;
                            default_uses = ci.uses_default * qty;
                            duration = ci.duration_seconds;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        bot.send_message(msg.channel_id, "❌ Item `" + item_id + "` not found in general shop. Use `.shop` to see available items or `.fish shop` for fishing items.");
                        return;
                    }
                }

                int64_t total_cost = unit_cost * qty;
                auto user = g_db->get_economy_user(msg.author.id);
                if (!user || user->wallet < total_cost) {
                    bot.send_message(msg.channel_id, "❌ You do not have enough coins in your wallet. Total cost: **" + std::to_string(total_cost) + "** coins.");
                    return;
                }

                g_db->update_balances(msg.author.id, -total_cost, 0);
                g_db->add_user_economy_item(msg.author.id, item_id, qty, default_uses, duration);

                bot.send_message(msg.channel_id, "🎉 Successfully purchased **" + std::to_string(qty) + "x " + item_name + "** for **" + std::to_string(total_cost) + "** coins! Use `.inventory` to view your items.");
            },
            "Economy"
        });

        // 19. useitem command
        bot.register_command({
            "useitem",
            {"use", "activate"},
            "uses or activates an item from your inventory.",
            "use <item_id>",
            {"<item_id>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                if (args.empty()) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `use <item_id>`.");
                    return;
                }

                std::string item_id = args[0];
                std::transform(item_id.begin(), item_id.end(), item_id.begin(), ::tolower);

                if (item_id == "guard" || item_id == "robbery_guard" || item_id == "shield") item_id = "robbery_guard";
                if (item_id == "reverse" || item_id == "reverse_card" || item_id == "uno_reverse") item_id = "reverse_card";
                if (item_id == "detector" || item_id == "item_detector") item_id = "item_detector";

                if (item_id == "item_detector") {
                    bot.send_message(msg.channel_id, "🔍 To use your Item Detector, run `.detect <user>`!");
                    return;
                }

                if (item_id == "robbery_guard") {
                    if (g_db->activate_item(msg.author.id, "robbery_guard", 86400)) {
                        bot.send_message(msg.channel_id, "🛡️ **Robbery Guard Activated!** You are protected against all robbery attempts for the next 24 hours.");
                    } else {
                        bot.send_message(msg.channel_id, "❌ You do not own a Robbery Guard! Buy one in `.shop`.");
                    }
                    return;
                }

                if (item_id == "reverse_card") {
                    if (g_db->activate_item(msg.author.id, "reverse_card", 86400)) {
                        bot.send_message(msg.channel_id, "💥 **Uno Reverse Card Armed!** For the next 24 hours, anyone attempting to rob you will be counter-robbed!");
                    } else {
                        bot.send_message(msg.channel_id, "❌ You do not own an Uno Reverse Card! Buy one in `.shop`.");
                    }
                    return;
                }

                bot.send_message(msg.channel_id, "❌ Item `" + item_id + "` cannot be activated directly or is not owned.");
            },
            "Economy"
        });

        // 20. detect command
        bot.register_command({
            "detect",
            {"detector", "scan"},
            "uses an Item Detector to scan a target user for active protections and items.",
            "detect <user>",
            {"<user>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                if (args.empty()) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `detect <user>`.");
                    return;
                }

                std::string target_uid = resolve_user_id(args[0]);

                if (!g_db->use_item_charge(msg.author.id, "item_detector")) {
                    bot.send_message(msg.channel_id, "❌ You do not have an Item Detector with available uses! Purchase one in `.shop`.");
                    return;
                }

                bool guard_active = g_db->is_item_active(target_uid, "robbery_guard");
                bool reverse_active = g_db->is_item_active(target_uid, "reverse_card");
                auto items = g_db->get_user_economy_items(target_uid);

                stoatpp::models::MessagePayload payload;
                payload.content = " ";
                stoatpp::models::Embed embed;
                embed.set_colour(0xF1C40F);
                embed.set_title("🔍 Item Detector Scan Results — <@" + target_uid + ">");

                std::string desc = "Scan completed! 1 Detector charge used.\n\n";

                desc += "🛡️ **Robbery Guard Status:** " + std::string(guard_active ? "🟢 **ACTIVE (PROTECTED)**" : "🔴 Inactive") + "\n";
                desc += "💥 **Uno Reverse Card Status:** " + std::string(reverse_active ? "🟢 **ARMED (GOTCHA TRAP ACTIVE!)**" : "🔴 Inactive") + "\n\n";

                desc += "📦 **Detected Inventory:**\n";
                if (items.empty()) {
                    desc += "*No stored inventory items detected.*";
                } else {
                    for (const auto& it : items) {
                        desc += "• `" + it.item_id + "` (Qty: " + std::to_string(it.quantity) + ")\n";
                    }
                }

                embed.set_description(desc);
                payload.add_embed(embed);
                bot.send_message(msg.channel_id, payload);
            },
            "Economy"
        });
    }
};

} // namespace bronx::cogs
