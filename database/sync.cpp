#include "database.h"
#include <mysql.h>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace bronx {

// ---------------------------------------------------------------------------
// save_server_sync_pairs
// Replaces all existing pairs for (local_server_id, remote_platform) with the
// supplied list in one transaction.
// ---------------------------------------------------------------------------
void Database::save_server_sync_pairs(const std::string &local_server_id,
                                      const std::string &remote_platform,
                                      const std::vector<ServerSyncPair> &pairs) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_) return;

    std::string esc_server  = escape_string(local_server_id);
    std::string esc_plat    = escape_string(remote_platform);

    uint64_t now = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
        .count());

    // Delete old rows for this (server, platform) combination
    std::string del = "DELETE FROM server_sync_pairs "
                      "WHERE local_server_id = '" + esc_server +
                      "' AND remote_platform = '" + esc_plat + "'";
    if (mysql_query(mysql_, del.c_str())) {
        std::cerr << "[bronx db] save_server_sync_pairs delete failed: "
                  << mysql_error(mysql_) << "\n";
        return;
    }

    // Insert each new pair
    for (const auto &p : pairs) {
        std::string esc_local_ch  = escape_string(p.local_channel_id);
        std::string esc_rem_srv   = escape_string(p.remote_server_id);
        std::string esc_rem_ch    = escape_string(p.remote_channel_id);

        std::string ins =
            "INSERT INTO server_sync_pairs "
            "(local_server_id, local_channel_id, remote_platform, "
            " remote_server_id, remote_channel_id, created_at) "
            "VALUES ('" + esc_server  + "', '" + esc_local_ch  + "', '"
                       + esc_plat    + "', '" + esc_rem_srv   + "', '"
                       + esc_rem_ch  + "', " + std::to_string(now) + ") "
            "ON DUPLICATE KEY UPDATE "
            "  remote_server_id  = '" + esc_rem_srv  + "', "
            "  remote_channel_id = '" + esc_rem_ch   + "', "
            "  created_at = "         + std::to_string(now);

        if (mysql_query(mysql_, ins.c_str())) {
            std::cerr << "[bronx db] save_server_sync_pairs insert failed: "
                      << mysql_error(mysql_) << "\n";
        }
    }
}

// ---------------------------------------------------------------------------
// get_server_sync_pairs
// Returns all pairs for a given local server (all platforms).
// ---------------------------------------------------------------------------
std::vector<Database::ServerSyncPair>
Database::get_server_sync_pairs(const std::string &local_server_id) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    std::vector<ServerSyncPair> results;
    if (!mysql_) return results;

    std::string esc_server = escape_string(local_server_id);
    std::string q =
        "SELECT id, local_server_id, local_channel_id, remote_platform, "
        "       remote_server_id, remote_channel_id, created_at "
        "FROM server_sync_pairs "
        "WHERE local_server_id = '" + esc_server + "'";

    if (mysql_query(mysql_, q.c_str()) != 0) {
        std::cerr << "[bronx db] get_server_sync_pairs query failed: "
                  << mysql_error(mysql_) << "\n";
        return results;
    }

    MYSQL_RES *res = mysql_store_result(mysql_);
    if (!res) return results;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        ServerSyncPair p;
        p.id               = row[0] ? std::stoi(row[0]) : 0;
        p.local_server_id  = row[1] ? row[1] : "";
        p.local_channel_id = row[2] ? row[2] : "";
        p.remote_platform  = row[3] ? row[3] : "";
        p.remote_server_id = row[4] ? row[4] : "";
        p.remote_channel_id= row[5] ? row[5] : "";
        p.created_at       = row[6] ? static_cast<uint64_t>(std::stoull(row[6])) : 0;
        results.push_back(std::move(p));
    }
    mysql_free_result(res);
    return results;
}

// ---------------------------------------------------------------------------
// get_server_sync_pair
// Looks up the single pair for a specific local channel + remote platform.
// ---------------------------------------------------------------------------
std::optional<Database::ServerSyncPair>
Database::get_server_sync_pair(const std::string &local_channel_id,
                               const std::string &remote_platform) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_) return std::nullopt;

    std::string esc_ch   = escape_string(local_channel_id);
    std::string esc_plat = escape_string(remote_platform);

    std::string q =
        "SELECT id, local_server_id, local_channel_id, remote_platform, "
        "       remote_server_id, remote_channel_id, created_at "
        "FROM server_sync_pairs "
        "WHERE local_channel_id = '" + esc_ch +
        "'  AND remote_platform = '" + esc_plat + "' LIMIT 1";

    if (mysql_query(mysql_, q.c_str()) != 0) {
        std::cerr << "[bronx db] get_server_sync_pair query failed: "
                  << mysql_error(mysql_) << "\n";
        return std::nullopt;
    }

    MYSQL_RES *res = mysql_store_result(mysql_);
    if (!res) return std::nullopt;

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) { mysql_free_result(res); return std::nullopt; }

    ServerSyncPair p;
    p.id               = row[0] ? std::stoi(row[0]) : 0;
    p.local_server_id  = row[1] ? row[1] : "";
    p.local_channel_id = row[2] ? row[2] : "";
    p.remote_platform  = row[3] ? row[3] : "";
    p.remote_server_id = row[4] ? row[4] : "";
    p.remote_channel_id= row[5] ? row[5] : "";
    p.created_at       = row[6] ? static_cast<uint64_t>(std::stoull(row[6])) : 0;
    mysql_free_result(res);
    return p;
}

// ---------------------------------------------------------------------------
// remove_server_sync_pairs
// Deletes all pairs for (local_server_id, remote_platform).
// ---------------------------------------------------------------------------
void Database::remove_server_sync_pairs(const std::string &local_server_id,
                                        const std::string &remote_platform) {
    std::lock_guard<std::mutex> db_lock(db_mutex_);
    if (!mysql_) return;

    std::string esc_server = escape_string(local_server_id);
    std::string esc_plat   = escape_string(remote_platform);

    std::string q = "DELETE FROM server_sync_pairs "
                    "WHERE local_server_id = '" + esc_server +
                    "'  AND remote_platform = '" + esc_plat + "'";

    if (mysql_query(mysql_, q.c_str())) {
        std::cerr << "[bronx db] remove_server_sync_pairs failed: "
                  << mysql_error(mysql_) << "\n";
    }
}

} // namespace bronx
