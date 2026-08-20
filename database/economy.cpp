#include "database.h"
#include <mysql.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bronx {

  bool Database::ensure_economy_user(const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "INSERT IGNORE INTO users (user_id) VALUES ('" +
                      escape_string(user_id) + "')";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::optional<EconomyUser> Database::get_economy_user(const std::string &user_id) {
    ensure_economy_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return std::nullopt;

    std::string sql =
        "SELECT wallet, bank, bank_limit, prestige, "
        "IFNULL(last_beg, ''), IFNULL(last_work, ''), IFNULL(last_rob, '') "
        "FROM users WHERE user_id = '" +
        escape_string(user_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          EconomyUser u;
          u.user_id = user_id;
          u.wallet = std::stoll(row[0]);
          u.bank = std::stoll(row[1]);
          u.bank_limit = std::stoll(row[2]);
          u.prestige = std::stoi(row[3]);
          u.last_beg = row[4];
          u.last_work = row[5];
          u.last_rob = row[6];
          mysql_free_result(res);
          return u;
        }
        mysql_free_result(res);
      }
    }
    return std::nullopt;
  }

  bool Database::update_balances(const std::string &user_id, int64_t wallet_delta,
                       int64_t bank_delta) {
    ensure_economy_user(user_id);
    bool success;
    {
      std::lock_guard<std::mutex> lock(db_mutex_);
      if (!mysql_)
        return false;
      std::string sql = "UPDATE users SET wallet = wallet + " +
                        std::to_string(wallet_delta) + ", bank = bank + " +
                        std::to_string(bank_delta) + " WHERE user_id = '" +
                        escape_string(user_id) + "'";
      success = (mysql_query(mysql_, sql.c_str()) == 0);
    }
    if (success && on_balance_update) {
      on_balance_update(user_id, wallet_delta, bank_delta);
    }
    return success;
  }

  bool Database::prestige_user(const std::string &user_id, int64_t new_bank_limit) {
    ensure_economy_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "UPDATE users SET wallet = 0, bank = 0, prestige = "
                      "prestige + 1, bank_limit = " +
                      std::to_string(new_bank_limit) + " WHERE user_id = '" +
                      escape_string(user_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool Database::update_beg_cooldown(const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "UPDATE users SET last_beg = CURRENT_TIMESTAMP WHERE user_id = '" +
        escape_string(user_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool Database::update_work_cooldown(const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "UPDATE users SET last_work = CURRENT_TIMESTAMP WHERE user_id = '" +
        escape_string(user_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool Database::update_rob_cooldown(const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "UPDATE users SET last_rob = CURRENT_TIMESTAMP WHERE user_id = '" +
        escape_string(user_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  int Database::get_cooldown_remaining(const std::string &user_id,
                             const std::string &field, int cooldown_seconds) {
    ensure_economy_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return 0;
    std::string sql = "SELECT GREATEST(0, CAST(" +
                      std::to_string(cooldown_seconds) +
                      " AS SIGNED) - TIMESTAMPDIFF(SECOND, " + field +
                      ", CURRENT_TIMESTAMP)) "
                      "FROM users WHERE user_id = '" +
                      escape_string(user_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0]) {
          int remaining = std::stoi(row[0]);
          mysql_free_result(res);
          return remaining;
        }
        mysql_free_result(res);
      }
    }
    return 0;
  }

  std::vector<TopEcoEntry> Database::get_top_economy_users(int limit) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<TopEcoEntry> entries;
    if (!mysql_)
      return entries;

    std::string sql = "SELECT user_id, (wallet + bank) AS networth FROM users "
                      "ORDER BY networth DESC LIMIT " +
                      std::to_string(limit);
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          if (row[0] && row[1]) {
            entries.push_back({row[0], std::stoll(row[1])});
          }
        }
        mysql_free_result(res);
      }
    }
    return entries;
  }

  bool Database::set_user_balances(const std::string &user_id, int64_t wallet,
                         int64_t bank) {
    ensure_economy_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "UPDATE users SET wallet = " + std::to_string(wallet) +
                      ", bank = " + std::to_string(bank) +
                      " WHERE user_id = '" + escape_string(user_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool Database::set_user_prestige(const std::string &user_id, int prestige_level) {
    ensure_economy_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "UPDATE users SET prestige = " + std::to_string(prestige_level) +
        " WHERE user_id = '" + escape_string(user_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool Database::set_fishing_prestige(const std::string &user_id, int prestige_level) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "UPDATE user_fishing SET fish_prestige = " +
                      std::to_string(prestige_level) + " WHERE user_id = '" +
                      escape_string(user_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool Database::add_user_economy_item(const std::string &user_id,
                             const std::string &item_id, int quantity,
                             int uses, int duration_seconds) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string esc_u = escape_string(user_id);
    std::string esc_i = escape_string(item_id);

    uint64_t active_until;
    if (duration_seconds > 0) {
      auto now = std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
      active_until = now + duration_seconds;
    }

    std::string sql =
        "INSERT INTO user_economy_items (user_id, item_id, quantity, "
        "uses_left, active_until) "
        "VALUES ('" +
        esc_u + "', '" + esc_i + "', " + std::to_string(quantity) + ", " +
        std::to_string(uses) + ", " + std::to_string(active_until) +
        ") "
        "ON DUPLICATE KEY UPDATE quantity = quantity + " +
        std::to_string(quantity) + ", uses_left = uses_left + " +
        std::to_string(uses) +
        (duration_seconds > 0
             ? (", active_until = " + std::to_string(active_until))
             : "");
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool Database::remove_user_economy_item(const std::string &user_id,
                                const std::string &item_id, int quantity) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string esc_u = escape_string(user_id);
    std::string esc_i = escape_string(item_id);

    std::string sql_sel =
        "SELECT quantity FROM user_economy_items WHERE user_id = '" + esc_u +
        "' AND item_id = '" + esc_i + "'";
    if (mysql_query(mysql_, sql_sel.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          int curr_q = std::stoi(row[0]);
          mysql_free_result(res);
          if (curr_q <= quantity) {
            std::string sql_del =
                "DELETE FROM user_economy_items WHERE user_id = '" + esc_u +
                "' AND item_id = '" + esc_i + "'";
            return mysql_query(mysql_, sql_del.c_str()) == 0;
          } else {
            std::string sql_upd =
                "UPDATE user_economy_items SET quantity = quantity - " +
                std::to_string(quantity) + " WHERE user_id = '" + esc_u +
                "' AND item_id = '" + esc_i + "'";
            return mysql_query(mysql_, sql_upd.c_str()) == 0;
          }
        }
        mysql_free_result(res);
      }
    }
    return false;
  }

  std::vector<UserEconomyItem>
  Database::get_user_economy_items(const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<UserEconomyItem> items;
    if (!mysql_)
      return items;

    std::string sql = "SELECT item_id, quantity, uses_left, active_until FROM "
                      "user_economy_items WHERE user_id = '" +
                      escape_string(user_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          UserEconomyItem it;
          it.item_id = row[0] ? row[0] : "";
          it.quantity = row[1] ? std::stoi(row[1]) : 0;
          it.uses_left = row[2] ? std::stoi(row[2]) : 0;
          it.active_until = row[3] ? std::stoull(row[3]) : 0;
          items.push_back(it);
        }
        mysql_free_result(res);
      }
    }
    return items;
  }

  bool Database::is_item_active(const std::string &user_id, const std::string &item_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "SELECT active_until FROM user_economy_items WHERE user_id = '" +
        escape_string(user_id) + "' AND item_id = '" + escape_string(item_id) +
        "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0]) {
          uint64_t until = std::stoull(row[0]);
          auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
          mysql_free_result(res);
          return (until > static_cast<uint64_t>(now));
        }
        mysql_free_result(res);
      }
    }
    return false;
  }

  bool Database::activate_item(const std::string &user_id, const std::string &item_id,
                     int duration_seconds) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    uint64_t active_until = now + duration_seconds;
    std::string sql = "UPDATE user_economy_items SET active_until = " +
                      std::to_string(active_until) + " WHERE user_id = '" +
                      escape_string(user_id) + "' AND item_id = '" +
                      escape_string(item_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool Database::use_item_charge(const std::string &user_id, const std::string &item_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string esc_u = escape_string(user_id);
    std::string esc_i = escape_string(item_id);

    std::string sql_sel =
        "SELECT uses_left, quantity FROM user_economy_items WHERE user_id = '" +
        esc_u + "' AND item_id = '" + esc_i + "'";
    if (mysql_query(mysql_, sql_sel.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          int uses = std::stoi(row[0]);
          int qty = std::stoi(row[1]);
          mysql_free_result(res);
          if (uses > 1) {
            std::string sql_upd = "UPDATE user_economy_items SET uses_left = "
                                  "uses_left - 1 WHERE user_id = '" +
                                  esc_u + "' AND item_id = '" + esc_i + "'";
            return mysql_query(mysql_, sql_upd.c_str()) == 0;
          } else if (qty > 1) {
            std::string sql_upd =
                "UPDATE user_economy_items SET quantity = quantity - 1, "
                "uses_left = 5 WHERE user_id = '" +
                esc_u + "' AND item_id = '" + esc_i + "'";
            return mysql_query(mysql_, sql_upd.c_str()) == 0;
          } else {
            std::string sql_del =
                "DELETE FROM user_economy_items WHERE user_id = '" + esc_u +
                "' AND item_id = '" + esc_i + "'";
            return mysql_query(mysql_, sql_del.c_str()) == 0;
          }
        }
        mysql_free_result(res);
      }
    }
    return false;
  }

  bool Database::save_custom_shop_item(const CustomShopItem &item) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT INTO custom_shop_items (item_id, name, type, cost, "
        "description, uses_default, duration_seconds) "
        "VALUES ('" +
        escape_string(item.item_id) + "', '" + escape_string(item.name) +
        "', '" + escape_string(item.type) + "', " + std::to_string(item.cost) +
        ", '" + escape_string(item.description) + "', " +
        std::to_string(item.uses_default) + ", " +
        std::to_string(item.duration_seconds) +
        ") "
        "ON DUPLICATE KEY UPDATE name=VALUES(name), type=VALUES(type), "
        "cost=VALUES(cost), description=VALUES(description), "
        "uses_default=VALUES(uses_default), "
        "duration_seconds=VALUES(duration_seconds)";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::vector<CustomShopItem> Database::get_custom_shop_items() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<CustomShopItem> items;
    if (!mysql_)
      return items;

    std::string sql =
        "SELECT item_id, name, type, cost, IFNULL(description,''), "
        "uses_default, duration_seconds FROM custom_shop_items";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          CustomShopItem item;
          item.item_id = row[0] ? row[0] : "";
          item.name = row[1] ? row[1] : "";
          item.type = row[2] ? row[2] : "general";
          item.cost = row[3] ? std::stoll(row[3]) : 0;
          item.description = row[4] ? row[4] : "";
          item.uses_default = row[5] ? std::stoi(row[5]) : 0;
          item.duration_seconds = row[6] ? std::stoi(row[6]) : 0;
          items.push_back(item);
        }
        mysql_free_result(res);
      }
    }
    return items;
  }

  bool Database::save_custom_rod(const CustomRodRow &r) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT INTO custom_rods (rod_id, name, cost, token_cost, "
        "req_prestige, luck_mult, base_cd, description) "
        "VALUES ('" +
        escape_string(r.rod_id) + "', '" + escape_string(r.name) + "', " +
        std::to_string(r.cost) + ", " + std::to_string(r.token_cost) + ", " +
        std::to_string(r.req_prestige) + ", " + std::to_string(r.luck_mult) +
        ", " + std::to_string(r.base_cd) + ", '" +
        escape_string(r.description) +
        "') "
        "ON DUPLICATE KEY UPDATE name=VALUES(name), cost=VALUES(cost), "
        "token_cost=VALUES(token_cost), req_prestige=VALUES(req_prestige), "
        "luck_mult=VALUES(luck_mult), base_cd=VALUES(base_cd), "
        "description=VALUES(description)";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::vector<CustomRodRow> Database::get_custom_rods() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<CustomRodRow> rods;
    if (!mysql_)
      return rods;

    std::string sql =
        "SELECT rod_id, name, cost, token_cost, req_prestige, luck_mult, "
        "base_cd, IFNULL(description,'') FROM custom_rods";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          CustomRodRow r;
          r.rod_id = row[0] ? row[0] : "";
          r.name = row[1] ? row[1] : "";
          r.cost = row[2] ? std::stoll(row[2]) : 0;
          r.token_cost = row[3] ? std::stoi(row[3]) : 0;
          r.req_prestige = row[4] ? std::stoi(row[4]) : 0;
          r.luck_mult = row[5] ? std::stod(row[5]) : 1.0;
          r.base_cd = row[6] ? std::stoi(row[6]) : 30;
          r.description = row[7] ? row[7] : "";
          rods.push_back(r);
        }
        mysql_free_result(res);
      }
    }
    return rods;
  }

  bool Database::save_custom_bait(const CustomBaitRow &b) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT INTO custom_baits (bait_id, name, pack_size, cost, token_cost, "
        "req_prestige, luck_bonus, cd_delta, description) "
        "VALUES ('" +
        escape_string(b.bait_id) + "', '" + escape_string(b.name) + "', " +
        std::to_string(b.pack_size) + ", " + std::to_string(b.cost) + ", " +
        std::to_string(b.token_cost) + ", " + std::to_string(b.req_prestige) +
        ", " + std::to_string(b.luck_bonus) + ", " +
        std::to_string(b.cd_delta) + ", '" + escape_string(b.description) +
        "') "
        "ON DUPLICATE KEY UPDATE name=VALUES(name), "
        "pack_size=VALUES(pack_size), cost=VALUES(cost), "
        "token_cost=VALUES(token_cost), req_prestige=VALUES(req_prestige), "
        "luck_bonus=VALUES(luck_bonus), cd_delta=VALUES(cd_delta), "
        "description=VALUES(description)";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::vector<CustomBaitRow> Database::get_custom_baits() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<CustomBaitRow> baits;
    if (!mysql_)
      return baits;

    std::string sql =
        "SELECT bait_id, name, pack_size, cost, token_cost, req_prestige, "
        "luck_bonus, cd_delta, IFNULL(description,'') FROM custom_baits";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          CustomBaitRow b;
          b.bait_id = row[0] ? row[0] : "";
          b.name = row[1] ? row[1] : "";
          b.pack_size = row[2] ? std::stoi(row[2]) : 10;
          b.cost = row[3] ? std::stoll(row[3]) : 0;
          b.token_cost = row[4] ? std::stoi(row[4]) : 0;
          b.req_prestige = row[5] ? std::stoi(row[5]) : 0;
          b.luck_bonus = row[6] ? std::stod(row[6]) : 0.0;
          b.cd_delta = row[7] ? std::stoi(row[7]) : 0;
          b.description = row[8] ? row[8] : "";
          baits.push_back(b);
        }
        mysql_free_result(res);
      }
    }
    return baits;
  }

  bool Database::save_custom_fish(const CustomFishRow &f) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT INTO custom_fish (fish_id, name, rarity, weight_min, "
        "weight_max, val_min, val_max, base_odds) "
        "VALUES ('" +
        escape_string(f.fish_id) + "', '" + escape_string(f.name) + "', '" +
        escape_string(f.rarity) + "', " + std::to_string(f.weight_min) + ", " +
        std::to_string(f.weight_max) + ", " + std::to_string(f.val_min) + ", " +
        std::to_string(f.val_max) + ", " + std::to_string(f.base_odds) +
        ") "
        "ON DUPLICATE KEY UPDATE name=VALUES(name), rarity=VALUES(rarity), "
        "weight_min=VALUES(weight_min), weight_max=VALUES(weight_max), "
        "val_min=VALUES(val_min), val_max=VALUES(val_max), "
        "base_odds=VALUES(base_odds)";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::vector<CustomFishRow> Database::get_custom_fish() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<CustomFishRow> fish;
    if (!mysql_)
      return fish;

    std::string sql = "SELECT fish_id, name, rarity, weight_min, weight_max, "
                      "val_min, val_max, base_odds FROM custom_fish";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          CustomFishRow f;
          f.fish_id = row[0] ? row[0] : "";
          f.name = row[1] ? row[1] : "";
          f.rarity = row[2] ? row[2] : "Common";
          f.weight_min = row[3] ? std::stod(row[3]) : 1.0;
          f.weight_max = row[4] ? std::stod(row[4]) : 5.0;
          f.val_min = row[5] ? std::stoll(row[5]) : 50;
          f.val_max = row[6] ? std::stoll(row[6]) : 150;
          f.base_odds = row[7] ? std::stod(row[7]) : 0.50;
          fish.push_back(f);
        }
        mysql_free_result(res);
      }
    }
    return fish;
  }

  bool Database::ensure_fishing_user(const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string esc_uid = escape_string(user_id);
    std::string sql =
        "INSERT IGNORE INTO user_fishing (user_id) VALUES ('" + esc_uid + "')";
    mysql_query(mysql_, sql.c_str());

    std::string sql_rod =
        "INSERT IGNORE INTO user_unlocked_rods (user_id, rod_id) VALUES ('" +
        esc_uid + "', 'wooden_rod')";
    mysql_query(mysql_, sql_rod.c_str());

    std::string sql_worm = "INSERT IGNORE INTO user_fish_baits (user_id, "
                           "bait_id, quantity) VALUES ('" +
                           esc_uid + "', 'worm', 10)";
    mysql_query(mysql_, sql_worm.c_str());
    return true;
  }

  std::optional<FishingUser> Database::get_fishing_user(const std::string &user_id) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return std::nullopt;

    std::string sql =
        "SELECT rod_id, bait_id, fish_prestige, prestige_tokens, "
        "total_catches, total_weight, total_earned, rarest_catch_name, "
        "rarest_catch_odds, rod_luck_level, rod_cd_level, rod_double_level, "
        "rod_rare_level, active_rod_effect, IFNULL(last_fish_time, ''), "
        "temp_luck_boost, temp_luck_casts, IFNULL(event_tokens, 0), "
        "IFNULL(active_pet_id, 'none'), IFNULL(perm_luck, 0.0), "
        "IFNULL(perm_val_mult, 1.0), IFNULL(perm_wt_mult, 1.0), "
        "IFNULL(perm_cd_reduction, 0) FROM user_fishing WHERE user_id = '" +
        escape_string(user_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          FishingUser u;
          u.user_id = user_id;
          u.rod_id = row[0] ? row[0] : "wooden_rod";
          u.bait_id = row[1] ? row[1] : "worm";
          u.fish_prestige = row[2] ? std::stoi(row[2]) : 0;
          u.prestige_tokens = row[3] ? std::stoi(row[3]) : 0;
          u.total_catches = row[4] ? std::stoi(row[4]) : 0;
          u.total_weight = row[5] ? std::stod(row[5]) : 0.0;
          u.total_earned = row[6] ? std::stoll(row[6]) : 0;
          u.rarest_catch_name = row[7] ? row[7] : "None";
          u.rarest_catch_odds = row[8] ? std::stod(row[8]) : 1.0;
          u.rod_luck_level = row[9] ? std::stoi(row[9]) : 0;
          u.rod_cd_level = row[10] ? std::stoi(row[10]) : 0;
          u.rod_double_level = row[11] ? std::stoi(row[11]) : 0;
          u.rod_rare_level = row[12] ? std::stoi(row[12]) : 0;
          u.active_rod_effect = row[13] ? row[13] : "none";
          u.last_fish_time = row[14] ? row[14] : "";
          u.temp_luck_boost = row[15] ? std::stod(row[15]) : 1.0;
          u.temp_luck_casts = row[16] ? std::stoi(row[16]) : 0;
          u.event_tokens = row[17] ? std::stoi(row[17]) : 0;
          u.active_pet_id = row[18] ? row[18] : "none";
          u.perm_luck = row[19] ? std::stod(row[19]) : 0.0;
          u.perm_val_mult = row[20] ? std::stod(row[20]) : 1.0;
          u.perm_wt_mult = row[21] ? std::stod(row[21]) : 1.0;
          u.perm_cd_reduction = row[22] ? std::stoi(row[22]) : 0;
          mysql_free_result(res);
          return u;
        }
        mysql_free_result(res);
      }
    }
    return std::nullopt;
  }

  bool Database::update_fishing_user(const FishingUser &u) {
    ensure_fishing_user(u.user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;

    std::string sql = "UPDATE user_fishing SET "
                      "rod_id = '" +
                      escape_string(u.rod_id) +
                      "', "
                      "bait_id = '" +
                      escape_string(u.bait_id) +
                      "', "
                      "fish_prestige = " +
                      std::to_string(u.fish_prestige) +
                      ", "
                      "prestige_tokens = " +
                      std::to_string(u.prestige_tokens) +
                      ", "
                      "total_catches = " +
                      std::to_string(u.total_catches) +
                      ", "
                      "total_weight = " +
                      std::to_string(u.total_weight) +
                      ", "
                      "total_earned = " +
                      std::to_string(u.total_earned) +
                      ", "
                      "rarest_catch_name = '" +
                      escape_string(u.rarest_catch_name) +
                      "', "
                      "rarest_catch_odds = " +
                      std::to_string(u.rarest_catch_odds) +
                      ", "
                      "rod_luck_level = " +
                      std::to_string(u.rod_luck_level) +
                      ", "
                      "rod_cd_level = " +
                      std::to_string(u.rod_cd_level) +
                      ", "
                      "rod_double_level = " +
                      std::to_string(u.rod_double_level) +
                      ", "
                      "rod_rare_level = " +
                      std::to_string(u.rod_rare_level) +
                      ", "
                      "active_rod_effect = '" +
                      escape_string(u.active_rod_effect) +
                      "', "
                      "temp_luck_boost = " +
                      std::to_string(u.temp_luck_boost) +
                      ", "
                      "temp_luck_casts = " +
                      std::to_string(u.temp_luck_casts) +
                      ", "
                      "event_tokens = " +
                      std::to_string(u.event_tokens) +
                      ", "
                      "active_pet_id = '" +
                      escape_string(u.active_pet_id) +
                      "', "
                      "perm_luck = " +
                      std::to_string(u.perm_luck) +
                      ", "
                      "perm_val_mult = " +
                      std::to_string(u.perm_val_mult) +
                      ", "
                      "perm_wt_mult = " +
                      std::to_string(u.perm_wt_mult) +
                      ", "
                      "perm_cd_reduction = " +
                      std::to_string(u.perm_cd_reduction) +
                      " "
                      "WHERE user_id = '" +
                      escape_string(u.user_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool Database::add_user_pet(const std::string &user_id, const std::string &pet_id) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT IGNORE INTO user_pets (user_id, pet_id) VALUES ('" +
        escape_string(user_id) + "', '" + escape_string(pet_id) + "')";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool Database::has_claimed_fishdex_reward(const std::string &user_id, const std::string &rarity) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "SELECT COUNT(*) FROM user_fishdex_claims WHERE user_id = '" +
                      escape_string(user_id) + "' AND rarity = '" + escape_string(rarity) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        int count = row && row[0] ? std::stoi(row[0]) : 0;
        mysql_free_result(res);
        return count > 0;
      }
    }
    return false;
  }

  bool Database::save_fishdex_claim(const std::string &user_id, const std::string &rarity) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "INSERT INTO user_fishdex_claims (user_id, rarity) VALUES ('" +
                      escape_string(user_id) + "', '" + escape_string(rarity) + "')";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::vector<std::string> Database::get_user_pets(const std::string &user_id) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<std::string> pets;
    if (!mysql_)
      return pets;
    std::string sql = "SELECT pet_id FROM user_pets WHERE user_id = '" +
                      escape_string(user_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          if (row[0])
            pets.push_back(row[0]);
        }
        mysql_free_result(res);
      }
    }
    return pets;
  }

  bool Database::has_user_pet(const std::string &user_id, const std::string &pet_id) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "SELECT 1 FROM user_pets WHERE user_id = '" +
                      escape_string(user_id) + "' AND pet_id = '" +
                      escape_string(pet_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        bool found = (mysql_num_rows(res) > 0);
        mysql_free_result(res);
        return found;
      }
    }
    return false;
  }

  std::optional<FishBossState> Database::get_global_boss(const std::string &boss_type) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return std::nullopt;
    std::string sql = "SELECT boss_type, boss_name, season_theme, current_hp, "
                      "max_hp, req_type, req_progress, req_target FROM "
                      "global_fish_bosses WHERE boss_type = '" +
                      escape_string(boss_type) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          FishBossState b;
          b.boss_type = row[0] ? row[0] : boss_type;
          b.boss_name = row[1] ? row[1] : "";
          b.season_theme = row[2] ? row[2] : "Summer";
          b.current_hp = row[3] ? std::stoll(row[3]) : 100000;
          b.max_hp = row[4] ? std::stoll(row[4]) : 100000;
          b.req_type = row[5] ? row[5] : "catches";
          b.req_progress = row[6] ? std::stoll(row[6]) : 0;
          b.req_target = row[7] ? std::stoll(row[7]) : 1000;
          mysql_free_result(res);
          return b;
        }
        mysql_free_result(res);
      }
    }
    return std::nullopt;
  }

  bool Database::save_global_boss(const FishBossState &b) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT INTO global_fish_bosses (boss_type, boss_name, season_theme, "
        "current_hp, max_hp, req_type, req_progress, req_target) VALUES ('" +
        escape_string(b.boss_type) + "', '" + escape_string(b.boss_name) +
        "', '" + escape_string(b.season_theme) + "', " +
        std::to_string(b.current_hp) + ", " + std::to_string(b.max_hp) + ", '" +
        escape_string(b.req_type) + "', " + std::to_string(b.req_progress) +
        ", " + std::to_string(b.req_target) +
        ") "
        "ON DUPLICATE KEY UPDATE "
        "boss_name = '" +
        escape_string(b.boss_name) +
        "', "
        "season_theme = '" +
        escape_string(b.season_theme) +
        "', "
        "current_hp = " +
        std::to_string(b.current_hp) +
        ", "
        "max_hp = " +
        std::to_string(b.max_hp) +
        ", "
        "req_type = '" +
        escape_string(b.req_type) +
        "', "
        "req_progress = " +
        std::to_string(b.req_progress) +
        ", "
        "req_target = " +
        std::to_string(b.req_target);
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool Database::add_user_boss_contribution(const std::string &user_id,
                                  const std::string &boss_type,
                                  int64_t amount) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT INTO user_boss_contributions (user_id, boss_type, "
        "contribution_amount) VALUES ('" +
        escape_string(user_id) + "', '" + escape_string(boss_type) + "', " +
        std::to_string(amount) +
        ") "
        "ON DUPLICATE KEY UPDATE contribution_amount = contribution_amount + " +
        std::to_string(amount);
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  int64_t Database::get_user_boss_contribution(const std::string &user_id,
                                     const std::string &boss_type) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return 0;
    std::string sql = "SELECT contribution_amount FROM user_boss_contributions "
                      "WHERE user_id = '" +
                      escape_string(user_id) + "' AND boss_type = '" +
                      escape_string(boss_type) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0]) {
          int64_t val = std::stoll(row[0]);
          mysql_free_result(res);
          return val;
        }
        mysql_free_result(res);
      }
    }
    return 0;
  }

  bool Database::update_last_fish_time(const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "UPDATE user_fishing SET last_fish_time = "
                      "CURRENT_TIMESTAMP WHERE user_id = '" +
                      escape_string(user_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  int Database::get_fish_cooldown_remaining(const std::string &user_id,
                                  int cooldown_seconds) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return 0;
    std::string sql =
        "SELECT GREATEST(0, CAST(" + std::to_string(cooldown_seconds) +
        " AS SIGNED) - TIMESTAMPDIFF(SECOND, last_fish_time, "
        "CURRENT_TIMESTAMP)) FROM user_fishing WHERE user_id = '" +
        escape_string(user_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0]) {
          int remaining = std::stoi(row[0]);
          mysql_free_result(res);
          return remaining;
        }
        mysql_free_result(res);
      }
    }
    return 0;
  }

  bool Database::add_fish_to_inventory(const std::string &user_id,
                             const std::string &fish_id,
                             const std::string &fish_name,
                             const std::string &rarity, double odds_percent,
                             double weight, int64_t value) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT INTO user_fish_inventory (user_id, fish_id, fish_name, rarity, "
        "odds_percent, weight, value) VALUES ('" +
        escape_string(user_id) + "', '" + escape_string(fish_id) + "', '" +
        escape_string(fish_name) + "', '" + escape_string(rarity) + "', " +
        std::to_string(odds_percent) + ", " + std::to_string(weight) + ", " +
        std::to_string(value) + ")";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::vector<CaughtFish> Database::get_user_fish_inventory(const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<CaughtFish> list;
    if (!mysql_)
      return list;
    std::string sql =
        "SELECT id, fish_id, fish_name, rarity, odds_percent, weight, value, "
        "IFNULL(caught_at, '') FROM user_fish_inventory WHERE user_id = '" +
        escape_string(user_id) + "' ORDER BY value DESC";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          CaughtFish cf;
          cf.id = std::stoi(row[0]);
          cf.user_id = user_id;
          cf.fish_id = row[1] ? row[1] : "";
          cf.fish_name = row[2] ? row[2] : "";
          cf.rarity = row[3] ? row[3] : "";
          cf.odds_percent = row[4] ? std::stod(row[4]) : 0.0;
          cf.weight = row[5] ? std::stod(row[5]) : 0.0;
          cf.value = row[6] ? std::stoll(row[6]) : 0;
          cf.caught_at = row[7] ? row[7] : "";
          list.push_back(cf);
        }
        mysql_free_result(res);
      }
    }
    return list;
  }

  std::pair<int, int64_t> Database::sell_all_user_fish(const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return {0, 0};

    int count;
    int64_t total_val;
    std::string esc_uid = escape_string(user_id);
    std::string sql_select = "SELECT COUNT(*), IFNULL(SUM(value), 0) FROM "
                             "user_fish_inventory WHERE user_id = '" +
                             esc_uid + "'";
    if (mysql_query(mysql_, sql_select.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          count = row[0] ? std::stoi(row[0]) : 0;
          total_val = row[1] ? std::stoll(row[1]) : 0;
        }
        mysql_free_result(res);
      }
    }

    if (count > 0) {
      std::string sql_del =
          "DELETE FROM user_fish_inventory WHERE user_id = '" + esc_uid + "'";
      mysql_query(mysql_, sql_del.c_str());
      std::string sql_add_coins = "UPDATE users SET wallet = wallet + " +
                                  std::to_string(total_val) +
                                  " WHERE user_id = '" + esc_uid + "'";
      mysql_query(mysql_, sql_add_coins.c_str());
      std::string sql_track_earned =
          "UPDATE user_fishing SET total_earned = total_earned + " +
          std::to_string(total_val) + " WHERE user_id = '" + esc_uid + "'";
      mysql_query(mysql_, sql_track_earned.c_str());
    }
    return {count, total_val};
  }

  std::pair<int, int64_t> Database::sell_user_fish_by_rarity(const std::string &user_id,
                                                   const std::string &rarity) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return {0, 0};

    int count;
    int64_t total_val;
    std::string esc_uid = escape_string(user_id);
    std::string esc_rarity = escape_string(rarity);

    std::string sql_select = "SELECT COUNT(*), IFNULL(SUM(value), 0) FROM "
                             "user_fish_inventory WHERE user_id = '" +
                             esc_uid + "' AND LOWER(rarity) = LOWER('" +
                             esc_rarity + "')";
    if (mysql_query(mysql_, sql_select.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          count = row[0] ? std::stoi(row[0]) : 0;
          total_val = row[1] ? std::stoll(row[1]) : 0;
        }
        mysql_free_result(res);
      }
    }

    if (count > 0) {
      std::string sql_del =
          "DELETE FROM user_fish_inventory WHERE user_id = '" + esc_uid +
          "' AND LOWER(rarity) = LOWER('" + esc_rarity + "')";
      mysql_query(mysql_, sql_del.c_str());
      std::string sql_add_coins = "UPDATE users SET wallet = wallet + " +
                                  std::to_string(total_val) +
                                  " WHERE user_id = '" + esc_uid + "'";
      mysql_query(mysql_, sql_add_coins.c_str());
      std::string sql_track_earned =
          "UPDATE user_fishing SET total_earned = total_earned + " +
          std::to_string(total_val) + " WHERE user_id = '" + esc_uid + "'";
      mysql_query(mysql_, sql_track_earned.c_str());
    }
    return {count, total_val};
  }

  bool Database::transfer_fish(uint64_t fish_id, const std::string &sender_id, const std::string &recipient_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "UPDATE user_fish_inventory SET user_id = '" +
                      escape_string(recipient_id) + "' WHERE id = " +
                      std::to_string(fish_id) + " AND user_id = '" +
                      escape_string(sender_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0 && mysql_affected_rows(mysql_) > 0;
  }

  bool Database::delete_single_fish(const std::string &user_id, uint64_t fish_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "DELETE FROM user_fish_inventory WHERE id = " +
                      std::to_string(fish_id) + " AND user_id = '" +
                      escape_string(user_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0 && mysql_affected_rows(mysql_) > 0;
  }

  int Database::get_bait_count(const std::string &user_id, const std::string &bait_id) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return 0;
    std::string sql = "SELECT quantity FROM user_fish_baits WHERE user_id = '" +
                      escape_string(user_id) + "' AND bait_id = '" +
                      escape_string(bait_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0]) {
          int qty = std::stoi(row[0]);
          mysql_free_result(res);
          return qty;
        }
        mysql_free_result(res);
      }
    }
    return 0;
  }

  bool Database::add_bait(const std::string &user_id, const std::string &bait_id,
                int quantity) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT INTO user_fish_baits (user_id, bait_id, quantity) VALUES ('" +
        escape_string(user_id) + "', '" + escape_string(bait_id) + "', " +
        std::to_string(quantity) +
        ") ON DUPLICATE KEY UPDATE quantity = quantity + " +
        std::to_string(quantity);
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  bool Database::consume_bait(const std::string &user_id, const std::string &bait_id) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "UPDATE user_fish_baits SET quantity = GREATEST(0, "
                      "quantity - 1) WHERE user_id = '" +
                      escape_string(user_id) + "' AND bait_id = '" +
                      escape_string(bait_id) + "'";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::vector<FishBaitEntry> Database::get_user_baits(const std::string &user_id) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<FishBaitEntry> list;
    if (!mysql_)
      return list;
    std::string sql =
        "SELECT bait_id, quantity FROM user_fish_baits WHERE user_id = '" +
        escape_string(user_id) + "' AND quantity > 0";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          if (row[0] && row[1]) {
            list.push_back({row[0], std::stoi(row[1])});
          }
        }
        mysql_free_result(res);
      }
    }
    return list;
  }

  bool Database::unlock_rod(const std::string &user_id, const std::string &rod_id) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql =
        "INSERT IGNORE INTO user_unlocked_rods (user_id, rod_id) VALUES ('" +
        escape_string(user_id) + "', '" + escape_string(rod_id) + "')";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::vector<std::string> Database::get_unlocked_rods(const std::string &user_id) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<std::string> list;
    if (!mysql_)
      return list;
    std::string sql =
        "SELECT rod_id FROM user_unlocked_rods WHERE user_id = '" +
        escape_string(user_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          if (row[0])
            list.push_back(row[0]);
        }
        mysql_free_result(res);
      }
    }
    return list;
  }

  bool Database::has_unlocked_rod(const std::string &user_id, const std::string &rod_id) {
    ensure_fishing_user(user_id);
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;
    std::string sql = "SELECT 1 FROM user_unlocked_rods WHERE user_id = '" +
                      escape_string(user_id) + "' AND rod_id = '" +
                      escape_string(rod_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        bool found = (mysql_num_rows(res) > 0);
        mysql_free_result(res);
        return found;
      }
    }
    return false;
  }

  std::vector<FishingUser> Database::get_top_fishers_by_odds(int limit) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<FishingUser> list;
    if (!mysql_)
      return list;
    std::string sql =
        "SELECT user_id, rod_id, bait_id, fish_prestige, prestige_tokens, "
        "total_catches, total_weight, total_earned, rarest_catch_name, "
        "rarest_catch_odds FROM user_fishing WHERE rarest_catch_name != 'None' "
        "ORDER BY rarest_catch_odds ASC LIMIT " +
        std::to_string(limit);
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          FishingUser u;
          u.user_id = row[0] ? row[0] : "";
          u.rod_id = row[1] ? row[1] : "";
          u.bait_id = row[2] ? row[2] : "";
          u.fish_prestige = row[3] ? std::stoi(row[3]) : 0;
          u.prestige_tokens = row[4] ? std::stoi(row[4]) : 0;
          u.total_catches = row[5] ? std::stoi(row[5]) : 0;
          u.total_weight = row[6] ? std::stod(row[6]) : 0.0;
          u.total_earned = row[7] ? std::stoll(row[7]) : 0;
          u.rarest_catch_name = row[8] ? row[8] : "None";
          u.rarest_catch_odds = row[9] ? std::stod(row[9]) : 1.0;
          list.push_back(u);
        }
        mysql_free_result(res);
      }
    }
    return list;
  }

  std::optional<GeoStats> Database::get_geo_stats(const std::string &user_id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return std::nullopt;

    std::string sql = "SELECT points, correct_countries, exact_cities, games_played, current_streak, max_streak FROM geoguessr_stats WHERE user_id = '" +
                      escape_string(user_id) + "'";
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          GeoStats st;
          st.user_id = user_id;
          st.points = row[0] ? std::stoll(row[0]) : 0;
          st.correct_countries = row[1] ? std::stoi(row[1]) : 0;
          st.exact_cities = row[2] ? std::stoi(row[2]) : 0;
          st.games_played = row[3] ? std::stoi(row[3]) : 0;
          st.current_streak = row[4] ? std::stoi(row[4]) : 0;
          st.max_streak = row[5] ? std::stoi(row[5]) : 0;
          mysql_free_result(res);
          return st;
        }
        mysql_free_result(res);
      }
    }
    return std::nullopt;
  }

  bool Database::update_geo_stats(const std::string &user_id, int64_t pts_earned,
                                  bool country_correct, bool city_correct, bool won) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!mysql_)
      return false;

    int current_streak = 0;
    int max_streak = 0;

    std::string sql_sel = "SELECT current_streak, max_streak FROM geoguessr_stats WHERE user_id = '" +
                          escape_string(user_id) + "'";
    if (mysql_query(mysql_, sql_sel.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
          current_streak = row[0] ? std::stoi(row[0]) : 0;
          max_streak = row[1] ? std::stoi(row[1]) : 0;
        }
        mysql_free_result(res);
      }
    }

    int new_streak = won ? (current_streak + 1) : 0;
    int new_max_streak = std::max(max_streak, new_streak);

    std::string sql = "INSERT INTO geoguessr_stats (user_id, points, correct_countries, exact_cities, games_played, current_streak, max_streak) VALUES ('" +
                      escape_string(user_id) + "', " + std::to_string(pts_earned) + ", " +
                      (country_correct ? "1" : "0") + ", " + (city_correct ? "1" : "0") + ", 1, " +
                      std::to_string(new_streak) + ", " + std::to_string(new_max_streak) + ") " +
                      "ON DUPLICATE KEY UPDATE points = points + " + std::to_string(pts_earned) +
                      ", correct_countries = correct_countries + " + (country_correct ? "1" : "0") +
                      ", exact_cities = exact_cities + " + (city_correct ? "1" : "0") +
                      ", games_played = games_played + 1" +
                      ", current_streak = " + std::to_string(new_streak) +
                      ", max_streak = GREATEST(max_streak, " + std::to_string(new_max_streak) + ")";
    return mysql_query(mysql_, sql.c_str()) == 0;
  }

  std::vector<GeoStats> Database::get_top_geo_leaderboard(int limit) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<GeoStats> list;
    if (!mysql_)
      return list;

    std::string sql = "SELECT user_id, points, correct_countries, exact_cities, games_played, current_streak, max_streak FROM geoguessr_stats ORDER BY points DESC LIMIT " +
                      std::to_string(limit);
    if (mysql_query(mysql_, sql.c_str()) == 0) {
      MYSQL_RES *res = mysql_store_result(mysql_);
      if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
          GeoStats st;
          st.user_id = row[0] ? row[0] : "";
          st.points = row[1] ? std::stoll(row[1]) : 0;
          st.correct_countries = row[2] ? std::stoi(row[2]) : 0;
          st.exact_cities = row[3] ? std::stoi(row[3]) : 0;
          st.games_played = row[4] ? std::stoi(row[4]) : 0;
          st.current_streak = row[5] ? std::stoi(row[5]) : 0;
          st.max_streak = row[6] ? std::stoi(row[6]) : 0;
          list.push_back(st);
        }
        mysql_free_result(res);
      }
    }
    return list;
  }

} // namespace bronx
