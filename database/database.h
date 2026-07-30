#pragma once
#include <algorithm>
#include <cmath>
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

#include "types.h"

namespace bronx {

class Database {
public:
  Database() = default;

  std::function<void(const std::string &, int64_t, int64_t)> on_balance_update = nullptr;
  std::function<void(const UserAFK &)> on_afk_save = nullptr;

  ~Database() { if (mysql_) mysql_close(mysql_); }

  // Nested type aliases for backward compatibility
  using AccountLink = ::bronx::AccountLink;
  using ActionMessageConfig = ::bronx::ActionMessageConfig;
  using ActiveJail = ::bronx::ActiveJail;
  using AntiRaidConfig = ::bronx::AntiRaidConfig;
  using ApplicationPanel = ::bronx::ApplicationPanel;
  using ApplicationSubmission = ::bronx::ApplicationSubmission;
  using AutoreactConfig = ::bronx::AutoreactConfig;
  using AutoresponderConfig = ::bronx::AutoresponderConfig;
  using AutoScopeFilter = ::bronx::AutoScopeFilter;
  using CaughtFish = ::bronx::CaughtFish;
  using CmdHistoryEntry = ::bronx::CmdHistoryEntry;
  using CountingConfig = ::bronx::CountingConfig;
  using CountingHighlights = ::bronx::CountingHighlights;
  using CountingStats = ::bronx::CountingStats;
  using CustomBaitRow = ::bronx::CustomBaitRow;
  using CustomFishRow = ::bronx::CustomFishRow;
  using CustomRodRow = ::bronx::CustomRodRow;
  using CustomShopItem = ::bronx::CustomShopItem;
  using EconomyUser = ::bronx::EconomyUser;
  using FishBaitEntry = ::bronx::FishBaitEntry;
  using FishBossState = ::bronx::FishBossState;
  using FishingUser = ::bronx::FishingUser;
  using GuildLevelingConfig = ::bronx::GuildLevelingConfig;
  using JailConfig = ::bronx::JailConfig;
  using LevelRole = ::bronx::LevelRole;
  using LoggingEmbed = ::bronx::LoggingEmbed;
  using MasqueradeConfig = ::bronx::MasqueradeConfig;
  using ModalInfo = ::bronx::ModalInfo;
  using ModHistoryEntry = ::bronx::ModHistoryEntry;
  using ModLogEntry = ::bronx::ModLogEntry;
  using RRMessage = ::bronx::RRMessage;
  using RRRule = ::bronx::RRRule;
  using RecurringMessage = ::bronx::RecurringMessage;
  using Reminder = ::bronx::Reminder;
  using RestrictionEntry = ::bronx::RestrictionEntry;
  using RobloxLink = ::bronx::RobloxLink;
  using RssFeed = ::bronx::RssFeed;
  using ServerCommandAlias = ::bronx::ServerCommandAlias;
  using ServerListing = ::bronx::ServerListing;
  using ServerTotals = ::bronx::ServerTotals;
  using SocialEmbedConfig = ::bronx::SocialEmbedConfig;
  using SocialSubscription = ::bronx::SocialSubscription;
  using StarboardConfig = ::bronx::StarboardConfig;
  using StarboardMessage = ::bronx::StarboardMessage;
  using Ticket = ::bronx::Ticket;
  using TicketConfig = ::bronx::TicketConfig;
  using TopChannelEntry = ::bronx::TopChannelEntry;
  using TopCmdEntry = ::bronx::TopCmdEntry;
  using TopEcoEntry = ::bronx::TopEcoEntry;
  using TopServerEntry = ::bronx::TopServerEntry;
  using TopUserEntry = ::bronx::TopUserEntry;
  using Tupper = ::bronx::Tupper;
  using TupperProxy = ::bronx::TupperProxy;
  using UserAFK = ::bronx::UserAFK;
  using UserChannelActivityEntry = ::bronx::UserChannelActivityEntry;
  using UserCommandAlias = ::bronx::UserCommandAlias;
  using UserEconomyItem = ::bronx::UserEconomyItem;
  using UserGuildActivityEntry = ::bronx::UserGuildActivityEntry;
  using UserMasquerade = ::bronx::UserMasquerade;
  using UserRankCard = ::bronx::UserRankCard;
  using UserXP = ::bronx::UserXP;
  using WebhookCacheEntry = ::bronx::WebhookCacheEntry;
  using WelcomeLeaveConfig = ::bronx::WelcomeLeaveConfig;
  using WordFilterGroup = ::bronx::WordFilterGroup;
  using WordFilterWhitelist = ::bronx::WordFilterWhitelist;
  using XPBlacklistEntry = ::bronx::XPBlacklistEntry;

  bool init(const std::string &host, const std::string &user,
            const std::string &password, const std::string &dbname,
            unsigned int port);

  std::string create_dashboard_token(const std::string &user_id,
                                     const std::string &user_name);

  void upsert_server(const std::string &server_id, const std::string &name,
                     const std::string &icon_id = "",
                     const std::string &banner_id = "");

  void upsert_server_member(const std::string &server_id,
                            const std::string &user_id, bool is_owner = false,
                            bool is_admin = false);

  bool is_server_owner(const std::string &server_id, const std::string &user_id);

  void print_cache_stats();

  std::vector<std::string> get_prefixes(const std::string &target_type,
                                        const std::string &target_id);

  bool set_prefixes(const std::string &target_type,
                    const std::string &target_id,
                    const std::vector<std::string> &prefixes);

  bool remove_prefixes(const std::string &target_type,
                       const std::string &target_id);

  bool get_prefix_default_enabled(const std::string &server_id);

  bool set_prefix_default_enabled(const std::string &server_id, bool enabled);

  bool add_rr_message(const std::string &message_id,
                      const std::string &channel_id,
                      const std::string &server_id,
                      const std::string &modal_name,
                      const std::string &type = "standard");

  bool update_rr_message_type(const std::string &message_id,
                              const std::string &type);

  bool update_rr_embed_json(const std::string &message_id,
                            const std::string &embed_json);

  bool add_rr_rule(const std::string &message_id, const std::string &emoji_id,
                   const std::string &role_id,
                   const std::string &rule_type = "add");

  bool remove_rr_rule(const std::string &message_id,
                      const std::string &emoji_id,
                      const std::string &role_id = "");

  bool clear_rr_rules(const std::string &message_id);

  bool delete_rr_message(const std::string &message_id);

  std::optional<RRMessage> get_rr_message(const std::string &message_id);

  std::string get_message_id_by_modal_name(const std::string &server_id,
                                           const std::string &modal_name);

  std::vector<std::string>
  get_message_ids_by_modal(const std::string &server_id,
                           const std::string &modal_name);

  std::vector<ModalInfo> get_server_modals(const std::string &server_id);

  bool increment_user_daily_messages(const std::string &guild_id,
                                     const std::string &user_id, bool is_bot,
                                     const std::string &type = "message");

  bool increment_user_daily_commands(const std::string &guild_id,
                                     const std::string &user_id, bool is_bot);

  bool increment_channel_daily_messages(const std::string &guild_id,
                                        const std::string &channel_id);

  bool increment_user_channel_messages(const std::string &guild_id,
                                       const std::string &channel_id,
                                       const std::string &user_id);

  bool increment_user_channel_commands(const std::string &guild_id,
                                       const std::string &channel_id,
                                       const std::string &user_id);

  bool increment_guild_member_joins(const std::string &guild_id);

  bool increment_guild_member_leaves(const std::string &guild_id);

  std::vector<UserChannelActivityEntry>
  get_user_channel_stats(const std::string &guild_id,
                         const std::string &user_id, int days);

  std::vector<UserGuildActivityEntry>
  get_user_global_stats(const std::string &user_id, int days);

  bool fix_bot_flags(const std::string &user_id);

  bool increment_command_usage(const std::string &guild_id,
                               const std::string &command_name,
                               const std::string &channel_id);

  std::vector<TopUserEntry> get_top_users(const std::string &guild_id, int days,
                                          bool include_bots = false,
                                          int limit = 10);

  std::vector<TopCmdEntry> get_top_commands(const std::string &guild_id,
                                            int days, int limit = 10);

  std::vector<TopCmdEntry> get_top_commands_global(int days, int limit = 10);

  std::vector<TopServerEntry>
  get_top_servers(int days, bool include_bots = false, int limit = 10);

  std::vector<TopUserEntry> get_top_users_global(int days, bool bots,
                                                 int limit = 10);

  std::vector<TopChannelEntry> get_top_channels(const std::string &guild_id,
                                                int days, int limit = 10);

  ServerTotals get_server_totals(const std::string &guild_id, int days,
                                 bool include_bots = false);

  std::vector<std::pair<std::string, int64_t>>
  get_daily_message_series(const std::string &guild_id, int days,
                           bool include_bots = false);

  std::vector<std::vector<double>>
  get_hourly_heatmap(const std::string &guild_id, int days,
                     bool include_bots = false);

  bool ensure_economy_user(const std::string &user_id);

  std::optional<EconomyUser> get_economy_user(const std::string &user_id);

  bool update_balances(const std::string &user_id, int64_t wallet_delta,
                       int64_t bank_delta);

  bool prestige_user(const std::string &user_id, int64_t new_bank_limit);

  bool update_beg_cooldown(const std::string &user_id);

  bool update_work_cooldown(const std::string &user_id);

  bool update_rob_cooldown(const std::string &user_id);

  int get_cooldown_remaining(const std::string &user_id,
                             const std::string &field, int cooldown_seconds);

  std::vector<TopEcoEntry> get_top_economy_users(int limit = 10);

  bool set_user_balances(const std::string &user_id, int64_t wallet,
                         int64_t bank);

  bool set_user_prestige(const std::string &user_id, int prestige_level);

  bool set_fishing_prestige(const std::string &user_id, int prestige_level);

  bool add_user_economy_item(const std::string &user_id,
                             const std::string &item_id, int quantity,
                             int uses, int duration_seconds);

  bool remove_user_economy_item(const std::string &user_id,
                                 const std::string &item_id, int quantity = 1);

  std::vector<UserEconomyItem>
  get_user_economy_items(const std::string &user_id);

  bool is_item_active(const std::string &user_id, const std::string &item_id);

  bool activate_item(const std::string &user_id, const std::string &item_id,
                     int duration_seconds);

  bool use_item_charge(const std::string &user_id, const std::string &item_id);

  bool save_custom_shop_item(const CustomShopItem &item);

  std::vector<CustomShopItem> get_custom_shop_items();

  bool save_custom_rod(const CustomRodRow &r);

  std::vector<CustomRodRow> get_custom_rods();

  bool save_custom_bait(const CustomBaitRow &b);

  std::vector<CustomBaitRow> get_custom_baits();

  bool save_custom_fish(const CustomFishRow &f);

  std::vector<CustomFishRow> get_custom_fish();

  bool ensure_fishing_user(const std::string &user_id);

  std::optional<FishingUser> get_fishing_user(const std::string &user_id);

  bool update_fishing_user(const FishingUser &u);

  bool add_user_pet(const std::string &user_id, const std::string &pet_id);

  bool has_claimed_fishdex_reward(const std::string &user_id, const std::string &rarity);

  bool save_fishdex_claim(const std::string &user_id, const std::string &rarity);

  std::vector<std::string> get_user_pets(const std::string &user_id);

  bool has_user_pet(const std::string &user_id, const std::string &pet_id);

  std::optional<FishBossState> get_global_boss(const std::string &boss_type);

  bool save_global_boss(const FishBossState &b);

  bool add_user_boss_contribution(const std::string &user_id,
                                  const std::string &boss_type,
                                  int64_t amount);

  int64_t get_user_boss_contribution(const std::string &user_id,
                                     const std::string &boss_type);

  bool update_last_fish_time(const std::string &user_id);

  int get_fish_cooldown_remaining(const std::string &user_id,
                                  int cooldown_seconds);

  bool add_fish_to_inventory(const std::string &user_id,
                             const std::string &fish_id,
                             const std::string &fish_name,
                             const std::string &rarity, double odds_percent,
                             double weight, int64_t value);

  std::vector<CaughtFish> get_user_fish_inventory(const std::string &user_id);

  std::pair<int, int64_t> sell_all_user_fish(const std::string &user_id);

  std::pair<int, int64_t> sell_user_fish_by_rarity(const std::string &user_id,
                                                   const std::string &rarity);

  bool transfer_fish(uint64_t fish_id, const std::string &sender_id, const std::string &recipient_id);

  bool delete_single_fish(const std::string &user_id, uint64_t fish_id);

  int get_bait_count(const std::string &user_id, const std::string &bait_id);

  bool add_bait(const std::string &user_id, const std::string &bait_id,
                int quantity);

  bool consume_bait(const std::string &user_id, const std::string &bait_id);

  std::vector<FishBaitEntry> get_user_baits(const std::string &user_id);

  bool unlock_rod(const std::string &user_id, const std::string &rod_id);

  std::vector<std::string> get_unlocked_rods(const std::string &user_id);

  bool has_unlocked_rod(const std::string &user_id, const std::string &rod_id);

  std::vector<FishingUser> get_top_fishers_by_odds(int limit = 10);

  std::optional<MasqueradeConfig>
  get_masquerade_config(const std::string &server_id);

  bool save_masquerade_config(const MasqueradeConfig &cfg);

  bool delete_masquerade_config(const std::string &server_id);

  std::optional<UserMasquerade>
  get_user_masquerade(const std::string &user_id);

  bool save_user_masquerade(const UserMasquerade &um);

  std::optional<std::string> get_user_timezone(const std::string &user_id);

  bool save_user_timezone(const std::string &user_id, const std::string &timezone);

  bool is_user_ignored(const std::string &user_id);

  bool toggle_user_ignored(const std::string &user_id, bool &is_now_ignored);

  std::optional<UserRankCard>
  get_user_rank_card(const std::string &user_id);

  bool save_user_rank_card(const UserRankCard &urc);

  bool delete_user_masquerade(const std::string &user_id);

  std::vector<Tupper> get_user_tuppers(const std::string &user_id);

  bool add_tupper(const std::string &user_id, const std::string &name,
                  const std::string &avatar_url, const std::string &prefix = "",
                  const std::string &suffix = "");

  bool remove_tupper(const std::string &user_id, const std::string &name);

  bool update_tupper_avatar(const std::string &user_id, const std::string &name,
                            const std::string &avatar_url);

  bool update_tupper_brackets(const std::string &user_id,
                              const std::string &name,
                              const std::string &prefix,
                              const std::string &suffix);

  bool rename_tupper(const std::string &user_id, const std::string &old_name,
                     const std::string &new_name);

  bool add_tupper_proxy(const std::string &message_id,
                        const std::string &user_id,
                        const std::string &tupper_name);

  std::optional<TupperProxy> get_tupper_proxy(const std::string &message_id);

  bool delete_tupper_proxy(const std::string &message_id);

  std::optional<WelcomeLeaveConfig>
  get_welcome_leave_config(const std::string &server_id);

  bool save_welcome_leave_config(const WelcomeLeaveConfig &wl);

  std::string get_logging_channel(const std::string &server_id,
                                  const std::string &event_type);

  std::unordered_map<std::string, std::string>
  get_server_logging_config(const std::string &server_id);

  std::optional<AntiRaidConfig>
  get_antiraid_config(const std::string &server_id);

  bool save_antiraid_config(const AntiRaidConfig &ar);

  bool save_logging_channel(const std::string &server_id,
                            const std::string &event_type,
                            const std::string &channel_id);

  bool remove_logging_channel(const std::string &server_id,
                              const std::string &event_type);

  std::optional<LoggingEmbed> get_logging_embed(const std::string &server_id,
                                                const std::string &event_type);

  bool save_logging_embed(const LoggingEmbed &le);

  bool remove_logging_embed(const std::string &server_id,
                            const std::string &event_type);

  std::optional<std::pair<std::string, std::string>>
  get_logging_webhook(const std::string &channel_id);

  bool save_logging_webhook(const std::string &channel_id,
                            const std::string &webhook_id,
                            const std::string &webhook_token);

  bool remove_logging_webhook(const std::string &channel_id);

  bool is_trusted(const std::string &server_id, const std::string &user_id,
                  const std::vector<std::string> &role_ids);

  std::vector<std::pair<std::string, std::string>>
  get_trusted(const std::string &server_id);

  bool add_trusted(const std::string &server_id, const std::string &type,
                   const std::string &id);

  bool remove_trusted(const std::string &server_id, const std::string &type,
                      const std::string &id);

  std::vector<RestrictionEntry>
  get_restrictions(const std::string &server_id, const std::string &target_type,
                   const std::string &target_name);

  bool add_restriction(const std::string &server_id,
                       const std::string &target_type,
                       const std::string &target_name,
                       const std::string &restriction_type,
                       const std::string &entity_type,
                       const std::string &entity_id);

  bool remove_restriction(const std::string &server_id,
                          const std::string &target_type,
                          const std::string &target_name,
                          const std::string &restriction_type,
                          const std::string &entity_type,
                          const std::string &entity_id);

  bool is_disabled(const std::string &server_id,
                   const std::string &target_type,
                   const std::string &target_name);

  std::vector<std::pair<std::string, std::string>>
  get_disabled_items(const std::string &server_id);

  bool disable_item(const std::string &server_id,
                    const std::string &target_type,
                    const std::string &target_name);

  bool enable_item(const std::string &server_id, const std::string &target_type,
                   const std::string &target_name);

  bool add_reminder(const std::string &user_id, const std::string &channel_id,
                    const std::string &message, uint64_t timestamp_set,
                    uint64_t timestamp_due);

  std::vector<Reminder> get_due_reminders(uint64_t current_timestamp);

  std::vector<Reminder> get_user_reminders(const std::string &user_id);

  bool delete_reminder(int id);

  std::optional<ActionMessageConfig>
  get_action_message(const std::string &server_id,
                     const std::string &action_type);

  bool save_action_message(const ActionMessageConfig &config);

  bool remove_action_message(const std::string &server_id,
                             const std::string &action_type);

  std::vector<ActionMessageConfig>
  list_action_messages(const std::string &server_id);

  std::optional<JailConfig> get_jail_config(const std::string &server_id);

  bool save_jail_config(const JailConfig &cfg);

  std::optional<ActiveJail> get_active_jail(const std::string &server_id,
                                            const std::string &user_id);

  bool add_active_jail(const ActiveJail &jail);

  bool remove_active_jail(const std::string &server_id,
                          const std::string &user_id);

  std::vector<ActiveJail> get_expired_jails(uint64_t now_unix);

  bool record_moderation_action(const std::string &server_id,
                                const std::string &action,
                                const std::string &moderator_id,
                                const std::string &target_id,
                                const std::string &reason,
                                const std::string &extra_details,
                                const std::string &channel_id);

  bool record_command_usage(const std::string &server_id,
                            const std::string &command_name,
                            const std::string &user_id,
                            const std::string &channel_id);

  std::vector<CmdHistoryEntry> get_server_command_history_by_user(const std::string &server_id,
                                                                   const std::string &user_id,
                                                                   int offset,
                                                                   int limit,
                                                                   int &total_count);

  std::vector<CmdHistoryEntry> get_server_command_history_by_command(const std::string &server_id,
                                                                      const std::string &command_name,
                                                                      int offset,
                                                                      int limit,
                                                                      int &total_count);

  std::vector<ModHistoryEntry> get_server_mod_history(const std::string &server_id,
                                                       const std::string &user_id,
                                                       int offset,
                                                       int limit,
                                                       int &total_count);

  bool delete_moderation_action(const std::string &server_id, int id);

  std::vector<ModLogEntry> get_mod_logs(const std::string &server_id,
                                        const std::string &target_id = "",
                                        int limit = 50);

  std::vector<ModLogEntry> get_mod_actions(const std::string &server_id,
                                           const std::string &action,
                                           int limit = 50);

  bool recurring_channel_occupied(const std::string &channel_id);

  int count_recurring_for_server(const std::string &server_id);

  std::vector<RecurringMessage>
  get_recurring_messages_for_server(const std::string &server_id);

  std::optional<RecurringMessage>
  get_recurring_by_channel(const std::string &channel_id);

  std::optional<RecurringMessage>
  get_recurring_by_id(int id, const std::string &server_id);

  std::optional<RecurringMessage>
  get_recurring_by_name(const std::string &server_id, const std::string &name);

  std::vector<RecurringMessage> get_due_recurring_messages(uint64_t now);

  bool add_recurring_message(RecurringMessage &rm);

  bool remove_recurring_message(int id, const std::string &server_id);

  bool update_recurring_message(const RecurringMessage &rm);

  bool update_recurring_last_sent(int id, uint64_t ts);

  std::optional<RssFeed> get_rss_by_id(int id, const std::string &server_id);

  std::optional<RssFeed> get_rss_by_url(const std::string &server_id,
                                        const std::string &url);

  std::vector<RssFeed> get_rss_feeds_for_server(const std::string &server_id);

  std::vector<RssFeed> get_all_rss_feeds();

  bool add_rss_feed(RssFeed &feed);

  bool remove_rss_feed(int id, const std::string &server_id);

  bool update_rss_feed_channels(int id, const std::string &server_id,
                                const std::string &channels);

  bool update_rss_feed_check(int id, uint64_t last_checked,
                             const std::string &seen_items);

  bool update_rss_feed_interval(int id, int interval);

  static std::string json_arr_str(const std::vector<std::string> &v);

  std::vector<AutoresponderConfig>
  get_autoresponders_for_server(const std::string &server_id);

  std::optional<AutoresponderConfig>
  get_autoresponder_by_id(int id, const std::string &server_id);

  std::optional<AutoresponderConfig>
  get_autoresponder_by_name(const std::string &server_id,
                            const std::string &name);

  bool add_autoresponder(AutoresponderConfig &c);

  bool remove_autoresponder(int id, const std::string &server_id);

  bool update_autoresponder(const AutoresponderConfig &c);

  std::vector<AutoreactConfig>
  get_autoreacts_for_server(const std::string &server_id);

  std::optional<AutoreactConfig>
  get_autoreact_by_id(int id, const std::string &server_id);

  std::optional<AutoreactConfig>
  get_autoreact_by_name(const std::string &server_id, const std::string &name);

  bool add_autoreact(AutoreactConfig &c);

  bool remove_autoreact(int id, const std::string &server_id);

  bool update_autoreact(const AutoreactConfig &c);

  bool update_autoresponder_filter(int id, const std::string &server_id,
                                   const AutoScopeFilter &f);

  bool update_autoreact_filter(int id, const std::string &server_id,
                               const AutoScopeFilter &f);

  std::optional<TicketConfig>
  get_ticket_config(const std::string &server_id,
                    const std::string &name = "default");

  bool save_ticket_config(const TicketConfig &config);

  bool delete_ticket_config(const std::string &server_id, const std::string &name);

  std::optional<Ticket> get_ticket(const std::string &channel_id);

  bool save_ticket(const Ticket &ticket);

  bool delete_ticket(const std::string &channel_id);

  std::vector<Ticket> get_server_tickets(const std::string &server_id);

  std::optional<Ticket> get_user_open_ticket(const std::string &server_id,
                                             const std::string &user_id);

  std::vector<TicketConfig>
  get_ticket_configs_for_server(const std::string &server_id);

  std::vector<TicketConfig> get_all_ticket_configs();

  std::vector<Ticket> get_all_tickets();

  std::optional<ApplicationPanel> get_application_panel(const std::string &server_id, const std::string &name);

  bool save_application_panel(const ApplicationPanel &panel);

  bool delete_application_panel(const std::string &server_id, const std::string &name);

  std::vector<ApplicationPanel> get_application_panels_for_server(const std::string &server_id);

  std::vector<ApplicationPanel> get_all_application_panels();

  bool save_application_submission(ApplicationSubmission &sub);

  std::optional<ApplicationSubmission> get_application_submission_by_message_id(const std::string &message_id);

  std::optional<ApplicationSubmission> get_application_submission_by_id(int id);

  std::optional<ApplicationSubmission> get_application_submission_by_channel_id(const std::string &channel_id);

  std::vector<ApplicationSubmission> get_application_submissions(const std::string &server_id, const std::string &panel_name = "", int limit = 50);

  std::vector<ApplicationSubmission> get_user_application_submissions(const std::string &user_id);

  bool update_application_submission_status(int id, const std::string &status);

  std::optional<ServerListing> get_server_listing(const std::string &server_id);

  bool save_server_listing(const ServerListing &listing);

  bool delete_server_listing(const std::string &server_id);

  std::vector<ServerListing> get_approved_server_listings();

  std::vector<ServerListing> get_all_server_listings();

  std::optional<CountingConfig>
  get_counting_config(const std::string &server_id);

  bool save_counting_config(const CountingConfig &cfg);

  void counting_increment_correct(const std::string &server_id,
                                  const std::string &user_id);

  void counting_increment_failed(const std::string &server_id,
                                 const std::string &user_id);

  std::vector<CountingStats>
  get_counting_leaderboard(const std::string &server_id, int limit = 10);

  CountingStats get_counting_stats(const std::string &server_id,
                                   const std::string &user_id);

  CountingHighlights get_counting_highlights(const std::string &server_id);

  std::optional<UserAFK> get_user_afk(const std::string &user_id);

  bool save_user_afk(const UserAFK &afk);

  std::string get_user_afk_preset(const std::string &user_id,
                                  const std::string &preset_name);

  bool save_user_afk_preset(const std::string &user_id,
                            const std::string &preset_name,
                            const std::string &template_json);

  bool delete_user_afk_preset(const std::string &user_id,
                              const std::string &preset_name);

  std::vector<std::string>
  get_user_afk_presets_list(const std::string &user_id);

  std::optional<RobloxLink> get_roblox_link(const std::string &user_id);

  std::optional<RobloxLink>
  get_roblox_link_by_roblox_username(const std::string &username);

  std::optional<RobloxLink> get_roblox_link_by_roblox_id(uint64_t roblox_id);

  bool save_roblox_link(const std::string &user_id, uint64_t roblox_id,
                        const std::string &roblox_username);

  bool delete_roblox_link(const std::string &user_id);

  bool save_account_link(const std::string &stoat_id,
                         const std::string &fluxer_id,
                         const std::string &discord_id);

  std::optional<AccountLink> get_account_link(const std::string &platform,
                                              const std::string &id);

  std::vector<SocialSubscription> get_all_social_subs();

  std::vector<SocialSubscription>
  get_social_subs_for_server(const std::string &server_id);

  std::optional<SocialSubscription>
  get_social_sub_by_id(int id, const std::string &server_id);

  bool add_social_sub(SocialSubscription &sub);

  bool remove_social_sub(int id, const std::string &server_id);

  bool update_social_sub_channels(int id, const std::string &server_id,
                                  const std::string &channels);

  bool update_social_sub_events(int id, const std::string &server_id,
                                const std::string &events);

  bool update_social_sub_check(int id, uint64_t last_checked,
                               const std::string &seen_items);

  std::optional<SocialEmbedConfig>
  get_social_embed(int sub_id, const std::string &event_type);

  bool save_social_embed(const SocialEmbedConfig &cfg);

  bool delete_social_embed(int sub_id, const std::string &event_type);

  std::unordered_map<std::string, std::string>
  get_server_aliases(const std::string &server_id);

  bool set_server_alias(const std::string &server_id, const std::string &alias,
                        const std::string &target);

  bool delete_server_alias(const std::string &server_id,
                           const std::string &alias);

  std::unordered_map<std::string, std::string>
  get_user_aliases(const std::string &user_id);

  bool set_user_alias(const std::string &user_id, const std::string &alias,
                      const std::string &target);

  bool delete_user_alias(const std::string &user_id, const std::string &alias);

  bool get_user_aliases_disabled(const std::string &server_id);

  bool set_user_aliases_disabled(const std::string &server_id, bool disabled);

  std::string resolve_alias(const std::string &server_id,
                            const std::string &user_id,
                            const std::string &cmd);

  static std::vector<std::string> parse_json_str_arr(const char *raw);

  bool filter_create_group(const std::string &server_id,
                           const std::string &name);

  bool filter_delete_group(const std::string &server_id,
                           const std::string &name);

  std::vector<WordFilterGroup>
  filter_list_groups(const std::string &server_id);

  std::optional<WordFilterGroup> filter_get_group(const std::string &server_id,
                                                  const std::string &name);

  bool filter_add_pattern(const std::string &server_id, const std::string &name,
                          const std::string &pattern);

  bool filter_remove_pattern(const std::string &server_id,
                             const std::string &name,
                             const std::string &pattern);

  bool filter_toggle(const std::string &server_id, const std::string &name,
                     const std::string &channel_id, bool enable);

  static bool filter_group_active_in(const WordFilterGroup &g,
                                     const std::string &channel_id);

  WordFilterWhitelist filter_get_whitelist(const std::string &server_id);

  bool filter_whitelist_add(const std::string &server_id,
                            const std::string &entry);

  bool filter_whitelist_remove(const std::string &server_id,
                               const std::string &entry);

  static uint32_t calculate_level_from_xp(uint64_t xp);

  static uint64_t calculate_xp_for_level(uint32_t level);

  static uint64_t calculate_xp_for_next_level(uint32_t current_level);

  std::optional<GuildLevelingConfig>
  get_guild_leveling_config(const std::string &server_id);

  bool create_guild_leveling_config(const std::string &server_id);

  bool update_guild_leveling_config(const GuildLevelingConfig &cfg);

  void invalidate_guild_leveling_config_cache(const std::string &server_id);

  std::vector<LevelRole> get_level_roles(const std::string &server_id);

  std::optional<LevelRole> get_level_role_at_level(const std::string &server_id,
                                                   uint32_t level);

  bool create_level_role(const LevelRole &role);

  bool delete_level_role(const std::string &server_id, uint32_t level);

  std::vector<XPBlacklistEntry> get_xp_blacklist(const std::string &server_id);

  bool add_xp_blacklist(const XPBlacklistEntry &entry);

  bool remove_xp_blacklist(const std::string &server_id,
                           const std::string &target_type,
                           const std::string &target_id);

  bool is_xp_blocked(const std::string &server_id,
                     const std::string &channel_id,
                     const std::string &user_id,
                     const std::vector<std::string> &role_ids);

  std::optional<UserXP> get_user_xp(const std::string &user_id,
                                    const std::string &server_id);

  bool add_xp(const std::string &user_id, uint64_t xp_amount,
              uint32_t &new_level, bool &leveled_up,
              const std::string &server_id);

  bool reset_guild_xp(const std::string &server_id);

  bool reset_user_guild_xp(const std::string &user_id,
                           const std::string &server_id);

  std::vector<UserXP> get_server_xp_leaderboard(const std::string &server_id,
                                                int limit);

  int get_user_server_xp_rank(const std::string &user_id,
                              const std::string &server_id);

  bool create_starboard(const std::string &server_id, const std::string &board_name, const std::string &channel_id = "");

  bool delete_starboard(const std::string &server_id, const std::string &board_name);

  bool set_starboard_threshold(const std::string &server_id, const std::string &board_name, int threshold);

  bool set_starboard_channel(const std::string &server_id, const std::string &board_name, const std::string &channel_id);

  bool set_starboard_emojis(const std::string &server_id, const std::string &board_name, const std::string &emojis);

  bool set_starboard_self(const std::string &server_id, const std::string &board_name, bool allow_self);

  bool set_starboard_delete_below_threshold(const std::string &server_id, const std::string &board_name, bool enabled);

  bool set_starboard_message_config(const std::string &server_id, const std::string &board_name, const std::string &field, const std::string &value);

  bool set_starboard_embed_enabled(const std::string &server_id, const std::string &board_name, bool enabled);

  std::vector<StarboardConfig> get_starboards(const std::string &server_id);

  std::optional<StarboardConfig> get_starboard(const std::string &server_id, const std::string &board_name);

  bool add_starboard_message(const std::string &server_id, const std::string &board_name, const std::string &orig_msg_id, const std::string &star_msg_id);

  std::string get_starboard_message(const std::string &server_id, const std::string &board_name, const std::string &orig_msg_id);

  bool delete_starboard_message(const std::string &server_id, const std::string &board_name, const std::string &orig_msg_id);

  void purge_server_data(const std::string& server_id);

private:
  MYSQL *mysql_ = nullptr;
  std::unordered_map<std::string, UserAFK> user_afk_cache_;
  std::shared_mutex user_afk_cache_mutex_;

  std::unordered_map<std::string, RobloxLink> roblox_links_cache_;
  std::shared_mutex roblox_links_cache_mutex_;

  std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
      user_afk_presets_cache_;
  std::shared_mutex user_afk_presets_cache_mutex_;

  std::unordered_map<std::string, std::vector<std::string>> cache_;
  std::shared_mutex cache_mutex_;
  std::mutex db_mutex_;

  // Message ID -> RRMessage
  std::unordered_map<std::string, RRMessage> rr_cache_;
  std::shared_mutex rr_cache_mutex_;

  // Server ID -> WelcomeLeaveConfig
  std::unordered_map<std::string, WelcomeLeaveConfig> wl_cache_;
  std::shared_mutex wl_cache_mutex_;

  // Server ID -> AntiRaidConfig
  std::unordered_map<std::string, AntiRaidConfig> ar_cache_;
  std::shared_mutex ar_cache_mutex_;

  // Server ID -> {Event Type -> Channel ID}
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
      log_cache_;
  std::shared_mutex log_cache_mutex_;

  // Server ID -> TicketConfig
  std::unordered_map<std::string, TicketConfig> ticket_config_cache_;
  std::shared_mutex ticket_config_cache_mutex_;

  // Server ID + ":" + Panel Name -> ApplicationPanel
  std::unordered_map<std::string, ApplicationPanel> app_panel_cache_;
  std::shared_mutex app_panel_cache_mutex_;

  // Channel ID -> Ticket
  std::unordered_map<std::string, Ticket> ticket_cache_;
  std::shared_mutex ticket_cache_mutex_;

  // Server ID -> CountingConfig
  std::unordered_map<std::string, CountingConfig> counting_cache_;
  std::shared_mutex counting_cache_mutex_;

  // server_id:user_id -> CountingStats
  std::unordered_map<std::string, CountingStats> counting_stats_cache_;
  std::shared_mutex counting_stats_cache_mutex_;

  // Server ID -> {Event Type -> LoggingEmbed}
  std::unordered_map<std::string, std::unordered_map<std::string, LoggingEmbed>>
      log_embed_cache_;
  std::shared_mutex log_embed_cache_mutex_;

  // Server ID -> {Action Type -> ActionMessageConfig}
  std::unordered_map<std::string,
                     std::unordered_map<std::string, ActionMessageConfig>>
      action_msg_cache_;
  std::shared_mutex action_msg_cache_mutex_;

  std::unordered_map<std::string, WebhookCacheEntry> webhook_cache_;
  std::shared_mutex webhook_cache_mutex_;

  // id -> RecurringMessage
  std::unordered_map<int, RecurringMessage> rm_cache_;
  // server_id -> list of IDs
  std::unordered_map<std::string, std::vector<int>> rm_server_index_;
  // channel_id -> id (for uniqueness)
  std::unordered_map<std::string, int> rm_channel_index_;
  std::shared_mutex rm_cache_mutex_;

  // id -> RssFeed
  std::unordered_map<int, RssFeed> rss_cache_;
  // server_id -> list of IDs
  std::unordered_map<std::string, std::vector<int>> rss_server_index_;
  std::shared_mutex rss_cache_mutex_;

  // id -> SocialSubscription
  std::unordered_map<int, SocialSubscription> social_sub_cache_;
  std::unordered_map<std::string, std::vector<int>> social_sub_server_index_;
  std::shared_mutex social_sub_cache_mutex_;

  // sub_id:event_type -> SocialEmbedConfig
  std::unordered_map<std::string, SocialEmbedConfig> social_embed_cache_;
  std::shared_mutex social_embed_cache_mutex_;

  // server_id -> { alias -> target }
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
      server_alias_cache_;
  std::shared_mutex server_alias_cache_mutex_;

  // user_id -> { alias -> target }
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
      user_alias_cache_;
  std::shared_mutex user_alias_cache_mutex_;

  // server_id -> { board_name -> StarboardConfig }
  std::unordered_map<std::string, std::unordered_map<std::string, StarboardConfig>> starboard_configs_cache_;
  std::shared_mutex starboard_configs_cache_mutex_;

  // server_id -> { board_name -> { original_message_id -> starboard_message_id } }
  std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<std::string, std::string>>> starboard_messages_cache_;
  std::shared_mutex starboard_messages_cache_mutex_;

  // server_id -> user aliases disabled?
  std::unordered_map<std::string, bool> server_alias_settings_cache_;
  std::shared_mutex server_alias_settings_cache_mutex_;

  // id -> AutoresponderConfig
  std::unordered_map<int, AutoresponderConfig> arsp_cache_;
  std::unordered_map<std::string, std::vector<int>> arsp_server_index_;
  std::shared_mutex arsp_cache_mutex_;

  // id -> AutoreactConfig
  std::unordered_map<int, AutoreactConfig> arct_cache_;
  std::unordered_map<std::string, std::vector<int>> arct_server_index_;
  std::shared_mutex arct_cache_mutex_;

  // Server ID -> Set of "Type:ID"
  std::unordered_map<std::string, std::unordered_set<std::string>>
      trusted_cache_;
  std::shared_mutex trusted_cache_mutex_;

  // Server ID -> List of restrictions
  std::unordered_map<std::string, std::vector<RestrictionEntry>>
      restrictions_cache_;
  std::shared_mutex restrictions_cache_mutex_;

  // Server ID -> Set of "Type:Name"
  std::unordered_map<std::string, std::unordered_set<std::string>>
      disabled_cache_;
  std::shared_mutex disabled_cache_mutex_;

  // Server ID -> default prefix enabled (true = default prefix active)
  std::unordered_map<std::string, bool> pfx_default_cache_;
  std::shared_mutex pfx_default_cache_mutex_;

  std::unordered_map<std::string, MasqueradeConfig> masquerade_cache_;
  std::shared_mutex masquerade_cache_mutex_;

  std::unordered_map<std::string, UserMasquerade> user_masq_cache_;
  std::shared_mutex user_masq_cache_mutex_;

  std::unordered_map<std::string, std::string> user_timezones_cache_;
  std::shared_mutex user_timezones_cache_mutex_;

  std::unordered_set<std::string> ignored_users_cache_;
  std::shared_mutex ignored_users_cache_mutex_;

  std::unordered_map<std::string, UserRankCard> user_rank_card_cache_;
  std::shared_mutex user_rank_card_cache_mutex_;

  std::unordered_map<std::string, std::vector<Tupper>> tupper_cache_;
  std::shared_mutex tupper_cache_mutex_;

  // Leveling caches
  std::unordered_map<std::string, GuildLevelingConfig> leveling_config_cache_;
  std::shared_mutex leveling_config_cache_mutex_;

  std::unordered_map<std::string, std::vector<LevelRole>> level_roles_cache_;
  std::shared_mutex level_roles_cache_mutex_;

  std::unordered_map<std::string, std::vector<XPBlacklistEntry>>
      xp_blacklist_cache_;
  std::shared_mutex xp_blacklist_cache_mutex_;

  std::string escape_string(const std::string &input) {
    if (!mysql_)
      return "";
    std::vector<char> buffer(input.size() * 2 + 1);
    mysql_real_escape_string(mysql_, buffer.data(), input.c_str(),
                             input.size());
    return std::string(buffer.data());
  }
};

} // namespace bronx
