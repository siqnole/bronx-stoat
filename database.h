#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <iostream>
#include <optional>
#include <mysql.h>
#include <nlohmann/json.hpp>

namespace bronx {

class Database {
public:
    struct RRRule {
        std::string emoji_id;
        std::string role_id;
    };

    struct RRMessage {
        std::string message_id;
        std::string channel_id;
        std::string server_id;
        std::string modal_name; // empty if scanned from existing msg
        std::vector<RRRule> rules;
    };

    struct ModalInfo {
        std::string name;
        int rule_count = 0;
        int message_count = 0;
    };

private:
    MYSQL* mysql_ = nullptr;
    std::unordered_map<std::string, std::vector<std::string>> cache_;
    std::shared_mutex cache_mutex_;
    std::mutex db_mutex_;

    // Message ID -> RRMessage
    std::unordered_map<std::string, RRMessage> rr_cache_;
    std::shared_mutex rr_cache_mutex_;

    std::string escape_string(const std::string& input) {
        if (!mysql_) return "";
        std::vector<char> buffer(input.size() * 2 + 1);
        mysql_real_escape_string(mysql_, buffer.data(), input.c_str(), input.size());
        return std::string(buffer.data());
    }

public:
    Database() = default;
    
    ~Database() {
        if (mysql_) {
            mysql_close(mysql_);
        }
    }

    bool init(const std::string& host, const std::string& user, const std::string& password,
              const std::string& dbname, unsigned int port) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);

        mysql_ = mysql_init(nullptr);
        if (!mysql_) {
            std::cerr << "[bronx db] mysql_init failed\n";
            return false;
        }

        // Connect to server (without DB name first, to create it if it doesn't exist)
        if (!mysql_real_connect(mysql_, host.c_str(), user.c_str(), password.c_str(), nullptr, port, nullptr, 0)) {
            std::cerr << "[bronx db] connection failed: " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Create Database if it doesn't exist
        std::string create_db = "CREATE DATABASE IF NOT EXISTS " + dbname;
        if (mysql_query(mysql_, create_db.c_str())) {
            std::cerr << "[bronx db] failed to create database '" << dbname << "': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Select the Database
        if (mysql_select_db(mysql_, dbname.c_str())) {
            std::cerr << "[bronx db] failed to select database '" << dbname << "': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Create Table prefixes: JSON column is native in MariaDB and MySQL
        std::string create_table = 
            "CREATE TABLE IF NOT EXISTS prefixes ("
            "  target_type VARCHAR(10) NOT NULL,"
            "  target_id VARCHAR(30) NOT NULL,"
            "  prefixes JSON NOT NULL,"
            "  PRIMARY KEY (target_type, target_id)"
            ")";
        if (mysql_query(mysql_, create_table.c_str())) {
            std::cerr << "[bronx db] failed to create table 'prefixes': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Create Table rr_messages
        std::string create_rr_msgs = 
            "CREATE TABLE IF NOT EXISTS rr_messages ("
            "  message_id VARCHAR(30) NOT NULL,"
            "  channel_id VARCHAR(30) NOT NULL,"
            "  server_id VARCHAR(30) NOT NULL,"
            "  modal_name VARCHAR(50) DEFAULT NULL,"
            "  PRIMARY KEY (message_id)"
            ")";
        if (mysql_query(mysql_, create_rr_msgs.c_str())) {
            std::cerr << "[bronx db] failed to create table 'rr_messages': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Create Table rr_rules
        std::string create_rr_rules = 
            "CREATE TABLE IF NOT EXISTS rr_rules ("
            "  message_id VARCHAR(30) NOT NULL,"
            "  emoji_id VARCHAR(100) NOT NULL,"
            "  role_id VARCHAR(30) NOT NULL,"
            "  PRIMARY KEY (message_id, emoji_id),"
            "  FOREIGN KEY (message_id) REFERENCES rr_messages(message_id) ON DELETE CASCADE"
            ")";
        if (mysql_query(mysql_, create_rr_rules.c_str())) {
            std::cerr << "[bronx db] failed to create table 'rr_rules': " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Load existing prefixes into cache
        std::string select_query = "SELECT target_type, target_id, prefixes FROM prefixes";
        if (mysql_query(mysql_, select_query.c_str())) {
            std::cerr << "[bronx db] failed to load prefixes: " << mysql_error(mysql_) << "\n";
            return false;
        }

        MYSQL_RES* result = mysql_store_result(mysql_);
        if (result) {
            MYSQL_ROW row;
            std::lock_guard<std::shared_mutex> cache_lock(cache_mutex_);
            while ((row = mysql_fetch_row(result))) {
                std::string t_type = row[0];
                std::string t_id = row[1];
                std::string json_str = row[2];

                std::vector<std::string> loaded_prefixes;
                try {
                    auto parsed = nlohmann::json::parse(json_str);
                    if (parsed.is_string()) {
                        loaded_prefixes.push_back(parsed.get<std::string>());
                    } else if (parsed.is_array()) {
                        for (const auto& item : parsed) {
                            if (item.is_string()) {
                                loaded_prefixes.push_back(item.get<std::string>());
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[bronx db] json parse error for target " << t_type << ":" << t_id << ": " << e.what() << "\n";
                }

                if (!loaded_prefixes.empty()) {
                    std::string cache_key = t_type + ":" + t_id;
                    cache_[cache_key] = loaded_prefixes;
                }
            }
            mysql_free_result(result);
        }

        // Load existing reaction messages
        std::string select_rr_msgs = "SELECT message_id, channel_id, server_id, modal_name FROM rr_messages";
        if (mysql_query(mysql_, select_rr_msgs.c_str())) {
            std::cerr << "[bronx db] failed to load rr_messages: " << mysql_error(mysql_) << "\n";
            return false;
        }

        MYSQL_RES* rr_msg_res = mysql_store_result(mysql_);
        if (rr_msg_res) {
            MYSQL_ROW row;
            std::lock_guard<std::shared_mutex> rr_lock(rr_cache_mutex_);
            while ((row = mysql_fetch_row(rr_msg_res))) {
                std::string msg_id = row[0];
                RRMessage msg;
                msg.message_id = msg_id;
                msg.channel_id = row[1];
                msg.server_id = row[2];
                msg.modal_name = row[3] ? row[3] : "";
                rr_cache_[msg_id] = msg;
            }
            mysql_free_result(rr_msg_res);
        }

        // Load existing reaction rules
        std::string select_rr_rules = "SELECT message_id, emoji_id, role_id FROM rr_rules";
        if (mysql_query(mysql_, select_rr_rules.c_str())) {
            std::cerr << "[bronx db] failed to load rr_rules: " << mysql_error(mysql_) << "\n";
            return false;
        }

        MYSQL_RES* rr_rule_res = mysql_store_result(mysql_);
        if (rr_rule_res) {
            MYSQL_ROW row;
            std::lock_guard<std::shared_mutex> rr_lock(rr_cache_mutex_);
            while ((row = mysql_fetch_row(rr_rule_res))) {
                std::string msg_id = row[0];
                std::string emoji = row[1];
                std::string role = row[2];
                
                auto it = rr_cache_.find(msg_id);
                if (it != rr_cache_.end()) {
                    it->second.rules.push_back({emoji, role});
                }
            }
            mysql_free_result(rr_rule_res);
        }

        std::cout << "[bronx db] database successfully initialized.\n"
                  << "           loaded " << cache_.size() << " prefix configurations.\n"
                  << "           loaded " << rr_cache_.size() << " reaction role messages.\n";
        return true;
    }

    std::vector<std::string> get_prefixes(const std::string& target_type, const std::string& target_id) {
        std::shared_lock<std::shared_mutex> lock(cache_mutex_);
        std::string cache_key = target_type + ":" + target_id;
        auto it = cache_.find(cache_key);
        if (it != cache_.end()) {
            return it->second;
        }
        return {};
    }

    bool set_prefixes(const std::string& target_type, const std::string& target_id, const std::vector<std::string>& prefixes) {
        if (prefixes.empty()) {
            return remove_prefixes(target_type, target_id);
        }

        std::lock_guard<std::mutex> db_lock(db_mutex_);

        nlohmann::json j;
        if (prefixes.size() == 1) {
            j = prefixes[0]; // Save as a JSON string
        } else {
            j = prefixes; // Save as a JSON array of strings
        }
        std::string json_str = j.dump();

        std::string esc_type = escape_string(target_type);
        std::string esc_id = escape_string(target_id);
        std::string esc_json = escape_string(json_str);

        // SQL Upsert using ON DUPLICATE KEY UPDATE
        std::string query = 
            "INSERT INTO prefixes (target_type, target_id, prefixes) "
            "VALUES ('" + esc_type + "', '" + esc_id + "', '" + esc_json + "') "
            "ON DUPLICATE KEY UPDATE prefixes = '" + esc_json + "'";

        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to save prefixes: " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Update cache
        {
            std::lock_guard<std::shared_mutex> cache_lock(cache_mutex_);
            std::string cache_key = target_type + ":" + target_id;
            cache_[cache_key] = prefixes;
        }

        return true;
    }

    bool remove_prefixes(const std::string& target_type, const std::string& target_id) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);

        std::string esc_type = escape_string(target_type);
        std::string esc_id = escape_string(target_id);

        std::string query = "DELETE FROM prefixes WHERE target_type = '" + esc_type + "' AND target_id = '" + esc_id + "'";
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to delete prefixes: " << mysql_error(mysql_) << "\n";
            return false;
        }

        // Remove from cache
        {
            std::lock_guard<std::shared_mutex> cache_lock(cache_mutex_);
            std::string cache_key = target_type + ":" + target_id;
            cache_.erase(cache_key);
        }

        return true;
    }

    // --- Reaction Roles Methods ---

    bool add_rr_message(const std::string& message_id, const std::string& channel_id, const std::string& server_id, const std::string& modal_name) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);

        std::string esc_msg = escape_string(message_id);
        std::string esc_ch = escape_string(channel_id);
        std::string esc_srv = escape_string(server_id);
        std::string esc_modal = escape_string(modal_name);

        std::string query = 
            "INSERT INTO rr_messages (message_id, channel_id, server_id, modal_name) "
            "VALUES ('" + esc_msg + "', '" + esc_ch + "', '" + esc_srv + "', " + 
            (modal_name.empty() ? "NULL" : "'" + esc_modal + "'") + ") "
            "ON DUPLICATE KEY UPDATE channel_id = '" + esc_ch + "', server_id = '" + esc_srv + "', modal_name = " + 
            (modal_name.empty() ? "NULL" : "'" + esc_modal + "'");

        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to save rr_message: " << mysql_error(mysql_) << "\n";
            return false;
        }

        {
            std::lock_guard<std::shared_mutex> cache_lock(rr_cache_mutex_);
            auto& cached = rr_cache_[message_id];
            cached.message_id = message_id;
            cached.channel_id = channel_id;
            cached.server_id = server_id;
            cached.modal_name = modal_name;
        }
        return true;
    }

    bool add_rr_rule(const std::string& message_id, const std::string& emoji_id, const std::string& role_id) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);

        std::string esc_msg = escape_string(message_id);
        std::string esc_emoji = escape_string(emoji_id);
        std::string esc_role = escape_string(role_id);

        std::string query = 
            "INSERT INTO rr_rules (message_id, emoji_id, role_id) "
            "VALUES ('" + esc_msg + "', '" + esc_emoji + "', '" + esc_role + "') "
            "ON DUPLICATE KEY UPDATE role_id = '" + esc_role + "'";

        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to save rr_rule: " << mysql_error(mysql_) << "\n";
            return false;
        }

        {
            std::lock_guard<std::shared_mutex> cache_lock(rr_cache_mutex_);
            auto it = rr_cache_.find(message_id);
            if (it != rr_cache_.end()) {
                bool found = false;
                for (auto& rule : it->second.rules) {
                    if (rule.emoji_id == emoji_id) {
                        rule.role_id = role_id;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    it->second.rules.push_back({emoji_id, role_id});
                }
            }
        }
        return true;
    }

    bool remove_rr_rule(const std::string& message_id, const std::string& emoji_id) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);

        std::string esc_msg = escape_string(message_id);
        std::string esc_emoji = escape_string(emoji_id);

        std::string query = "DELETE FROM rr_rules WHERE message_id = '" + esc_msg + "' AND emoji_id = '" + esc_emoji + "'";
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to delete rr_rule: " << mysql_error(mysql_) << "\n";
            return false;
        }

        {
            std::lock_guard<std::shared_mutex> cache_lock(rr_cache_mutex_);
            auto it = rr_cache_.find(message_id);
            if (it != rr_cache_.end()) {
                auto& rules = it->second.rules;
                for (auto r_it = rules.begin(); r_it != rules.end(); ++r_it) {
                    if (r_it->emoji_id == emoji_id) {
                        rules.erase(r_it);
                        break;
                    }
                }
            }
        }
        return true;
    }

    bool delete_rr_message(const std::string& message_id) {
        std::lock_guard<std::mutex> db_lock(db_mutex_);

        std::string esc_msg = escape_string(message_id);

        std::string query = "DELETE FROM rr_messages WHERE message_id = '" + esc_msg + "'";
        if (mysql_query(mysql_, query.c_str())) {
            std::cerr << "[bronx db] failed to delete rr_message: " << mysql_error(mysql_) << "\n";
            return false;
        }

        {
            std::lock_guard<std::shared_mutex> cache_lock(rr_cache_mutex_);
            rr_cache_.erase(message_id);
        }
        return true;
    }

    std::optional<RRMessage> get_rr_message(const std::string& message_id) {
        std::shared_lock<std::shared_mutex> lock(rr_cache_mutex_);
        auto it = rr_cache_.find(message_id);
        if (it != rr_cache_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    std::string get_message_id_by_modal_name(const std::string& server_id, const std::string& modal_name) {
        std::shared_lock<std::shared_mutex> lock(rr_cache_mutex_);
        for (const auto& [msg_id, msg] : rr_cache_) {
            if (msg.server_id == server_id && msg.modal_name == modal_name) {
                return msg_id;
            }
        }
        return "";
    }

    std::vector<std::string> get_message_ids_by_modal(const std::string& server_id, const std::string& modal_name) {
        std::shared_lock<std::shared_mutex> lock(rr_cache_mutex_);
        std::vector<std::string> ids;
        for (const auto& [msg_id, msg] : rr_cache_) {
            if (msg.server_id == server_id && msg.modal_name == modal_name) {
                ids.push_back(msg_id);
            }
        }
        return ids;
    }

    std::vector<ModalInfo> get_server_modals(const std::string& server_id) {
        std::shared_lock<std::shared_mutex> lock(rr_cache_mutex_);
        std::unordered_map<std::string, std::pair<int, int>> modal_counts;
        for (const auto& [msg_id, msg] : rr_cache_) {
            if (msg.server_id == server_id && !msg.modal_name.empty()) {
                auto& p = modal_counts[msg.modal_name];
                p.second++;
                if (p.first == 0) {
                    p.first = msg.rules.size();
                }
            }
        }
        
        std::vector<ModalInfo> result;
        for (const auto& [name, counts] : modal_counts) {
            result.push_back({name, counts.first, counts.second});
        }
        return result;
    }
};

} // namespace bronx
