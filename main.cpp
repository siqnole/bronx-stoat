#include "stoatpp/bot_module.h"
#include "stoatpp/cluster.h"
#include "stoatpp/models/permissions.h"
#include "stoatpp/utils/ulid.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "emojis.h"

// Include our Cogs
#include "cogs/afk.h"
#include "cogs/alias.h"
#include "cogs/antiraid.h"
#include "cogs/applications.h"
#include "cogs/auto.h"
#include "cogs/blackjack.h"
#include "cogs/bridge.h"
#include "cogs/captcha.h"
#include "cogs/command_config.h"
#include "cogs/counting.h"
#include "cogs/economy.h"
#include "cogs/emoji.h"
#include "cogs/filter.h"
#include "cogs/fishing.h"
#include "cogs/games.h"
#include "cogs/geoguessr.h"
#include "cogs/general.h"
#include "cogs/help.h"
#include "cogs/history.h"
#include "cogs/honeypot.h"
#include "cogs/lastfm.h"
#include "cogs/leveling.h"
#include "cogs/logging.h"
#include "cogs/masquerade.h"
#include "cogs/media.h"
#include "cogs/moderation.h"
#include "cogs/owner.h"
#include "cogs/prefix.h"
#include "cogs/qte.h"
#include "cogs/reaction_roles.h"
#include "cogs/reminders.h"
#include "cogs/roblox.h"
#include "cogs/roles.h"
#include "cogs/rss.h"
#include "cogs/serverlist.h"
#include "cogs/setup.h"
#include "cogs/snipe.h"
#include "cogs/social.h"
#include "cogs/starboard.h"
#include "cogs/stats.h"
#include "cogs/tickets.h"
#include "cogs/timezone.h"
#include "cogs/language.h"
#include "cogs/tupper.h"
#include "i18n.h"
#include "cogs/users.h"
#include "cogs/welcome_leave.h"
#include "cogs/voice.h"
#include "cogs/imageboard.h"


// Simple self-contained helper to load environment variables from a .env file
void load_env(const std::string &filepath = ".env") {
  std::ifstream file(filepath);
  std::string path_used = filepath;
  if (!file.is_open()) {
    path_used = "../" + filepath;
    file.open(path_used);
  }
  if (!file.is_open()) {
    std::cout << "[bronx env] warning: could not open " << filepath
              << " or parent directory file. falling back to system "
                 "environment variables.\n";
    return;
  }
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#')
      continue;
    auto pos = line.find('=');
    if (pos == std::string::npos)
      continue;

    std::string key = line.substr(0, pos);
    std::string value = line.substr(pos + 1);

    key.erase(0, key.find_first_not_of(" \t\r\n"));
    key.erase(key.find_last_not_of(" \t\r\n") + 1);
    value.erase(0, value.find_first_not_of(" \t\r\n"));
    value.erase(value.find_last_not_of(" \t\r\n") + 1);

    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
      value = value.substr(1, value.size() - 2);
    }

    setenv(key.c_str(), value.c_str(), 1);
  }
  std::cout << "[bronx env] successfully loaded variables from " << path_used
            << "\n";
}

static std::mutex g_invoker_mutex;
static std::unordered_map<std::string, std::string> g_channel_last_invoker;
static std::unordered_map<std::string, std::string> g_server_last_channel;
static std::string g_last_active_channel;

static std::mutex g_handling_mutex;
static std::unordered_set<std::string> g_handling_channels;

// Toggleable verbose logging — when false, only command/error logs are shown.
// Set via --verbose / -v CLI flag. Can also be toggled at runtime.
static std::atomic<bool> g_verbose_logging{false};
static std::atomic<bool> g_bot_ready{false};

bool bot_has_masquerade_permission(stoatpp::cluster &bot,
                                   const std::string &channel_id) {
  auto chan_opt = bot.get_channel(channel_id);
  if (!chan_opt || !chan_opt->server)
    return true;

  std::string server_id = *(chan_opt->server);
  auto server_opt = bot.get_server(server_id);
  if (!server_opt)
    return true;

  if (server_opt->owner == bot.current_user().id) {
    return true;
  }

  auto bot_member_opt = bot.get_member(server_id, bot.current_user().id);

  // 1. Server-level permissions
  int64_t allowed = 0;
  if (server_opt->raw.contains("default_permissions")) {
    const auto &dp = server_opt->raw["default_permissions"];
    if (dp.is_number()) {
      allowed |= dp.get<int64_t>();
    } else if (dp.is_object() && dp.contains("server") &&
               dp["server"].is_number()) {
      allowed |= dp["server"].get<int64_t>();
    }
  }
  if (bot_member_opt) {
    for (const auto &role_id : bot_member_opt->roles) {
      for (const auto &r : server_opt->roles) {
        if (r.id == role_id) {
          allowed |= r.allowed_permissions();
        }
      }
    }
  }

  // 2. Channel default permissions overrides
  int64_t ch_allow = 0;
  int64_t ch_deny = 0;
  if (chan_opt->default_permissions) {
    const auto &dp = *(chan_opt->default_permissions);
    if (dp.is_number()) {
      allowed = dp.get<int64_t>();
    } else if (dp.is_object()) {
      if (dp.contains("a") && dp["a"].is_number())
        ch_allow = dp["a"].get<int64_t>();
      else if (dp.contains("allow") && dp["allow"].is_number())
        ch_allow = dp["allow"].get<int64_t>();

      if (dp.contains("d") && dp["d"].is_number())
        ch_deny = dp["d"].get<int64_t>();
      else if (dp.contains("deny") && dp["deny"].is_number())
        ch_deny = dp["deny"].get<int64_t>();

      allowed = (allowed & ~ch_deny) | ch_allow;
    }
  }

  // 3. Channel role overrides
  if (bot_member_opt && chan_opt->role_permissions &&
      chan_opt->role_permissions->is_object()) {
    const auto &rp = *(chan_opt->role_permissions);
    int64_t roles_allow = 0;
    int64_t roles_deny = 0;
    for (const auto &role_id : bot_member_opt->roles) {
      if (rp.contains(role_id) && rp[role_id].is_object()) {
        const auto &override_obj = rp[role_id];
        if (override_obj.contains("a") && override_obj["a"].is_number()) {
          roles_allow |= override_obj["a"].get<int64_t>();
        } else if (override_obj.contains("allow") &&
                   override_obj["allow"].is_number()) {
          roles_allow |= override_obj["allow"].get<int64_t>();
        }

        if (override_obj.contains("d") && override_obj["d"].is_number()) {
          roles_deny |= override_obj["d"].get<int64_t>();
        } else if (override_obj.contains("deny") &&
                   override_obj["deny"].is_number()) {
          roles_deny |= override_obj["deny"].get<int64_t>();
        }
      }
    }
    allowed = (allowed & ~roles_deny) | roles_allow;
  }

  return (allowed & stoatpp::permissions::Masquerade) ==
         stoatpp::permissions::Masquerade;
}

void start_reload_server() {
  std::thread([]() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
      return;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(3010);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
      close(server_fd);
      return;
    }

    if (listen(server_fd, 10) < 0) {
      close(server_fd);
      return;
    }

    std::cout << "\033[1;36m[reload server]\033[0m listening on port 3010...\n";

    while (true) {
      int new_socket = accept(server_fd, nullptr, nullptr);
      if (new_socket < 0)
        continue;

      char buffer[2048] = {0};
      ssize_t valread = read(new_socket, buffer, sizeof(buffer) - 1);
      if (valread > 0) {
        std::string req(buffer);
        size_t reload_pos = req.find("GET /reload");
        if (reload_pos != std::string::npos) {
          size_t q_pos = req.find('?', reload_pos);
          size_t space_pos = req.find(' ', q_pos);
          if (q_pos != std::string::npos && space_pos != std::string::npos) {
            std::string query = req.substr(q_pos + 1, space_pos - q_pos - 1);

            auto parse_param = [](const std::string &query,
                                  const std::string &key) -> std::string {
              size_t k_pos = query.find(key + "=");
              if (k_pos == std::string::npos)
                return "";
              size_t val_start = k_pos + key.size() + 1;
              size_t amp_pos = query.find('&', val_start);
              std::string enc_val =
                  (amp_pos == std::string::npos)
                      ? query.substr(val_start)
                      : query.substr(val_start, amp_pos - val_start);

              std::string res;
              for (size_t i = 0; i < enc_val.length(); ++i) {
                if (enc_val[i] == '%' && i + 2 < enc_val.length()) {
                  try {
                    int hex = std::stoi(enc_val.substr(i + 1, 2), nullptr, 16);
                    res += static_cast<char>(hex);
                    i += 2;
                  } catch (...) {
                  }
                } else if (enc_val[i] == '+') {
                  res += ' ';
                } else {
                  res += enc_val[i];
                }
              }
              return res;
            };

            std::string server_id = parse_param(query, "server_id");
            std::string module = parse_param(query, "module");

            if (!server_id.empty() && bronx::g_db) {
              std::cout << "\033[1;36m[reload server]\033[0m reloading module '"
                        << module << "' for server " << server_id << "\n";
              if (module == "leveling" || module == "all") {
                bronx::g_db->reload_guild_leveling_config(server_id);
              }
              if (module == "level_roles" || module == "leveling" ||
                  module == "all") {
                bronx::g_db->reload_level_roles(server_id);
              }
              if (module == "welcome_leave" || module == "all") {
                bronx::g_db->reload_welcome_leave_config(server_id);
              }
              if (module == "logging" || module == "all") {
                bronx::g_db->reload_logging_config(server_id);
                bronx::g_db->reload_logging_embeds(server_id);
              }
              if (module == "antiraid" || module == "all") {
                bronx::g_db->reload_antiraid_config(server_id);
              }
              if (module == "prefixes" || module == "all") {
                bronx::g_db->reload_prefixes(server_id);
              }
              if (module == "autoresponder" || module == "all") {
                bronx::g_db->reload_autoresponders(server_id);
              }
              if (module == "autoreact" || module == "all") {
                bronx::g_db->reload_autoreacts(server_id);
              }
              if (module == "commands" || module == "all") {
                bronx::g_db->reload_disabled_items(server_id);
              }
            }
          }
        }

        std::string response =
            "HTTP/1.1 200 OK\r\nContent-Type: "
            "application/json\r\nContent-Length: 17\r\nConnection: "
            "close\r\n\r\n{\"success\":true}";
        write(new_socket, response.c_str(), response.size());
      }
      close(new_socket);
    }
  }).detach();
}

int main(int argc, char *argv[]) {
  load_env();
  bronx::load_emojis();
  bronx::i18n::init();

  bool verbose = false;
  bool staging = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--verbose" || arg == "-v") {
      verbose = true;
    } else if (arg == "--staging" || arg == "-staging") {
      staging = true;
    }
  }
  g_verbose_logging.store(verbose);

  const char *token_env =
      std::getenv(staging ? "STAGING_BOT_TOKEN" : "BOT_TOKEN");
  std::string token = token_env ? token_env : "";
  if (token.empty() || token == "token") {
    if (staging) {
      std::cerr << "[bronx error] staging bot token (STAGING_BOT_TOKEN) is "
                   "empty or not set. please update your .env file.\n";
    } else {
      std::cerr << "[bronx error] bot_token is empty or not set. please update "
                   "your .env file.\n";
    }
    return 1;
  }

  if (staging) {
    std::cout << "[bronx] running in staging mode using STAGING_BOT_TOKEN.\n";
  }

  // DB configuration
  const char *db_host_env = std::getenv("DB_HOST");
  std::string db_host = db_host_env ? db_host_env : "127.0.0.1";

  const char *db_user_env = std::getenv("DB_USER");
  std::string db_user = db_user_env ? db_user_env : "root";

  const char *db_pass_env = std::getenv("DB_PASSWORD");
  std::string db_pass = db_pass_env ? db_pass_env : "";

  const char *db_name_env = nullptr;
  std::string db_name;
  if (staging) {
    db_name_env = std::getenv("STAGING_DB_DATABASE");
    if (db_name_env) {
      db_name = db_name_env;
    } else {
      const char *base_db_name = std::getenv("DB_DATABASE");
      db_name = base_db_name ? std::string(base_db_name) + "_staging"
                             : "bronx_staging";
    }
  } else {
    db_name_env = std::getenv("DB_DATABASE");
    db_name = db_name_env ? db_name_env : "bronx";
  }

  int db_port = 3306;
  if (const char *db_port_env = std::getenv("DB_PORT")) {
    try {
      db_port = std::stoi(db_port_env);
    } catch (...) {
    }
  }

  std::cout << "[bronx db] connecting to MariaDB on " << db_host << ":"
            << db_port << "...\n";
  bronx::g_db = std::make_shared<bronx::Database>();
  if (!bronx::g_db->init(db_host, db_user, db_pass, db_name, db_port)) {
    std::cerr
        << "[bronx error] failed to initialize database connection. exiting.\n";
    return 1;
  }

  start_reload_server();

  stoatpp::event_dispatcher::should_ignore_user =
      [](const std::string &user_id) -> bool {
    if (bronx::g_db) {
      return bronx::g_db->is_user_ignored(user_id);
    }
    return false;
  };

  stoatpp::ClientConfig cfg;
  cfg.log_level = verbose ? stoatpp::LogLevel::TRACE : stoatpp::LogLevel::INFO;
  cfg.enable_default_help = false;
  cfg.dispatch_commands_on_edit = true;
  cfg.case_insensitive_commands = true;
  cfg.ws_ping_interval_ms = 3000;  // server kills idle connections at ~5s; ping every 3s

  cfg.command_prefix = ".";
  if (const char *pfx = std::getenv("COMMAND_PREFIX")) {
    cfg.command_prefix = pfx;
  }

  cfg.owner_id = "01FZB2QAPRVT8PVMF11480GRCD";
  if (const char *oid = std::getenv("OWNER_ID")) {
    cfg.owner_id = oid;
  } else if (const char *boid = std::getenv("BOT_OWNER_ID")) {
    cfg.owner_id = boid;
  }

  if (const char *api_url = std::getenv("API_BASE_URL")) {
    cfg.api_base_url = api_url;
  }
  if (const char *ws_url = std::getenv("WS_URL")) {
    cfg.ws_url = ws_url;
  }

  cfg.masquerade_handler = [](stoatpp::cluster &bot,
                              const std::string &channel_id,
                              stoatpp::models::MessagePayload &payload) {
    if (!bronx::g_db)
      return;
    auto chan_opt = bot.get_channel(channel_id);
    if (!chan_opt || !chan_opt->server)
      return;
    std::string server_id = *(chan_opt->server);

    // Check if the bot actually has the Masquerade permission in this channel.
    // If not, discard masquerade to prevent a 403 HTTP error when sending
    // messages.
    if (!bot_has_masquerade_permission(bot, channel_id)) {
      payload.masquerade = std::nullopt;
      return;
    }

    // 1. Check server config — if masquerades are completely disabled for this
    // server, apply nothing
    auto server_cfg = bronx::g_db->get_masquerade_config(server_id);
    if (server_cfg && server_cfg->disabled) {
      payload.masquerade = std::nullopt;
      return;
    }

    // 2. Look up per-user masquerade — only if user masquerades are NOT
    // disabled
    std::string invoker_id;
    {
      std::lock_guard<std::mutex> lock(g_invoker_mutex);
      auto it = g_channel_last_invoker.find(channel_id);
      if (it != g_channel_last_invoker.end())
        invoker_id = it->second;
    }

    if (!invoker_id.empty() && (!server_cfg || !server_cfg->disable_user)) {
      auto user_masq = bronx::g_db->get_user_masquerade(invoker_id);
      if (user_masq) {
        if (!payload.masquerade) {
          payload.masquerade = stoatpp::models::MessagePayload::Masquerade{};
        }
        if (user_masq->name && !payload.masquerade->name &&
            (!server_cfg || !server_cfg->disable_user_name))
          payload.masquerade->name = user_masq->name;
        if (user_masq->avatar && !payload.masquerade->avatar &&
            (!server_cfg || !server_cfg->disable_user_avatar))
          payload.masquerade->avatar = user_masq->avatar;
        if (user_masq->colour && !payload.masquerade->colour &&
            (!server_cfg || !server_cfg->disable_user_colour))
          payload.masquerade->colour = user_masq->colour;
      }
    }

    // 3. Apply server override on top (field-by-field: server beats user)
    if (server_cfg &&
        (server_cfg->name || server_cfg->avatar || server_cfg->colour)) {
      if (!payload.masquerade) {
        payload.masquerade = stoatpp::models::MessagePayload::Masquerade{};
      }
      if (server_cfg->name)
        payload.masquerade->name = server_cfg->name;
      if (server_cfg->avatar)
        payload.masquerade->avatar = server_cfg->avatar;
      if (server_cfg->colour)
        payload.masquerade->colour = server_cfg->colour;
    }

    // 4. If payload.masquerade is still empty (no fields set), clear it to
    // avoid sending empty object
    if (payload.masquerade && !payload.masquerade->name &&
        !payload.masquerade->avatar && !payload.masquerade->colour) {
      payload.masquerade = std::nullopt;
    }
  };

  cfg.prefix_resolver =
      [](stoatpp::cluster &bot,
         const stoatpp::events::Message &msg) -> std::vector<std::string> {
    std::vector<std::string> prefixes;
    std::string bot_id = bot.current_user().id;
    if (!bot_id.empty()) {
      prefixes.push_back("<@" + bot_id + "> ");
      prefixes.push_back("<@" + bot_id + ">");
    }
    bool server_has_custom = false;

    if (bronx::g_db) {
      // 1. User prefixes always apply
      std::vector<std::string> user_pfx =
          bronx::g_db->get_prefixes("user", msg.author.id);
      prefixes.insert(prefixes.end(), user_pfx.begin(), user_pfx.end());

      // 2. Server custom prefixes
      if (!msg.server_id.empty()) {
        std::vector<std::string> server_pfx =
            bronx::g_db->get_prefixes("server", msg.server_id);
        if (!server_pfx.empty()) {
          server_has_custom = true;
          prefixes.insert(prefixes.end(), server_pfx.begin(), server_pfx.end());
        }
      }
    }

    // 3. Default prefix — skip if the server has custom prefixes AND default is
    // disabled
    bool add_default = true;
    if (server_has_custom && !msg.server_id.empty() && bronx::g_db) {
      add_default = bronx::g_db->get_prefix_default_enabled(msg.server_id);
    }
    if (add_default) {
      prefixes.push_back(bot.config().command_prefix);
    }

    // Deduplicate
    std::vector<std::string> unique_prefixes;
    for (const auto &p : prefixes) {
      if (std::find(unique_prefixes.begin(), unique_prefixes.end(), p) ==
          unique_prefixes.end()) {
        unique_prefixes.push_back(p);
      }
    }
    return unique_prefixes;
  };

  std::cout << "[bronx] initialising stoatpp cluster...\n";
  std::cout << "        api base: " << cfg.api_base_url << "\n";
  std::cout << "        ws gateway: " << cfg.ws_url << "\n";
  std::cout << "        command prefix: " << cfg.command_prefix << "\n";
  std::cout << "        owner id: " << cfg.owner_id << "\n";

  stoatpp::cluster bot(token, cfg);

  // Register our Cogs/Modules
  bot.use(std::make_unique<bronx::cogs::HelpModule>());
  bot.use(std::make_unique<bronx::cogs::GeneralModule>());
  bot.use(std::make_unique<bronx::cogs::ModerationModule>());
  bot.use(std::make_unique<bronx::cogs::RolesModule>());
  bot.use(std::make_unique<bronx::cogs::UsersModule>());
  bot.use(std::make_unique<bronx::cogs::PrefixModule>());
  bot.use(std::make_unique<bronx::cogs::ReactionRolesModule>());
  bot.use(std::make_unique<bronx::cogs::EmojiModule>());
  bot.use(std::make_unique<bronx::cogs::StatsModule>());
  bot.use(std::make_unique<bronx::cogs::EconomyModule>());
  bot.use(std::make_unique<bronx::cogs::BlackjackModule>());
  bot.use(std::make_unique<bronx::cogs::CaptchaModule>());
  bot.use(std::make_unique<bronx::cogs::QteModule>());
  bot.use(std::make_unique<bronx::cogs::GamesModule>());
  bot.use(std::make_unique<bronx::cogs::GeoGuessrModule>());
  bot.use(std::make_unique<bronx::cogs::WelcomeLeaveModule>());
  bot.use(std::make_unique<bronx::cogs::LoggingModule>());
  bot.use(std::make_unique<bronx::cogs::CommandConfigModule>());
  bot.use(std::make_unique<bronx::cogs::MediaModule>());
  bot.use(std::make_unique<bronx::cogs::RemindersModule>());
  bot.use(std::make_unique<bronx::cogs::AntiRaidModule>());
  bot.use(std::make_unique<bronx::cogs::HoneypotModule>());
  bot.use(std::make_unique<bronx::cogs::AutoModule>());
  bot.use(std::make_unique<bronx::cogs::TicketsModule>());
  bot.use(std::make_unique<bronx::cogs::ApplicationsModule>());
  bot.use(std::make_unique<bronx::cogs::ServerListModule>());
  bot.use(std::make_unique<bronx::cogs::CountingModule>());
  bot.use(std::make_unique<bronx::cogs::AFKModule>());
  bot.use(std::make_unique<bronx::cogs::RobloxModule>());
  bot.use(std::make_unique<bronx::cogs::LastfmModule>());
  bot.use(std::make_unique<bronx::cogs::RSSModule>());
  bot.use(std::make_unique<bronx::cogs::MasqueradeModule>());
  bot.use(std::make_unique<bronx::cogs::TupperModule>());
  bot.use(std::make_unique<bronx::cogs::SocialModule>());
  bot.use(std::make_unique<bronx::cogs::AliasModule>());
  bot.use(std::make_unique<bronx::cogs::FilterModule>());
  bot.use(std::make_unique<bronx::cogs::BridgeModule>());
  bot.use(std::make_unique<bronx::cogs::LevelingModule>());
  bot.use(std::make_unique<bronx::cogs::FishingModule>());
  bot.use(std::make_unique<bronx::cogs::OwnerModule>());
  bot.use(std::make_unique<bronx::cogs::StarboardModule>());
  bot.use(std::make_unique<bronx::cogs::HistoryModule>());
  bot.use(std::make_unique<bronx::cogs::SnipeModule>());
  bot.use(std::make_unique<bronx::cogs::TimezoneModule>());
  bot.use(std::make_unique<bronx::cogs::LanguageModule>());
  bot.use(std::make_unique<bronx::cogs::SetupModule>());
  bot.use(std::make_unique<bronx::cogs::VoiceModule>());
  bot.use(std::make_unique<bronx::cogs::ImageBoardModule>());


  bot.on_ready([&bot, staging](const stoatpp::events::Ready &e) {
    g_bot_ready = true;
    std::cout << "\033[1;32m[bronx event]\033[0m ready! logged in as: "
              << e.user.username << " (id: " << e.user.id << ")\n";
    std::cout << "              serving: " << e.servers.size() << " servers, "
              << e.channels.size() << " channels.\n";
    std::cout << "              users in ready payload: " << e.users.size()
              << "\n";

    if (bronx::g_db) {
      for (const auto &srv : e.servers) {
        std::string icon = srv.icon ? *srv.icon : "";
        std::string banner = srv.banner ? *srv.banner : "";
        bronx::g_db->upsert_server(srv.id, srv.name, icon, banner);
        if (!srv.owner.empty()) {
          bronx::g_db->upsert_server_member(srv.id, srv.owner, true, true);
        }
      }
    }

    std::string status_text =
        "serving " + std::to_string(e.servers.size()) + " servers";
    bot.update_status(status_text, staging ? "Focus" : "Online", [](bool success) {
      if (success) {
        std::cout
            << "\033[1;36m[bronx]\033[0m successfully set presence status.\n";
      } else {
        std::cout
            << "\033[1;31m[bronx]\033[0m failed to set presence status.\n";
      }
    });

    // Shifting status timer (runs every 90 seconds).
    // Cancel any previous timer from an earlier on_ready (reconnect scenario).
    static std::atomic<uint64_t> status_timer_id{0};
    {
        uint64_t old_id = status_timer_id.exchange(0);
        if (old_id != 0) {
            bot.stop_timer(old_id);
        }
    }
    uint64_t new_timer_id = bot.start_timer(
        [&bot, staging](stoatpp::timer t) {
          static int status_index = 1;
          std::string status_text;

          size_t server_count = bot.get_servers().size();
          size_t user_count = bot.get_total_member_count();
          const auto &cmds = bot.get_commands();
          size_t cmd_count = cmds.size();

          static bool seeded = false;
          if (!seeded) {
            std::srand(static_cast<unsigned int>(std::time(nullptr)));
            seeded = true;
          }

          switch (status_index) {
          case 0:
            status_text =
                "serving " + std::to_string(server_count) + " servers";
            break;
          case 1:
            status_text =
                "watching over " + std::to_string(user_count) + " users";
            break;
          case 2:
            status_text = "bronx.siqnole.dev";
            break;
          case 3:
            status_text = ".help";
            break;
          case 4: {
            std::string rand_cmd = "ping";
            std::vector<stoatpp::Command> non_owner_cmds;
            for (const auto &cmd : cmds) {
              if (cmd.category != "Owner") {
                non_owner_cmds.push_back(cmd);
              }
            }
            if (!non_owner_cmds.empty()) {
              int idx = std::rand() % non_owner_cmds.size();
              rand_cmd = non_owner_cmds[idx].name;
            }
            status_text = "try ." + rand_cmd;
            break;
          }
          case 5:
            status_text = "i have " + std::to_string(cmd_count) + " commands !";
            break;
          case 6:
            status_text = "made in c++!";
            break;
          default:
            status_text = "bronx.siqnole.dev";
            break;
          }

          bot.update_status(status_text, staging ? "Focus" : "Online");
          status_index = (status_index + 1) % 7;
        },
        90);
    status_timer_id.store(new_timer_id);

    // Fix historical DB rows where bot users were mis-tagged as is_bot=FALSE.
    if (bronx::g_db) {
      // Collect bot user IDs: all users in Ready with bot=true, plus self
      std::vector<std::string> bot_ids;
      for (const auto &u : e.users) {
        if (u.bot && !u.id.empty()) {
          bot_ids.push_back(u.id);
        }
      }
      if (!e.user.id.empty()) {
        bot_ids.push_back(e.user.id);
      }
      std::cout << "\033[1;36m[bronx]\033[0m ready: detected " << bot_ids.size()
                << " bot user(s) in ready payload.\n";

      std::thread([bot_ids]() {
        for (const auto &id : bot_ids) {
          bronx::g_db->fix_bot_flags(id);
        }
      }).detach();
    }

    // Background user & member caching across all servers on startup
    std::thread([&bot, servers = e.servers]() {
      std::cout << "\033[1;35m[bronx cache]\033[0m starting background fetch "
                   "to cache all server members and users...\n";
      size_t total_cached_users = 0;
      size_t total_cached_members = 0;
      for (const auto &srv : servers) {
        auto res = bot.rest().get_server_members(srv.id);
        if (res.success() && res.body.is_object()) {
          if (res.body.contains("users") && res.body["users"].is_array()) {
            for (const auto &item : res.body["users"]) {
              auto u = stoatpp::models::User::from_json(item);
              bot.cache_user(u);
              total_cached_users++;
              if (bronx::g_db && u.bot && !u.id.empty()) {
                bronx::g_db->fix_bot_flags(u.id);
              }
            }
          }
          if (res.body.contains("members") && res.body["members"].is_array()) {
            for (const auto &item : res.body["members"]) {
              auto m = stoatpp::models::Member::from_json(item);
              if (m.server_id.empty())
                m.server_id = srv.id;
              bot.cache_member(m);
              total_cached_members++;
              if (bronx::g_db && !m.id.empty() && !srv.id.empty()) {
                bool is_owner = (!srv.owner.empty() && srv.owner == m.id);
                bronx::g_db->upsert_server_member(srv.id, m.id, is_owner,
                                                  false);
              }
            }
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      std::cout << "\033[1;35m[bronx cache]\033[0m finished caching all server "
                   "users & members! total users cached: "
                << total_cached_users
                << ", total members cached: " << total_cached_members << "\n";
    }).detach();
  });

  bot.set_message_preprocessor([](stoatpp::cluster &bot,
                                  stoatpp::events::Message &msg) {
    if (msg.author.bot || msg.content.empty() || !bronx::g_db)
      return;

    // 1. Resolve message prefix
    std::vector<std::string> prefixes;
    std::string bot_id = bot.current_user().id;
    if (!bot_id.empty()) {
      prefixes.push_back("<@" + bot_id + "> ");
      prefixes.push_back("<@" + bot_id + ">");
    }
    std::vector<std::string> user_pfx =
        bronx::g_db->get_prefixes("user", msg.author.id);
    prefixes.insert(prefixes.end(), user_pfx.begin(), user_pfx.end());

    if (!msg.server_id.empty()) {
      std::vector<std::string> server_pfx =
          bronx::g_db->get_prefixes("server", msg.server_id);
      prefixes.insert(prefixes.end(), server_pfx.begin(), server_pfx.end());
    }
    prefixes.push_back(bot.config().command_prefix);

    std::vector<std::string> unique_prefixes;
    for (const auto &p : prefixes) {
      if (std::find(unique_prefixes.begin(), unique_prefixes.end(), p) ==
          unique_prefixes.end()) {
        unique_prefixes.push_back(p);
      }
    }

    std::string matched_prefix;
    for (const auto &p : unique_prefixes) {
      if (!p.empty() && msg.content.rfind(p, 0) == 0) {
        matched_prefix = p;
        break;
      }
    }

    if (matched_prefix.empty())
      return;

    // 2. Parse command name from content
    std::string command_part = msg.content.substr(matched_prefix.size());
    std::stringstream ss(command_part);
    std::string cmd;
    ss >> cmd;
    if (cmd.empty())
      return;

    // 3. Resolve alias in database (checking server-level, then user-level)
    std::string target =
        bronx::g_db->resolve_alias(msg.server_id, msg.author.id, cmd);
    if (!target.empty()) {
      // Keep any trailing arguments/characters intact
      std::string remainder = command_part.substr(cmd.size());
      msg.content = matched_prefix + target + remainder;
      std::cout << "[bronx alias] resolved alias: '" << cmd << "' -> '"
                << target << "'. rewritten content: '" << msg.content << "'\n";
    }
  });

  bot.on_message([&bot](const stoatpp::events::Message &msg) {
    if (g_verbose_logging.load()) {
      std::cout << "[bronx event] message in channel " << msg.channel_id
                << " by " << msg.author.username << ": " << msg.content << "\n";
    }

    bronx::cogs::FilterModule::scan_message(bot, msg);

    // Tupperbox proxying check
    if (!msg.author.bot && !msg.content.empty() && msg.content[0] != '\\' &&
        bronx::g_db) {
      bool is_tupper_disabled = false;
      if (!msg.server_id.empty()) {
        is_tupper_disabled =
            bronx::g_db->is_disabled(msg.server_id, "tupper", "tupper");
      }
      if (!is_tupper_disabled) {
        std::vector<bronx::Database::Tupper> tuppers =
            bronx::g_db->get_user_tuppers(msg.author.id);
        for (const auto &t : tuppers) {
          bool prefix_match =
              t.prefix.empty() || (msg.content.rfind(t.prefix, 0) == 0);
          bool suffix_match =
              t.suffix.empty() ||
              (msg.content.size() >= t.prefix.size() + t.suffix.size() &&
               msg.content.substr(msg.content.size() - t.suffix.size()) ==
                   t.suffix);

          if (prefix_match && suffix_match) {
            std::string inner = msg.content.substr(
                t.prefix.size(),
                msg.content.size() - t.prefix.size() - t.suffix.size());
            if (!inner.empty()) {
              bot.delete_message(msg.channel_id, msg.id);

              stoatpp::models::MessagePayload payload;
              payload.content = inner;
              payload.masquerade =
                  stoatpp::models::MessagePayload::Masquerade{};
              payload.masquerade->name =
                  t.name + " [" + msg.author.username + "]";
              payload.masquerade->avatar = t.avatar_url;

              for (const auto &reply_id : msg.replies) {
                payload.add_reply(reply_id, false);
              }

              std::string author_id = msg.author.id;
              std::string tupper_name = t.name;
              bot.send_message(msg.channel_id, payload,
                               [&bot, author_id, tupper_name](
                                   stoatpp::models::Message res, bool success) {
                                 if (success && bronx::g_db) {
                                   bronx::g_db->add_tupper_proxy(
                                       res.id, author_id, tupper_name);
                                 }
                               });
              return; // Handled proxy, skip further processing
            }
          }
        }
      }
    }

    if (!msg.author.bot && !msg.channel_id.empty() && !msg.author.id.empty()) {
      std::lock_guard<std::mutex> lock(g_invoker_mutex);
      g_channel_last_invoker[msg.channel_id] = msg.author.id;
      if (!msg.server_id.empty()) {
        g_server_last_channel[msg.server_id] = msg.channel_id;
      }
      g_last_active_channel = msg.channel_id;
    }

    if (bronx::g_db) {
      // Resolve bot flag — Revolt sometimes omits "bot" in message payloads,
      // so fall back to the user cache if the field isn't set on this message.
      bool is_bot = msg.author.bot;
      if (!is_bot) {
        auto cached = bot.get_user(msg.author.id);
        if (cached && cached->bot)
          is_bot = true;
      }

      // 1. Message Telemetry
      bronx::g_db->increment_user_daily_messages(msg.server_id, msg.author.id,
                                                 is_bot, "message");

      // 2. Channel Telemetry
      bronx::g_db->increment_channel_daily_messages(msg.server_id,
                                                    msg.channel_id);

      // 3. User Channel Telemetry
      if (!msg.server_id.empty() && !is_bot) {
        bronx::g_db->increment_user_channel_messages(
            msg.server_id, msg.channel_id, msg.author.id);

        // XP / Leveling gain check
        auto cfg_opt = bronx::g_db->get_guild_leveling_config(msg.server_id);
        if (cfg_opt && cfg_opt->enabled) {
          bool skip_xp = false;

          // 1. Check message length
          if (msg.content.size() <
                  static_cast<size_t>(cfg_opt->min_message_chars) ||
              msg.content.size() >
                  static_cast<size_t>(cfg_opt->max_message_chars)) {
            skip_xp = true;
          }

          // 2. Check cooldown
          uint64_t now_sec =
              std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
          std::string cd_key = msg.author.id + ":" + msg.server_id;
          if (!skip_xp) {
            static std::unordered_map<std::string, uint64_t>
                g_xp_cooldown_cache;
            static std::mutex g_xp_cooldown_mutex;
            std::lock_guard<std::mutex> cd_lock(g_xp_cooldown_mutex);
            auto cd_it = g_xp_cooldown_cache.find(cd_key);
            if (cd_it != g_xp_cooldown_cache.end() &&
                now_sec < cd_it->second + cfg_opt->xp_cooldown_seconds) {
              skip_xp = true;
            } else {
              g_xp_cooldown_cache[cd_key] = now_sec;
            }
          }

          // 3. Check blacklist
          if (!skip_xp) {
            std::vector<std::string> author_roles;
            auto m_opt = bot.get_member(msg.server_id, msg.author.id);
            if (m_opt) {
              author_roles = m_opt->roles;
            }
            if (bronx::g_db->is_xp_blocked(msg.server_id, msg.channel_id,
                                           msg.author.id, author_roles)) {
              skip_xp = true;
            }
          }

          if (!skip_xp) {
            // Calculate random XP to award
            int min_xp = cfg_opt->min_xp_per_message;
            int max_xp = cfg_opt->max_xp_per_message;
            int xp_gain = min_xp;
            if (max_xp > min_xp) {
              xp_gain = min_xp + (std::rand() % (max_xp - min_xp + 1));
            }

            uint32_t new_level = 1;
            bool leveled_up = false;
            if (bronx::g_db->add_xp(msg.author.id, xp_gain, new_level,
                                    leveled_up, msg.server_id)) {
              // Economy coin reward if enabled
              if (cfg_opt->reward_coins && cfg_opt->coins_per_message > 0) {
                bronx::g_db->update_balances(msg.author.id,
                                             cfg_opt->coins_per_message, 0);
              }

              if (leveled_up) {
                // 1. Custom level announcement
                if (cfg_opt->announce_levelup) {
                  std::string announce_channel =
                      cfg_opt->announcement_channel.value_or(msg.channel_id);
                  std::string ann_msg = cfg_opt->announcement_message;

                  // Replace tokens: {name}, {mention}, {level}
                  size_t pos;
                  while ((pos = ann_msg.find("{mention}")) !=
                         std::string::npos) {
                    ann_msg.replace(pos, 9, "<@" + msg.author.id + ">");
                  }
                  while ((pos = ann_msg.find("{name}")) != std::string::npos) {
                    ann_msg.replace(pos, 6, msg.author.username);
                  }
                  while ((pos = ann_msg.find("{level}")) != std::string::npos) {
                    ann_msg.replace(pos, 7, std::to_string(new_level));
                  }

                  bot.send_message(announce_channel, ann_msg);
                }

                // 2. Assign level roles rewards
                auto level_roles = bronx::g_db->get_level_roles(msg.server_id);
                if (!level_roles.empty()) {
                  // Fetch current member to know their current roles
                  bot.fetch_member(
                      msg.server_id, msg.author.id,
                      [&bot, msg, level_roles, new_level,
                       cfg_opt](stoatpp::models::Member member, bool ok) {
                        if (!ok)
                          return;

                        std::vector<std::string> roles_to_remove;
                        std::string role_to_add = "";

                        for (const auto &r : level_roles) {
                          if (r.level == new_level) {
                            role_to_add = r.role_id;
                          } else if (r.level < new_level &&
                                     !cfg_opt->sticky_roles) {
                            // If sticky roles is off, collect all lower role
                            // rewards to remove
                            roles_to_remove.push_back(r.role_id);
                          }
                        }

                        if (!role_to_add.empty()) {
                          bot.add_role_to_member(
                              msg.server_id, msg.author.id, role_to_add,
                              [&bot, msg, roles_to_remove](bool added) {
                                if (added) {
                                  // Remove old level roles if needed
                                  for (const auto &r_id : roles_to_remove) {
                                    bot.remove_role_from_member(
                                        msg.server_id, msg.author.id, r_id);
                                  }
                                }
                              });
                        }
                      });
                }
              }
            }
          }
        }
      }

      // Skip command telemetry tracking for bots
      if (is_bot)
        return;

      // 3. Command Telemetry
      std::vector<std::string> prefixes;
      std::string bot_id = bot.current_user().id;
      if (!bot_id.empty()) {
        prefixes.push_back("<@" + bot_id + "> ");
        prefixes.push_back("<@" + bot_id + ">");
      }
      std::vector<std::string> user_pfx =
          bronx::g_db->get_prefixes("user", msg.author.id);
      prefixes.insert(prefixes.end(), user_pfx.begin(), user_pfx.end());

      if (!msg.server_id.empty()) {
        std::vector<std::string> server_pfx =
            bronx::g_db->get_prefixes("server", msg.server_id);
        prefixes.insert(prefixes.end(), server_pfx.begin(), server_pfx.end());
      }
      prefixes.push_back(bot.config().command_prefix);

      std::vector<std::string> unique_prefixes;
      for (const auto &p : prefixes) {
        if (std::find(unique_prefixes.begin(), unique_prefixes.end(), p) ==
            unique_prefixes.end()) {
          unique_prefixes.push_back(p);
        }
      }

      std::string matched_prefix;
      for (const auto &p : unique_prefixes) {
        if (!p.empty() && msg.content.rfind(p, 0) == 0) {
          matched_prefix = p;
          break;
        }
      }

      if (!matched_prefix.empty()) {
        std::stringstream ss(msg.content.substr(matched_prefix.size()));
        std::string cmd_name;
        ss >> cmd_name;

        if (!cmd_name.empty()) {
          bool cmd_exists = false;
          std::string normalized_name = cmd_name;
          std::string category = "";
          for (const auto &cmd : bot.get_commands()) {
            if (cmd.name == cmd_name ||
                std::find(cmd.aliases.begin(), cmd.aliases.end(), cmd_name) !=
                    cmd.aliases.end()) {
              cmd_exists = true;
              normalized_name = cmd.name;
              category = cmd.category;
              break;
            }
          }

          if (cmd_exists) {
            bool is_disabled = false;
            if (bronx::g_db && !msg.server_id.empty()) {
              std::string cmd_lower = normalized_name;
              std::string mod_lower = category;
              std::transform(cmd_lower.begin(), cmd_lower.end(),
                             cmd_lower.begin(), ::tolower);
              std::transform(mod_lower.begin(), mod_lower.end(),
                             mod_lower.begin(), ::tolower);

              if (bronx::g_db->is_disabled(msg.server_id, cmd_lower,
                                           mod_lower)) {
                bool exception = false;
                if (bot.is_owner(msg.author.id) ||
                    bot.is_server_owner(msg.server_id, msg.author.id)) {
                  if (cmd_lower == "command" || cmd_lower == "trust" ||
                      cmd_lower == "cmd") {
                    exception = true;
                  }
                }
                if (!exception) {
                  is_disabled = true;
                }
              }
            }

            if (!is_disabled) {
              bronx::g_db->increment_command_usage(
                  msg.server_id, normalized_name, msg.channel_id);
              bronx::g_db->record_command_usage(msg.server_id, normalized_name,
                                                msg.author.id, msg.channel_id);
              bronx::g_db->increment_user_daily_commands(
                  msg.server_id, msg.author.id, msg.author.bot);
              if (!msg.server_id.empty()) {
                bronx::g_db->increment_user_channel_commands(
                    msg.server_id, msg.channel_id, msg.author.id);
              }
              bronx::cogs::QteModule::on_command_executed(
                  bot, msg, normalized_name, category);
            }
          }
        }
      }
    }
  });

  bot.on_message_update([](const stoatpp::events::MessageUpdate &e) {
    if (g_verbose_logging.load()) {
      std::cout << "[bronx event] message updated! id: " << e.id
                << " in channel " << e.channel_id << "\n";
    }
  });

  bot.on_message_delete([](const stoatpp::events::MessageDelete &e) {
    if (g_verbose_logging.load()) {
      std::cout << "[bronx event] message deleted! id: " << e.id
                << " in channel " << e.channel_id << "\n";
    }
  });

  bot.on_message_react([&bot](const stoatpp::events::MessageReact &e) {
    if (g_verbose_logging.load()) {
      std::cout << "[bronx event] react: user " << e.user_id << " added emoji "
                << e.emoji_id << " to message " << e.id << "\n";
    }

    if (bronx::g_db) {
      auto proxy_opt = bronx::g_db->get_tupper_proxy(e.id);
      if (proxy_opt) {
        if (e.emoji_id == "" + bronx::emj_no + "") {
          if (e.user_id == proxy_opt->user_id) {
            bot.delete_message(e.channel_id, e.id);
            bronx::g_db->delete_tupper_proxy(e.id);
          }
        } else if (e.emoji_id == "❓" || e.emoji_id == "❔") {
          std::string proxy_user = proxy_opt->user_id;
          std::string tupper = proxy_opt->tupper_name;
          std::thread([&bot, e, proxy_user, tupper]() {
            auto dm_res = bot.rest().open_dm(e.user_id);
            if (dm_res.success() && dm_res.body.is_object() &&
                dm_res.body.contains("_id")) {
              std::string dm_channel = dm_res.body["_id"].get<std::string>();
              bot.send_message(dm_channel,
                               "💬 The message with ID `" + e.id + "` in <#" +
                                   e.channel_id + "> was sent by <@" +
                                   proxy_user + "> (User ID: `" + proxy_user +
                                   "`) using the tupper profile **" + tupper +
                                   "**.");
            }
          }).detach();
        }
      }
    }
  });

  bot.on_message_unreact([](const stoatpp::events::MessageUnreact &e) {
    if (g_verbose_logging.load()) {
      std::cout << "[bronx event] unreact: user " << e.user_id
                << " removed emoji " << e.emoji_id << " from message " << e.id
                << "\n";
    }
  });

  bot.on_channel_create([](const stoatpp::events::ChannelCreate &e) {
    if (g_verbose_logging.load()) {
      std::cout << "[bronx event] channel created: "
                << e.channel.name.value_or("(unnamed)")
                << " (id: " << e.channel.id << ")\n";
    }
  });

  bot.on_channel_delete([](const stoatpp::events::ChannelDelete &e) {
    if (g_verbose_logging.load()) {
      std::cout << "[bronx event] channel deleted: " << e.id << "\n";
    }
  });

  bot.on_server_create([&bot](const stoatpp::events::ServerCreate &e) {
    std::cout << "[bronx event] server added: " << e.server.name
              << " (id: " << e.server.id << ")\n";

    if (bronx::g_db) {
      bool is_new = !bronx::g_db->server_exists(e.server.id);
      std::string icon = e.server.icon ? *e.server.icon : "";
      std::string banner = e.server.banner ? *e.server.banner : "";
      bronx::g_db->upsert_server(e.server.id, e.server.name, icon, banner);
      if (!e.server.owner.empty()) {
        bronx::g_db->upsert_server_member(e.server.id, e.server.owner, true,
                                          true);
      }

      if (is_new && g_bot_ready) {
        std::thread([&bot, e]() {
          std::string target_channel_id = "";
          if (e.server.raw.contains("system_channel") &&
              !e.server.raw["system_channel"].is_null()) {
            target_channel_id =
                e.server.raw["system_channel"].get<std::string>();
          }
          if (target_channel_id.empty() && !e.server.channels.empty()) {
            for (const auto &ch_id : e.server.channels) {
              auto ch = bot.get_channel(ch_id);
              if (ch && (ch->channel_type == "TextChannel" ||
                         ch->channel_type == "Text")) {
                target_channel_id = ch_id;
                break;
              }
            }
          }
          if (target_channel_id.empty() && !e.server.channels.empty()) {
            target_channel_id = e.server.channels[0];
          }

          if (!target_channel_id.empty()) {
            stoatpp::models::MessagePayload payload;
            stoatpp::models::Embed embed;
            embed.set_title("hello, I am bronx!");
            embed.set_description(
                "im made in c++, im also a bot for everything\n\n"
                "**if you need:**\n"
                ":01KWKZDN9J1XY3QZ74PAZT3Q3A: **reaction roles:** give people "
                "roles on join\n"
                ":01KWKZDPWGYC8YK28JE8WTQAC3: **anti-raid:** stop bad actors "
                "before they start\n"
                ":01KWKZDKCBXRCRWE6RRF4KR6BP: **no mods?:** timeouts, jails, "
                "edit `.message`s the bot sends on command!\n"
                ":01KYSP2TT76QF7JFW7X1JJPQMH: **need members?:** get your "
                "server out to participating "
                "`.list`ed servers!\n\n"
                "**for now:**\n"
                "pls choose:\n"
                ":01KWKZKNMTBYXG0GRSEDKP7B2X: **quick setup:** learn "
                "configuration for "
                "essential features.\n"
                "📖 **help:** List commands and setup documentation.");
            embed.set_colour("#5865f2");
            payload.add_embed(embed);
            payload.set_interactions({"01KWKZKNMTBYXG0GRSEDKP7B2X", "📖"},
                                     false);
            bot.send_message(target_channel_id, payload);
          }
        }).detach();
      }
    }

    stoatpp::models::MessagePayload payload;
    nlohmann::json embed = nlohmann::json::object();
    embed["title"] = "📥 Joined Server";
    embed["colour"] = "#44ff44";
    std::string desc = "• **Name:** " + e.server.name +
                       "\n"
                       "• **ID:** `" +
                       e.server.id +
                       "`\n"
                       "• **Owner:** <@" +
                       e.server.owner +
                       ">\n"
                       "• **Channels:** " +
                       std::to_string(e.server.channels.size()) +
                       "\n"
                       "• **Roles:** " +
                       std::to_string(e.server.roles.size()) + "\n";
    embed["description"] = desc;
    payload.embeds.push_back(embed);

    bot.send_message("01KWJBMZFEW97D3VM09TDADRPN", payload);
  });

  bot.on_server_delete([&bot](const stoatpp::events::ServerDelete &e) {
    std::cout << "[bronx event] server removed: " << e.id << "\n";

    auto cached = bot.get_server(e.id);
    std::string server_name = cached ? cached->name : "Unknown Server";
    std::string owner_id = cached ? cached->owner : "";

    stoatpp::models::MessagePayload payload;
    nlohmann::json embed = nlohmann::json::object();
    embed["title"] = "📤 Left Server";
    embed["colour"] = "#ff4444";
    std::string desc = "• **Name:** " + server_name +
                       "\n"
                       "• **ID:** `" +
                       e.id + "`\n";
    if (!owner_id.empty()) {
      desc += "• **Owner:** <@" + owner_id + ">\n";
    }
    embed["description"] = desc;
    payload.embeds.push_back(embed);

    bot.send_message("01KWJBMZFEW97D3VM09TDADRPN", payload);

    // Purge all server-scoped data from the database on a background thread
    if (bronx::g_db) {
      std::string purge_id = e.id;
      std::thread([purge_id]() {
        bronx::g_db->purge_server_data(purge_id);
      }).detach();
    }
  });

  bot.on_error([](const stoatpp::events::Error &e) {
    std::cerr << "[bronx event] gateway error: " << e.error_id << "\n";
  });

  bot.on_rest_error([&bot](const std::string &method, const std::string &path,
                           int status_code, const std::string &error_msg) {
    std::cerr << "[bronx rest error] " << method << " " << path << " -> "
              << status_code << ": " << error_msg << "\n";

    // Ignore all errors from GET requests. GETs are read-only and queries
    // are typically handled directly by the calling functions via result
    // checks.
    if (method == "GET") {
      return;
    }

    // Try to parse channel ID from path or server ID fallback
    std::string channel_id = "";
    if (path.rfind("/channels/", 0) == 0) {
      std::string sub = path.substr(10);
      size_t slash = sub.find('/');
      if (slash != std::string::npos) {
        channel_id = sub.substr(0, slash);
      } else {
        channel_id = sub;
      }
    } else if (path.rfind("/servers/", 0) == 0) {
      std::string server_id = "";
      std::string sub = path.substr(9);
      size_t slash = sub.find('/');
      if (slash != std::string::npos) {
        server_id = sub.substr(0, slash);
      } else {
        server_id = sub;
      }
      if (!server_id.empty()) {
        std::lock_guard<std::mutex> lock(g_invoker_mutex);
        auto it = g_server_last_channel.find(server_id);
        if (it != g_server_last_channel.end()) {
          channel_id = it->second;
        }
      }
    }

    // Final fallback if we still don't have a channel ID
    if (channel_id.empty()) {
      std::lock_guard<std::mutex> lock(g_invoker_mutex);
      channel_id = g_last_active_channel;
    }

    if (!channel_id.empty()) {
      // Prevent recursive REST error handling loops
      {
        std::lock_guard<std::mutex> lock(g_handling_mutex);
        if (g_handling_channels.count(channel_id)) {
          return;
        }
        g_handling_channels.insert(channel_id);
      }

      // Extract the missing permission from error_msg (if format matches)
      std::string missing_perm = "";
      size_t pos = error_msg.find("missing permission: ");
      if (pos != std::string::npos) {
        missing_perm = error_msg.substr(pos + 20);
        size_t close_paren = missing_perm.find(')');
        if (close_paren != std::string::npos) {
          missing_perm = missing_perm.substr(0, close_paren);
        }
      }

      std::string lower_perm = missing_perm;
      std::transform(lower_perm.begin(), lower_perm.end(), lower_perm.begin(),
                     [](unsigned char c) { return std::tolower(c); });

      bool is_send_msg_perm_error =
          (status_code == 403 && method == "POST" &&
           path.find("/messages") != std::string::npos) ||
          (lower_perm == "sendmessage" || lower_perm == "sendmessages" ||
           lower_perm == "send_message" || lower_perm == "send_messages" ||
           lower_perm == "send message" || lower_perm == "send messages");

      if (is_send_msg_perm_error) {
        std::string invoker_id = "";
        {
          std::lock_guard<std::mutex> lock(g_invoker_mutex);
          auto it = g_channel_last_invoker.find(channel_id);
          if (it != g_channel_last_invoker.end()) {
            invoker_id = it->second;
          }
        }
        if (!invoker_id.empty()) {
          std::thread([&bot, invoker_id, channel_id]() {
            auto dm_res = bot.rest().open_dm(invoker_id);
            if (dm_res.success() && dm_res.body.is_object() &&
                dm_res.body.contains("_id")) {
              std::string dm_channel_id =
                  dm_res.body["_id"].template get<std::string>();
              bot.send_message(
                  dm_channel_id,
                  "" + bronx::emj_warning + " **Permission Error**: Bronx is missing the `SendMessage` "
                  "permission in <#" +
                      channel_id +
                      "> so your command response could not be sent.");
            }
          }).detach();
        }
      } else if (!missing_perm.empty()) {
        if (lower_perm == "embedlinks" || lower_perm == "embed_links" ||
            lower_perm == "embeds" || lower_perm == "embed") {
          bot.send_message(channel_id, "Missing permission: EmbedLinks");
        } else {
          bot.send_message(channel_id, "Missing permission: " + missing_perm);
        }
      } else {
        // No specific permission extracted — send a generic error to the user
        // Skip benign 404s from cleanup operations (reaction removal, message
        // lookups)
        bool is_benign_404 =
            (status_code == 404) &&
            (method == "DELETE" ||
             (method == "GET" && path.find("/messages/") != std::string::npos));

        if (!is_benign_404) {
          // Map common error types to user-friendly messages
          std::string user_msg;
          if (error_msg == "NotElevated") {
            user_msg =
                "" + bronx::emj_warning + " I don't have a high enough role to perform this action. "
                "Please move my role above the target user's roles.";
          } else if (error_msg == "Forbidden" || status_code == 403) {
            user_msg = "" + bronx::emj_warning + " I don't have permission to perform this action (" +
                       std::to_string(status_code) + ": " + error_msg + ").";
          } else if (error_msg == "NotFound" || status_code == 404) {
            user_msg = "" + bronx::emj_warning + " The target resource was not found (it may have been "
                       "deleted).";
          } else if (status_code == 400 &&
                     path.find("/reactions/") != std::string::npos) {
            user_msg =
                "" + bronx::emj_warning + " The specified emoji is invalid or cannot be used here.";
          } else if (status_code >= 500) {
            user_msg = "" + bronx::emj_warning + " The server encountered an internal error. Please try "
                       "again later.";
          } else if (status_code >= 400) {
            user_msg = "" + bronx::emj_warning + " Request failed (" + std::to_string(status_code) +
                       ": " + error_msg + ").";
          }

          if (!user_msg.empty()) {
            if (error_msg == "NotFound" || status_code == 404) {
              stoatpp::models::MessagePayload payload;
              payload.content = user_msg;
              payload.delete_after = 5; // delete after 5 seconds
              bot.send_message(channel_id, payload);
            } else {
              bot.send_message(channel_id, user_msg);
            }
          }
        }
      }

      // Release handling block after 2 seconds
      std::thread([channel_id]() {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::lock_guard<std::mutex> lock(g_handling_mutex);
        g_handling_channels.erase(channel_id);
      }).detach();
    }
  });

  bot.on_logout([](const stoatpp::events::Logout &) {
    std::cout << "[bronx event] logout detected\n";
  });

  if (verbose) {
    bot.on_raw_event([](const std::string &type, const nlohmann::json &data) {
      std::cout << "[bronx audit raw] " << type
                << " payload size: " << data.dump().size() << "\n";
    });
  }

  bot.on_command_cooldown([](stoatpp::cluster &bot,
                             const stoatpp::events::Message &msg,
                             const stoatpp::Command &cmd, int64_t remaining) {
    if (cmd.category == "Economy") {
      std::string time_str = "";
      if (remaining >= 3600) {
        int64_t hours = remaining / 3600;
        int64_t mins = (remaining % 3600) / 60;
        time_str = std::to_string(hours) + "h " + std::to_string(mins) + "m";
      } else if (remaining >= 60) {
        time_str = std::to_string(remaining / 60) + "m " +
                   std::to_string(remaining % 60) + "s";
      } else {
        time_str = std::to_string(remaining) + "s";
      }
      bot.send_message(msg.channel_id,
                       "you are on cooldown. please wait " + time_str + ".");
    } else {
      auto now_unix = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
      std::string ts_tag =
          stoatpp::utils::format_timestamp(now_unix + remaining, "R");
      bot.send_message(msg.channel_id,
                       "you are on cooldown. please wait " + ts_tag + ".");
    }
  });

  bot.on_command_error(
      [](stoatpp::cluster &bot, const stoatpp::events::Message &msg,
         const stoatpp::Command &cmd, const std::string &error_type) {
        if (error_type == "member_fetch_failed") {
          bot.send_message(msg.channel_id,
                           "failed to verify your server member details.");
        } else if (error_type == "server_resolve_failed") {
          bot.send_message(msg.channel_id,
                           "failed to verify server configuration.");
        } else if (error_type == "permission_denied") {
          bot.send_message(msg.channel_id,
                           "permission error: you do not have the required "
                           "permissions to use this command.");
        } else if (error_type == "server_only") {
          bot.send_message(msg.channel_id,
                           "this command can only be used in a server.");
        }
      });

  bot.on_command_disabled_check([](stoatpp::cluster &bot,
                                   const stoatpp::events::Message &msg,
                                   const stoatpp::Command &cmd) {
    if (msg.server_id.empty() || !bronx::g_db)
      return false;

    std::string cmd_name = cmd.name;
    std::string mod_name = cmd.category;
    std::transform(cmd_name.begin(), cmd_name.end(), cmd_name.begin(),
                   ::tolower);
    std::transform(mod_name.begin(), mod_name.end(), mod_name.begin(),
                   ::tolower);

    if (bronx::g_db->is_disabled(msg.server_id, cmd_name, mod_name)) {
      if (bot.is_owner(msg.author.id) ||
          bot.is_server_owner(msg.server_id, msg.author.id)) {
        if (cmd_name == "command" || cmd_name == "trust" || cmd_name == "cmd") {
          return false;
        }
      }
      return true;
    }
    return false;
  });

  bot.on_command_run([](stoatpp::cluster &bot,
                        const stoatpp::events::Message &msg,
                        const stoatpp::Command &cmd,
                        const std::optional<stoatpp::models::Member> &mem_opt,
                        const std::vector<std::string> &args)
                         -> stoatpp::CommandRunResult {
    if (msg.server_id.empty())
      return stoatpp::CommandRunResult::Allow;
    if (!bronx::g_db)
      return stoatpp::CommandRunResult::Allow;

    std::string cmd_name = cmd.name;
    std::string mod_name = cmd.category;
    std::transform(cmd_name.begin(), cmd_name.end(), cmd_name.begin(),
                   ::tolower);
    std::transform(mod_name.begin(), mod_name.end(), mod_name.begin(),
                   ::tolower);

    // 1. Completely Disabled Commands/Modules Check (ignores invocation
    // completely)
    if (bronx::g_db->is_disabled(msg.server_id, cmd_name, mod_name)) {
      // Bypass for Server Owner or Bot Owner on command config commands to
      // avoid locked out state
      if (bot.is_owner(msg.author.id) ||
          bot.is_server_owner(msg.server_id, msg.author.id)) {
        if (cmd_name == "command" || cmd_name == "trust" || cmd_name == "cmd") {
          return stoatpp::CommandRunResult::AllowBypassPermissions;
        }
      }
      return stoatpp::CommandRunResult::Deny;
    }

    // Emergency Override: Server Owner, Bot Owner, and ManageServer users
    // only bypass config commands.
    bool bypass = false;
    bool is_admin = bot.is_server_owner(msg.server_id, msg.author.id) ||
                    bot.is_owner(msg.author.id);
    if (!is_admin && mem_opt) {
      auto server_opt = bot.get_server(msg.server_id);
      if (server_opt &&
          bot.has_permission(*server_opt, *mem_opt,
                             stoatpp::permissions::ManageServer)) {
        is_admin = true;
      }
    }
    if (is_admin &&
        (cmd_name == "command" || cmd_name == "trust" || cmd_name == "cmd")) {
      bypass = true;
    }

    bool explicitly_allowed_by_role = false;

    // Whitelist/blacklist restrictions check
    if (!bypass) {
      auto rules =
          bronx::g_db->get_restrictions(msg.server_id, "command", cmd_name);
      auto mod_rules =
          bronx::g_db->get_restrictions(msg.server_id, "module", mod_name);
      rules.insert(rules.end(), mod_rules.begin(), mod_rules.end());

      if (!rules.empty()) {
        std::vector<std::string> whitelist_roles;
        std::vector<std::string> whitelist_channels;
        std::vector<int64_t> whitelist_permissions;

        std::vector<std::string> blacklist_roles;
        std::vector<std::string> blacklist_channels;
        std::vector<int64_t> blacklist_permissions;

        for (const auto &re : rules) {
          if (re.restriction_type == "allow") {
            if (re.entity_type == "role")
              whitelist_roles.push_back(re.entity_id);
            else if (re.entity_type == "channel")
              whitelist_channels.push_back(re.entity_id);
            else if (re.entity_type == "permission") {
              whitelist_permissions.push_back(
                  bronx::cogs::parse_permission_name(re.entity_id));
            }
          } else if (re.restriction_type == "restrict") {
            if (re.entity_type == "role")
              blacklist_roles.push_back(re.entity_id);
            else if (re.entity_type == "channel")
              blacklist_channels.push_back(re.entity_id);
            else if (re.entity_type == "permission") {
              blacklist_permissions.push_back(
                  bronx::cogs::parse_permission_name(re.entity_id));
            }
          }
        }

        bool has_channel_whitelist = !whitelist_channels.empty();
        bool has_member_whitelist =
            !whitelist_roles.empty() || !whitelist_permissions.empty();

        if (has_channel_whitelist) {
          bool channel_ok =
              (std::find(whitelist_channels.begin(), whitelist_channels.end(),
                         msg.channel_id) != whitelist_channels.end());
          if (!channel_ok) {
            bot.send_message(
                msg.channel_id,
                "" + bronx::emj_warning + " This command/module is allowed only in specific channels.");
            return stoatpp::CommandRunResult::Deny;
          }
        }

        if (has_member_whitelist) {
          bool member_ok = false;
          if (mem_opt) {
            for (const auto &r : mem_opt->roles) {
              if (std::find(whitelist_roles.begin(), whitelist_roles.end(),
                            r) != whitelist_roles.end()) {
                member_ok = true;
                explicitly_allowed_by_role = true;
                break;
              }
            }
            auto server_opt = bot.get_server(msg.server_id);
            if (server_opt && !member_ok) {
              for (auto mask : whitelist_permissions) {
                if (bot.has_permission(*server_opt, *mem_opt, mask)) {
                  member_ok = true;
                  break;
                }
              }
            }
          }
          if (!member_ok) {
            bot.send_message(
                msg.channel_id,
                "" + bronx::emj_warning + " This command/module is whitelisted and not accessible by "
                "your current roles or permissions.");
            return stoatpp::CommandRunResult::Deny;
          }
        }

        // Check blacklist
        bool matches_blacklist = false;
        if (std::find(blacklist_channels.begin(), blacklist_channels.end(),
                      msg.channel_id) != blacklist_channels.end()) {
          matches_blacklist = true;
        }
        if (mem_opt) {
          for (const auto &r : mem_opt->roles) {
            if (std::find(blacklist_roles.begin(), blacklist_roles.end(), r) !=
                blacklist_roles.end()) {
              matches_blacklist = true;
              break;
            }
          }
          // Check permissions
          auto server_opt = bot.get_server(msg.server_id);
          if (server_opt) {
            for (auto mask : blacklist_permissions) {
              if (bot.has_permission(*server_opt, *mem_opt, mask)) {
                matches_blacklist = true;
                break;
              }
            }
          }
        }

        if (matches_blacklist) {
          bot.send_message(msg.channel_id,
                           "" + bronx::emj_warning + " This command/module has been blacklisted for "
                           "your current roles, permissions, or channel.");
          return stoatpp::CommandRunResult::Deny;
        }
      }
    }

    // 3. Config commands protection (trust, command)
    if (cmd_name == "trust" || cmd_name == "command" || cmd_name == "cmd") {
      if (mem_opt) {
        if (!bronx::cogs::is_user_trusted_or_owner(bot, msg.server_id,
                                                   msg.author.id, *mem_opt)) {
          bot.send_message(
              msg.channel_id,
              "permission error: you need to be trusted or have Manage Server "
              "permission to configure command permissions.");
          return stoatpp::CommandRunResult::Deny;
        }
      }
    }

    if (bypass || explicitly_allowed_by_role) {
      return stoatpp::CommandRunResult::AllowBypassPermissions;
    }

    return stoatpp::CommandRunResult::Allow;
  });

  std::cout
      << "[bronx] starting cluster start() loop. press ctrl+c to terminate.\n";
  bot.start();

  return 0;
}
