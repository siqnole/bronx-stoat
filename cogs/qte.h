#pragma once
// ============================================================
//  qte.h — Command-driven channel Quicktime Events (QTE)
// ============================================================

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <random>
#include <algorithm>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>
#include "stoatpp/bot_module.h"
#include "../database.h"
#include "helpers.h"

namespace bronx::cogs {

class QteModule : public stoatpp::bot_module {
private:
    inline static std::unordered_map<std::string, int> channel_cmd_counts_;
    inline static std::unordered_map<std::string, int> channel_qte_targets_;
    inline static std::unordered_map<std::string, int> channel_money_cmds_;
    inline static std::unordered_map<std::string, int> channel_fish_cmds_;
    inline static std::unordered_set<std::string> active_qtes_;
    inline static std::mutex qte_mutex_;

    static void give_qte_reward(stoatpp::cluster &bot, const std::string &channel_id, const std::string &user_id, const std::string &qte_type) {
        if (!bronx::g_db) return;

        std::random_device rd;
        std::mt19937 gen(rd());

        if (qte_type == "fishing") {
            std::uniform_int_distribution<> reward_dis(0, 2);
            int choice = reward_dis(gen);

            if (choice == 0) {
                // Give bait
                std::vector<std::string> bait_choices = {"worm", "crayfish", "minnow", "plasma_worm", "void_grub", "starlight_lure", "chronos_bait"};
                std::uniform_int_distribution<> bait_dis(0, bait_choices.size() - 1);
                std::string bait_id = bait_choices[bait_dis(gen)];
                std::uniform_int_distribution<> qty_dis(2, 5);
                int qty = qty_dis(gen);

                bronx::g_db->add_bait(user_id, bait_id, qty);
                bot.send_message(channel_id, "🎉 **Congratulations!** <@" + user_id + "> claimed the QTE and received **" + std::to_string(qty) + "x " + bait_id + "** baits! 🪱", [&bot, channel_id](auto prompt, bool ok) {
                    if (ok) {
                        std::thread([&bot, channel_id, prompt]() {
                            std::this_thread::sleep_for(std::chrono::seconds(7));
                            bot.delete_message(channel_id, prompt.id);
                        }).detach();
                    }
                });
            } else if (choice == 1) {
                // Give fish
                std::vector<std::string> fish_ids = {"common_carp", "rainbow_trout", "salmon", "golden_koi", "crimson_barracuda", "plasma_eel"};
                std::vector<std::string> fish_names = {"Common Carp", "Rainbow Trout", "Salmon", "Golden Koi", "Crimson Barracuda", "Plasma Eel"};
                std::vector<std::string> rarities = {"Common", "Uncommon", "Rare", "Epic", "Legendary", "Mythic"};
                std::uniform_int_distribution<> fish_dis(0, fish_ids.size() - 1);
                int f_idx = fish_dis(gen);

                std::uniform_real_distribution<double> weight_dis(5.0, 35.0);
                double weight = weight_dis(gen);
                int64_t val = (f_idx + 1) * 1500;

                bronx::g_db->add_fish_to_inventory(user_id, fish_ids[f_idx], fish_names[f_idx], rarities[f_idx], 0.05, weight, val);
                
                std::ostringstream ss_wt;
                ss_wt << std::fixed << std::setprecision(1) << weight;
                bot.send_message(channel_id, "🎉 **Congratulations!** <@" + user_id + "> claimed the QTE and reeled in a **" + fish_names[f_idx] + " (" + rarities[f_idx] + ", " + ss_wt.str() + "kg)**! 🐟", [&bot, channel_id](auto prompt, bool ok) {
                    if (ok) {
                        std::thread([&bot, channel_id, prompt]() {
                            std::this_thread::sleep_for(std::chrono::seconds(7));
                            bot.delete_message(channel_id, prompt.id);
                        }).detach();
                    }
                });
            } else {
                // Give coins
                std::uniform_int_distribution<> coin_dis(1500, 4500);
                int coins = coin_dis(gen);
                bronx::g_db->update_balances(user_id, coins, 0);

                bot.send_message(channel_id, "🎉 **Congratulations!** <@" + user_id + "> claimed the QTE and received **" + std::to_string(coins) + " coins**! 🪙", [&bot, channel_id](auto prompt, bool ok) {
                    if (ok) {
                        std::thread([&bot, channel_id, prompt]() {
                            std::this_thread::sleep_for(std::chrono::seconds(7));
                            bot.delete_message(channel_id, prompt.id);
                        }).detach();
                    }
                });
            }
        } else {
            // Give coins
            std::uniform_int_distribution<> coin_dis(2000, 6000);
            int coins = coin_dis(gen);
            bronx::g_db->update_balances(user_id, coins, 0);

            bot.send_message(channel_id, "🎉 **Congratulations!** <@" + user_id + "> claimed the QTE and received **" + std::to_string(coins) + " coins**! 🪙", [&bot, channel_id](auto prompt, bool ok) {
                if (ok) {
                    std::thread([&bot, channel_id, prompt]() {
                        std::this_thread::sleep_for(std::chrono::seconds(7));
                        bot.delete_message(channel_id, prompt.id);
                    }).detach();
                }
            });
        }
    }

    static void trigger_qte_event(stoatpp::cluster &bot, const std::string &channel_id, const std::string &qte_type) {
        {
            std::lock_guard<std::mutex> lock(qte_mutex_);
            active_qtes_.insert(channel_id);
        }

        std::string claim_word;
        std::string embed_title;
        std::string embed_desc;
        uint32_t embed_color = 0x00FF00;

        std::vector<std::string> money_words = {"CLAIM", "COLLECT", "GRAB", "GOLD", "CASH"};
        std::vector<std::string> fish_words = {"CATCH", "REEL", "HOOK", "NET", "GRAB"};

        std::random_device rd;
        std::mt19937 gen(rd());

        if (qte_type == "fishing") {
            std::uniform_int_distribution<> dis(0, fish_words.size() - 1);
            claim_word = fish_words[dis(gen)];
            embed_title = "🎣 **QUICKTIME EVENT: FISHING FRENZY!** 🎣";
            embed_desc = "A mysterious ripple appears in the water! A rare bait/fish is nearby!\n"
                         "Type **`" + claim_word + "`** immediately to reel it in!";
            embed_color = 0x3498DB; // Blue
        } else {
            std::uniform_int_distribution<> dis(0, money_words.size() - 1);
            claim_word = money_words[dis(gen)];
            embed_title = "💰 **QUICKTIME EVENT: MONEY DROP!** 💰";
            embed_desc = "A sack of gold coins fell from the sky! Who will get it?\n"
                         "Type **`" + claim_word + "`** immediately to claim it!";
            embed_color = 0xF1C40F; // Gold
        }

        stoatpp::models::MessagePayload payload;
        payload.content = " ";
        stoatpp::models::Embed embed;
        embed.set_colour(embed_color);
        embed.set_title(embed_title);
        embed.set_description(embed_desc);
        payload.add_embed(embed);

        bot.send_message(channel_id, payload, [&bot, channel_id, qte_type, claim_word](stoatpp::models::Message qte_msg, bool success) {
            if (!success) {
                std::lock_guard<std::mutex> lock(qte_mutex_);
                active_qtes_.erase(channel_id);
                return;
            }

            // Await any message in this channel (timeout of 15 seconds)
            bot.await_message(channel_id, "", std::chrono::seconds(15), [&bot, channel_id, qte_msg, qte_type, claim_word](std::optional<stoatpp::events::Message> reply) {
                // Delete the QTE prompt message to keep the chat clean
                bot.delete_message(channel_id, qte_msg.id);

                if (!reply) {
                    bot.send_message(channel_id, "⏱️ The Quicktime Event ended. No one responded in time!", [&bot, channel_id](auto prompt, bool ok) {
                        if (ok) {
                            std::thread([&bot, channel_id, prompt]() {
                                std::this_thread::sleep_for(std::chrono::seconds(5));
                                bot.delete_message(channel_id, prompt.id);
                            }).detach();
                        }
                    });
                    std::lock_guard<std::mutex> lock(qte_mutex_);
                    active_qtes_.erase(channel_id);
                    return;
                }

                // Check if the reply's content matches the claim word
                std::string ans = reply->content;
                ans.erase(std::remove_if(ans.begin(), ans.end(), ::isspace), ans.end());
                std::transform(ans.begin(), ans.end(), ans.begin(), ::toupper);

                if (ans == claim_word) {
                    give_qte_reward(bot, channel_id, reply->author.id, qte_type);
                    
                    // Delete the user's triggering reply to keep the chat extremely clean
                    bot.delete_message(channel_id, reply->id);
                } else {
                    bot.send_message(channel_id, "" + bronx::emj_no + " <@" + reply->author.id + "> tried to claim but typed the wrong word! The opportunity was lost.", [&bot, channel_id](auto prompt, bool ok) {
                        if (ok) {
                            std::thread([&bot, channel_id, prompt]() {
                                std::this_thread::sleep_for(std::chrono::seconds(5));
                                bot.delete_message(channel_id, prompt.id);
                            }).detach();
                        }
                    });
                }

                std::lock_guard<std::mutex> lock(qte_mutex_);
                active_qtes_.erase(channel_id);
            });
        });
    }

public:
    static void on_command_executed(stoatpp::cluster &bot, const stoatpp::events::Message &msg,
                                    const std::string &cmd_name, const std::string &category) {
        if (msg.author.bot) return;

        std::lock_guard<std::mutex> lock(qte_mutex_);

        if (active_qtes_.count(msg.channel_id)) return;

        std::string cmd_lower = cmd_name;
        std::transform(cmd_lower.begin(), cmd_lower.end(), cmd_lower.begin(), ::tolower);

        if (cmd_lower == "fish" || cmd_lower == "bait" || cmd_lower == "equip" || cmd_lower == "rod" || cmd_lower == "prestige") {
            channel_fish_cmds_[msg.channel_id]++;
        } else if (cmd_lower == "work" || cmd_lower == "beg" || cmd_lower == "crime" || cmd_lower == "rob" || cmd_lower == "daily" || category == "Economy") {
            channel_money_cmds_[msg.channel_id]++;
        }

        channel_cmd_counts_[msg.channel_id]++;

        if (channel_qte_targets_.find(msg.channel_id) == channel_qte_targets_.end()) {
            channel_qte_targets_[msg.channel_id] = 150 + (std::rand() % 100);
        }

        if (channel_cmd_counts_[msg.channel_id] >= channel_qte_targets_[msg.channel_id]) {
            std::string qte_type = "money";
            if (channel_fish_cmds_[msg.channel_id] > channel_money_cmds_[msg.channel_id]) {
                qte_type = "fishing";
            }

            channel_cmd_counts_[msg.channel_id] = 0;
            channel_money_cmds_[msg.channel_id] = 0;
            channel_fish_cmds_[msg.channel_id] = 0;
            channel_qte_targets_[msg.channel_id] = 150 + (std::rand() % 100);

            trigger_qte_event(bot, msg.channel_id, qte_type);
        }
    }

    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "qte",
            {"qteforce"},
            "manually triggers a Quicktime Event in the current channel (Owner only).",
            "qte [fishing/money]",
            {},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!bot.is_owner(msg.author.id)) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Only the bot owner can manually trigger QTEs.");
                    return;
                }

                std::string qte_type = "money";
                if (args.size() > 0) {
                    std::string arg_type = args[0];
                    std::transform(arg_type.begin(), arg_type.end(), arg_type.begin(), ::tolower);
                    if (arg_type == "fishing" || arg_type == "fish" || arg_type == "f") {
                        qte_type = "fishing";
                    }
                }

                // Delete the triggering command message
                bot.delete_message(msg.channel_id, msg.id);

                trigger_qte_event(bot, msg.channel_id, qte_type);
            },
            "Owner"
        });
    }
};

} // namespace bronx::cogs
