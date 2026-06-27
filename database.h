#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <iostream>
#include <mysql.h>
#include <nlohmann/json.hpp>

namespace bronx {

class Database {
private:
    MYSQL* mysql_ = nullptr;
    std::unordered_map<std::string, std::vector<std::string>> cache_;
    std::shared_mutex cache_mutex_;
    std::mutex db_mutex_;

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

        std::cout << "[bronx db] database successfully initialized. loaded " << cache_.size() << " prefix configurations.\n";
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
};

} // namespace bronx
