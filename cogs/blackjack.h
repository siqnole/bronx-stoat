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
    std::string message_id;
    std::string channel_id;
    int64_t bet;
    std::vector<BJCard> player_hand;
    std::vector<BJCard> dealer_hand;
    std::vector<BJCard> deck;
    bool active = true;
    bool player_stood = false;
    bool doubled = false;
};

class BlackjackModule : public stoatpp::bot_module {
private:
    std::unordered_map<std::string, BJGame> active_games_;
    std::mutex bj_mutex_;

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

        embed.set_description(desc);
        payload.add_embed(embed);
        return payload;
    }

    void handle_hit(stoatpp::cluster& bot, const std::string& user_id, BJGame& game, std::unique_lock<std::mutex>& lock) {
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
            "blackjack <amount>",
            {"<amount>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!g_db) return;

                if (args.empty()) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `blackjack <amount>`.");
                    return;
                }

                auto self = g_db->get_economy_user(msg.author.id);
                if (!self) return;

                {
                    std::lock_guard<std::mutex> lock(bj_mutex_);
                    if (active_games_.count(msg.author.id) > 0) {
                        bot.send_message(msg.channel_id, "you already have an active game of blackjack.");
                        return;
                    }
                }

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

                g_db->update_balances(msg.author.id, -bet, 0);

                BJGame game;
                game.user_id = msg.author.id;
                game.channel_id = msg.channel_id;
                game.bet = bet;
                game.deck = make_deck();

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
            auto it = active_games_.find(msg.author.id);
            if (it != active_games_.end() && it->second.channel_id == msg.channel_id) {
                std::string content = msg.content;
                std::transform(content.begin(), content.end(), content.begin(), ::tolower);
                
                std::string prefix = bot.config().command_prefix;
                if (content.rfind(prefix, 0) == 0) {
                    content = content.substr(prefix.size());
                }

                bool handled = false;
                if (content == "h" || content == "hit") {
                    handle_hit(bot, msg.author.id, it->second, lock);
                    handled = true;
                } else if (content == "s" || content == "stand") {
                    handle_stand(bot, msg.author.id, it->second, lock);
                    handled = true;
                } else if (content == "d" || content == "double") {
                    handle_double(bot, msg.author.id, it->second, lock);
                    handled = true;
                }

                if (handled) {
                    bot.delete_message(msg.channel_id, msg.id);
                }
            }
        });

        // Gateway on_message_react hooks to allow reaction buttons
        bot.on_message_react([this, &bot](const stoatpp::events::MessageReact& e) {
            if (e.user_id == bot.current_user().id) return;

            std::unique_lock<std::mutex> lock(bj_mutex_);
            auto it = active_games_.find(e.user_id);
            if (it != active_games_.end() && it->second.message_id == e.id) {
                bool handled = false;
                if (e.emoji_id == "🃏" || e.emoji_id == "➕") {
                    handle_hit(bot, e.user_id, it->second, lock);
                    handled = true;
                } else if (e.emoji_id == "🛑" || e.emoji_id == "✋") {
                    handle_stand(bot, e.user_id, it->second, lock);
                    handled = true;
                } else if (e.emoji_id == "💵" || e.emoji_id == "2️⃣") {
                    handle_double(bot, e.user_id, it->second, lock);
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
