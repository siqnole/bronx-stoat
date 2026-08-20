#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <sstream>

namespace bronx::cogs {

struct BJCard {
    std::string suit;
    std::string rank;
    int value;
    
    std::string display() const {
        return rank + suit;
    }
};

struct BJGame {
    std::string user_id;
    std::string target_id;
    std::string active_id;
    std::string message_id;
    std::string channel_id;
    int64_t bet;
    std::vector<BJCard> player_hand;
    std::vector<BJCard> dealer_hand;
    std::vector<BJCard> deck;
    bool active = true;
    bool player_stood = false;
    bool dealer_stood = false;
    bool doubled = false;
    bool target_doubled = false;
    bool is_pvp = false;
};

struct BJChallenge {
    std::string challenger_id;
    std::string target_id;
    std::string channel_id;
    int64_t bet = 0;
    bool accepted = false;
};

class BlackjackModule : public stoatpp::bot_module {
private:
    std::unordered_map<std::string, BJGame> active_games_;
    std::mutex bj_mutex_;
    std::unordered_map<std::string, BJChallenge> active_challenges_;
    std::mutex challenge_mutex_;

public:
    inline static BlackjackModule* instance = nullptr;

    BlackjackModule() {
        instance = this;
    }
    ~BlackjackModule() override {
        if (instance == this) {
            instance = nullptr;
        }
    }

    std::string resolve_user_id(const std::string& arg) {
        if (arg.size() > 3 && arg.front() == '<' && arg[1] == '@' && arg.back() == '>') {
            return arg.substr(2, arg.size() - 3);
        }
        return arg;
    }

    BJGame* find_active_game(const std::string& user_id) {
        auto it = active_games_.find(user_id);
        if (it != active_games_.end()) {
            return &it->second;
        }
        for (auto& [k, game] : active_games_) {
            if (game.is_pvp && (game.user_id == user_id || game.target_id == user_id)) {
                return &game;
            }
        }
        return nullptr;
    }

    std::vector<BJCard> make_deck() {
        std::vector<BJCard> d;
        std::vector<std::string> suits = {"♠", "♥", "♦", "♣"};
        std::vector<std::pair<std::string, int>> ranks = {
            {"a", 11}, {"2", 2}, {"3", 3}, {"4", 4}, {"5", 5}, {"6", 6},
            {"7", 7}, {"8", 8}, {"9", 9}, {"10", 10}, {"j", 10}, {"q", 10}, {"k", 10}
        };
        for (int i = 0; i < 4; ++i) { // 4 decks
            for (const auto& s : suits) {
                for (const auto& r : ranks) {
                    d.push_back({s, r.first, r.second});
                }
            }
        }
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(d.begin(), d.end(), g);
        return d;
    }

    int calculate_hand(const std::vector<BJCard>& hand) {
        int score = 0;
        int aces = 0;
        for (const auto& c : hand) {
            score += c.value;
            if (c.rank == "a") aces++;
        }
        while (score > 21 && aces > 0) {
            score -= 10;
            aces--;
        }
        return score;
    }

    void dealer_turn(BJGame& game, std::string& outcome, int64_t& payout) {
        int player_score = calculate_hand(game.player_hand);
        if (player_score > 21) {
            outcome = "**you busted!** dealer wins.";
            payout = 0;
            return;
        }

        int dealer_score = calculate_hand(game.dealer_hand);
        while (dealer_score < 17) {
            game.dealer_hand.push_back(game.deck.back());
            game.deck.pop_back();
            dealer_score = calculate_hand(game.dealer_hand);
        }

        if (dealer_score > 21) {
            outcome = "**dealer busted!** you win!";
            payout = game.bet * 2;
        } else if (player_score > dealer_score) {
            outcome = "**you won!** you beat the dealer.";
            payout = game.bet * 2;
        } else if (player_score < dealer_score) {
            outcome = "**dealer wins!** better luck next time.";
            payout = 0;
        } else {
            outcome = "**push!** bets have been refunded.";
            payout = game.bet;
        }
    }

    bool check_initial_naturals(BJGame& game, std::string& outcome, int64_t& payout) {
        int player_score = calculate_hand(game.player_hand);
        int dealer_score = calculate_hand(game.dealer_hand);
        if (player_score == 21 && dealer_score == 21) {
            outcome = "**push!** both you and the dealer got blackjack.";
            payout = game.bet;
            return true;
        }
        if (player_score == 21) {
            outcome = "**blackjack!** you win!";
            payout = static_cast<int64_t>(game.bet * 2.5);
            return true;
        }
        if (dealer_score == 21) {
            outcome = "**dealer got blackjack!** dealer wins.";
            payout = 0;
            return true;
        }
        return false;
    }

    stoatpp::models::MessagePayload render_bj_game(const BJGame& game, bool show_dealer_all, const std::string& outcome = "") {
        stoatpp::models::MessagePayload payload;
        payload.content = " ";
        stoatpp::models::Embed embed;
        embed.set_colour(0x5865F2);
        embed.set_title("bronx blackjack");

        std::string desc;

        if (game.is_pvp) {
            desc += "**challenger (<@" + game.user_id + ">):**\n";
            for (const auto& c : game.player_hand) {
                desc += "`" + c.display() + "` ";
            }
            int challenger_val = calculate_hand(game.player_hand);
            desc += "\nvalue: " + std::to_string(challenger_val);
            if (challenger_val > 21) desc += " 💥 *(busted)*";
            else if (game.player_stood) desc += " 🛑 *(stood)*";
            else if (game.active && game.active_id == game.user_id) desc += " 🃏 *(active turn)*";
            desc += "\n\n";

            desc += "**target (<@" + game.target_id + ">):**\n";
            for (const auto& c : game.dealer_hand) {
                desc += "`" + c.display() + "` ";
            }
            int target_val = calculate_hand(game.dealer_hand);
            desc += "\nvalue: " + std::to_string(target_val);
            if (target_val > 21) desc += " 💥 *(busted)*";
            else if (game.dealer_stood) desc += " 🛑 *(stood)*";
            else if (game.active && game.active_id == game.target_id) desc += " 🃏 *(active turn)*";
            desc += "\n\n";

            int64_t challenger_contrib = game.bet * (game.doubled ? 2 : 1);
            int64_t target_contrib = game.bet * (game.target_doubled ? 2 : 1);
            desc += "bet: **" + std::to_string(game.bet) + "** coins (challenger: **" + std::to_string(challenger_contrib) + "**, target: **" + std::to_string(target_contrib) + "**)\n";

            if (game.active) {
                desc += "---\n*it is <@" + game.active_id + ">'s turn.*\n";
                desc += "*react with 🃏 to hit, 🛑 to stand, or 💵 to double down.*\n";
                desc += "*or type `h` / `s` / `d` in the chat.*";
            } else {
                desc += "---\n" + outcome;
            }
        } else {
            desc += "**dealer hand:**\n";
            int dealer_val = 0;
            if (show_dealer_all) {
                for (const auto& c : game.dealer_hand) {
                    desc += "`" + c.display() + "` ";
                }
                dealer_val = calculate_hand(game.dealer_hand);
                desc += "\nvalue: " + std::to_string(dealer_val) + "\n\n";
            } else {
                desc += "`" + game.dealer_hand[0].display() + "` `?` \nvalue: " + std::to_string(game.dealer_hand[0].value) + "\n\n";
            }

            desc += "**your hand:**\n";
            for (const auto& c : game.player_hand) {
                desc += "`" + c.display() + "` ";
            }
            int player_val = calculate_hand(game.player_hand);
            desc += "\nvalue: " + std::to_string(player_val) + "\n\n";

            desc += "bet: **" + std::to_string(game.bet) + "** coins\n";

            if (game.active) {
                desc += "---\n*react with 🃏 to hit, 🛑 to stand, or 💵 to double down.*\n";
                desc += "*or type `h` / `s` / `d` in the chat.*";
            } else {
                desc += "---\n" + outcome;
            }
        }

        embed.set_description(desc);
        payload.add_embed(embed);
        return payload;
    }

    void evaluate_pvp_outcome(stoatpp::cluster& bot, BJGame& game, std::unique_lock<std::mutex>& lock) {
        int challenger_score = calculate_hand(game.player_hand);
        int target_score = calculate_hand(game.dealer_hand);

        int64_t challenger_contrib = game.bet * (game.doubled ? 2 : 1);
        int64_t target_contrib = game.bet * (game.target_doubled ? 2 : 1);
        int64_t pool = challenger_contrib + target_contrib;

        std::string outcome;

        bool challenger_bust = (challenger_score > 21);
        bool target_bust = (target_score > 21);

        if (challenger_bust && target_bust) {
            outcome = "**both players busted!** push — bets refunded.";
            g_db->update_balances(game.user_id, challenger_contrib, 0);
            g_db->update_balances(game.target_id, target_contrib, 0);
        } else if (challenger_bust) {
            outcome = "🏆 <@" + game.target_id + "> wins! <@" + game.user_id + "> busted.";
            g_db->update_balances(game.target_id, pool, 0);
        } else if (target_bust) {
            outcome = "🏆 <@" + game.user_id + "> wins! <@" + game.target_id + "> busted.";
            g_db->update_balances(game.user_id, pool, 0);
        } else if (challenger_score > target_score) {
            outcome = "🏆 <@" + game.user_id + "> wins with **" + std::to_string(challenger_score) + "** against **" + std::to_string(target_score) + "**!";
            g_db->update_balances(game.user_id, pool, 0);
        } else if (target_score > challenger_score) {
            outcome = "🏆 <@" + game.target_id + "> wins with **" + std::to_string(target_score) + "** against **" + std::to_string(challenger_score) + "**!";
            g_db->update_balances(game.target_id, pool, 0);
        } else {
            outcome = "**push!** both players have the same score (**" + std::to_string(challenger_score) + "**). bets refunded.";
            g_db->update_balances(game.user_id, challenger_contrib, 0);
            g_db->update_balances(game.target_id, target_contrib, 0);
        }

        auto payload = render_bj_game(game, true, outcome);
        std::string chan = game.channel_id;
        std::string msg_id = game.message_id;
        active_games_.erase(game.user_id);
        lock.unlock();

        bot.edit_message(chan, msg_id, payload);
    }

    void start_pvp_game(stoatpp::cluster& bot, const BJChallenge& challenge) {
        if (challenge.bet > 0 && g_db) {
            auto self = g_db->get_economy_user(challenge.challenger_id);
            auto target = g_db->get_economy_user(challenge.target_id);
            if (!self || self->wallet < challenge.bet || !target || target->wallet < challenge.bet) {
                bot.send_message(challenge.channel_id, "one of the players no longer has enough coins. blackjack duel cancelled.");
                return;
            }
            g_db->update_balances(challenge.challenger_id, -challenge.bet, 0);
            g_db->update_balances(challenge.target_id, -challenge.bet, 0);
        }

        BJGame game;
        game.user_id = challenge.challenger_id;
        game.target_id = challenge.target_id;
        game.active_id = challenge.challenger_id;
        game.channel_id = challenge.channel_id;
        game.bet = challenge.bet;
        game.deck = make_deck();
        game.is_pvp = true;

        // Deal cards
        game.player_hand.push_back(game.deck.back()); game.deck.pop_back();
        game.dealer_hand.push_back(game.deck.back()); game.deck.pop_back();
        game.player_hand.push_back(game.deck.back()); game.deck.pop_back();
        game.dealer_hand.push_back(game.deck.back()); game.deck.pop_back();

        int challenger_score = calculate_hand(game.player_hand);
        int target_score = calculate_hand(game.dealer_hand);

        if (challenger_score == 21 || target_score == 21) {
            game.active = false;
            std::string outcome;
            if (challenger_score == 21 && target_score == 21) {
                outcome = "**push!** both players got blackjack. bets refunded.";
                g_db->update_balances(challenge.challenger_id, challenge.bet, 0);
                g_db->update_balances(challenge.target_id, challenge.bet, 0);
            } else if (challenger_score == 21) {
                outcome = "🏆 <@" + challenge.challenger_id + "> got blackjack and won!";
                g_db->update_balances(challenge.challenger_id, challenge.bet * 2, 0);
            } else {
                outcome = "🏆 <@" + challenge.target_id + "> got blackjack and won!";
                g_db->update_balances(challenge.target_id, challenge.bet * 2, 0);
            }
            auto payload = render_bj_game(game, true, outcome);
            bot.send_message(challenge.channel_id, payload);
        } else {
            {
                std::lock_guard<std::mutex> lock(bj_mutex_);
                active_games_[challenge.challenger_id] = game;
            }

            auto payload = render_bj_game(game, false);
            bot.send_message(challenge.channel_id, payload, [this, &bot, challenge](stoatpp::models::Message res, bool success) {
                if (success) {
                    std::lock_guard<std::mutex> lock(bj_mutex_);
                    auto it = active_games_.find(challenge.challenger_id);
                    if (it != active_games_.end()) {
                        it->second.message_id = res.id;
                    }
                    bot.react_to_message(challenge.channel_id, res.id, "🃏");
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    bot.react_to_message(challenge.channel_id, res.id, "🛑");
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    bot.react_to_message(challenge.channel_id, res.id, "💵");
                }
            });
        }
    }

    void handle_hit(stoatpp::cluster& bot, const std::string& user_id, BJGame& game, std::unique_lock<std::mutex>& lock) {
        if (game.is_pvp) {
            if (user_id != game.active_id) {
                lock.unlock();
                return;
            }
            
            bool is_challenger = (user_id == game.user_id);
            auto& hand = is_challenger ? game.player_hand : game.dealer_hand;
            hand.push_back(game.deck.back());
            game.deck.pop_back();

            int score = calculate_hand(hand);
            if (score >= 21) {
                if (is_challenger) {
                    game.player_stood = true;
                    game.active_id = game.target_id;
                    auto payload = render_bj_game(game, false);
                    std::string chan = game.channel_id;
                    std::string msg_id = game.message_id;
                    lock.unlock();
                    bot.edit_message(chan, msg_id, payload);
                } else {
                    game.dealer_stood = true;
                    game.active = false;
                    evaluate_pvp_outcome(bot, game, lock);
                }
            } else {
                auto payload = render_bj_game(game, false);
                std::string chan = game.channel_id;
                std::string msg_id = game.message_id;
                lock.unlock();
                bot.edit_message(chan, msg_id, payload);
            }
            return;
        }

        game.player_hand.push_back(game.deck.back());
        game.deck.pop_back();

        int player_score = calculate_hand(game.player_hand);
        if (player_score >= 21) {
            game.active = false;
            std::string outcome;
            int64_t payout = 0;
            dealer_turn(game, outcome, payout);
            
            if (payout > 0) {
                g_db->update_balances(user_id, payout, 0);
            }

            auto payload = render_bj_game(game, true, outcome);
            std::string chan = game.channel_id;
            std::string msg_id = game.message_id;
            active_games_.erase(user_id);
            lock.unlock();

            bot.edit_message(chan, msg_id, payload);
        } else {
            auto payload = render_bj_game(game, false);
            std::string chan = game.channel_id;
            std::string msg_id = game.message_id;
            lock.unlock();

            bot.edit_message(chan, msg_id, payload);
        }
    }

    void handle_stand(stoatpp::cluster& bot, const std::string& user_id, BJGame& game, std::unique_lock<std::mutex>& lock) {
        if (game.is_pvp) {
            if (user_id != game.active_id) {
                lock.unlock();
                return;
            }
            
            bool is_challenger = (user_id == game.user_id);
            if (is_challenger) {
                game.player_stood = true;
                game.active_id = game.target_id;
                auto payload = render_bj_game(game, false);
                std::string chan = game.channel_id;
                std::string msg_id = game.message_id;
                lock.unlock();
                bot.edit_message(chan, msg_id, payload);
            } else {
                game.dealer_stood = true;
                game.active = false;
                evaluate_pvp_outcome(bot, game, lock);
            }
            return;
        }

        game.active = false;
        std::string outcome;
        int64_t payout = 0;
        dealer_turn(game, outcome, payout);

        if (payout > 0) {
            g_db->update_balances(user_id, payout, 0);
        }

        auto payload = render_bj_game(game, true, outcome);
        std::string chan = game.channel_id;
        std::string msg_id = game.message_id;
        active_games_.erase(user_id);
        lock.unlock();

        bot.edit_message(chan, msg_id, payload);
    }

    void handle_double(stoatpp::cluster& bot, const std::string& user_id, BJGame& game, std::unique_lock<std::mutex>& lock) {
        if (game.is_pvp) {
            if (user_id != game.active_id) {
                lock.unlock();
                return;
            }
            
            auto u = g_db->get_economy_user(user_id);
            if (!u || u->wallet < game.bet) {
                lock.unlock();
                bot.send_message(game.channel_id, "you do not have enough coins in your wallet to double down.");
                return;
            }
            
            g_db->update_balances(user_id, -game.bet, 0);
            
            bool is_challenger = (user_id == game.user_id);
            if (is_challenger) {
                game.doubled = true;
                game.player_hand.push_back(game.deck.back());
                game.deck.pop_back();
                game.player_stood = true;
                game.active_id = game.target_id;
                
                auto payload = render_bj_game(game, false);
                std::string chan = game.channel_id;
                std::string msg_id = game.message_id;
                lock.unlock();
                bot.edit_message(chan, msg_id, payload);
            } else {
                game.target_doubled = true;
                game.dealer_hand.push_back(game.deck.back());
                game.deck.pop_back();
                game.dealer_stood = true;
                game.active = false;
                evaluate_pvp_outcome(bot, game, lock);
            }
            return;
        }

        auto u = g_db->get_economy_user(user_id);
        if (!u || u->wallet < game.bet) {
            lock.unlock();
            bot.send_message(game.channel_id, "you do not have enough coins in your wallet to double down.");
            return;
        }

        g_db->update_balances(user_id, -game.bet, 0);
        game.bet *= 2;
        game.doubled = true;

        game.player_hand.push_back(game.deck.back());
        game.deck.pop_back();

        game.active = false;
        std::string outcome;
        int64_t payout = 0;
        dealer_turn(game, outcome, payout);

        if (payout > 0) {
            g_db->update_balances(user_id, payout, 0);
        }

        auto payload = render_bj_game(game, true, outcome);
        std::string chan = game.channel_id;
        std::string msg_id = game.message_id;
        active_games_.erase(user_id);
        lock.unlock();

        bot.edit_message(chan, msg_id, payload);
    }

public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "blackjack",
            {"bj"},
            "play a game of blackjack.",
            "blackjack <amount> [user]",
            {"<amount>", "[user]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                if (args.empty()) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `blackjack <amount> [user]`.");
                    return;
                }

                auto self = g_db->get_economy_user(msg.author.id);
                if (!self) return;

                int64_t bet = 0;
                std::string target_uid = "";

                auto parse_bet_val = [](const std::string& str, int64_t wallet) -> std::pair<bool, int64_t> {
                    if (str == "all" || str == "max") return {true, wallet};
                    if (str == "half") return {true, wallet / 2};
                    try {
                        size_t idx;
                        int64_t val = std::stoll(str, &idx);
                        if (idx == str.size()) return {true, val};
                    } catch(...) {}
                    return {false, 0};
                };

                if (args.size() >= 2) {
                    auto [is_num0, parsed_bet0] = parse_bet_val(args[0], self->wallet);
                    if (is_num0) {
                        bet = parsed_bet0;
                        target_uid = resolve_user_id(args[1]);
                    } else {
                        target_uid = resolve_user_id(args[0]);
                        auto [is_num1, parsed_bet1] = parse_bet_val(args[1], self->wallet);
                        if (is_num1) {
                            bet = parsed_bet1;
                        }
                    }
                } else {
                    auto [is_num, parsed_bet] = parse_bet_val(args[0], self->wallet);
                    if (is_num) {
                        bet = parsed_bet;
                    } else {
                        target_uid = resolve_user_id(args[0]);
                        bet = 0;
                    }
                }

                if (!target_uid.empty()) {
                    if (target_uid == msg.author.id) {
                        bot.send_message(msg.channel_id, "you cannot challenge yourself.");
                        return;
                    }

                    {
                        std::lock_guard<std::mutex> lock(bj_mutex_);
                        if (find_active_game(msg.author.id) != nullptr) {
                            bot.send_message(msg.channel_id, "you already have an active game of blackjack.");
                            return;
                        }
                        if (find_active_game(target_uid) != nullptr) {
                            bot.send_message(msg.channel_id, "the challenged user is already in a game of blackjack.");
                            return;
                        }
                    }

                    if (bet > 0) {
                        auto target_eco = g_db->get_economy_user(target_uid);
                        if (!target_eco || target_eco->wallet < bet) {
                            bot.send_message(msg.channel_id, "the challenged user does not have enough coins in their wallet.");
                            return;
                        }
                        if (self->wallet < bet) {
                            bot.send_message(msg.channel_id, "you do not have enough coins in your wallet.");
                            return;
                        }
                    }

                    std::string bet_str = (bet > 0) ? "**" + std::to_string(bet) + "** coins" : "fun (no bet)";
                    std::string invite_msg = "<@" + target_uid + ">, you have been challenged to a **blackjack** duel by <@" + msg.author.id + "> for " + bet_str + "!\nReact with " + bronx::emj_yes + " to accept or " + bronx::emj_no + " to decline.";

                    bot.send_message(msg.channel_id, invite_msg, [this, msg, target_uid, bet, &bot](stoatpp::models::Message res, bool success) {
                        if (success) {
                            BJChallenge challenge;
                            challenge.challenger_id = msg.author.id;
                            challenge.target_id = target_uid;
                            challenge.channel_id = msg.channel_id;
                            challenge.bet = bet;
                            challenge.accepted = false;

                            {
                                std::lock_guard<std::mutex> lock(challenge_mutex_);
                                active_challenges_[res.id] = challenge;
                            }

                            bot.react_to_message(msg.channel_id, res.id, "" + bronx::emj_yes + "");
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            bot.react_to_message(msg.channel_id, res.id, "" + bronx::emj_no + "");

                            std::thread([this, &bot, channel_id = msg.channel_id, msg_id = res.id]() {
                                std::this_thread::sleep_for(std::chrono::seconds(60));
                                std::lock_guard<std::mutex> lock(challenge_mutex_);
                                auto it = active_challenges_.find(msg_id);
                                if (it != active_challenges_.end() && !it->second.accepted) {
                                    bot.send_message(channel_id, "the challenge from <@" + it->second.challenger_id + "> timed out.");
                                    active_challenges_.erase(it);
                                }
                            }).detach();
                        }
                    });
                    return;
                }

                if (bet < 10) {
                    bot.send_message(msg.channel_id, "minimum bet is 10 coins.");
                    return;
                }

                if (self->wallet < bet) {
                    bot.send_message(msg.channel_id, "you do not have enough coins in your wallet.");
                    return;
                }

                g_db->update_balances(msg.author.id, -bet, 0);

                BJGame game;
                game.user_id = msg.author.id;
                game.channel_id = msg.channel_id;
                game.bet = bet;
                game.deck = make_deck();
                game.is_pvp = false;

                // Deal cards
                game.player_hand.push_back(game.deck.back()); game.deck.pop_back();
                game.dealer_hand.push_back(game.deck.back()); game.deck.pop_back();
                game.player_hand.push_back(game.deck.back()); game.deck.pop_back();
                game.dealer_hand.push_back(game.deck.back()); game.deck.pop_back();

                std::string outcome;
                int64_t payout = 0;
                if (check_initial_naturals(game, outcome, payout)) {
                    game.active = false;
                    if (payout > 0) {
                        g_db->update_balances(msg.author.id, payout, 0);
                    }
                    auto payload = render_bj_game(game, true, outcome);
                    bot.send_message(msg.channel_id, payload);
                } else {
                    {
                        std::lock_guard<std::mutex> lock(bj_mutex_);
                        active_games_[msg.author.id] = game;
                    }

                    auto payload = render_bj_game(game, false);
                    bot.send_message(msg.channel_id, payload, [this, &bot, msg](stoatpp::models::Message res, bool success) {
                        if (success) {
                            std::lock_guard<std::mutex> lock(bj_mutex_);
                            auto it = active_games_.find(msg.author.id);
                            if (it != active_games_.end()) {
                                it->second.message_id = res.id;
                            }
                            bot.react_to_message(msg.channel_id, res.id, "🃏");
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            bot.react_to_message(msg.channel_id, res.id, "🛑");
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            bot.react_to_message(msg.channel_id, res.id, "💵");
                        }
                    });
                }
            },
            "Economy"
        });

        // Gateway on_message hooks to allow text commands (h/s/d) inside active games
        bot.on_message([this, &bot](const stoatpp::events::Message& msg) {
            if (msg.author.bot) return;

            std::unique_lock<std::mutex> lock(bj_mutex_);
            auto* game = find_active_game(msg.author.id);
            if (game && game->channel_id == msg.channel_id) {
                std::string content = msg.content;
                std::transform(content.begin(), content.end(), content.begin(), ::tolower);
                
                std::string prefix = bot.config().command_prefix;
                if (content.rfind(prefix, 0) == 0) {
                    content = content.substr(prefix.size());
                }

                bool handled = false;
                if (content == "h" || content == "hit") {
                    handle_hit(bot, msg.author.id, *game, lock);
                    handled = true;
                } else if (content == "s" || content == "stand") {
                    handle_stand(bot, msg.author.id, *game, lock);
                    handled = true;
                } else if (content == "d" || content == "double") {
                    handle_double(bot, msg.author.id, *game, lock);
                    handled = true;
                }

                if (handled) {
                    bot.delete_message(msg.channel_id, msg.id);
                }
            }
        });

        // Gateway on_message_react hooks to allow reaction buttons & challenge acceptance
        bot.on_message_react([this, &bot](const stoatpp::events::MessageReact& e) {
            if (e.user_id == bot.current_user().id) return;

            // 1. Check if this is a challenge acceptance
            BJChallenge challenge;
            bool is_challenge = false;
            bool accepted = false;
            {
                std::lock_guard<std::mutex> lock(challenge_mutex_);
                auto it = active_challenges_.find(e.id);
                if (it != active_challenges_.end() && e.user_id == it->second.target_id) {
                    if (e.emoji_id == "" + bronx::emj_yes + "") {
                        it->second.accepted = true;
                        challenge = it->second;
                        active_challenges_.erase(it);
                        is_challenge = true;
                        accepted = true;
                    } else if (e.emoji_id == "" + bronx::emj_no + "") {
                        challenge = it->second;
                        active_challenges_.erase(it);
                        is_challenge = true;
                        accepted = false;
                    }
                }
            }

            if (is_challenge) {
                if (accepted) {
                    bot.send_message(challenge.channel_id, "" + bronx::emj_yes + " <@" + challenge.target_id + "> accepted the challenge!");
                    start_pvp_game(bot, challenge);
                } else {
                    bot.send_message(challenge.channel_id, "" + bronx::emj_no + " <@" + challenge.target_id + "> declined the challenge.");
                }
                return;
            }

            // 2. Otherwise, check if this is an active game reaction
            std::unique_lock<std::mutex> lock(bj_mutex_);
            auto* game = find_active_game(e.user_id);
            if (game && game->message_id == e.id) {
                bool handled = false;
                if (e.emoji_id == "🃏" || e.emoji_id == "➕") {
                    handle_hit(bot, e.user_id, *game, lock);
                    handled = true;
                } else if (e.emoji_id == "🛑" || e.emoji_id == "✋") {
                    handle_stand(bot, e.user_id, *game, lock);
                    handled = true;
                } else if (e.emoji_id == "💵" || e.emoji_id == "2️⃣") {
                    handle_double(bot, e.user_id, *game, lock);
                    handled = true;
                }

                if (handled) {
                    bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
                }
            }
        });
    }
};

} // namespace bronx::cogs
