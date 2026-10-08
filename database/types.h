#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <mysql.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bronx {

struct AccountLink {
    std::string stoat_id;
    std::string fluxer_id;
    std::string discord_id;
};

struct ActionMessageConfig {
    std::string server_id;
    std::string action_type;           // "ban", "kick", "timeout", "purge"
    std::string message_type = "text"; // "text" or "embed"
    std::string content;
    std::string embed_json;
};

struct ActiveJail {
    std::string server_id;
    std::string user_id;
    std::string roles; // comma-separated roles
    uint64_t jailed_at = 0;
    uint64_t expires_at = 0; // 0 if indefinite
};

struct AntiRaidConfig {
    std::string server_id;
    int joins_threshold_count = 5;
    int joins_threshold_seconds = 60;
    std::string joins_action = "none";
    int spam_threshold_count = 5;
    int spam_threshold_seconds = 2;
    std::string spam_action = "none";
    std::string default_profile_action = "none";
    int mentions_threshold_count = 10;
    int mentions_threshold_messages = 5;
    std::string mentions_action = "none";
};

struct HoneypotConfig {
    std::string server_id;
    std::string channel_id;
    std::string honeypot_message = "hold on!";
    std::string disclaimer_message = "Warning: This channel is a trap. Messaging or reacting here results in immediate action.";
    std::string action = "ban"; // customizable action: jail, warn, kick, ban, log, timeout
    bool message_triggers = true;
    bool react_triggers = false;
    bool enabled = false;
};

struct MultiSpamConfig {
    std::string server_id;
    bool enabled = false;
    std::string action = "timeout"; // timeout, ban, kick, warn, delete, jail, log, none
    int threshold_count = 2;
    int time_window = 60; // seconds, 0 = unlimited
    bool delete_messages = true;
    std::vector<std::string> whitelist; // whitelisted words/phrases ignored during spam detection
};



struct ApplicationPanel {
    std::string server_id;
    std::string name;                // unique panel name per server
    std::string trigger_command;     // e.g. "apply" — "" = disabled
    std::string trigger_emoji;       // reaction emoji — "" = disabled
    std::string modal_channel_id;    // channel where panel embed is posted
    std::string modal_message_id;    // stored so we can watch reactions
    std::string modal_content;       // text above the panel embed
    std::string modal_embed_json;
    std::string output_channel_id;   // where completed apps are posted / "" if ticket mode
    std::string mode = "post";       // "post" | "ticket" | "channel"
    std::string view_roles;          // comma-sep: who can see ticket-mode channels
    std::string view_users;
    std::string view_permissions;
    std::string member_role_id;      // deny role for ticket mode
    std::string naming_scheme = "app-{number}";      // ticket mode: "app-{number}"
    std::string welcome_content;     // ticket mode: message sent on open
    std::string close_emoji = "🔒";        // ticket mode close reaction
    bool require_role = false;       // if true, require_role_id must be held
    std::string require_role_id;
    bool role_to_give = false;
    std::string role_to_give_id;
    std::string end_message;
    std::vector<std::string> questions; // ordered question list (unlimited)
    int app_counter = 0;
};

struct ApplicationSubmission {
    int id = 0;
    std::string server_id;
    std::string panel_name;
    std::string user_id;
    uint64_t submitted_at = 0;
    std::string answers_json;        // JSON array of answers
    std::string status = "pending";  // "pending" | "accepted" | "rejected"
    std::string output_message_id;   // message id in output channel (if post mode)
    std::string ticket_channel_id;   // channel id (if ticket/channel mode)
};

// Scope filter helper (shared by autoresponder and autoreact)
struct AutoScopeFilter {
    std::vector<std::string> allow_roles;
    std::vector<std::string> allow_users;
    std::vector<std::string> allow_channels;
    std::vector<std::string> ignore_roles;
    std::vector<std::string> ignore_users;
    std::vector<std::string> ignore_channels;
};

struct AutoreactConfig {
    int id = 0;
    std::string server_id;
    std::string name;
    std::string keyword;
    std::string match_type = "contains";
    std::string emojis; // comma-separated cleaned emoji IDs / unicode
    AutoScopeFilter filter;
};

struct AutoresponderConfig {
    int id = 0;
    std::string server_id;
    std::string name;
    std::string keyword;
    std::string match_type =
        "contains"; // "contains" | "exact" | "starts" | "ends"
    std::string message_type = "text"; // "text" | "embed"
    std::string content;
    std::string embed_json;
    AutoScopeFilter filter;
    std::string actions; // comma-sep action strings: "timeout:10m,addrole:123,kick,ban,delete"
};

struct CustomCommand {
    int id = 0;
    std::string server_id;
    std::string name;
    std::string description;
    std::string message_type = "text"; // "text" or "embed"
    std::string content;
    std::string embed_json;
    std::string allowed_roles; // legacy comma-separated role IDs
    bool owner_only = false;
    std::string actions; // comma-sep action strings: "timeout:10m,addrole:123,kick,ban,delete"
    AutoScopeFilter filter; // allow/ignore roles, users, channels
};

struct CaughtFish {
    int id = 0;
    std::string user_id;
    std::string fish_id;
    std::string fish_name;
    std::string rarity;
    double odds_percent = 0.0;
    double weight = 0.0;
    int64_t value = 0;
    std::string caught_at;
};

struct CmdHistoryEntry {
    int id;
    std::string command_name;
    std::string user_id;
    std::string channel_id;
    uint64_t created_at; // unix timestamp
};

//  Counting
struct CountingConfig {
    std::string server_id;
    std::string channel_id;          // channel where counting happens
    int64_t current_number = 0;      // next expected number
    std::string last_user_id;        // user who sent the last valid count
    std::string fail_role_id;        // role ID to assign on a failed count
    uint64_t fail_role_duration = 0; // seconds, 0 = permanent
    bool passive =
        false; // if true, silently ignore wrong msgs instead of breaking count
    bool warn_double_count =
        false; // if true, warn on double-count instead of immediately breaking
    int64_t highest_count = 0; // all-time record
};

struct CountingHighlights {
    std::string top_counter_id;
    int64_t top_counter_score = 0;
    std::string top_failer_id;
    int64_t top_failer_score = 0;
    int64_t highest_count = 0;
};

struct CountingStats {
    std::string server_id;
    std::string user_id;
    int64_t correct_counts = 0;
    int64_t failed_counts = 0;
};

struct CustomBaitRow {
    std::string bait_id;
    std::string name;
    int pack_size = 10;
    int64_t cost = 0;
    int token_cost = 0;
    int req_prestige = 0;
    double luck_bonus = 0.0;
    int cd_delta = 0;
    std::string description;
};

struct CustomFishRow {
    std::string fish_id;
    std::string name;
    std::string rarity = "Common";
    double weight_min = 1.0;
    double weight_max = 5.0;
    int64_t val_min = 50;
    int64_t val_max = 150;
    double base_odds = 0.50;
};

struct CustomRodRow {
    std::string rod_id;
    std::string name;
    int64_t cost = 0;
    int token_cost = 0;
    int req_prestige = 0;
    double luck_mult = 1.0;
    int base_cd = 30;
    std::string description;
};

struct CustomShopItem {
    std::string item_id;
    std::string name;
    std::string type = "general";
    int64_t cost = 0;
    std::string description;
    int uses_default = 0;
    int duration_seconds = 0;
};

struct EconomyUser {
    std::string user_id;
    int64_t wallet = 0;
    int64_t bank = 0;
    int64_t bank_limit = 10000;
    int prestige = 0;
    std::string last_beg;
    std::string last_work;
    std::string last_rob;
};

struct FishBaitEntry {
    std::string bait_id;
    int quantity = 0;
};

struct FishBossState {
    std::string boss_type; // "daily", "monthly", "seasonal"
    std::string boss_name;
    std::string season_theme; // "Summer", "Fall", "Winter", "Spring"
    int64_t current_hp = 100000;
    int64_t max_hp = 100000;
    std::string req_type = "catches"; // "catches", "sell_value", "total_weight"
    int64_t req_progress = 0;
    int64_t req_target = 5000;
};

struct FishingUser {
    std::string user_id;
    std::string rod_id = "wooden_rod";
    std::string bait_id = "worm";
    int fish_prestige = 0;
    int prestige_tokens = 0;
    int total_catches = 0;
    double total_weight = 0.0;
    int64_t total_earned = 0;
    std::string rarest_catch_name = "None";
    double rarest_catch_odds = 1.0;
    int rod_luck_level = 0;
    int rod_cd_level = 0;
    int rod_double_level = 0;
    int rod_rare_level = 0;
    std::string active_rod_effect = "none";
    std::string last_fish_time;
    double temp_luck_boost = 1.0;
    int temp_luck_casts = 0;
    int event_tokens = 0;
    std::string active_pet_id = "none";
    double perm_luck = 0.0;
    double perm_val_mult = 1.0;
    double perm_wt_mult = 1.0;
    int perm_cd_reduction = 0;
};

struct GeoStats {
    std::string user_id;
    int64_t points = 0;
    int correct_countries = 0;
    int exact_cities = 0;
    int games_played = 0;
    int current_streak = 0;
    int max_streak = 0;
};

struct GuildLevelingConfig {
    std::string server_id;
    bool enabled = false;
    bool reward_coins = true;
    int coins_per_message = 1;
    int min_xp_per_message = 15;
    int max_xp_per_message = 25;
    int min_message_chars = 5;
    int max_message_chars = 2000;
    int xp_cooldown_seconds = 60;
    std::optional<std::string> announcement_channel;
    bool announce_levelup = true;
    std::string announcement_message = "{name} reached **Level {level}**!";
    bool sticky_roles = true;
};

struct JailConfig {
    std::string server_id;
    std::string jail_role_id;
    std::string jail_channel_id;
};

struct LevelRole {
    int id = 0;
    std::string server_id;
    uint32_t level = 1;
    std::string role_id;
    std::string role_name;
    bool remove_previous = false;
};

struct LoggingEmbed {
    std::string server_id;
    std::string event_type;
    std::string message_type = "embed"; // "text" or "embed"
    std::string content;
    std::string embed_json;
};

struct MasqueradeConfig {
    std::string server_id;
    std::optional<std::string> avatar;
    std::optional<std::string> colour;
    std::optional<std::string> name;
    bool disabled = false;
    bool disable_user = false;
    bool disable_user_name = false;
    bool disable_user_avatar = false;
    bool disable_user_colour = false;
};

struct ModalInfo {
    std::string name;
    int rule_count = 0;
    int message_count = 0;
};

struct ModHistoryEntry {
    int id;
    std::string action;
    std::string moderator_id;
    std::string target_id;
    std::string reason;
    std::string extra_details;
    std::string channel_id;
    uint64_t created_at; // unix timestamp
};

struct ModLogEntry {
    int id;
    std::string action;
    std::string moderator_id;
    std::string target_id;
    std::string reason;
    std::string extra_details;
    std::string created_at; // YYYY-MM-DD HH:MM:SS
};

struct RRRule {
    std::string emoji_id;
    std::string role_id;
    std::string rule_type = "add"; // "add" or "remove"
};

struct RRMessage {
    std::string message_id;
    std::string channel_id;
    std::string server_id;
    std::string modal_name; // empty if scanned from existing msg
    std::string embed_json;
    std::string type = "standard"; // "standard", "panel", "verify"
    std::vector<RRRule> rules;
};

struct RecurringMessage {
    int id = 0;
    std::string server_id;
    std::string channel_id;
    std::string name;                  // user-defined label
    std::string message_type = "text"; // "text" or "embed"
    std::string
        content; // plain-text content / pre-embed text (supports tokens)
    std::string embed_json; // JSON object for embed fields (supports tokens)
    uint64_t interval_seconds = 300; // >= 300 (5 min)
    uint64_t last_sent = 0;          // unix timestamp
    bool owner_tokens = false;       // if true, owner-only tokens are resolved
};

struct Reminder {
    int id;
    std::string user_id;
    std::string channel_id;
    std::string message;
    uint64_t timestamp_set;
    uint64_t timestamp_due;
};

struct RestrictionEntry {
    std::string target_type; // "command" or "module"
    std::string target_name;
    std::string restriction_type; // "allow" or "restrict"
    std::string entity_type;      // "role" or "channel"
    std::string entity_id;
};

struct RobloxLink {
    std::string user_id;
    uint64_t roblox_id = 0;
    std::string roblox_username;
};

struct LastfmLink {
    std::string user_id;
    std::string lastfm_username;
};

struct RssFeed {
    int id = 0;
    std::string server_id;
    std::string url;
    std::string channels; // comma-separated channel IDs
    uint64_t last_checked = 0;
    std::string seen_items;    // JSON array of strings
    int check_interval = 1200; // in seconds, default to 20 minutes (1200)
};

//  Command Aliases
struct ServerCommandAlias {
    std::string server_id;
    std::string alias;  // the short-hand the user types
    std::string target; // the canonical command it maps to
};

struct ServerListing {
    std::string server_id;
    std::string name;
    std::string mode = "disabled"; // "website", "bump", "both", "disabled"
    std::string tags;
    std::string description;
    std::string embed_json;
    std::string invite_url;
    std::string advertising_channel_id;
    std::string status = "pending"; // "pending", "approved", "denied"
    uint64_t last_bumped_at = 0;
    uint64_t created_at = 0;
    int bump_count = 0;
};

struct ServerTotals {
    int total_messages = 0;
    int total_commands = 0;
    int active_users = 0;
};

// Per-event embed config (same flexibility as welcome/logging embeds)
struct SocialEmbedConfig {
    int sub_id = 0;                     // FK → social_subscriptions.id
    std::string event_type;             // "live" | "vod" | "post" | "clip"
    std::string message_type = "embed"; // "text" | "embed"
    std::string content;                // plain-text prefix / mention
    std::string embed_json;             // JSON for the embed builder
};

//  Social Subscriptions
struct SocialSubscription {
    int id = 0;
    std::string server_id;
    std::string
        platform; // "youtube" | "twitch" | "tiktok" | "twitter" | "bluesky"
    std::string channel_handle; // e.g. "@MrBeast", "xqc", "did:plc:..."
    std::string channels;       // comma-separated Revolt channel IDs to post to
    std::string events;         // comma-separated: "live","vod","post" etc.
    uint64_t last_checked = 0;
    std::string seen_items; // JSON array of already-posted IDs
};

// Starboards
struct StarboardConfig {
    std::string server_id;
    std::string board_name;
    std::string channel_id;
    int threshold = 3;
    std::string emojis = "⭐";
    bool allow_self = true;
    bool delete_below_threshold = true;
    bool use_embed = true;
    std::string message_content = "{emoji} **{count}** | {channel} (ID: {message_id})";
    std::string embed_title = "";
    std::string embed_description = "{content}\n\n[Jump to message]({jump_url})";
};

struct StarboardMessage {
    std::string server_id;
    std::string board_name;
    std::string original_message_id;
    std::string starboard_message_id;
};

struct Ticket {
    std::string channel_id;
    std::string server_id;
    std::string user_id;
    int ticket_number = 0;
    std::string welcome_message_id;
    std::string status = "open"; // "open" or "closed"
    std::string panel_name = "default";
};

struct TicketConfig {
    std::string server_id;
    std::string name = "default";
    std::string modal_channel_id;
    std::string modal_message_id;
    std::string modal_content;
    std::string modal_embed_json;
    std::string reaction_emoji = "📩";
    bool input_creation_enabled = false;
    std::string naming_scheme = "ticket-{number}";
    std::string category_id;
    std::string view_roles; // raw comma-separated list of role IDs
    std::string view_users; // raw comma-separated list of user IDs
    std::string
        view_permissions; // raw comma-separated list of Revolt permission names
    std::string ticket_welcome_content;
    std::string ticket_welcome_embed_json;
    std::string ticket_close_emoji = "🔒";
    std::string ticket_close_content =
        "🔒 **Closing ticket...**\nThis channel will be deleted in 5 seconds.";
    std::string member_role_id; // role to DENY in the ticket channel (the
                                // server's @member role)
    int ticket_counter = 0;
};

struct TopChannelEntry {
    std::string channel_id;
    int message_count;
};

struct TopCmdEntry {
    std::string command_name;
    int use_count;
};

struct TopEcoEntry {
    std::string user_id;
    int64_t networth;
};

struct TopServerEntry {
    std::string guild_id;
    int message_count;
};

struct TopUserEntry {
    std::string user_id;
    int message_count;
};

struct Tupper {
    int id;
    std::string user_id;
    std::string name;
    std::string avatar_url;
    std::string prefix;
    std::string suffix;
};

struct TupperProxy {
    std::string message_id;
    std::string user_id;
    std::string tupper_name;
};

struct UserAFK {
    std::string user_id;
    std::string custom_template;
    bool is_afk = false;
    std::string reason;
    int64_t since = 0;
    std::string active_preset = "default";
};

struct UserChannelActivityEntry {
    std::string channel_id;
    int messages = 0;
    int commands_used = 0;
};

struct UserCommandAlias {
    std::string user_id;
    std::string alias;
    std::string target;
};

struct UserEconomyItem {
    std::string item_id;
    int quantity = 0;
    int uses_left = 0;
    uint64_t active_until = 0;
};

struct UserGuildActivityEntry {
    std::string guild_id;
    int messages = 0;
    int commands_used = 0;
};

struct UserMasquerade {
    std::string user_id;
    std::optional<std::string> avatar;
    std::optional<std::string> colour;
    std::optional<std::string> name;
};

struct UserRankCard {
    std::string user_id;
    std::string style = "image";
    std::optional<std::string> bg_color;
    std::optional<std::string> bg_image;
    std::optional<std::string> bar_active;
    std::optional<std::string> bar_dimmed;
    std::optional<std::string> text_color;
    std::optional<std::string> font_name;
};

struct UserXP {
    std::string user_id;
    std::string server_id = "0";
    uint64_t total_xp = 0;
    uint32_t level = 1;
    std::string last_message_xp;
};

struct WebhookCacheEntry {
    std::string id;
    std::string token;
};

struct WelcomeLeaveConfig {
    std::string server_id;
    std::string welcome_channel_id;    // empty/NULL if disabled
    std::string welcome_type = "text"; // "text" or "embed"
    std::string welcome_content;       // markdown message text
    std::string welcome_embed_json;    // JSON object storing builder fields
    std::string leave_channel_id;      // empty/NULL if disabled
    std::string leave_type = "text";   // "text" or "embed"
    std::string leave_content;
    std::string leave_embed_json;
    std::string autorole_role_id;         // empty/NULL if disabled
    std::string welcome_dm_type = "none"; // "none", "text", or "embed"
    std::string welcome_dm_content;
    std::string welcome_dm_embed_json;
};

//  Word Filter
struct WordFilterGroup {
    int id = 0;
    std::string server_id;
    std::string name;                  // group label
    std::vector<std::string> patterns; // words or regex patterns
    bool server_wide = true; // if true, active in all channels by default
    // channels where the group is explicitly enabled (overrides
    // server_wide=false)
    std::vector<std::string> enabled_channels;
    // channels where the group is explicitly disabled (overrides
    // server_wide=true)
    std::vector<std::string> disabled_channels;
};

struct WordFilterWhitelist {
    std::string server_id;
    // entries: plain words, '#'+channel_id, '%'+role_id
    std::vector<std::string> entries;
};

struct XPBlacklistEntry {
    int id = 0;
    std::string server_id;
    std::string target_type; // "channel", "role", "user"
    std::string target_id;
    std::string added_by;
};

struct ServerSyncPair {
    int id = 0;
    std::string local_server_id;
    std::string local_channel_id;
    std::string remote_platform;    // "fluxer", "discord"
    std::string remote_server_id;
    std::string remote_channel_id;
    uint64_t created_at = 0;
};

} // namespace bronx
