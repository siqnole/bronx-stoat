#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <iostream>
#include <chrono>

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
            "leaderboard",
            {},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                auto top = g_db->get_top_economy_users(10);

                stoatpp::models::MessagePayload payload;
                payload.content = " ";
                stoatpp::models::Embed embed;
                embed.set_colour(0x5865F2);
                embed.set_title("bronx economy — leaderboard");

                std::string desc;
                if (top.empty()) {
                    desc = "no user balance records found.";
                } else {
                    int rank = 1;
                    for (const auto& u : top) {
                        desc += std::to_string(rank) + ". <@" + u.user_id + "> — **" + std::to_string(u.networth) + "** coins\n";
                        rank++;
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
            "coinflip <amount> [heads/tails]",
            {"<amount>", "[choice]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                if (args.empty()) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `coinflip <amount> [heads/tails]`.");
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

                std::string choice = "heads";
                if (args.size() > 1) {
                    choice = args[1];
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

                int64_t delta = won ? bet : -bet;
                g_db->update_balances(msg.author.id, delta, 0);

                stoatpp::models::MessagePayload payload;
                payload.content = " ";
                stoatpp::models::Embed embed;
                embed.set_colour(won ? 0x2ECC71 : 0xE74C3C);
                embed.set_title("bronx economy — coinflip");

                std::string result_str = heads ? "heads" : "tails";
                std::string desc = "the coin landed on **" + result_str + "**!\n\n";
                if (won) {
                    desc += "you won **" + std::to_string(bet) + "** coins!";
                } else {
                    desc += "you lost **" + std::to_string(bet) + "** coins.";
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
    }
};

} // namespace bronx::cogs
