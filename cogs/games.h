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
#include <unordered_set>
#include <sstream>
#include <future>
#include <thread>
#include <cctype>
#include <atomic>

namespace bronx::cogs {

class GamesModule : public stoatpp::bot_module {
public:
    static GamesModule* instance;

    struct DuelChallenge {
        std::string game_type; // "coinflip" or "russianroulette"
        std::string challenger_id;
        std::string target_id;
        std::string channel_id;
        std::string message_id;
        int64_t bet = 0;
        bool accepted = false;
        std::chrono::system_clock::time_point expires_at;
    };

    struct RouletteBet {
        std::string user_id;
        int64_t bet_amount;
        std::string space;
    };

    struct RouletteGame {
        std::string channel_id;
        std::string message_id;
        std::vector<RouletteBet> bets;
        std::chrono::system_clock::time_point spin_time;
        bool active = false;
    };

    struct TeaGame {
        std::string type; // "blacktea" or "whitetea"
        std::string channel_id;
        std::string message_id;
        std::string host_id;
        std::vector<std::string> players;
        std::unordered_map<std::string, int> lives;
        std::unordered_set<std::string> used_words;
        std::unordered_map<std::string, std::string> patterns; // player -> pattern
        std::unordered_map<std::string, bool> responded;
        std::string global_pattern;
        bool active = false;
        bool round_active = false;
        int round = 0;
        bool cancelled = false;
    };

    struct WordEntry {
        std::string word;
        std::string category;
    };

private:
    std::unordered_map<std::string, DuelChallenge> active_challenges_; // message_id -> challenge
    std::mutex duel_mutex_;

    std::unordered_map<std::string, RouletteGame> active_roulettes_; // channel_id -> game
    std::mutex roulette_mutex_;

    std::unordered_map<std::string, TeaGame> active_tea_games_; // channel_id -> game
    std::mutex tea_mutex_;

    struct ConwayGame {
        std::string channel_id;
        std::string message_id;
        int width = 5;
        int height = 5;
        std::vector<std::vector<bool>> grid;
        bool playing = false;
        int generation = 0;
    };

    std::unordered_map<std::string, ConwayGame> active_conway_games_; // message_id -> game
    std::mutex conway_mutex_;
    std::thread conway_thread_;
    std::atomic<bool> conway_running_{true};

    std::vector<WordEntry> word_salad_list_ = {
        {"programming", "technology"}, {"developer", "technology"}, {"computer", "technology"},
        {"elephant", "animals"}, {"kangaroo", "animals"}, {"crocodile", "animals"},
        {"spaghetti", "food"}, {"pineapple", "food"}, {"hamburger", "food"},
        {"astronaut", "occupations"}, {"firefighter", "occupations"}, {"engineer", "occupations"},
        {"adventure", "common words"}, {"beautiful", "common words"}, {"yesterday", "common words"},
        {"keyboard", "technology"}, {"butterfly", "animals"}, {"chocolate", "food"}
    };

    std::vector<std::string> tea_patterns_ = {
        "pre", "pro", "con", "com", "dis", "mis", "non", "out", "sub", "tra", "tri",
        "ing", "ion", "ness", "ment", "ful", "less", "able", "ible", "ous",
        "ive", "ate", "ize", "ent", "ant", "ble", "ure", "ity", "age", "dom",
        "and", "ard", "art", "ast", "eat", "est", "ight", "old", "ort",
        "ain", "ear", "ean", "eal", "ead", "oar", "oor", "air", "ait", "all",
        "ang", "ank", "ant", "ape", "are", "ark", "arm", "arn", "ash", "ask",
        "bar", "bat", "ber", "bet", "bir", "ble", "ock", "ond", "ong", "ook",
        "ool", "oom", "oon", "oot", "ope", "ord", "ore", "ork", "orm", "orn",
        "ound", "our", "ove", "own", "uck", "ude", "uff", "ump", "und", "ung",
        "unk", "unt", "urn", "ust", "ace", "ack", "act", "ade", "afe", "age",
        "ake", "ale", "ame", "ane", "ave", "aze", "ice", "ick", "ide", "ife",
        "ike", "ile", "ime", "ine", "ipe", "ire", "ise", "ite", "ive", "ize"
    };

    std::string resolve_user_id(const std::string& arg) {
        if (arg.size() > 3 && arg.front() == '<' && arg[1] == '@' && arg.back() == '>') {
            return arg.substr(2, arg.size() - 3);
        }
        return arg;
    }

    std::string sanitize_word(const std::string& s) {
        std::string out;
        for (char c : s) {
            if (std::isalpha((unsigned char)c)) {
                out.push_back(std::tolower((unsigned char)c));
            }
        }
        return out;
    }

    std::string get_roulette_color(int number) {
        if (number == 0) return "green";
        static const std::unordered_set<int> red_numbers = {
            1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36
        };
        if (red_numbers.count(number)) return "red";
        return "black";
    }

public:
    GamesModule() {
        instance = this;
    }

    ~GamesModule() {
        conway_running_ = false;
        if (conway_thread_.joinable()) {
            conway_thread_.join();
        }
        if (instance == this) {
            instance = nullptr;
        }
    }

    void create_challenge(stoatpp::cluster& bot, const std::string& game_type, const std::string& challenger_id, const std::string& target_id, const std::string& channel_id, int64_t bet) {
        if (challenger_id == target_id) {
            bot.send_message(channel_id, "you cannot challenge yourself.");
            return;
        }

        if (bet > 0 && bronx::g_db) {
            auto self = bronx::g_db->get_economy_user(challenger_id);
            if (!self || self->wallet < bet) {
                bot.send_message(channel_id, "you do not have enough coins in your wallet.");
                return;
            }
            
            auto target = bronx::g_db->get_economy_user(target_id);
            if (!target || target->wallet < bet) {
                bot.send_message(channel_id, "the challenged user does not have enough coins in their wallet.");
                return;
            }
        }

        std::string bet_str = (bet > 0) ? "**" + std::to_string(bet) + "** coins" : "fun (no bet)";
        std::string invite_msg = "<@" + target_id + ">, you have been challenged to a **" + game_type + "** duel by <@" + challenger_id + "> for " + bet_str + "!\nReact with ✅ to accept or ❌ to decline.";
        
        bot.send_message(channel_id, invite_msg, [this, game_type, challenger_id, target_id, channel_id, bet, &bot](stoatpp::models::Message res, bool success) {
            if (success) {
                DuelChallenge challenge;
                challenge.game_type = game_type;
                challenge.challenger_id = challenger_id;
                challenge.target_id = target_id;
                challenge.channel_id = channel_id;
                challenge.message_id = res.id;
                challenge.bet = bet;
                challenge.accepted = false;
                challenge.expires_at = std::chrono::system_clock::now() + std::chrono::seconds(60);
                
                {
                    std::lock_guard<std::mutex> lock(duel_mutex_);
                    active_challenges_[res.id] = challenge;
                }
                
                bot.react_to_message(channel_id, res.id, "✅");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                bot.react_to_message(channel_id, res.id, "❌");
                
                std::thread([this, &bot, channel_id, msg_id = res.id]() {
                    std::this_thread::sleep_for(std::chrono::seconds(60));
                    std::lock_guard<std::mutex> lock(duel_mutex_);
                    auto it = active_challenges_.find(msg_id);
                    if (it != active_challenges_.end() && !it->second.accepted) {
                        bot.send_message(channel_id, "the challenge from <@" + it->second.challenger_id + "> timed out.");
                        active_challenges_.erase(it);
                    }
                }).detach();
            }
        });
    }

    void start_duel_game(stoatpp::cluster& bot, const DuelChallenge& challenge) {
        if (challenge.bet > 0 && bronx::g_db) {
            auto self = bronx::g_db->get_economy_user(challenge.challenger_id);
            auto target = bronx::g_db->get_economy_user(challenge.target_id);
            if (!self || self->wallet < challenge.bet || !target || target->wallet < challenge.bet) {
                bot.send_message(challenge.channel_id, "one of the players no longer has enough coins. duel cancelled.");
                return;
            }
            
            bronx::g_db->update_balances(challenge.challenger_id, -challenge.bet, 0);
            bronx::g_db->update_balances(challenge.target_id, -challenge.bet, 0);
        }
        
        if (challenge.game_type == "coinflip") {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 1);
            bool challenger_wins = (dis(gen) == 0);
            
            std::string winner_id = challenger_wins ? challenge.challenger_id : challenge.target_id;
            std::string loser_id = challenger_wins ? challenge.target_id : challenge.challenger_id;
            
            stoatpp::models::MessagePayload payload;
            payload.content = " ";
            stoatpp::models::Embed embed;
            embed.set_colour(0x2ECC71);
            embed.set_title("bronx economy — coinflip duel");
            
            std::string desc = "the coin landed and... <@" + winner_id + "> won the duel against <@" + loser_id + ">!\n\n";
            if (challenge.bet > 0 && bronx::g_db) {
                int64_t winnings = challenge.bet * 2;
                bronx::g_db->update_balances(winner_id, winnings, 0);
                desc += "<@" + winner_id + "> won **" + std::to_string(winnings) + "** coins!";
            } else {
                desc += "it was a clean victory for <@" + winner_id + ">!";
            }
            embed.set_description(desc);
            payload.add_embed(embed);
            bot.send_message(challenge.channel_id, payload);
        } else if (challenge.game_type == "russianroulette") {
            std::thread([this, &bot, challenge]() {
                run_russian_roulette_duel(bot, challenge);
            }).detach();
        }
    }

    void run_russian_roulette_duel(stoatpp::cluster& bot, const DuelChallenge& challenge) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 5);
        int bullet_slot = dis(gen);
        int current_chamber = 0;
        
        std::string p1 = challenge.challenger_id;
        std::string p2 = challenge.target_id;
        
        bool p1_spun = false;
        bool p2_spun = false;
        
        bot.send_message(challenge.channel_id, "🔫 The Russian Roulette duel begins! <@" + p1 + "> goes first.");
        
        bool game_over = false;
        std::string active_player = p1;
        std::string waiting_player = p2;
        
        while (!game_over) {
            std::string spin_info = "";
            if (active_player == p1 && !p1_spun) spin_info = " (or type `spin` to randomize the cylinder)";
            if (active_player == p2 && !p2_spun) spin_info = " (or type `spin` to randomize the cylinder)";
            
            bot.send_message(challenge.channel_id, "💀 <@" + active_player + ">, it is your turn. Type `shoot`" + spin_info + ".");
            
            auto promise = std::make_shared<std::promise<std::optional<stoatpp::events::Message>>>();
            bot.await_message(challenge.channel_id, active_player, std::chrono::seconds(30), [promise](std::optional<stoatpp::events::Message> res) {
                promise->set_value(res);
            });
            auto response = promise->get_future().get();
            
            if (!response) {
                bot.send_message(challenge.channel_id, "⏱️ <@" + active_player + "> took too long to make a move and died of fear!");
                
                std::string winner = waiting_player;
                if (challenge.bet > 0 && bronx::g_db) {
                    int64_t winnings = challenge.bet * 2;
                    bronx::g_db->update_balances(winner, winnings, 0);
                    bot.send_message(challenge.channel_id, "🏆 <@" + winner + "> wins the duel and **" + std::to_string(winnings) + "** coins!");
                } else {
                    bot.send_message(challenge.channel_id, "🏆 <@" + winner + "> wins the duel!");
                }
                game_over = true;
                break;
            }
            
            std::string content = response->content;
            std::transform(content.begin(), content.end(), content.begin(), ::tolower);
            
            if (content == "spin") {
                if ((active_player == p1 && p1_spun) || (active_player == p2 && p2_spun)) {
                    bot.send_message(challenge.channel_id, "❌ You have already spun the cylinder once! You must shoot.");
                    continue;
                }
                
                if (active_player == p1) p1_spun = true;
                else p2_spun = true;
                
                bullet_slot = dis(gen);
                current_chamber = 0;
                bot.send_message(challenge.channel_id, "🌀 You spin the cylinder... it lands on a random chamber.");
                content = "shoot";
            }
            
            if (content == "shoot" || content == "shoot self" || content == "shoot_self") {
                if (current_chamber == bullet_slot) {
                    bot.send_message(challenge.channel_id, "💥 **BANG!** <@" + active_player + "> shot themselves and died!");
                    std::string winner = waiting_player;
                    if (challenge.bet > 0 && bronx::g_db) {
                        int64_t winnings = challenge.bet * 2;
                        bronx::g_db->update_balances(winner, winnings, 0);
                        bot.send_message(challenge.channel_id, "🏆 <@" + winner + "> wins the duel and **" + std::to_string(winnings) + "** coins!");
                    } else {
                        bot.send_message(challenge.channel_id, "🏆 <@" + winner + "> wins the duel!");
                    }
                    game_over = true;
                } else {
                    bot.send_message(challenge.channel_id, "🔫 *click.* <@" + active_player + "> survived!");
                    current_chamber = (current_chamber + 1) % 6;
                    std::swap(active_player, waiting_player);
                }
            } else {
                bot.send_message(challenge.channel_id, "❌ Invalid input. Please type `shoot` or `spin`.");
            }
        }
    }

    void run_russian_roulette_solo(stoatpp::cluster& bot, const stoatpp::events::Message& msg, int64_t bet) {
        if (bet > 0 && bronx::g_db) {
            auto self = bronx::g_db->get_economy_user(msg.author.id);
            if (!self || self->wallet < bet) {
                bot.send_message(msg.channel_id, "you do not have enough coins in your wallet.");
                return;
            }
            bronx::g_db->update_balances(msg.author.id, -bet, 0);
        }
        
        std::thread([this, &bot, msg, bet]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 5);
            int bullet_slot = dis(gen);
            int current_chamber = 0;
            
            bool spun = false;
            bool game_over = false;
            
            bot.send_message(msg.channel_id, "🔫 Solo Russian Roulette begins against the dealer! You go first.");
            
            std::string active_player = "user"; 
            
            while (!game_over) {
                if (active_player == "user") {
                    std::string spin_info = !spun ? " (or type `spin` to randomize)" : "";
                    bot.send_message(msg.channel_id, "💀 <@" + msg.author.id + ">, it is your turn. Type `shoot`" + spin_info + ".");
                    
                    auto promise = std::make_shared<std::promise<std::optional<stoatpp::events::Message>>>();
                    bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30), [promise](std::optional<stoatpp::events::Message> res) {
                        promise->set_value(res);
                    });
                    auto response = promise->get_future().get();
                    
                    if (!response) {
                        bot.send_message(msg.channel_id, "⏱️ You took too long to pull the trigger and died of fear! You lost.");
                        game_over = true;
                        break;
                    }
                    
                    std::string content = response->content;
                    std::transform(content.begin(), content.end(), content.begin(), ::tolower);
                    
                    if (content == "spin") {
                        if (spun) {
                            bot.send_message(msg.channel_id, "❌ You have already spun once! You must shoot.");
                            continue;
                        }
                        spun = true;
                        bullet_slot = dis(gen);
                        current_chamber = 0;
                        bot.send_message(msg.channel_id, "🌀 You spin the cylinder...");
                        content = "shoot";
                    }
                    
                    if (content == "shoot" || content == "shoot self" || content == "shoot_self") {
                        if (current_chamber == bullet_slot) {
                            bot.send_message(msg.channel_id, "💥 **BANG!** You shot yourself! Game over.");
                            game_over = true;
                        } else {
                            bot.send_message(msg.channel_id, "🔫 *click.* You survived! Now it is the dealer's turn.");
                            current_chamber = (current_chamber + 1) % 6;
                            active_player = "bot";
                        }
                    } else {
                        bot.send_message(msg.channel_id, "❌ Invalid input. Please type `shoot` or `spin`.");
                    }
                } else {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    bot.send_message(msg.channel_id, "🤖 The dealer puts the gun to their head...");
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    
                    if (current_chamber == bullet_slot) {
                        bot.send_message(msg.channel_id, "💥 **BANG!** The dealer shot themselves!");
                        if (bet > 0 && bronx::g_db) {
                            int64_t winnings = bet * 2;
                            bronx::g_db->update_balances(msg.author.id, winnings, 0);
                            bot.send_message(msg.channel_id, "🏆 You win and receive **" + std::to_string(winnings) + "** coins!");
                        } else {
                            bot.send_message(msg.channel_id, "🏆 You win!");
                        }
                        game_over = true;
                    } else {
                        bot.send_message(msg.channel_id, "🔫 *click.* The dealer survived! Pass the gun back to you.");
                        current_chamber = (current_chamber + 1) % 6;
                        active_player = "user";
                    }
                }
            }
        }).detach();
    }

    void register_handlers(stoatpp::cluster& bot) override {
        conway_thread_ = std::thread([this, &bot]() {
            run_conway_loop(bot);
        });

        // reaction duel listener
        bot.on_message_react([this, &bot](const stoatpp::events::MessageReact& e) {
            if (e.user_id == bot.current_user().id) return;
            
            DuelChallenge challenge;
            bool found = false;
            bool accepted = false;
            {
                std::lock_guard<std::mutex> lock(duel_mutex_);
                auto it = active_challenges_.find(e.id);
                if (it != active_challenges_.end() && e.user_id == it->second.target_id) {
                    if (e.emoji_id == "✅") {
                        it->second.accepted = true;
                        challenge = it->second;
                        active_challenges_.erase(it);
                        found = true;
                        accepted = true;
                    } else if (e.emoji_id == "❌") {
                        challenge = it->second;
                        active_challenges_.erase(it);
                        found = true;
                        accepted = false;
                    }
                }
            }
            
            if (found) {
                if (accepted) {
                    bot.send_message(challenge.channel_id, "✅ <@" + challenge.target_id + "> accepted the challenge!");
                    start_duel_game(bot, challenge);
                } else {
                    bot.send_message(challenge.channel_id, "❌ <@" + challenge.target_id + "> declined the challenge.");
                }
            }

            // lobby join listener for BlackTea/WhiteTea
            {
                std::lock_guard<std::mutex> lock(tea_mutex_);
                for (auto& kv : active_tea_games_) {
                    auto& game = kv.second;
                    if (!game.active && game.message_id == e.id && game.channel_id == e.channel_id) {
                        if (e.emoji_id == "✅") {
                            if (std::find(game.players.begin(), game.players.end(), e.user_id) == game.players.end()) {
                                game.players.push_back(e.user_id);
                                bot.send_message(game.channel_id, "<@" + e.user_id + "> joined the tea party! (" + std::to_string(game.players.size()) + " players)");
                            }
                        }
                    }
                }
            }
        });

        // host tea command listener in channel
        bot.on_message([this, &bot](const stoatpp::events::Message& msg) {
            if (msg.author.bot) return;

            // 1. Process host lobby actions (cancel)
            {
                std::lock_guard<std::mutex> lock(tea_mutex_);
                auto it = active_tea_games_.find(msg.channel_id);
                if (it != active_tea_games_.end() && !it->second.active) {
                    if (msg.author.id == it->second.host_id) {
                        std::string content = msg.content;
                        std::transform(content.begin(), content.end(), content.begin(), ::tolower);
                        if (content == ".cancel") {
                            it->second.cancelled = true;
                            bot.send_message(msg.channel_id, "❌ The tea game lobby was cancelled by the host.");
                            active_tea_games_.erase(it);
                            return;
                        }
                    }
                }
            }

            // 2. Process active tea game submissions
            {
                std::lock_guard<std::mutex> lock(tea_mutex_);
                auto it = active_tea_games_.find(msg.channel_id);
                if (it != active_tea_games_.end() && it->second.active && it->second.round_active) {
                    auto& game = it->second;
                    std::string uid = msg.author.id;
                    if (std::find(game.players.begin(), game.players.end(), uid) != game.players.end() && game.lives[uid] > 0) {
                        if (!game.responded[uid]) {
                            std::string cleaned = sanitize_word(msg.content);
                            if (!cleaned.empty()) {
                                std::string pattern = (game.type == "blacktea") ? game.global_pattern : game.patterns[uid];
                                if (cleaned.find(pattern) != std::string::npos) {
                                    if (game.used_words.count(cleaned)) {
                                        bot.send_reply(msg, "❌ That word was already used in this game!");
                                    } else {
                                        game.used_words.insert(cleaned);
                                        game.responded[uid] = true;
                                        bot.react_to_message(msg.channel_id, msg.id, "✅");
                                    }
                                }
                            }
                        }
                    }
                }
            }
        });

        // Conway react listener
        bot.on_message_react([this, &bot](const stoatpp::events::MessageReact& e) {
            if (e.user_id == bot.current_user().id) return;
            handle_conway_interaction(bot, e.id, e.emoji_id);
        });

        // Conway unreact listener
        bot.on_message_unreact([this, &bot](const stoatpp::events::MessageUnreact& e) {
            if (e.user_id == bot.current_user().id) return;
            handle_conway_interaction(bot, e.id, e.emoji_id);
        });

        // roulette command
        bot.register_command({
            "roulette",
            {"rlt", "spinwheel"},
            "bets on a color (red/black/green) or a specific number (0-36).",
            "roulette <bet/fun> <space>",
            {"<bet>", "<space>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (args.empty()) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `roulette <amount/fun> <space>` (e.g. `roulette 100 red` or `roulette black`).");
                    return;
                }

                int64_t bet = 0;
                std::string space = "";

                if (args.size() == 1) {
                    space = args[0];
                } else {
                    // Try to parse bet
                    if (args[0] == "fun" || args[0] == "0") {
                        bet = 0;
                        space = args[1];
                    } else if (args[0] == "all" || args[0] == "max" || args[0] == "half") {
                        if (bronx::g_db) {
                            auto self = bronx::g_db->get_economy_user(msg.author.id);
                            if (self) {
                                if (args[0] == "half") bet = self->wallet / 2;
                                else bet = self->wallet;
                            }
                        }
                        space = args[1];
                    } else {
                        try {
                            bet = std::stoll(args[0]);
                            space = args[1];
                        } catch (...) {
                            // Fallback if they put space first e.g. roulette red 100
                            space = args[0];
                            try {
                                if (args[1] == "fun") bet = 0;
                                else bet = std::stoll(args[1]);
                            } catch (...) {
                                bet = 0;
                            }
                        }
                    }
                }

                std::transform(space.begin(), space.end(), space.begin(), ::tolower);
                if (space != "red" && space != "black" && space != "green") {
                    try {
                        int num = std::stoi(space);
                        if (num < 0 || num > 36) {
                            bot.send_message(msg.channel_id, "please bet on red, black, green, or a number between 0 and 36.");
                            return;
                        }
                    } catch (...) {
                        bot.send_message(msg.channel_id, "please bet on red, black, green, or a number between 0 and 36.");
                        return;
                    }
                }

                if (bet > 0 && bronx::g_db) {
                    auto self = bronx::g_db->get_economy_user(msg.author.id);
                    if (!self || self->wallet < bet) {
                        bot.send_message(msg.channel_id, "you do not have enough coins in your wallet.");
                        return;
                    }
                    bronx::g_db->update_balances(msg.author.id, -bet, 0);
                }

                std::lock_guard<std::mutex> lock(roulette_mutex_);
                auto it = active_roulettes_.find(msg.channel_id);
                if (it == active_roulettes_.end()) {
                    RouletteGame game;
                    game.channel_id = msg.channel_id;
                    game.active = true;
                    game.spin_time = std::chrono::system_clock::now() + std::chrono::seconds(30);
                    
                    RouletteBet rb;
                    rb.user_id = msg.author.id;
                    rb.bet_amount = bet;
                    rb.space = space;
                    game.bets.push_back(rb);

                    std::string bet_desc = (bet > 0) ? "**" + std::to_string(bet) + "** coins" : "fun (no bet)";
                    std::string initial_msg = "🎰 **Roulette Lobby Started!** The wheel spins in 30 seconds.\n"
                                              "Place your bets using `.roulette <bet> <space>`.\n\n"
                                              "**Current Bets:**\n- <@" + msg.author.id + ">: bet " + bet_desc + " on **" + space + "**";
                    
                    bot.send_message(msg.channel_id, initial_msg, [this, &bot, channel_id = msg.channel_id](stoatpp::models::Message res, bool success) {
                        if (success) {
                            std::lock_guard<std::mutex> lock(roulette_mutex_);
                            active_roulettes_[channel_id].message_id = res.id;
                        }
                    });

                    active_roulettes_[msg.channel_id] = game;

                    std::thread([this, &bot, channel_id = msg.channel_id]() {
                        std::this_thread::sleep_for(std::chrono::seconds(30));
                        run_roulette_spin(bot, channel_id);
                    }).detach();
                } else {
                    RouletteBet rb;
                    rb.user_id = msg.author.id;
                    rb.bet_amount = bet;
                    rb.space = space;
                    it->second.bets.push_back(rb);

                    std::string list_bets = "🎰 **Roulette Lobby Active!** The wheel spins shortly.\n"
                                            "Place your bets using `.roulette <bet> <space>`.\n\n"
                                            "**Current Bets:**\n";
                    for (const auto& b : it->second.bets) {
                        std::string b_desc = (b.bet_amount > 0) ? "**" + std::to_string(b.bet_amount) + "** coins" : "fun (no bet)";
                        list_bets += "- <@" + b.user_id + ">: bet " + b_desc + " on **" + b.space + "**\n";
                    }
                    bot.edit_message(msg.channel_id, it->second.message_id, list_bets);
                }
            },
            "Games"
        });

        // russianroulette command
        bot.register_command({
            "russianroulette",
            {"rr", "rroulette"},
            "plays a thrilling game of russian roulette against a user or the dealer.",
            "russianroulette <amount/fun> [@user]",
            {"<amount>", "[user]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                int64_t bet = 0;
                std::string target_uid = "";

                if (!args.empty()) {
                    if (args[0] == "fun") {
                        bet = 0;
                        if (args.size() > 1) target_uid = resolve_user_id(args[1]);
                    } else if (args[0] == "all" || args[0] == "max" || args[0] == "half") {
                        if (bronx::g_db) {
                            auto self = bronx::g_db->get_economy_user(msg.author.id);
                            if (self) {
                                if (args[0] == "half") bet = self->wallet / 2;
                                else bet = self->wallet;
                            }
                        }
                        if (args.size() > 1) target_uid = resolve_user_id(args[1]);
                    } else {
                        try {
                            bet = std::stoll(args[0]);
                            if (args.size() > 1) target_uid = resolve_user_id(args[1]);
                        } catch (...) {
                            target_uid = resolve_user_id(args[0]);
                            if (args.size() > 1) {
                                try {
                                    if (args[1] == "fun") bet = 0;
                                    else bet = std::stoll(args[1]);
                                } catch (...) {
                                    bet = 0;
                                }
                            }
                        }
                    }
                }

                if (!target_uid.empty()) {
                    create_challenge(bot, "russianroulette", msg.author.id, target_uid, msg.channel_id, bet);
                } else {
                    run_russian_roulette_solo(bot, msg, bet);
                }
            },
            "Games"
        });

        // wordsalad command
        bot.register_command({
            "wordsalad",
            {"ws", "salad"},
            "unscrambles a scrambled word.",
            "wordsalad [amount]",
            {"[amount]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                int64_t bet = 0;
                if (!args.empty()) {
                    if (args[0] == "all" || args[0] == "max" || args[0] == "half") {
                        if (bronx::g_db) {
                            auto self = bronx::g_db->get_economy_user(msg.author.id);
                            if (self) {
                                if (args[0] == "half") bet = self->wallet / 2;
                                else bet = self->wallet;
                            }
                        }
                    } else {
                        try {
                            bet = std::stoll(args[0]);
                        } catch (...) {
                            bet = 0;
                        }
                    }
                }

                if (bet > 0 && bronx::g_db) {
                    auto self = bronx::g_db->get_economy_user(msg.author.id);
                    if (!self || self->wallet < bet) {
                        bot.send_message(msg.channel_id, "you do not have enough coins in your wallet.");
                        return;
                    }
                    bronx::g_db->update_balances(msg.author.id, -bet, 0);
                }

                std::thread([this, &bot, msg, bet]() {
                    std::random_device rd;
                    std::mt19937 gen(rd());
                    std::uniform_int_distribution<> dis(0, word_salad_list_.size() - 1);
                    WordEntry entry = word_salad_list_[dis(gen)];

                    std::string scrambled = entry.word;
                    while (scrambled == entry.word && entry.word.size() > 1) {
                        std::shuffle(scrambled.begin(), scrambled.end(), gen);
                    }

                    bot.send_message(msg.channel_id, "🥗 **Word Salad!** Unscramble this word: **" + scrambled + "** (Category: " + entry.category + "). You have 15 seconds!");

                    auto promise = std::make_shared<std::promise<std::optional<stoatpp::events::Message>>>();
                    bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(15), [promise](std::optional<stoatpp::events::Message> res) {
                        promise->set_value(res);
                    });
                    auto response = promise->get_future().get();

                    if (response && sanitize_word(response->content) == entry.word) {
                        if (bet > 0 && bronx::g_db) {
                            int64_t winnings = bet * 2;
                            bronx::g_db->update_balances(msg.author.id, winnings, 0);
                            bot.send_message(msg.channel_id, "🎉 Correct! You unscrambled the word and won **" + std::to_string(winnings) + "** coins!");
                        } else {
                            std::uniform_int_distribution<> rew(50, 150);
                            int64_t random_reward = rew(gen);
                            if (bronx::g_db) {
                                bronx::g_db->update_balances(msg.author.id, random_reward, 0);
                            }
                            bot.send_message(msg.channel_id, "🎉 Correct! You unscrambled the word and got **" + std::to_string(random_reward) + "** coins for fun!");
                        }
                    } else {
                        bot.send_message(msg.channel_id, "❌ Time's up or incorrect! The word was **" + entry.word + "**.");
                    }
                }).detach();
            },
            "Games"
        });

        // blacktea command
        bot.register_command({
            "blacktea",
            {"bt", "tea"},
            "starts a black tea lobby.",
            "blacktea",
            {},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                create_tea_lobby(bot, msg, "blacktea");
            },
            "Games"
        });

        // whitetea command
        bot.register_command({
            "whitetea",
            {"wt"},
            "starts a white tea lobby.",
            "whitetea",
            {},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                create_tea_lobby(bot, msg, "whitetea");
            },
            "Games"
        });

        // conway command
        bot.register_command({
            "conway",
            {"life", "gameoflife"},
            "starts a conway game of life simulation.",
            "conway [widthxheight]",
            {"[size]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                int width = 5;
                int height = 5;
                if (!args.empty()) {
                    std::string arg = args[0];
                    size_t x_pos = arg.find_first_of("xX");
                    if (x_pos != std::string::npos) {
                        try {
                            width = std::stoi(arg.substr(0, x_pos));
                            height = std::stoi(arg.substr(x_pos + 1));
                        } catch (...) {
                            width = 5;
                            height = 5;
                        }
                    } else {
                        try {
                            width = std::stoi(arg);
                            height = width;
                        } catch (...) {
                            width = 5;
                            height = 5;
                        }
                    }
                }
                
                if (width < 3) width = 3;
                if (width > 15) width = 15;
                if (height < 3) height = 3;
                if (height > 15) height = 15;
                
                ConwayGame game;
                game.channel_id = msg.channel_id;
                game.width = width;
                game.height = height;
                game.grid.assign(height, std::vector<bool>(width, false));
                game.playing = false;
                game.generation = 0;
                
                randomize_conway_game(game);
                
                stoatpp::models::MessagePayload payload;
                payload.content = " ";
                stoatpp::models::Embed embed;
                embed.set_colour(0xF1C40F);
                embed.set_title("🦠 Conway's Game of Life (" + std::to_string(width) + "x" + std::to_string(height) + ")");
                
                std::string grid_str = render_conway_grid(game);
                embed.set_description(grid_str + "\n\n**Generation:** 0\n**Status:** Paused ⏸️");
                payload.add_embed(embed);
                
                std::vector<std::string> reactions = {"⏯️", "⏭️", "🔄", "⏹️"};
                payload.set_interactions(reactions, false);
                
                bot.send_message(msg.channel_id, payload, [this, game](stoatpp::models::Message res, bool success) mutable {
                    if (success) {
                        game.message_id = res.id;
                        std::lock_guard<std::mutex> lock(conway_mutex_);
                        active_conway_games_[res.id] = game;
                    }
                });
            },
            "Games"
        });
    }

    void run_roulette_spin(stoatpp::cluster& bot, const std::string& channel_id) {
        RouletteGame game;
        {
            std::lock_guard<std::mutex> lock(roulette_mutex_);
            auto it = active_roulettes_.find(channel_id);
            if (it == active_roulettes_.end()) return;
            game = it->second;
            active_roulettes_.erase(it);
        }

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 36);
        int rolled_number = dis(gen);
        std::string rolled_color = get_roulette_color(rolled_number);

        stoatpp::models::MessagePayload payload;
        payload.content = " ";
        stoatpp::models::Embed embed;
        embed.set_colour((rolled_color == "red") ? 0xE74C3C : (rolled_color == "green" ? 0x2ECC71 : 0x2C3E50));
        embed.set_title("🎰 Roulette Spin Result");

        std::string number_emoji = (rolled_color == "red") ? "🔴" : (rolled_color == "green" ? "🟢" : "⚫");
        std::string roll_desc = "The wheel spun and landed on: " + number_emoji + " **" + std::to_string(rolled_number) + " " + rolled_color + "**\n\n";

        roll_desc += "**Results:**\n";
        for (const auto& b : game.bets) {
            bool won = false;
            int multiplier = 2;
            
            if (b.space == "red" && rolled_color == "red") won = true;
            else if (b.space == "black" && rolled_color == "black") won = true;
            else if (b.space == "green" && rolled_color == "green") {
                won = true;
                multiplier = 35;
            } else {
                try {
                    int space_num = std::stoi(b.space);
                    if (space_num == rolled_number) {
                        won = true;
                        multiplier = 35;
                    }
                } catch (...) {}
            }

            std::string player_mention = "<@" + b.user_id + ">";
            if (won) {
                if (b.bet_amount > 0 && bronx::g_db) {
                    int64_t winnings = b.bet_amount * multiplier;
                    bronx::g_db->update_balances(b.user_id, winnings, 0);
                    roll_desc += "🎉 " + player_mention + " won **" + std::to_string(winnings) + "** coins!\n";
                } else {
                    roll_desc += "🎉 " + player_mention + " won (for fun)!\n";
                }
            } else {
                std::string loss_desc = (b.bet_amount > 0) ? "**" + std::to_string(b.bet_amount) + "** coins" : "for fun";
                roll_desc += "❌ " + player_mention + " lost " + loss_desc + ".\n";
            }
        }

        embed.set_description(roll_desc);
        payload.add_embed(embed);
        bot.send_message(channel_id, payload);
    }

    void create_tea_lobby(stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::string& type) {
        std::lock_guard<std::mutex> lock(tea_mutex_);
        if (active_tea_games_.count(msg.channel_id) > 0) {
            bot.send_message(msg.channel_id, "there is already an active tea game in this channel.");
            return;
        }

        TeaGame game;
        game.type = type;
        game.channel_id = msg.channel_id;
        game.host_id = msg.author.id;
        game.players.push_back(msg.author.id);
        game.active = false;

        std::string title = (type == "blacktea") ? "☕ blacktea - lobby" : "🍵 whitetea - lobby";
        std::string desc = "Waiting for players! React with ✅ to join this tea party.\n"
                           "Need at least 3 players. Lobby closes in 30 seconds.\n"
                           "Host can type `.cancel` in chat to cancel.";

        bot.send_message(msg.channel_id, desc, [this, &bot, channel_id = msg.channel_id, type](stoatpp::models::Message res, bool success) {
            if (success) {
                {
                    std::lock_guard<std::mutex> lock(tea_mutex_);
                    active_tea_games_[channel_id].message_id = res.id;
                }
                bot.react_to_message(channel_id, res.id, "✅");

                std::thread([this, &bot, channel_id]() {
                    std::this_thread::sleep_for(std::chrono::seconds(30));
                    start_tea_game(bot, channel_id);
                }).detach();
            }
        });

        active_tea_games_[msg.channel_id] = game;
    }

    void start_tea_game(stoatpp::cluster& bot, const std::string& channel_id) {
        TeaGame game;
        {
            std::lock_guard<std::mutex> lock(tea_mutex_);
            auto it = active_tea_games_.find(channel_id);
            if (it == active_tea_games_.end()) return;
            if (it->second.cancelled) return;
            
            if (it->second.players.size() < 3) {
                bot.send_message(channel_id, "❌ Not enough players joined (minimum 3 required). Lobby cancelled.");
                active_tea_games_.erase(it);
                return;
            }

            it->second.active = true;
            for (const auto& p : it->second.players) {
                it->second.lives[p] = 2;
            }
            game = it->second;
        }

        std::string tea_emoji = (game.type == "blacktea") ? "☕" : "🍵";
        bot.send_message(channel_id, tea_emoji + " The **" + game.type + "** party has started! Every player has **2 lives**.");

        std::random_device rd;
        std::mt19937 gen(rd());

        while (true) {
            std::vector<std::string> alive_players;
            {
                std::lock_guard<std::mutex> lock(tea_mutex_);
                auto it = active_tea_games_.find(channel_id);
                if (it == active_tea_games_.end() || it->second.cancelled) break;
                for (const auto& p : it->second.players) {
                    if (it->second.lives[p] > 0) alive_players.push_back(p);
                }
                if (alive_players.size() <= 1) break;

                // Setup round
                it->second.round++;
                it->second.round_active = true;
                it->second.responded.clear();
                for (const auto& p : alive_players) {
                    it->second.responded[p] = false;
                }

                if (game.type == "blacktea") {
                    std::uniform_int_distribution<> dis(0, tea_patterns_.size() - 1);
                    it->second.global_pattern = tea_patterns_[dis(gen)];
                } else {
                    // White tea: assign unique patterns
                    std::vector<std::string> pool = tea_patterns_;
                    std::shuffle(pool.begin(), pool.end(), gen);
                    for (size_t i = 0; i < alive_players.size(); ++i) {
                        it->second.patterns[alive_players[i]] = pool[i % pool.size()];
                    }
                }
                game = it->second;
            }

            std::string round_ann = tea_emoji + " **Round " + std::to_string(game.round) + "** begins!\n";
            if (game.type == "blacktea") {
                round_ann += "Send a word containing: **" + game.global_pattern + "**\n";
            } else {
                round_ann += "Each player has their own pattern:\n";
                for (const auto& p : alive_players) {
                    round_ann += "- <@" + p + ">: **" + game.patterns[p] + "**\n";
                }
            }
            round_ann += "\nYou have **10 seconds** to respond!";
            bot.send_message(channel_id, round_ann);

            std::this_thread::sleep_for(std::chrono::seconds(10));

            // Evaluate round results
            std::vector<std::string> eliminated;
            std::string round_summary = "🔔 **Round " + std::to_string(game.round) + " Results:**\n";

            {
                std::lock_guard<std::mutex> lock(tea_mutex_);
                auto it = active_tea_games_.find(channel_id);
                if (it == active_tea_games_.end()) break;
                
                it->second.round_active = false;
                for (const auto& p : alive_players) {
                    if (!it->second.responded[p]) {
                        it->second.lives[p]--;
                        if (it->second.lives[p] <= 0) {
                            eliminated.push_back(p);
                        }
                    }
                    round_summary += "- <@" + p + ">: " + std::to_string(it->second.lives[p]) + " ❤️";
                    if (!it->second.responded[p]) round_summary += " *(missed)*";
                    round_summary += "\n";
                }
                game = it->second;
            }

            if (!eliminated.empty()) {
                round_summary += "\n💀 **Eliminated:** ";
                for (const auto& p : eliminated) round_summary += "<@" + p + "> ";
            }
            bot.send_message(channel_id, round_summary);

            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        }

        // Announce winner
        std::vector<std::string> final_winners;
        {
            std::lock_guard<std::mutex> lock(tea_mutex_);
            auto it = active_tea_games_.find(channel_id);
            if (it != active_tea_games_.end()) {
                for (const auto& p : it->second.players) {
                    if (it->second.lives[p] > 0) final_winners.push_back(p);
                }
                active_tea_games_.erase(it);
            }
        }

        if (final_winners.empty()) {
            bot.send_message(channel_id, "🍵 Game over! No one survived the tea party.");
        } else {
            std::string win_str = "🏆 Winner: ";
            for (const auto& p : final_winners) {
                win_str += "<@" + p + "> ";
                if (bronx::g_db) {
                    bronx::g_db->update_balances(p, 200, 0); // Reward 200 coins
                }
            }
            win_str += "\nCongratulations! You won 200 coins.";
            bot.send_message(channel_id, win_str);
        }
    }

private:
    void handle_conway_interaction(stoatpp::cluster& bot, const std::string& message_id, const std::string& emoji) {
        ConwayGame game;
        bool found = false;
        bool should_update = false;
        bool should_delete = false;
        
        {
            std::lock_guard<std::mutex> lock(conway_mutex_);
            auto it = active_conway_games_.find(message_id);
            if (it != active_conway_games_.end()) {
                found = true;
                if (emoji == "⏯️") {
                    it->second.playing = !it->second.playing;
                    should_update = true;
                } else if (emoji == "⏭️") {
                    step_conway_game(it->second);
                    should_update = true;
                } else if (emoji == "🔄") {
                    randomize_conway_game(it->second);
                    it->second.generation = 0;
                    should_update = true;
                } else if (emoji == "⏹️") {
                    should_delete = true;
                }
                game = it->second;
            }
        }
        
        if (found) {
            if (should_delete) {
                stoatpp::models::MessagePayload payload;
                payload.content = " ";
                stoatpp::models::Embed embed;
                embed.set_colour(0xFF5555);
                embed.set_title("🦠 Conway's Game of Life — Ended");
                
                std::string grid_str = render_conway_grid(game);
                embed.set_description(grid_str + "\n\n**Generation:** " + std::to_string(game.generation) + "\n**Status:** Stopped 🛑");
                payload.add_embed(embed);
                payload.set_interactions({}, false);
                
                bot.edit_message(game.channel_id, message_id, payload);
                
                std::lock_guard<std::mutex> lock(conway_mutex_);
                active_conway_games_.erase(message_id);
            } else if (should_update) {
                update_conway_message(bot, game);
            }
        }
    }

    void update_conway_message(stoatpp::cluster& bot, const ConwayGame& game) {
        stoatpp::models::MessagePayload payload;
        payload.content = " ";
        stoatpp::models::Embed embed;
        embed.set_colour(game.playing ? 0x2ECC71 : 0xF1C40F);
        embed.set_title("🦠 Conway's Game of Life (" + std::to_string(game.width) + "x" + std::to_string(game.height) + ")");
        
        std::string grid_str = render_conway_grid(game);
        std::string status_str = game.playing ? "Playing ⏯️" : "Paused ⏸️";
        embed.set_description(grid_str + "\n\n**Generation:** " + std::to_string(game.generation) + "\n**Status:** " + status_str);
        payload.add_embed(embed);
        
        std::vector<std::string> reactions = {"⏯️", "⏭️", "🔄", "⏹️"};
        payload.set_interactions(reactions, false);
        
        bot.edit_message(game.channel_id, game.message_id, payload, [this, msg_id = game.message_id](stoatpp::models::Message res, bool success) {
            if (!success) {
                std::lock_guard<std::mutex> lock(conway_mutex_);
                active_conway_games_.erase(msg_id);
            }
        });
    }

    void run_conway_loop(stoatpp::cluster& bot) {
        while (conway_running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            if (!conway_running_) break;
            
            std::vector<ConwayGame> games_to_update;
            {
                std::lock_guard<std::mutex> lock(conway_mutex_);
                for (auto& [msg_id, game] : active_conway_games_) {
                    if (game.playing) {
                        step_conway_game(game);
                        games_to_update.push_back(game);
                    }
                }
            }
            for (const auto& game : games_to_update) {
                update_conway_message(bot, game);
            }
        }
    }

    static void step_conway_game(ConwayGame& game) {
        std::vector<std::vector<bool>> next_grid = game.grid;
        for (int y = 0; y < game.height; ++y) {
            for (int x = 0; x < game.width; ++x) {
                int alive_neighbors = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        int ny = y + dy;
                        int nx = x + dx;
                        
                        if (ny < 0) ny = game.height - 1;
                        else if (ny >= game.height) ny = 0;
                        if (nx < 0) nx = game.width - 1;
                        else if (nx >= game.width) nx = 0;
                        
                        if (game.grid[ny][nx]) {
                            alive_neighbors++;
                        }
                    }
                }
                
                if (game.grid[y][x]) {
                    if (alive_neighbors < 2 || alive_neighbors > 3) {
                        next_grid[y][x] = false;
                    }
                } else {
                    if (alive_neighbors == 3) {
                        next_grid[y][x] = true;
                    }
                }
            }
        }
        game.grid = next_grid;
        game.generation++;
    }

    static void randomize_conway_game(ConwayGame& game) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 1);
        for (int y = 0; y < game.height; ++y) {
            for (int x = 0; x < game.width; ++x) {
                game.grid[y][x] = (dis(gen) == 1);
            }
        }
    }

    static std::string render_conway_grid(const ConwayGame& game) {
        std::string out = "";
        for (int y = 0; y < game.height; ++y) {
            for (int x = 0; x < game.width; ++x) {
                out += game.grid[y][x] ? "🟩" : "⬛";
            }
            out += "\n";
        }
        return out;
    }
};

inline GamesModule* GamesModule::instance = nullptr;

} // namespace bronx::cogs
