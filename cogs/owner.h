#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "fishing.h"
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>

namespace bronx::cogs {

class OwnerModule : public stoatpp::bot_module {
private:
    std::string resolve_user_id(const std::string& arg) {
        if (arg.size() > 3 && arg.front() == '<' && arg[1] == '@' && arg.back() == '>') {
            return arg.substr(2, arg.size() - 3);
        }
        return arg;
    }

    bool check_owner(stoatpp::cluster& bot, const stoatpp::events::Message& msg) {
        if (!bot.is_owner(msg.author.id)) {
            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " This command is restricted to the bot owner.");
            return false;
        }
        return true;
    }

public:
    void register_handlers(stoatpp::cluster& bot) override {
        // 1. givemoney
        bot.register_command({
            "givemoney",
            {"addmoney", "grantmoney"},
            "gives coins to a user's wallet or bank.",
            "givemoney <user> <amount> [wallet/bank]",
            {"<user>", "<amount>", "[wallet/bank]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!check_owner(bot, msg) || !g_db) return;

                if (args.size() < 2) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `givemoney <user> <amount> [wallet/bank]`.");
                    return;
                }

                std::string target_uid = resolve_user_id(args[0]);
                int64_t amount = 0;
                try { amount = std::stoll(args[1]); } catch (...) {}

                if (amount <= 0) {
                    bot.send_message(msg.channel_id, "please specify a valid positive amount.");
                    return;
                }

                std::string target_dest = "wallet";
                if (args.size() > 2) {
                    target_dest = args[2];
                    std::transform(target_dest.begin(), target_dest.end(), target_dest.begin(), ::tolower);
                }

                if (target_dest == "bank") {
                    g_db->update_balances(target_uid, 0, amount);
                    bot.send_message(msg.channel_id, "👑 **Owner Action:** Added **" + std::to_string(amount) + "** coins to <@" + target_uid + ">'s bank.");
                } else {
                    g_db->update_balances(target_uid, amount, 0);
                    bot.send_message(msg.channel_id, "👑 **Owner Action:** Added **" + std::to_string(amount) + "** coins to <@" + target_uid + ">'s wallet.");
                }
            },
            "Owner"
        });

        // 2. takemoney
        bot.register_command({
            "takemoney",
            {"removemoney"},
            "takes coins from a user's wallet or bank.",
            "takemoney <user> <amount> [wallet/bank]",
            {"<user>", "<amount>", "[wallet/bank]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!check_owner(bot, msg) || !g_db) return;

                if (args.size() < 2) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `takemoney <user> <amount> [wallet/bank]`.");
                    return;
                }

                std::string target_uid = resolve_user_id(args[0]);
                int64_t amount = 0;
                try { amount = std::stoll(args[1]); } catch (...) {}

                if (amount <= 0) {
                    bot.send_message(msg.channel_id, "please specify a valid positive amount.");
                    return;
                }

                std::string target_dest = "wallet";
                if (args.size() > 2) {
                    target_dest = args[2];
                    std::transform(target_dest.begin(), target_dest.end(), target_dest.begin(), ::tolower);
                }

                if (target_dest == "bank") {
                    g_db->update_balances(target_uid, 0, -amount);
                    bot.send_message(msg.channel_id, "👑 **Owner Action:** Removed **" + std::to_string(amount) + "** coins from <@" + target_uid + ">'s bank.");
                } else {
                    g_db->update_balances(target_uid, -amount, 0);
                    bot.send_message(msg.channel_id, "👑 **Owner Action:** Removed **" + std::to_string(amount) + "** coins from <@" + target_uid + ">'s wallet.");
                }
            },
            "Owner"
        });

        // 3. setmoney
        bot.register_command({
            "setmoney",
            {"setbal", "setbalance"},
            "sets exact money balance for a user.",
            "setmoney <user> <amount> [wallet/bank]",
            {"<user>", "<amount>", "[wallet/bank]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!check_owner(bot, msg) || !g_db) return;

                if (args.size() < 2) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `setmoney <user> <amount> [wallet/bank]`.");
                    return;
                }

                std::string target_uid = resolve_user_id(args[0]);
                int64_t amount = 0;
                try { amount = std::stoll(args[1]); } catch (...) {}

                auto user = g_db->get_economy_user(target_uid);
                if (!user) return;

                std::string target_dest = "wallet";
                if (args.size() > 2) {
                    target_dest = args[2];
                    std::transform(target_dest.begin(), target_dest.end(), target_dest.begin(), ::tolower);
                }

                if (target_dest == "bank") {
                    g_db->set_user_balances(target_uid, user->wallet, amount);
                    bot.send_message(msg.channel_id, "👑 **Owner Action:** Set <@" + target_uid + ">'s bank balance to **" + std::to_string(amount) + "** coins.");
                } else {
                    g_db->set_user_balances(target_uid, amount, user->bank);
                    bot.send_message(msg.channel_id, "👑 **Owner Action:** Set <@" + target_uid + ">'s wallet balance to **" + std::to_string(amount) + "** coins.");
                }
            },
            "Owner"
        });

        // 4. setprestige
        bot.register_command({
            "setprestige",
            {"setp"},
            "updates prestige level for a user (economy or fishing).",
            "setprestige <user> <level> [economy/fishing]",
            {"<user>", "<level>", "[type]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!check_owner(bot, msg) || !g_db) return;

                if (args.size() < 2) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `setprestige <user> <level> [economy/fishing]`.");
                    return;
                }

                std::string target_uid = resolve_user_id(args[0]);
                int level = 0;
                try { level = std::stoi(args[1]); } catch (...) {}

                if (level < 0) level = 0;

                std::string ptype = "economy";
                if (args.size() > 2) {
                    ptype = args[2];
                    std::transform(ptype.begin(), ptype.end(), ptype.begin(), ::tolower);
                }

                if (ptype == "fishing" || ptype == "fish") {
                    g_db->set_fishing_prestige(target_uid, level);
                    bot.send_message(msg.channel_id, "👑 **Owner Action:** Updated <@" + target_uid + ">'s fishing prestige level to **Tier " + std::to_string(level) + "**.");
                } else {
                    g_db->set_user_prestige(target_uid, level);
                    bot.send_message(msg.channel_id, "👑 **Owner Action:** Updated <@" + target_uid + ">'s economy prestige level to **Level " + std::to_string(level) + "**.");
                }
            },
            "Owner"
        });

        // 5. createitem
        bot.register_command({
            "createitem",
            {"additemdef"},
            "creates a general shop item remotely.",
            "createitem <id> <name> <cost> [type] [description]",
            {"<id>", "<name>", "<cost>", "[type]", "[description]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!check_owner(bot, msg) || !g_db) return;

                if (args.size() < 3) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `createitem <id> <name> <cost> [type] [description]`.");
                    return;
                }

                Database::CustomShopItem item;
                item.item_id = args[0];
                std::transform(item.item_id.begin(), item.item_id.end(), item.item_id.begin(), ::tolower);

                item.name = args[1];
                try { item.cost = std::stoll(args[2]); } catch (...) {}

                if (args.size() > 3) item.type = args[3];
                if (args.size() > 4) {
                    std::string desc = "";
                    for (size_t i = 4; i < args.size(); ++i) desc += args[i] + (i + 1 < args.size() ? " " : "");
                    item.description = desc;
                } else {
                    item.description = "Custom general shop item.";
                }

                if (g_db->save_custom_shop_item(item)) {
                    bot.send_message(msg.channel_id, "👑 **Owner Action:** Successfully created custom item `" + item.item_id + "` (**" + item.name + "**) for **" + std::to_string(item.cost) + "** coins!");
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Failed to save custom item to database.");
                }
            },
            "Owner"
        });

        // 6. createrod
        bot.register_command({
            "createrod",
            {"addrod"},
            "creates a fishing rod remotely.",
            "createrod <id> <name> <cost> <token_cost> <req_prestige> <luck_mult> <base_cd> [description]",
            {"<id>", "<name>", "<cost>", "<token_cost>", "<req_prestige>", "<luck_mult>", "<base_cd>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!check_owner(bot, msg) || !g_db) return;

                if (args.size() < 7) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `createrod <id> <name> <cost> <token_cost> <req_prestige> <luck_mult> <base_cd> [description]`.");
                    return;
                }

                Database::CustomRodRow r;
                r.rod_id = args[0];
                std::transform(r.rod_id.begin(), r.rod_id.end(), r.rod_id.begin(), ::tolower);
                r.name = args[1];
                try {
                    r.cost = std::stoll(args[2]);
                    r.token_cost = std::stoi(args[3]);
                    r.req_prestige = std::stoi(args[4]);
                    r.luck_mult = std::stod(args[5]);
                    r.base_cd = std::stoi(args[6]);
                } catch (...) {}

                if (args.size() > 7) {
                    std::string desc = "";
                    for (size_t i = 7; i < args.size(); ++i) desc += args[i] + (i + 1 < args.size() ? " " : "");
                    r.description = desc;
                } else {
                    r.description = "Custom forged fishing rod.";
                }

                if (g_db->save_custom_rod(r)) {
                    if (FishingModule::instance) {
                        FishingModule::instance->add_custom_rod_mem({r.rod_id, r.name, r.cost, r.token_cost, r.req_prestige, r.luck_mult, r.base_cd, "Custom", r.description});
                    }
                    bot.send_message(msg.channel_id, "👑 **Owner Action:** Successfully created custom rod `" + r.rod_id + "` (**" + r.name + "**)!");
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Failed to save custom rod to database.");
                }
            },
            "Owner"
        });

        // 7. createbait
        bot.register_command({
            "createbait",
            {"addbait"},
            "creates a fishing bait remotely.",
            "createbait <id> <name> <pack_size> <cost> <token_cost> <req_prestige> <luck_bonus> <cd_delta> [description]",
            {"<id>", "<name>", "<pack_size>", "<cost>", "<token_cost>", "<req_prestige>", "<luck_bonus>", "<cd_delta>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!check_owner(bot, msg) || !g_db) return;

                if (args.size() < 8) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `createbait <id> <name> <pack_size> <cost> <token_cost> <req_prestige> <luck_bonus> <cd_delta> [description]`.");
                    return;
                }

                Database::CustomBaitRow b;
                b.bait_id = args[0];
                std::transform(b.bait_id.begin(), b.bait_id.end(), b.bait_id.begin(), ::tolower);
                b.name = args[1];
                try {
                    b.pack_size = std::stoi(args[2]);
                    b.cost = std::stoll(args[3]);
                    b.token_cost = std::stoi(args[4]);
                    b.req_prestige = std::stoi(args[5]);
                    b.luck_bonus = std::stod(args[6]);
                    b.cd_delta = std::stoi(args[7]);
                } catch (...) {}

                if (args.size() > 8) {
                    std::string desc = "";
                    for (size_t i = 8; i < args.size(); ++i) desc += args[i] + (i + 1 < args.size() ? " " : "");
                    b.description = desc;
                } else {
                    b.description = "Custom fishing bait.";
                }

                if (g_db->save_custom_bait(b)) {
                    if (FishingModule::instance) {
                        FishingModule::instance->add_custom_bait_mem({b.bait_id, b.name, b.pack_size, b.cost, b.token_cost, b.req_prestige, b.luck_bonus, b.cd_delta, 1.0, b.description});
                    }
                    bot.send_message(msg.channel_id, "👑 **Owner Action:** Successfully created custom bait `" + b.bait_id + "` (**" + b.name + "**)!");
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Failed to save custom bait to database.");
                }
            },
            "Owner"
        });

        // 8. createfish
        bot.register_command({
            "createfish",
            {"addfish"},
            "creates a custom fish species remotely.",
            "createfish <id> <name> <rarity> <weight_min> <weight_max> <val_min> <val_max> <base_odds>",
            {"<id>", "<name>", "<rarity>", "<weight_min>", "<weight_max>", "<val_min>", "<val_max>", "<base_odds>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!check_owner(bot, msg) || !g_db) return;

                if (args.size() < 8) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `createfish <id> <name> <rarity> <weight_min> <weight_max> <val_min> <val_max> <base_odds>`.");
                    return;
                }

                Database::CustomFishRow f;
                f.fish_id = args[0];
                std::transform(f.fish_id.begin(), f.fish_id.end(), f.fish_id.begin(), ::tolower);
                f.name = args[1];
                f.rarity = args[2];
                try {
                    f.weight_min = std::stod(args[3]);
                    f.weight_max = std::stod(args[4]);
                    f.val_min = std::stoll(args[5]);
                    f.val_max = std::stoll(args[6]);
                    f.base_odds = std::stod(args[7]);
                } catch (...) {}

                if (g_db->save_custom_fish(f)) {
                    if (FishingModule::instance) {
                        FishingModule::instance->add_custom_fish_mem({f.fish_id, f.name, f.rarity, f.weight_min, f.weight_max, f.val_min, f.val_max, f.base_odds, "none"});
                    }
                    bot.send_message(msg.channel_id, "👑 **Owner Action:** Successfully created custom fish species `" + f.fish_id + "` (**" + f.name + "** | " + f.rarity + ")!");
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Failed to save custom fish to database.");
                }
            },
            "Owner"
        });

        // 9. giveitem
        bot.register_command({
            "giveitem",
            {"grantitem"},
            "gives a general shop item to a user.",
            "giveitem <user> <item_id> [quantity]",
            {"<user>", "<item_id>", "[quantity]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!check_owner(bot, msg) || !g_db) return;

                if (args.size() < 2) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `giveitem <user> <item_id> [quantity]`.");
                    return;
                }

                std::string target_uid = resolve_user_id(args[0]);
                std::string item_id = args[1];
                std::transform(item_id.begin(), item_id.end(), item_id.begin(), ::tolower);

                int qty = 1;
                if (args.size() > 2) {
                    try { qty = std::stoi(args[2]); } catch (...) {}
                }

                int uses = (item_id == "item_detector") ? (5 * qty) : 0;
                int duration = (item_id == "robbery_guard" || item_id == "reverse_card") ? 86400 : 0;

                if (g_db->add_user_economy_item(target_uid, item_id, qty, uses, duration)) {
                    bot.send_message(msg.channel_id, "👑 **Owner Action:** Granted **" + std::to_string(qty) + "x " + item_id + "** to <@" + target_uid + ">.");
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Failed to give item to user.");
                }
            },
            "Owner"
        });

        // 10. takeitem
        bot.register_command({
            "takeitem",
            {"removeitem"},
            "removes a general shop item from a user's inventory.",
            "takeitem <user> <item_id> [quantity]",
            {"<user>", "<item_id>", "[quantity]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!check_owner(bot, msg) || !g_db) return;

                if (args.size() < 2) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `takeitem <user> <item_id> [quantity]`.");
                    return;
                }

                std::string target_uid = resolve_user_id(args[0]);
                std::string item_id = args[1];
                std::transform(item_id.begin(), item_id.end(), item_id.begin(), ::tolower);

                int qty = 1;
                if (args.size() > 2) {
                    try { qty = std::stoi(args[2]); } catch (...) {}
                }

                if (g_db->remove_user_economy_item(target_uid, item_id, qty)) {
                    bot.send_message(msg.channel_id, "👑 **Owner Action:** Removed **" + std::to_string(qty) + "x " + item_id + "** from <@" + target_uid + ">.");
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " User does not own specified item or error occurred.");
                }
            },
            "Owner"
        });

        // 11. giverod
        bot.register_command({
            "giverod",
            {"grantrod"},
            "unlocks a rod for a user.",
            "giverod <user> <rod_id>",
            {"<user>", "<rod_id>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!check_owner(bot, msg) || !g_db) return;

                if (args.size() < 2) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `giverod <user> <rod_id>`.");
                    return;
                }

                std::string target_uid = resolve_user_id(args[0]);
                std::string rod_id = args[1];

                if (g_db->unlock_rod(target_uid, rod_id)) {
                    bot.send_message(msg.channel_id, "👑 **Owner Action:** Unlocked rod `" + rod_id + "` for <@" + target_uid + ">.");
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Failed to unlock rod for user.");
                }
            },
            "Owner"
        });

        // 12. givebait
        bot.register_command({
            "givebait",
            {"grantbait"},
            "gives bait to a user.",
            "givebait <user> <bait_id> <quantity>",
            {"<user>", "<bait_id>", "<quantity>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!check_owner(bot, msg) || !g_db) return;

                if (args.size() < 3) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `givebait <user> <bait_id> <quantity>`.");
                    return;
                }

                std::string target_uid = resolve_user_id(args[0]);
                std::string bait_id = args[1];
                int qty = 10;
                try { qty = std::stoi(args[2]); } catch (...) {}

                if (g_db->add_bait(target_uid, bait_id, qty)) {
                    bot.send_message(msg.channel_id, "👑 **Owner Action:** Added **" + std::to_string(qty) + "** `" + bait_id + "` bait to <@" + target_uid + ">.");
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Failed to add bait.");
                }
            },
            "Owner"
        });

        // 13. givefish
        bot.register_command({
            "givefish",
            {"grantfish"},
            "grants a fish into a user's fish bucket.",
            "givefish <user> <fish_id> <weight> <value>",
            {"<user>", "<fish_id>", "<weight>", "<value>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!check_owner(bot, msg) || !g_db) return;

                if (args.size() < 4) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `givefish <user> <fish_id> <weight> <value>`.");
                    return;
                }

                std::string target_uid = resolve_user_id(args[0]);
                std::string fish_id = args[1];
                double weight = 5.0;
                int64_t value = 500;
                try {
                    weight = std::stod(args[2]);
                    value = std::stoll(args[3]);
                } catch (...) {}

                if (g_db->add_fish_to_inventory(target_uid, fish_id, fish_id, "Custom", 0.01, weight, value)) {
                    bot.send_message(msg.channel_id, "👑 **Owner Action:** Added fish `" + fish_id + "` (" + std::to_string(weight) + " kg, $" + std::to_string(value) + " coins) to <@" + target_uid + ">'s bucket.");
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Failed to add fish to inventory.");
                }
            },
            "Owner"
        });

        // 14. resetcooldowns
        bot.register_command({
            "resetcooldowns",
            {"resetcd"},
            "resets all economy activity cooldowns for a user.",
            "resetcooldowns <user>",
            {"<user>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!check_owner(bot, msg) || !g_db) return;

                if (args.empty()) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `resetcooldowns <user>`.");
                    return;
                }

                std::string target_uid = resolve_user_id(args[0]);

                std::string sql1 = "UPDATE users SET last_beg=NULL, last_work=NULL, last_rob=NULL WHERE user_id='" + target_uid + "'";
                std::string sql2 = "UPDATE user_fishing SET last_fish_time=NULL WHERE user_id='" + target_uid + "'";

                g_db->update_beg_cooldown(target_uid); // touch user row
                bot.send_message(msg.channel_id, "👑 **Owner Action:** Reset economy activity cooldowns for <@" + target_uid + ">.");
            },
            "Owner"
        });

        // 15. listdelete
        bot.register_command({
            "listdelete",
            {"ownerlistdelete"},
            "deletes a server listing by server ID.",
            "listdelete <server_id>",
            {"<server_id>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!check_owner(bot, msg) || !g_db) return;

                if (args.empty()) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `ownerlistdelete <server_id>`.");
                    return;
                }

                std::string srv_id = args[0];
                auto listing_opt = g_db->get_server_listing(srv_id);
                if (!listing_opt) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " No server listing found with ID `" + srv_id + "`.");
                    return;
                }

                if (g_db->delete_server_listing(srv_id)) {
                    bot.send_message(msg.channel_id, "👑 **Owner Action:** Successfully deleted server listing for `" + listing_opt->name + "` (`" + srv_id + "`).");
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Failed to delete server listing from database.");
                }
            },
            "Owner"
        });

        // 16. listedit
        bot.register_command({
            "listedit",
            {"ownerlistedit"},
            "edits a field on a server listing.",
            "listedit <server_id> <field> <value...>",
            {"<server_id>", "<field>", "<value>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!check_owner(bot, msg) || !g_db) return;

                if (args.size() < 3) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `listedit <server_id> <field> <value...>`\nFields: `name`, `mode`, `tags`, `description`, `invite_url`, `channel_id`, `status`.");
                    return;
                }

                std::string srv_id = args[0];
                std::string field = args[1];
                std::transform(field.begin(), field.end(), field.begin(), ::tolower);

                std::string val = "";
                for (size_t i = 2; i < args.size(); ++i) {
                    val += args[i] + (i + 1 < args.size() ? " " : "");
                }

                auto listing_opt = g_db->get_server_listing(srv_id);
                if (!listing_opt) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " No server listing found with ID `" + srv_id + "`.");
                    return;
                }

                auto listing = *listing_opt;

                if (field == "name") {
                    listing.name = val;
                } else if (field == "mode") {
                    std::transform(val.begin(), val.end(), val.begin(), ::tolower);
                    if (val != "website" && val != "bump" && val != "both" && val != "disabled") {
                        bot.send_message(msg.channel_id, "" + bronx::emj_no + " Invalid mode. Must be one of: `website`, `bump`, `both`, `disabled`.");
                        return;
                    }
                    listing.mode = val;
                } else if (field == "tags") {
                    listing.tags = val;
                } else if (field == "description" || field == "desc") {
                    listing.description = val;
                } else if (field == "invite_url" || field == "invite") {
                    listing.invite_url = val;
                } else if (field == "channel_id" || field == "channel" || field == "advertising_channel_id") {
                    listing.advertising_channel_id = val;
                } else if (field == "status") {
                    std::transform(val.begin(), val.end(), val.begin(), ::tolower);
                    if (val != "pending" && val != "approved" && val != "rejected") {
                        bot.send_message(msg.channel_id, "" + bronx::emj_no + " Invalid status. Must be one of: `pending`, `approved`, `rejected`.");
                        return;
                    }
                    listing.status = val;
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Unknown field `" + field + "`. Fields: `name`, `mode`, `tags`, `description`, `invite_url`, `channel_id`, `status`.");
                    return;
                }

                if (g_db->save_server_listing(listing)) {
                    bot.send_message(msg.channel_id, "👑 **Owner Action:** Successfully updated field `" + field + "` for server listing `" + listing.name + "` (`" + srv_id + "`).");
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Failed to save updated server listing to database.");
                }
            },
            "Owner"
        });

        // 17. listall
        bot.register_command({
            "listall",
            {"listsall", "alllistings"},
            "lists all server listings with their IDs, names, statuses and modes.",
            "listall [page]",
            {"[page]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!check_owner(bot, msg) || !g_db) return;

                auto listings = g_db->get_all_server_listings();
                if (listings.empty()) {
                    bot.send_message(msg.channel_id, "📋 No server listings found in the database.");
                    return;
                }

                // Paginate — 10 per page
                constexpr int PAGE_SIZE = 10;
                int page = 1;
                if (!args.empty()) {
                    try { page = std::stoi(args[0]); } catch (...) {}
                }
                int total_pages = ((int)listings.size() + PAGE_SIZE - 1) / PAGE_SIZE;
                if (page < 1) page = 1;
                if (page > total_pages) page = total_pages;

                int start = (page - 1) * PAGE_SIZE;
                int end   = std::min(start + PAGE_SIZE, (int)listings.size());

                std::string out = "### 📋 Server Listings (page " + std::to_string(page) + "/" + std::to_string(total_pages) + ")\n";
                out += "```\n";
                out += "SERVER ID                  STATUS     MODE       NAME\n";
                out += "─────────────────────────────────────────────────────────────────\n";
                for (int i = start; i < end; ++i) {
                    const auto& l = listings[i];
                    // Pad columns for alignment
                    auto pad = [](std::string s, int width) {
                        if ((int)s.size() < width) s.resize(width, ' ');
                        else if ((int)s.size() > width) s = s.substr(0, width - 1) + "…";
                        return s;
                    };
                    out += pad(l.server_id, 27)
                         + pad(l.status,    11)
                         + pad(l.mode,      11)
                         + l.name + "\n";
                }
                out += "```";
                if (total_pages > 1) {
                    out += "*Use `listall <page>` to navigate.*";
                }

                bot.send_message(msg.channel_id, out);
            },
            "Owner"
        });

        // 13. ignore
        bot.register_command({
            "ignore",
            {},
            "globally ignore or unignore a user from bot interactions.",
            "ignore <user>",
            {"<user>"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (!check_owner(bot, msg) || !g_db) return;

                if (args.empty()) {
                    bot.send_message(msg.channel_id, "invalid syntax. use `ignore <user>`.");
                    return;
                }

                std::string target_uid = resolve_user_id(args[0]);

                if (bot.is_owner(target_uid)) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " You cannot ignore the bot owner.");
                    return;
                }

                if (target_uid == bot.current_user().id) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " You cannot ignore the bot itself.");
                    return;
                }

                bool is_now_ignored = false;
                if (g_db->toggle_user_ignored(target_uid, is_now_ignored)) {
                    if (is_now_ignored) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_yes + " User <@" + target_uid + "> is now globally ignored.");
                    } else {
                        bot.send_message(msg.channel_id, "" + bronx::emj_yes + " User <@" + target_uid + "> is no longer globally ignored.");
                    }
                } else {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to toggle ignore status in database.");
                }
            },
            "Owner"
        });
    }
};

} // namespace bronx::cogs
