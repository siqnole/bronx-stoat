#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "command_config.h"
#include "../database.h"
#include <sstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <mutex>
#include <unordered_set>
#include <nlohmann/json.hpp>

namespace bronx::cogs {

inline std::unordered_set<std::string> g_creating_tickets;
inline std::mutex g_creating_tickets_mutex;

class TicketsModule : public stoatpp::bot_module {
private:
    static std::string format_placeholders(std::string text, const stoatpp::models::User& user, const stoatpp::models::Server& server, int number) {
        std::string mention = "<@" + user.id + ">";
        
        auto replace_all = [](std::string& str, const std::string& from, const std::string& to) {
            size_t start_pos = 0;
            while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
                str.replace(start_pos, from.length(), to);
                start_pos += to.length();
            }
        };

        replace_all(text, "{user}", mention);
        replace_all(text, "{creator}", mention);
        replace_all(text, "{username}", user.username);
        replace_all(text, "{server}", server.name);
        replace_all(text, "{number}", std::to_string(number));
        
        return text;
    }

    static nlohmann::json build_embed(const std::string& embed_json_str, const stoatpp::models::User& user, const stoatpp::models::Server& server, int number) {
        nlohmann::json emb = nlohmann::json::object();
        if (embed_json_str.empty()) return emb;

        try {
            auto parsed = nlohmann::json::parse(embed_json_str);
            if (parsed.contains("title") && parsed["title"].is_string()) {
                emb["title"] = format_placeholders(parsed["title"].get<std::string>(), user, server, number);
            }
            if (parsed.contains("description") && parsed["description"].is_string()) {
                emb["description"] = format_placeholders(parsed["description"].get<std::string>(), user, server, number);
            }
            if (parsed.contains("color") && parsed["color"].is_string()) {
                emb["colour"] = parsed["color"].get<std::string>();
            } else if (parsed.contains("colour") && parsed["colour"].is_string()) {
                emb["colour"] = parsed["colour"].get<std::string>();
            }
            if (parsed.contains("image") && parsed["image"].is_string()) {
                emb["image"] = format_placeholders(parsed["image"].get<std::string>(), user, server, number);
            }
        } catch(...) {}

        return emb;
    }

    struct ParsedViewers {
        std::vector<std::string> roles;
        std::vector<std::string> users;
        std::vector<std::string> permissions;
    };

    static ParsedViewers parse_viewers(const std::string& input, stoatpp::cluster& bot, const std::string& server_id) {
        ParsedViewers v;
        if (input.empty() || input == "none" || input == "null") return v;

        // Tokenize: split by comma AND by whitespace so both "a,b" and "a b" work.
        // We also strip bare <%...> and <@...> wrappers from each token.
        std::vector<std::string> tokens;
        {
            std::stringstream outer(input);
            std::string chunk;
            while (std::getline(outer, chunk, ',')) {
                std::stringstream inner(chunk);
                std::string tok;
                while (inner >> tok) {   // >> skips all whitespace
                    tokens.push_back(tok);
                }
            }
        }

        for (auto token : tokens) {
            if (token.empty()) continue;

            if (token.rfind("<@", 0) == 0 && token.back() == '>') {
                std::string uid = token.substr(2, token.size() - 3);
                if (uid.rfind("!", 0) == 0) uid = uid.substr(1);
                v.users.push_back(uid);
            }
            else if (token.rfind("<%", 0) == 0 && token.back() == '>') {
                std::string rid = token.substr(2, token.size() - 3);
                v.roles.push_back(rid);
            }
            else if (parse_permission_name(token) > 0) {
                int64_t perm_mask = parse_permission_name(token);
                std::string perm_name = "";
                if (perm_mask == stoatpp::permissions::ManageServer) perm_name = "ManageServer";
                else if (perm_mask == stoatpp::permissions::ManageChannels) perm_name = "ManageChannels";
                else if (perm_mask == stoatpp::permissions::ManageRole) perm_name = "ManageRole";
                else if (perm_mask == stoatpp::permissions::ManageMessages) perm_name = "ManageMessages";
                else if (perm_mask == stoatpp::permissions::KickMembers) perm_name = "KickMembers";
                else if (perm_mask == stoatpp::permissions::BanMembers) perm_name = "BanMembers";
                else perm_name = token;
                
                v.permissions.push_back(perm_name);
            }
            else {
                auto srv_opt = bot.get_server(server_id);
                std::string matched_role_id = "";
                if (srv_opt) {
                    for (const auto& r : srv_opt->roles) {
                        if (r.id == token || r.name == token) {
                            matched_role_id = r.id;
                            break;
                        }
                    }
                }
                if (!matched_role_id.empty()) {
                    v.roles.push_back(matched_role_id);
                } else {
                    if (token.size() == 26) {
                        v.users.push_back(token);
                    }
                }
            }
        }
        return v;
    }

    static std::string join_vec(const std::vector<std::string>& v) {
        std::string s = "";
        for (size_t i = 0; i < v.size(); ++i) {
            s += v[i] + (i + 1 < v.size() ? "," : "");
        }
        return s;
    }

    // Strip <%roleId> or <@userId> mention wrappers, returning just the bare ID.
    static std::string strip_mention(std::string s) {
        if (s.size() >= 4 && s[0] == '<' && (s[1] == '%' || s[1] == '@') && s.back() == '>') {
            s = s.substr(2, s.size() - 3);
            // Strip optional '!' inside <@!id>
            if (!s.empty() && s[0] == '!') s = s.substr(1);
        }
        return s;
    }

    static std::vector<std::string> split_str(const std::string& s) {
        std::vector<std::string> v;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, ',')) {
            if (!item.empty()) v.push_back(strip_mention(item));
        }
        return v;
    }

    static void execute_create_ticket(stoatpp::cluster& bot, const std::string& server_id, const std::string& user_id, const std::string& panel_name, const std::string& modal_channel_id = "") {
        // Prevent concurrent creations for the same user
        {
            std::lock_guard<std::mutex> creating_lock(g_creating_tickets_mutex);
            if (g_creating_tickets.find(user_id) != g_creating_tickets.end()) return;
            g_creating_tickets.insert(user_id);
        }

        std::thread([&bot, server_id, user_id, panel_name, modal_channel_id]() {
            auto clean_creating = [user_id]() {
                std::lock_guard<std::mutex> creating_lock(g_creating_tickets_mutex);
                g_creating_tickets.erase(user_id);
            };

            if (!g_db) {
                clean_creating();
                return;
            }

            auto config_opt = g_db->get_ticket_config(server_id, panel_name);
            if (!config_opt) {
                clean_creating();
                return;
            }
            auto config = *config_opt;

            // Check if user already has an open ticket for this specific panel (or server)
            auto active_opt = g_db->get_user_open_ticket(server_id, user_id);
            if (active_opt && active_opt->panel_name == panel_name) {
                // Verify if channel actually exists in server
                auto cached_channel = bot.get_channel(active_opt->channel_id);
                bool channel_exists = true;
                if (!cached_channel) {
                    auto rest_channel = bot.rest().get_channel(active_opt->channel_id);
                    if (!rest_channel.success() && rest_channel.status_code == 404) {
                        channel_exists = false;
                    }
                }

                if (!channel_exists) {
                    g_db->delete_ticket(active_opt->channel_id);
                    active_opt = std::nullopt;
                }
            }

            if (active_opt && active_opt->panel_name == panel_name) {
                stoatpp::models::MessagePayload payload;
                payload.content = "" + bronx::emj_warning + " You already have an open ticket for this panel: <#" + active_opt->channel_id + ">";
                payload.delete_after = 5;
                if (!modal_channel_id.empty()) {
                    bot.send_message(modal_channel_id, payload);
                } else {
                    bot.send_message(config.modal_channel_id, payload);
                }
                clean_creating();
                return;
            }

            // Increment counter and save config
            config.ticket_counter++;
            g_db->save_ticket_config(config);

            // Fetch user info — try cache first, fall back to REST so alt-server
            // users not present in the Ready payload don't cause a silent abort.
            auto user_opt = bot.get_user(user_id);
            std::optional<stoatpp::models::User> user_rest_holder;
            if (!user_opt) {
                auto ures = bot.rest().get_user(user_id);
                if (ures.success() && ures.body.is_object()) {
                    stoatpp::models::User u;
                    u.id = ures.body.contains("_id") ? ures.body["_id"].get<std::string>() : user_id;
                    u.username = ures.body.contains("username") ? ures.body["username"].get<std::string>() : "user";
                    u.bot = ures.body.value("bot", false);
                    user_rest_holder = u;
                    user_opt = user_rest_holder;
                }
            }
            auto server_opt = bot.get_server(server_id);
            if (!user_opt || !server_opt) {
                std::cerr << "[bronx tickets] execute_create_ticket: could not resolve user (" << user_id
                          << ") or server (" << server_id << ") — aborting.\n";
                clean_creating();
                return;
            }

            // Generate channel name based on scheme
            std::string name_scheme = config.naming_scheme;
            if (name_scheme.empty()) name_scheme = "ticket-{number}";
            
            std::string channel_name = format_placeholders(name_scheme, *user_opt, *server_opt, config.ticket_counter);
            std::transform(channel_name.begin(), channel_name.end(), channel_name.begin(), ::tolower);

            // Create text channel
            auto chan_res = bot.rest().create_channel(server_id, "Text", channel_name);
            if (!chan_res.success()) {
                std::cerr << "[bronx tickets] failed to create channel: " << chan_res.error_message() << "\n";
                clean_creating();
                return;
            }

            std::string ticket_channel_id = chan_res.body["_id"].get<std::string>();

            // Categorize the channel if category is configured
            // FIXME: once https://github.com/stoatchat/stoatchat/pull/821/ is merged this works
            // if (!config.category_id.empty()) {
            //     bot.move_channel_to_category(server_id, ticket_channel_id, config.category_id);
            // }

            // Create a custom role named after the user for this ticket
            std::string user_role_name = "ticket-" + user_opt->username + "-" + std::to_string(config.ticket_counter);
            auto role_res = bot.rest().create_role(server_id, user_role_name);
            std::string user_role_id = "";
            if (role_res.success()) {
                if (role_res.body.contains("id") && role_res.body["id"].is_string()) {
                    user_role_id = role_res.body["id"].get<std::string>();
                } else if (role_res.body.contains("role") && role_res.body["role"].is_object() && role_res.body["role"].contains("id") && role_res.body["role"]["id"].is_string()) {
                    user_role_id = role_res.body["role"]["id"].get<std::string>();
                }
            }

            if (!user_role_id.empty()) {
                // Move the ticket role above the member role so its ALLOW overrides
                // the member role's DENY in Revolt's role-priority permission calculation.
                if (!config.member_role_id.empty()) {
                    auto srv_res = bot.rest().get_server(server_id);
                    if (srv_res.success()) {
                        stoatpp::models::Server fresh_server = stoatpp::models::Server::from_json(srv_res.body);
                        std::vector<stoatpp::models::Role> sorted_roles = fresh_server.roles;
                        std::cout << "[bronx debug] reordering roles using fresh server info. Total roles: " << sorted_roles.size() << "\n";
                        for (const auto& r : sorted_roles) {
                            std::cout << "  - Server Role ID: " << r.id << ", Name: " << r.name << ", Rank: " << r.rank << "\n";
                        }

                        std::sort(sorted_roles.begin(), sorted_roles.end(), [](const auto& a, const auto& b) {
                            return a.rank < b.rank;
                        });

                        std::vector<std::string> rank_order;
                        bool inserted = false;
                        int64_t member_rank = 0;
                        bool found_member = false;
                        for (const auto& r : sorted_roles) {
                            if (r.id == config.member_role_id) {
                                member_rank = r.rank;
                                found_member = true;
                            }
                            if (r.id == user_role_id) {
                                continue;
                            }
                            if (r.id == config.member_role_id) {
                                rank_order.push_back(user_role_id);
                                inserted = true;
                            }
                            rank_order.push_back(r.id);
                        }
                        if (!inserted) {
                            rank_order.insert(rank_order.begin(), user_role_id);
                        }

                        std::cout << "[bronx debug] target member role ID: " << config.member_role_id << ", found: " << (found_member ? "yes" : "no") << " (rank: " << member_rank << ")\n";
                        std::cout << "[bronx debug] rank_order list:\n";
                        for (size_t idx = 0; idx < rank_order.size(); ++idx) {
                            std::cout << "  [" << idx << "] " << rank_order[idx] << "\n";
                        }

                        // 1. Bulk edit server role positions route
                        auto res1 = bot.rest().edit_role_ranks(server_id, rank_order);
                        std::cout << "[bronx debug] patch /roles/ranks status: " << res1.status_code << ", success: " << (res1.success() ? "yes" : "no") << ", body: " << res1.body.dump() << "\n";
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));

                        // 2. Fallback: Edit individual role rank
                        if (found_member) {
                            nlohmann::json ind_body;
                            ind_body["rank"] = member_rank - 1;
                            auto res2 = bot.rest().edit_role(server_id, user_role_id, ind_body);
                            std::cout << "[bronx debug] patch /roles/" << user_role_id << " status: " << res2.status_code << ", success: " << (res2.success() ? "yes" : "no") << ", body: " << res2.body.dump() << "\n";
                            std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        }
                    }
                }

                bot.add_role_to_member(server_id, user_id, user_role_id);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            int64_t creator_allow = stoatpp::permissions::ViewChannel | 
                                    stoatpp::permissions::SendMessages | 
                                    stoatpp::permissions::EmbedLinks | 
                                    stoatpp::permissions::UploadFiles | 
                                    stoatpp::permissions::AddReactions;

            if (!user_role_id.empty()) {
                bot.rest().set_channel_permission(ticket_channel_id, user_role_id, creator_allow, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            std::unordered_set<std::string> allowed_role_ids;
            if (!user_role_id.empty()) {
                allowed_role_ids.insert(user_role_id);
            }

            auto roles = split_str(config.view_roles);
            for (const auto& rid : roles) {
                // Skip setting permissions for roles that are equal to or higher rank than the bot's highest role
                bool skip_role = false;
                auto bot_member_opt = bot.get_member(server_id, bot.current_user().id);
                std::optional<stoatpp::models::Member> bot_rest_member;
                if (!bot_member_opt) {
                    auto res = bot.rest().get_server_member(server_id, bot.current_user().id);
                    if (res.success() && res.body.is_object()) {
                        bot_rest_member = stoatpp::models::Member::from_json(res.body);
                        bot_member_opt = bot_rest_member;
                    }
                }
                if (bot_member_opt && server_opt) {
                    int bot_highest = server_opt->get_highest_rank(*bot_member_opt);
                    if (bot_highest != 999999) {
                        for (const auto& r : server_opt->roles) {
                            if (r.id == rid) {
                                if (r.rank <= bot_highest) {
                                    skip_role = true;
                                }
                                break;
                            }
                        }
                    }
                }
                if (skip_role) continue;

                allowed_role_ids.insert(rid);
                bot.rest().set_channel_permission(ticket_channel_id, rid, 
                    creator_allow | stoatpp::permissions::ManageMessages, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            auto users = split_str(config.view_users);
            for (const auto& uid : users) {
                if (uid == bot.current_user().id) continue;

                bool skip_user = false;
                auto target_member_opt = bot.get_member(server_id, uid);
                std::optional<stoatpp::models::Member> target_rest_member;
                if (!target_member_opt) {
                    auto res = bot.rest().get_server_member(server_id, uid);
                    if (res.success() && res.body.is_object()) {
                        target_rest_member = stoatpp::models::Member::from_json(res.body);
                        target_member_opt = target_rest_member;
                    }
                }
                
                auto bot_member_opt = bot.get_member(server_id, bot.current_user().id);
                std::optional<stoatpp::models::Member> bot_rest_member;
                if (!bot_member_opt) {
                    auto res = bot.rest().get_server_member(server_id, bot.current_user().id);
                    if (res.success() && res.body.is_object()) {
                        bot_rest_member = stoatpp::models::Member::from_json(res.body);
                        bot_member_opt = bot_rest_member;
                    }
                }

                if (bot_member_opt && target_member_opt && server_opt) {
                    int bot_highest = server_opt->get_highest_rank(*bot_member_opt);
                    int target_highest = server_opt->get_highest_rank(*target_member_opt);
                    if (bot_highest != 999999 && target_highest != 999999) {
                        if (target_highest <= bot_highest) {
                            skip_user = true;
                        }
                    }
                }
                if (skip_user) continue;

                bot.rest().set_channel_permission(ticket_channel_id, uid, 
                    creator_allow | stoatpp::permissions::ManageMessages, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            auto perms = split_str(config.view_permissions);
            for (const auto& perm_name : perms) {
                int64_t target_perm = parse_permission_name(perm_name);
                if (target_perm == 0) continue;

                if (server_opt) {
                    for (const auto& r : server_opt->roles) {
                        int64_t allowed = r.allowed_permissions();

                        if ((allowed & target_perm) != 0) {
                            // Skip setting permissions for roles that are equal to or higher rank than the bot's highest role
                            bool skip_role = false;
                            auto bot_member_opt = bot.get_member(server_id, bot.current_user().id);
                            std::optional<stoatpp::models::Member> bot_rest_member;
                            if (!bot_member_opt) {
                                auto res = bot.rest().get_server_member(server_id, bot.current_user().id);
                                if (res.success() && res.body.is_object()) {
                                    bot_rest_member = stoatpp::models::Member::from_json(res.body);
                                    bot_member_opt = bot_rest_member;
                                }
                            }
                            if (bot_member_opt && server_opt) {
                                int bot_highest = server_opt->get_highest_rank(*bot_member_opt);
                                if (bot_highest != 999999) {
                                    if (r.rank <= bot_highest) {
                                        skip_role = true;
                                    }
                                }
                            }
                            if (skip_role) continue;

                            allowed_role_ids.insert(r.id);
                            bot.rest().set_channel_permission(ticket_channel_id, r.id, 
                                creator_allow | stoatpp::permissions::ManageMessages, 0);
                            std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        }
                    }
                }
            }

            // Grant the bot's own server roles explicit ALLOW so it keeps channel
            // access after the member role DENY is applied below.
            // User-level overrides are unreliable on Revolt; role-level ones work correctly.
            {
                std::string bot_id = bot.current_user().id;
                auto bot_mbr = bot.rest().get_server_member(server_id, bot_id);
                int64_t bot_allow = stoatpp::permissions::ViewChannel |
                                    stoatpp::permissions::SendMessages |
                                    stoatpp::permissions::EmbedLinks |
                                    stoatpp::permissions::UploadFiles |
                                    stoatpp::permissions::AddReactions |
                                    stoatpp::permissions::ManageMessages;
                if (bot_mbr.success() && bot_mbr.body.contains("roles") && bot_mbr.body["roles"].is_array()) {
                    for (const auto& rj : bot_mbr.body["roles"]) {
                        std::string rid = rj.get<std::string>();
                        bot.rest().set_channel_permission(ticket_channel_id, rid, bot_allow, 0);
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                }
            }

            // Deny the server's "member" role in this channel so it's private.
            // We target the configured member_role_id rather than @default because
            // the @default channel override nukes all other role overrides on Revolt.
            // Standard role priority then lets the ticket role's ALLOW take precedence.
            if (!config.member_role_id.empty()) {
                bool skip_role = false;
                auto bot_member_opt = bot.get_member(server_id, bot.current_user().id);
                std::optional<stoatpp::models::Member> bot_rest_member;
                if (!bot_member_opt) {
                    auto res = bot.rest().get_server_member(server_id, bot.current_user().id);
                    if (res.success() && res.body.is_object()) {
                        bot_rest_member = stoatpp::models::Member::from_json(res.body);
                        bot_member_opt = bot_rest_member;
                    }
                }
                if (bot_member_opt && server_opt) {
                    int bot_highest = server_opt->get_highest_rank(*bot_member_opt);
                    if (bot_highest != 999999) {
                        for (const auto& r : server_opt->roles) {
                            if (r.id == config.member_role_id) {
                                if (r.rank <= bot_highest) {
                                    skip_role = true;
                                }
                                break;
                            }
                        }
                    }
                }

                if (!skip_role) {
                    int64_t lock_deny = stoatpp::permissions::ViewChannel |
                                        stoatpp::permissions::SendMessages |
                                        stoatpp::permissions::EmbedLinks |
                                        stoatpp::permissions::UploadFiles |
                                        stoatpp::permissions::AddReactions;
                    bot.rest().set_channel_permission(ticket_channel_id, config.member_role_id, 0, lock_deny);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }

            stoatpp::models::MessagePayload payload;
            std::string welcome_txt = config.ticket_welcome_content;
            if (welcome_txt.empty()) welcome_txt = "Welcome {user}!";
            payload.content = format_placeholders(welcome_txt, *user_opt, *server_opt, config.ticket_counter);

            nlohmann::json embed = nlohmann::json::object();
            if (!config.ticket_welcome_embed_json.empty()) {
                embed = build_embed(config.ticket_welcome_embed_json, *user_opt, *server_opt, config.ticket_counter);
            } else {
                embed["title"] = "🎫 Ticket Support (" + panel_name + ")";
                embed["description"] = "Thank you for opening a support ticket, " + user_opt->username + "!\nSupport staff will be with you shortly.\nReact with " + config.ticket_close_emoji + " to close this ticket.";
                embed["colour"] = "#6C5CE7";
            }
            payload.embeds.push_back(embed);
            payload.set_interactions({config.ticket_close_emoji});

            bot.send_message(ticket_channel_id, payload, [&bot, server_id, user_id, config, ticket_channel_id, panel_name](stoatpp::models::Message res, bool ok) {
                if (ok) {
                    Database::Ticket ticket;
                    ticket.channel_id = ticket_channel_id;
                    ticket.server_id = server_id;
                    ticket.user_id = user_id;
                    ticket.ticket_number = config.ticket_counter;
                    ticket.welcome_message_id = res.id;
                    ticket.status = "open";
                    ticket.panel_name = panel_name;

                    g_db->save_ticket(ticket);
                }
            });

            clean_creating();
        }).detach();
    }

    static void execute_close_ticket(stoatpp::cluster& bot, const std::string& channel_id) {
        if (!g_db) return;

        auto ticket_opt = g_db->get_ticket(channel_id);
        if (!ticket_opt || ticket_opt->status == "closed") return;

        auto ticket = *ticket_opt;
        ticket.status = "closed";
        g_db->save_ticket(ticket);

        auto tc_opt = g_db->get_ticket_config(ticket.server_id, ticket.panel_name);
        std::string msg_text = "🔒 **Closing ticket...**\nThis channel will be deleted in 5 seconds.";
        if (tc_opt && !tc_opt->ticket_close_content.empty()) {
            msg_text = tc_opt->ticket_close_content;
        }

        auto user_opt = bot.get_user(ticket.user_id);
        auto server_opt = bot.get_server(ticket.server_id);
        if (user_opt && server_opt) {
            msg_text = format_placeholders(msg_text, *user_opt, *server_opt, ticket.ticket_number);
        }

        bot.send_message(channel_id, msg_text);

        std::thread([&bot, ticket, channel_id]() {
            // Delete custom user-role
            auto ures = bot.rest().get_user(ticket.user_id);
            if (ures.success() && ures.body.is_object()) {
                std::string username = ures.body.value("username", "user");
                std::string target_role_name = "ticket-" + username + "-" + std::to_string(ticket.ticket_number);
                std::string matched_role_id = "";
                auto srv_res = bot.rest().get_server(ticket.server_id);
                if (srv_res.success()) {
                    if (srv_res.body.contains("roles") && srv_res.body["roles"].is_object()) {
                        for (auto& [role_id, role_json] : srv_res.body["roles"].items()) {
                            if (role_json.contains("name") && role_json["name"].is_string()) {
                                if (role_json["name"].get<std::string>() == target_role_name) {
                                    matched_role_id = role_id;
                                    break;
                                }
                            }
                        }
                    }
                }

                if (!matched_role_id.empty()) {
                    bot.remove_role_from_member(ticket.server_id, ticket.user_id, matched_role_id);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    bot.rest().delete_role(ticket.server_id, matched_role_id);
                }
            }

            std::this_thread::sleep_for(std::chrono::seconds(5));
            bot.rest().delete_channel(channel_id);
            g_db->delete_ticket(channel_id);
        }).detach();
    }

    static void send_ticket_panel(stoatpp::cluster& bot, const Database::TicketConfig& tc, const std::string& notify_channel_id) {
        stoatpp::models::MessagePayload payload;
        payload.content = tc.modal_content.empty() ? " " : tc.modal_content;

        nlohmann::json embed = nlohmann::json::object();
        if (!tc.modal_embed_json.empty()) {
            try {
                embed = nlohmann::json::parse(tc.modal_embed_json);
            } catch(...) {}
        } else {
            embed["title"] = "🎫 Open a Ticket (" + tc.name + ")";
            embed["description"] = "React with " + tc.reaction_emoji + " below or send a message in this channel to open a support ticket.";
            embed["colour"] = "#6C5CE7";
        }
        payload.embeds.push_back(embed);
        payload.set_interactions({tc.reaction_emoji});

        bot.send_message(tc.modal_channel_id, payload, [&bot, notify_channel_id, tc](stoatpp::models::Message res, bool ok) {
            if (ok) {
                auto update_tc = tc;
                update_tc.modal_message_id = res.id;
                if (g_db) {
                    g_db->save_ticket_config(update_tc);
                }

                bot.send_message(notify_channel_id, "" + bronx::emj_yes + " Ticket panel `" + tc.name + "` sent to <#" + tc.modal_channel_id + ">.");
            } else {
                bot.send_message(notify_channel_id, "" + bronx::emj_warning + " Failed to send ticket panel to <#" + tc.modal_channel_id + ">. Make sure I have posting permissions in that channel.");
            }
        });
    }

public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "ticket",
            {"tickets", "t"},
            "manage and configure the ticket system.",
            "",
            {"<subcommand>", "[args...]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling ticket...\n";
                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " This command can only be used in a server channel.");
                    return;
                }

                if (!g_db) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Database is unavailable.");
                    return;
                }

                std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                if (pfx.rfind("<@", 0) == 0) {
                    auto resolved = bot.config().prefix_resolver ? bot.config().prefix_resolver(bot, msg) : std::vector<std::string>{};
                    bool found_clean = false;
                    for (const auto& r : resolved) {
                        if (r.rfind("<@", 0) != 0) {
                            pfx = r;
                            found_clean = true;
                            break;
                        }
                    }
                    if (!found_clean) pfx = bot.config().command_prefix;
                }

                auto server_opt = bot.get_server(msg.server_id);
                if (!server_opt) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to resolve server details.");
                    return;
                }

                if (args.empty()) {
                    bot.send_message(msg.channel_id, "📋 **Ticket System Commands:**\n"
                                                     "• `" + pfx + "ticket setup [panel]` — Setup wizard for a named ticket panel.\n"
                                                     "• `" + pfx + "ticket edit [panel] [key:val ...]` — Edit panel settings (omit keys to list all).\n"
                                                     "• `" + pfx + "ticket panel [panel] [#channel]` — Send the ticket creation panel.\n"
                                                     "• `" + pfx + "ticket list` — List all configured panels.\n"
                                                     "• `" + pfx + "ticket delete [panel]` — Delete a configured ticket panel.\n"
                                                     "• `" + pfx + "ticket close` — Close the current ticket.\n"
                                                     "• `" + pfx + "ticket add <@&role>` — Add a role to the current ticket.\n"
                                                     "• `" + pfx + "ticket remove <@&role>` — Remove a role from the current ticket.\n"
                                                     "• `" + pfx + "ticket config [panel]` — Show full panel configuration.");
                    return;
                }

                std::string subcommand = args[0];
                std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);

                if (subcommand == "close") {
                    auto ticket_opt = g_db->get_ticket(msg.channel_id);
                    if (!ticket_opt) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " This channel is not a registered ticket channel.");
                        return;
                    }
                    execute_close_ticket(bot, msg.channel_id);
                    return;
                }

                if (subcommand == "add") {
                    auto ticket_opt = g_db->get_ticket(msg.channel_id);
                    if (!ticket_opt) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " This channel is not a registered ticket channel.");
                        return;
                    }
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "ticket add <@&role|roleID>`");
                        return;
                    }
                    std::string target = args[1];
                    std::string target_role_id = target;
                    if (target.rfind("<%", 0) == 0 && target.back() == '>') {
                        target_role_id = target.substr(2, target.size() - 3);
                    } else if (target.rfind("<@&", 0) == 0 && target.back() == '>') {
                        target_role_id = target.substr(3, target.size() - 4);
                    }

                    int64_t creator_allow = stoatpp::permissions::ViewChannel | 
                                            stoatpp::permissions::SendMessages | 
                                            stoatpp::permissions::EmbedLinks | 
                                            stoatpp::permissions::UploadFiles | 
                                            stoatpp::permissions::AddReactions;
                    bot.rest().set_channel_permission(msg.channel_id, target_role_id, creator_allow, 0);
                    bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Added role <%" + target_role_id + "> to the ticket.");
                    return;
                }

                if (subcommand == "remove") {
                    auto ticket_opt = g_db->get_ticket(msg.channel_id);
                    if (!ticket_opt) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " This channel is not a registered ticket channel.");
                        return;
                    }
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "usage: `" + pfx + "ticket remove <@&role|roleID>`");
                        return;
                    }
                    std::string target = args[1];
                    std::string target_role_id = target;
                    if (target.rfind("<%", 0) == 0 && target.back() == '>') {
                        target_role_id = target.substr(2, target.size() - 3);
                    } else if (target.rfind("<@&", 0) == 0 && target.back() == '>') {
                        target_role_id = target.substr(3, target.size() - 4);
                    }

                    bot.rest().delete_channel_permission(msg.channel_id, target_role_id);
                    bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Removed role <%" + target_role_id + "> from the ticket.");
                    return;
                }

                bot.fetch_member(msg.server_id, msg.author.id, [this, &bot, msg, pfx, args, subcommand, server_opt](stoatpp::models::Member member, bool success) {
                    if (!success) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to verify permissions.");
                        return;
                    }
                    if (!is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, member)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Permission error: you need Manage Server permission to configure the ticket system.");
                        return;
                    }

                    if (subcommand == "list") {
                        auto list = g_db->get_ticket_configs_for_server(msg.server_id);
                        if (list.empty()) {
                            bot.send_message(msg.channel_id, "ℹ️ No ticket panels configured on this server.");
                            return;
                        }

                        stoatpp::models::MessagePayload payload;
                        nlohmann::json embed = nlohmann::json::object();
                        embed["title"] = "🎫 Configured Ticket Panels";
                        embed["colour"] = "#6C5CE7";

                        std::string desc = "Here are the ticket panels set up on this server:\n\n";
                        for (const auto& tc : list) {
                            desc += "• **" + tc.name + "** — Channel: " + (tc.modal_channel_id.empty() ? "*None*" : "<#" + tc.modal_channel_id + ">") + " | Counter: `" + std::to_string(tc.ticket_counter) + "`\n";
                        }
                        embed["description"] = desc;
                        payload.embeds.push_back(embed);
                        bot.send_message(msg.channel_id, payload);
                        return;
                    }

                    if (subcommand == "delete") {
                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "usage: `" + pfx + "ticket delete [panel]`");
                            return;
                        }
                        std::string target_panel = args[1];
                        auto tc_opt = g_db->get_ticket_config(msg.server_id, target_panel);
                        if (!tc_opt) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Ticket panel `" + target_panel + "` not found.");
                            return;
                        }
                        if (g_db->delete_ticket_config(msg.server_id, target_panel)) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Ticket panel `" + target_panel + "` successfully deleted.");
                        } else {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to delete ticket panel `" + target_panel + "`.");
                        }
                        return;
                    }

                    std::string panel_name = (args.size() > 1) ? args[1] : "default";

                    if (subcommand == "config") {
                        auto tc_opt = g_db->get_ticket_config(msg.server_id, panel_name);
                        if (!tc_opt) {
                            bot.send_message(msg.channel_id, "ℹ️ Ticket panel `" + panel_name + "` is not configured.");
                            return;
                        }
                        auto tc = *tc_opt;

                        stoatpp::models::MessagePayload payload;
                        nlohmann::json embed = nlohmann::json::object();
                        embed["title"] = "🎫 Ticket Panel Configuration: `" + panel_name + "`";
                        embed["colour"] = "#6C5CE7";

                        std::string desc = "• **Modal Channel:** " + (tc.modal_channel_id.empty() ? "*None*" : "<#" + tc.modal_channel_id + ">") + "\n"
                                           "• **Reaction Emoji:** " + tc.reaction_emoji + "\n"
                                           "• **Input Creation:** " + (tc.input_creation_enabled ? "Enabled" : "Disabled") + "\n"
                                           "• **Naming Scheme:** `" + tc.naming_scheme + "`\n"
                                           "• **Category:** " + (tc.category_id.empty() ? "*None*" : "`" + tc.category_id + "`") + "\n";
                        
                        if (!tc.view_roles.empty()) {
                            desc += "• **Allowed Roles:** ";
                            auto r_list = split_str(tc.view_roles);
                            for (size_t i = 0; i < r_list.size(); ++i) {
                                desc += "<%" + r_list[i] + ">" + (i + 1 < r_list.size() ? ", " : "");
                            }
                            desc += "\n";
                        }
                        if (!tc.view_users.empty()) {
                            desc += "• **Allowed Users:** ";
                            auto u_list = split_str(tc.view_users);
                            for (size_t i = 0; i < u_list.size(); ++i) {
                                desc += "<@" + u_list[i] + ">" + (i + 1 < u_list.size() ? ", " : "");
                            }
                            desc += "\n";
                        }
                        if (!tc.view_permissions.empty()) {
                            desc += "• **Allowed Permissions:** `" + tc.view_permissions + "`\n";
                        }
                        desc += "• **Close Reaction:** " + tc.ticket_close_emoji + "\n"
                                "• **Close Message:** `" + (tc.ticket_close_content.empty() ? "*Default*" : tc.ticket_close_content) + "`\n"
                                "• **Ticket Counter:** " + std::to_string(tc.ticket_counter);
                        
                        embed["description"] = desc;
                        payload.embeds.push_back(embed);
                        bot.send_message(msg.channel_id, payload);
                        return;
                    }

                    // ── ticket edit ──────────────────────────────────────────────────────
                    if (subcommand == "edit") {
                        // Determine panel name: if args[1] doesn't contain ':' treat as panel name
                        std::string edit_panel = "default";
                        size_t kv_start = 1; // index in args where key:value pairs begin
                        if (args.size() > 1 && args[1].find(':') == std::string::npos) {
                            edit_panel = args[1];
                            kv_start = 2;
                        }

                        auto tc_opt = g_db->get_ticket_config(msg.server_id, edit_panel);
                        if (!tc_opt) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Ticket panel `" + edit_panel + "` is not configured. Use `" + pfx + "ticket setup " + edit_panel + "` first.");
                            return;
                        }

                        // No key:value pairs — show the editable keys with current values
                        if (args.size() <= kv_start) {
                            auto& tc = *tc_opt;
                            stoatpp::models::MessagePayload payload;
                            nlohmann::json embed = nlohmann::json::object();
                            embed["title"] = "✏️ Ticket Edit Keys — Panel `" + edit_panel + "`";
                            embed["colour"] = "#6C5CE7";
                            std::string desc =
                                "Use `" + pfx + "ticket edit " + edit_panel + " key:value [key:value ...]` to modify.\n"
                                "You can supply multiple keys in one command.\n\n"
                                "**Key** | **Current Value**\n"
                                "```\n"
                                "channel:    " + (tc.modal_channel_id.empty()  ? "(none)"     : tc.modal_channel_id)  + "\n"
                                "emoji:      " + (tc.reaction_emoji.empty()     ? "(none)"     : tc.reaction_emoji)    + "\n"
                                "naming:     " + (tc.naming_scheme.empty()      ? "(none)"     : tc.naming_scheme)     + "\n"
                                "category:   " + (tc.category_id.empty()        ? "(none)"     : tc.category_id)       + "\n"
                                "input:      " + (tc.input_creation_enabled     ? "true"       : "false")              + "\n"
                                "viewers:    " + (tc.view_roles.empty() && tc.view_users.empty() && tc.view_permissions.empty()
                                                   ? "(none)"
                                                   : "roles=[" + tc.view_roles + "] users=[" + tc.view_users + "] perms=[" + tc.view_permissions + "]") + "\n"
                                "welcome:    " + (tc.ticket_welcome_content.empty() ? "(default)" : tc.ticket_welcome_content) + "\n"
                                "close_emoji:" + (tc.ticket_close_emoji.empty()  ? "(default)" : tc.ticket_close_emoji) + "\n"
                                "close_msg:  " + (tc.ticket_close_content.empty() ? "(default)" : tc.ticket_close_content) + "\n"
                                "member_role:" + (tc.member_role_id.empty() ? "(none - channel will be public!)" : "<%" + tc.member_role_id + ">") + "\n"
                                "```";
                            embed["description"] = desc;
                            payload.embeds.push_back(embed);
                            bot.send_message(msg.channel_id, payload);
                            return;
                        }

                        // Reconstruct the key:value text from remaining args
                        std::string kv_text;
                        for (size_t i = kv_start; i < args.size(); ++i) {
                            if (i > kv_start) kv_text += " ";
                            kv_text += args[i];
                        }

                        // Parse key:value pairs (ticket-specific keys)
                        // We parse manually rather than reusing parse_kv_pairs to keep tickets.h self-contained
                        struct TicketKP { std::string key; size_t pos; };
                        std::vector<std::string> tkt_keys = {
                            "channel:", "emoji:", "naming:", "category:",
                            "viewers:", "welcome:", "close_emoji:", "close_msg:",
                            "input:", "member_role:"
                        };
                        std::vector<TicketKP> occ;
                        for (const auto& key : tkt_keys) {
                            size_t pos = 0;
                            while ((pos = kv_text.find(key, pos)) != std::string::npos) {
                                if (pos == 0 || std::isspace((unsigned char)kv_text[pos-1]))
                                    occ.push_back({key, pos});
                                pos += key.size();
                            }
                        }

                        if (occ.empty()) {
                            bot.send_message(msg.channel_id,
                                "" + bronx::emj_warning + " No valid keys found. Run `" + pfx + "ticket edit " + edit_panel + "` with no args to see all keys.");
                            return;
                        }

                        std::sort(occ.begin(), occ.end(), [](const TicketKP& a, const TicketKP& b){ return a.pos < b.pos; });

                        std::unordered_map<std::string, std::string> pairs;
                        for (size_t i = 0; i < occ.size(); ++i) {
                            size_t sv = occ[i].pos + occ[i].key.size();
                            size_t ev = (i+1 < occ.size()) ? occ[i+1].pos : kv_text.size();
                            std::string val = kv_text.substr(sv, ev - sv);
                            val.erase(0, val.find_first_not_of(" \t\r\n"));
                            if (!val.empty()) val.erase(val.find_last_not_of(" \t\r\n,") + 1);
                            std::string k = occ[i].key; k.pop_back(); // strip trailing ':'
                            pairs[k] = val;
                        }

                        auto tc = *tc_opt;
                        std::vector<std::string> notes;

                        for (const auto& [k, v] : pairs) {
                            if (k == "channel") {
                                std::string chan_id = v;
                                if (chan_id.rfind("<#", 0) == 0 && chan_id.back() == '>') {
                                    chan_id = chan_id.substr(2, chan_id.size() - 3);
                                }
                                tc.modal_channel_id = chan_id;
                                notes.push_back("channel→<#" + chan_id + ">");
                            } else if (k == "emoji") {
                                tc.reaction_emoji = v;
                                notes.push_back("emoji→" + v);
                            } else if (k == "naming") {
                                tc.naming_scheme = v;
                                notes.push_back("naming→`" + v + "`");
                            } else if (k == "category") {
                                if (v == "none" || v == "clear") {
                                    tc.category_id = "";
                                    notes.push_back("category cleared");
                                } else {
                                    tc.category_id = v;
                                    notes.push_back("category→`" + v + "`");
                                }
                            } else if (k == "input") {
                                std::string lv = v;
                                std::transform(lv.begin(), lv.end(), lv.begin(), ::tolower);
                                tc.input_creation_enabled = (lv == "true" || lv == "yes" || lv == "1" || lv == "on");
                                notes.push_back(std::string("input→") + (tc.input_creation_enabled ? "enabled" : "disabled"));
                            } else if (k == "viewers") {
                                if (v == "none" || v == "clear") {
                                    tc.view_roles = "";
                                    tc.view_users = "";
                                    tc.view_permissions = "";
                                    notes.push_back("viewers cleared");
                                } else {
                                    ParsedViewers pv = parse_viewers(v, bot, msg.server_id);
                                    tc.view_roles       = join_vec(pv.roles);
                                    tc.view_users       = join_vec(pv.users);
                                    tc.view_permissions = join_vec(pv.permissions);
                                    notes.push_back("viewers updated");
                                }
                            } else if (k == "welcome") {
                                if (v == "none" || v == "clear") {
                                    tc.ticket_welcome_content = "";
                                    notes.push_back("welcome cleared");
                                } else {
                                    tc.ticket_welcome_content = v;
                                    notes.push_back("welcome updated");
                                }
                            } else if (k == "close_emoji") {
                                tc.ticket_close_emoji = v;
                                notes.push_back("close_emoji→" + v);
                            } else if (k == "close_msg") {
                                if (v == "none" || v == "clear") {
                                    tc.ticket_close_content = "";
                                    notes.push_back("close_msg cleared");
                                } else {
                                    tc.ticket_close_content = v;
                                    notes.push_back("close_msg updated");
                                }
                            } else if (k == "member_role") {
                                if (v == "none" || v == "clear") {
                                    tc.member_role_id = "";
                                    notes.push_back("member_role cleared (" + bronx::emj_warning + " channel will be public)");
                                } else {
                                    // Accept mention <%id> or bare ID
                                    std::string rid = v;
                                    if (rid.rfind("<%", 0) == 0 && rid.back() == '>')
                                        rid = rid.substr(2, rid.size() - 3);
                                    tc.member_role_id = rid;
                                    notes.push_back("member_role→<%" + rid + ">");
                                }
                            }
                        }

                        if (notes.empty()) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " No valid keys applied.");
                            return;
                        }

                        if (g_db->save_ticket_config(tc)) {
                            std::string changes;
                            for (size_t i = 0; i < notes.size(); ++i) {
                                if (i) changes += ", ";
                                changes += notes[i];
                            }
                            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Panel `" + edit_panel + "` updated: " + changes + ".");
                        } else {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save panel configuration.");
                        }
                        return;
                    }

                    if (subcommand == "panel" || subcommand == "send") {
                        std::string target_panel = "default";
                        std::string target_channel_id = "";

                        for (size_t i = 1; i < args.size(); ++i) {
                            std::string arg = args[i];
                            bool is_channel = false;
                            std::string chan_part = arg;
                            
                            if (arg.rfind("channel:", 0) == 0) {
                                chan_part = arg.substr(8);
                                is_channel = true;
                            } else if (arg.rfind("<#", 0) == 0 && arg.back() == '>') {
                                is_channel = true;
                            } else if (arg.size() == 26) {
                                is_channel = true;
                            }

                            if (is_channel) {
                                if (chan_part.rfind("<#", 0) == 0 && chan_part.back() == '>') {
                                    target_channel_id = chan_part.substr(2, chan_part.size() - 3);
                                } else {
                                    target_channel_id = chan_part;
                                }
                            } else {
                                target_panel = arg;
                            }
                        }

                        auto tc_opt = g_db->get_ticket_config(msg.server_id, target_panel);
                        if (!tc_opt) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Ticket panel `" + target_panel + "` is not set up yet. Use `" + pfx + "ticket setup " + target_panel + "` first.");
                            return;
                        }

                        auto tc = *tc_opt;
                        if (!target_channel_id.empty()) {
                            tc.modal_channel_id = target_channel_id;
                        }

                        if (tc.modal_channel_id.empty()) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " No destination channel configured for panel `" + target_panel + "`. Set it using `" + pfx + "ticket edit " + target_panel + " channel:#channel`.");
                            return;
                        }

                        send_ticket_panel(bot, tc, msg.channel_id);
                        return;
                    }

                    if (subcommand == "setup") {
                        bot.send_message(msg.channel_id, "ticket setup [" + panel_name + "] [1/14]: specify channel (mention, ID, or name).");
                        bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(45), [&bot, msg, panel_name](std::optional<stoatpp::events::Message> rep1) {
                            if (!rep1) {
                                bot.send_message(msg.channel_id, "⏱️ Setup timed out. Setup canceled.");
                                return;
                            }
                            if (rep1->content == "cancel") {
                                bot.send_message(msg.channel_id, "" + bronx::emj_no + " Setup canceled.");
                                return;
                            }

                            std::string target_channel = "";
                            std::string chan_resp = rep1->content;
                            if (chan_resp.rfind("<#", 0) == 0 && chan_resp.back() == '>') {
                                target_channel = chan_resp.substr(2, chan_resp.size() - 3);
                            } else {
                                auto server_channels_res = bot.rest().get_server_channels(msg.server_id);
                                if (server_channels_res.success() && server_channels_res.body.is_array()) {
                                    for (const auto& c_json : server_channels_res.body) {
                                        auto c = stoatpp::models::Channel::from_json(c_json);
                                        if (c.id == chan_resp || (c.name && *c.name == chan_resp)) {
                                            target_channel = c.id;
                                            break;
                                        }
                                    }
                                }
                            }

                            if (target_channel.empty()) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid channel. Setup canceled.");
                                return;
                            }

                            bot.send_message(msg.channel_id, "💬 **Ticket Setup Wizard**\n**Step 2/14**: Type the **Title** of the ticket panel (or reply `skip` for default: `🎫 Open a Ticket`).");
                            bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(45), [&bot, msg, target_channel, panel_name](std::optional<stoatpp::events::Message> rep2) {
                                if (!rep2) {
                                    bot.send_message(msg.channel_id, "⏱️ Setup timed out. Setup canceled.");
                                    return;
                                }

                                std::string title_resp = rep2->content;
                                if (title_resp == "cancel") {
                                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Setup canceled.");
                                    return;
                                }

                                nlohmann::json emb = nlohmann::json::object();
                                if (title_resp != "skip") {
                                    emb["title"] = title_resp;
                                } else {
                                    emb["title"] = "🎫 Open a Ticket";
                                }

                                bot.send_message(msg.channel_id, "💬 **Ticket Setup Wizard**\n**Step 3/14**: Type the **Description** of the panel (or reply `skip` for default: `React below to open a support ticket.`).");
                                bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(60), [&bot, msg, target_channel, emb, panel_name](std::optional<stoatpp::events::Message> rep3) {
                                    if (!rep3) {
                                        bot.send_message(msg.channel_id, "⏱️ Setup timed out. Setup canceled.");
                                        return;
                                    }

                                    std::string desc_resp = rep3->content;
                                    if (desc_resp == "cancel") {
                                        bot.send_message(msg.channel_id, "" + bronx::emj_no + " Setup canceled.");
                                        return;
                                    }

                                    auto current_emb = emb;
                                    if (desc_resp != "skip") {
                                        current_emb["description"] = desc_resp;
                                    } else {
                                        current_emb["description"] = "React below to open a support ticket.";
                                    }

                                    bot.send_message(msg.channel_id, "💬 **Ticket Setup Wizard**\n**Step 4/14**: Type the panel **Color hex** (e.g. `#6C5CE7` or `random`, or reply `skip` for default).");
                                    bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30), [&bot, msg, target_channel, current_emb, panel_name](std::optional<stoatpp::events::Message> rep4) {
                                        if (!rep4) {
                                            bot.send_message(msg.channel_id, "⏱️ Setup timed out. Setup canceled.");
                                            return;
                                        }

                                        std::string color_resp = rep4->content;
                                        if (color_resp == "cancel") {
                                            bot.send_message(msg.channel_id, "" + bronx::emj_no + " Setup canceled.");
                                            return;
                                        }

                                        auto final_emb = current_emb;
                                        if (color_resp != "skip") {
                                            final_emb["color"] = color_resp;
                                        } else {
                                            final_emb["color"] = "#6C5CE7";
                                        }

                                        bot.send_message(msg.channel_id, "💬 **Ticket Setup Wizard**\n**Step 5/14**: Type the panel **Reaction emoji** to open a ticket (or reply `skip` for default `📩`).");
                                        bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30), [&bot, msg, target_channel, final_emb, panel_name](std::optional<stoatpp::events::Message> rep5) {
                                            if (!rep5) {
                                                bot.send_message(msg.channel_id, "⏱️ Setup timed out. Setup canceled.");
                                                return;
                                            }

                                            std::string emoji_resp = rep5->content;
                                            if (emoji_resp == "cancel") {
                                                bot.send_message(msg.channel_id, "" + bronx::emj_no + " Setup canceled.");
                                                return;
                                            }

                                            std::string emoji = clean_emoji(emoji_resp);
                                            if (emoji == "skip") emoji = "📩";

                                            bot.send_message(msg.channel_id, "💬 **Ticket Setup Wizard**\n**Step 6/14**: Should users also be able to create a ticket by sending *any* text message in the panel channel? Reply with `yes` or `no` (default: `no`).");
                                            bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30), [&bot, msg, target_channel, final_emb, emoji, panel_name](std::optional<stoatpp::events::Message> rep6) {
                                                if (!rep6) {
                                                    bot.send_message(msg.channel_id, "⏱️ Setup timed out. Setup canceled.");
                                                    return;
                                                }

                                                std::string input_resp = rep6->content;
                                                std::transform(input_resp.begin(), input_resp.end(), input_resp.begin(), ::tolower);
                                                if (input_resp == "cancel") {
                                                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Setup canceled.");
                                                    return;
                                                }

                                                bool input_creation = (input_resp == "yes" || input_resp == "true" || input_resp == "y");

                                                bot.send_message(msg.channel_id, "💬 **Ticket Setup Wizard**\n**Step 7/14**: What channel **Naming Scheme** should be used? (e.g. `support-{number}`, `ticket-{number}`, `ticket-{username}`) (or reply `skip` for default: `ticket-{number}`).");
                                                bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(45), [&bot, msg, target_channel, final_emb, emoji, input_creation, panel_name](std::optional<stoatpp::events::Message> rep7) {
                                                    if (!rep7) {
                                                        bot.send_message(msg.channel_id, "⏱️ Setup timed out. Setup canceled.");
                                                        return;
                                                    }

                                                    std::string scheme_resp = rep7->content;
                                                    if (scheme_resp == "cancel") {
                                                        bot.send_message(msg.channel_id, "" + bronx::emj_no + " Setup canceled.");
                                                        return;
                                                    }

                                                    std::string naming_scheme = scheme_resp;
                                                    if (naming_scheme == "skip") naming_scheme = "ticket-{number}";

                                                    bot.send_message(msg.channel_id, "💬 **Ticket Setup Wizard**\n**Step 8/14**: What **Category name** or category ID should the tickets be created under? (e.g. `Tickets`), or reply `none` / `skip` for no category.");
                                                    bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(45), [&bot, msg, target_channel, final_emb, emoji, input_creation, naming_scheme, panel_name](std::optional<stoatpp::events::Message> rep8) {
                                                        if (!rep8) {
                                                            bot.send_message(msg.channel_id, "⏱️ Setup timed out. Setup canceled.");
                                                            return;
                                                        }

                                                        std::string cat_resp = rep8->content;
                                                        if (cat_resp == "cancel") {
                                                            bot.send_message(msg.channel_id, "" + bronx::emj_no + " Setup canceled.");
                                                            return;
                                                        }

                                                        std::string category = cat_resp;
                                                        if (category == "skip" || category == "none") category = "";

                                                        bot.send_message(msg.channel_id, "💬 **Ticket Setup Wizard**\n**Step 9/14**: Who can view the ticket channels? Mention the roles/users, or type permissions (e.g. `@Support, ManageServer`), or reply `none` / `skip`.");
                                                        bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(60), [&bot, msg, target_channel, final_emb, emoji, input_creation, naming_scheme, category, panel_name](std::optional<stoatpp::events::Message> rep9) {
                                                            if (!rep9) {
                                                                bot.send_message(msg.channel_id, "⏱️ Setup timed out. Setup canceled.");
                                                                return;
                                                            }

                                                            std::string view_resp = rep9->content;
                                                            if (view_resp == "cancel") {
                                                                bot.send_message(msg.channel_id, "" + bronx::emj_no + " Setup canceled.");
                                                                return;
                                                            }

                                                            auto parsed = parse_viewers(view_resp, bot, msg.server_id);

                                                            bot.send_message(msg.channel_id, "💬 **Ticket Setup Wizard**\n**Step 10/14**: Inside the ticket, type the welcome embed **Title** (or reply `skip` for default: `🎫 Ticket Support`).");
                                                            bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(45), [&bot, msg, target_channel, final_emb, emoji, input_creation, naming_scheme, category, parsed, panel_name](std::optional<stoatpp::events::Message> rep10) {
                                                                if (!rep10) {
                                                                    bot.send_message(msg.channel_id, "⏱️ Setup timed out. Setup canceled.");
                                                                    return;
                                                                }

                                                                std::string t_title_resp = rep10->content;
                                                                if (t_title_resp == "cancel") {
                                                                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Setup canceled.");
                                                                    return;
                                                                }

                                                                nlohmann::json t_emb = nlohmann::json::object();
                                                                if (t_title_resp != "skip") {
                                                                    t_emb["title"] = t_title_resp;
                                                                } else {
                                                                    t_emb["title"] = "🎫 Ticket Support";
                                                                }

                                                                bot.send_message(msg.channel_id, "💬 **Ticket Setup Wizard**\n**Step 11/14**: Inside the ticket, type the welcome embed **Description** (or reply `skip` for default).");
                                                                bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(60), [&bot, msg, target_channel, final_emb, emoji, input_creation, naming_scheme, category, parsed, t_emb, panel_name](std::optional<stoatpp::events::Message> rep11) {
                                                                    if (!rep11) {
                                                                        bot.send_message(msg.channel_id, "⏱️ Setup timed out. Setup canceled.");
                                                                        return;
                                                                    }

                                                                    std::string t_desc_resp = rep11->content;
                                                                    if (t_desc_resp == "cancel") {
                                                                        bot.send_message(msg.channel_id, "" + bronx::emj_no + " Setup canceled.");
                                                                        return;
                                                                    }

                                                                    auto t_current_emb = t_emb;
                                                                    if (t_desc_resp != "skip") {
                                                                        t_current_emb["description"] = t_desc_resp;
                                                                    } else {
                                                                        t_current_emb["description"] = "Thank you for opening a support ticket!\nSupport staff will be with you shortly.\nReact with {close_emoji} to close this ticket.";
                                                                    }

                                                                    bot.send_message(msg.channel_id, "💬 **Ticket Setup Wizard**\n**Step 12/14**: Inside the ticket, type the **Message content** outside the embed (default: `{user}`, or reply `skip` to just mention the user).");
                                                                    bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(45), [&bot, msg, target_channel, final_emb, emoji, input_creation, naming_scheme, category, parsed, t_current_emb, panel_name](std::optional<stoatpp::events::Message> rep12) {
                                                                        if (!rep12) {
                                                                            bot.send_message(msg.channel_id, "⏱️ Setup timed out. Setup canceled.");
                                                                            return;
                                                                        }

                                                                        std::string t_content_resp = rep12->content;
                                                                        if (t_content_resp == "cancel") {
                                                                            bot.send_message(msg.channel_id, "" + bronx::emj_no + " Setup canceled.");
                                                                            return;
                                                                        }

                                                                        std::string t_welcome_txt = t_content_resp;
                                                                        if (t_welcome_txt == "skip") t_welcome_txt = "{user}";

                                                                        bot.send_message(msg.channel_id, "💬 **Ticket Setup Wizard**\n**Step 13/14**: Inside the ticket, type the **Close emoji** added to the welcome message (or reply `skip` for default `🔒`).");
                                                                        bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(30), [&bot, msg, target_channel, final_emb, emoji, input_creation, naming_scheme, category, parsed, t_current_emb, t_welcome_txt, panel_name](std::optional<stoatpp::events::Message> rep13) {
                                                                            if (!rep13) {
                                                                                bot.send_message(msg.channel_id, "⏱️ Setup timed out. Setup canceled.");
                                                                                return;
                                                                            }

                                                                            std::string close_emoji_resp = rep13->content;
                                                                            if (close_emoji_resp == "cancel") {
                                                                                bot.send_message(msg.channel_id, "" + bronx::emj_no + " Setup canceled.");
                                                                                return;
                                                                            }

                                                                            std::string close_emoji = clean_emoji(close_emoji_resp);
                                                                            if (close_emoji == "skip") close_emoji = "🔒";

                                                                            bot.send_message(msg.channel_id, "💬 **Ticket Setup Wizard**\n**Step 14/14**: Type the custom **Closing message** sent when a ticket is closed (or reply `skip` for default: `🔒 **Closing ticket...**\nThis channel will be deleted in 5 seconds.`).");
                                                                            bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(60), [&bot, msg, target_channel, final_emb, emoji, input_creation, naming_scheme, category, parsed, t_current_emb, t_welcome_txt, close_emoji, panel_name](std::optional<stoatpp::events::Message> rep14) {
                                                                                if (!rep14) {
                                                                                    bot.send_message(msg.channel_id, "⏱️ Setup timed out. Setup canceled.");
                                                                                    return;
                                                                                }

                                                                                std::string close_msg_resp = rep14->content;
                                                                                if (close_msg_resp == "cancel") {
                                                                                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Setup canceled.");
                                                                                    return;
                                                                                }

                                                                                std::string close_content = close_msg_resp;
                                                                                if (close_content == "skip") close_content = "🔒 **Closing ticket...**\nThis channel will be deleted in 5 seconds.";

                                                                                auto config = g_db->get_ticket_config(msg.server_id, panel_name).value_or(Database::TicketConfig{msg.server_id, panel_name});
                                                                                config.modal_channel_id = target_channel;
                                                                                config.modal_content = "";
                                                                                config.modal_embed_json = final_emb.dump();
                                                                                config.reaction_emoji = emoji;
                                                                                config.input_creation_enabled = input_creation;
                                                                                config.naming_scheme = naming_scheme;
                                                                                config.category_id = category;
                                                                                config.view_roles = join_vec(parsed.roles);
                                                                                config.view_users = join_vec(parsed.users);
                                                                                config.view_permissions = join_vec(parsed.permissions);
                                                                                config.ticket_welcome_content = t_welcome_txt;
                                                                                config.ticket_welcome_embed_json = t_current_emb.dump();
                                                                                config.ticket_close_emoji = close_emoji;
                                                                                config.ticket_close_content = close_content;

                                                                                if (g_db->save_ticket_config(config)) {
                                                                                    bot.send_message(msg.channel_id, "" + bronx::emj_yes + " **Setup complete!** Sending the ticket panel automatically...");
                                                                                    send_ticket_panel(bot, config, msg.channel_id);
                                                                                } else {
                                                                                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save ticket configuration to the database.");
                                                                                }
                                                                            });
                                                                        });
                                                                    });
                                                                });
                                                            });
                                                        });
                                                    });
                                                });
                                            });
                                        });
                                    });
                                });
                            });
                        });
                        return;
                    }

                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Unknown subcommand. Use setup, panel, list, delete, close, add, remove, config.");
                });
            },
            "Tickets"
        });

        bot.on_message_react([&bot](const stoatpp::events::MessageReact& e) {
            if (e.user_id == bot.current_user().id) return;
            if (!g_db) return;

            std::string cleaned_emoji = clean_emoji(e.emoji_id);

            // Search all configs directly by modal message ID
            auto configs = g_db->get_all_ticket_configs();
            for (const auto& config : configs) {
                std::string target_emoji = clean_emoji(config.reaction_emoji);
                if (e.id == config.modal_message_id && cleaned_emoji == target_emoji) {
                    bot.unreact_from_message(e.channel_id, e.id, e.emoji_id, e.user_id);
                    execute_create_ticket(bot, config.server_id, e.user_id, config.name, e.channel_id);
                    return;
                }
            }

            // Close reactions
            auto ticket_opt = g_db->get_ticket(e.channel_id);
            if (ticket_opt && ticket_opt->status == "open") {
                auto tc_opt = g_db->get_ticket_config(ticket_opt->server_id, ticket_opt->panel_name);
                if (tc_opt) {
                    std::string target_close_emoji = clean_emoji(tc_opt->ticket_close_emoji);
                    if (e.id == ticket_opt->welcome_message_id && cleaned_emoji == target_close_emoji) {
                        execute_close_ticket(bot, e.channel_id);
                    }
                }
            }
        });

        bot.on_channel_delete([](const stoatpp::events::ChannelDelete& e) {
            if (g_db) {
                g_db->delete_ticket(e.id);
            }
        });

        bot.on_message([this, &bot](const stoatpp::events::Message& msg) {
            if (msg.author.bot || msg.server_id.empty()) return;
            if (!g_db) return;

            auto configs = g_db->get_ticket_configs_for_server(msg.server_id);
            for (const auto& config : configs) {
                if (config.input_creation_enabled && msg.channel_id == config.modal_channel_id) {
                    std::vector<std::string> prefixes;
                    std::string bot_id = bot.current_user().id;
                    if (!bot_id.empty()) {
                        prefixes.push_back("<@" + bot_id + "> ");
                        prefixes.push_back("<@" + bot_id + ">");
                    }
                    std::vector<std::string> user_pfx = g_db->get_prefixes("user", msg.author.id);
                    prefixes.insert(prefixes.end(), user_pfx.begin(), user_pfx.end());
                    std::vector<std::string> server_pfx = g_db->get_prefixes("server", msg.server_id);
                    prefixes.insert(prefixes.end(), server_pfx.begin(), server_pfx.end());
                    prefixes.push_back(bot.config().command_prefix);

                    bool is_command = false;
                    for (const auto& p : prefixes) {
                        if (!p.empty() && msg.content.rfind(p, 0) == 0) {
                            is_command = true;
                            break;
                        }
                    }

                    if (!is_command) {
                        bot.delete_message(msg.channel_id, msg.id);
                        execute_create_ticket(bot, msg.server_id, msg.author.id, config.name, msg.channel_id);
                        return; // Found matching channel
                    }
                }
            }
        });

        // Startup scan thread for offline reactions
        std::thread([&bot]() {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            if (!g_db) return;

            std::cout << "[bronx tickets] starting startup scan for offline reactions...\n";

            // 1. Scan modal panel messages for new ticket creation reactions
            auto configs = g_db->get_all_ticket_configs();
            for (const auto& config : configs) {
                if (config.modal_channel_id.empty() || config.modal_message_id.empty()) continue;
                auto res = bot.rest().get_message(config.modal_channel_id, config.modal_message_id);
                if (res.success()) {
                    if (res.body.contains("reactions") && res.body["reactions"].is_object()) {
                        auto reactions = res.body["reactions"];
                        std::string target_emoji = clean_emoji(config.reaction_emoji);
                        for (auto& [emo_id, uids] : reactions.items()) {
                            if (clean_emoji(emo_id) == target_emoji && uids.is_array()) {
                                for (auto& uid : uids) {
                                    if (uid.is_string()) {
                                        std::string user_id = uid.get<std::string>();
                                        if (user_id != bot.current_user().id) {
                                            std::cout << "[bronx tickets] startup scan found reaction from " << user_id << " to open ticket on panel " << config.name << "\n";
                                            execute_create_ticket(bot, config.server_id, user_id, config.name, config.modal_channel_id);
                                            bot.unreact_from_message(config.modal_channel_id, config.modal_message_id, emo_id, user_id);
                                            std::this_thread::sleep_for(std::chrono::milliseconds(200));
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }

            // 2. Scan open tickets' welcome messages for close reactions
            auto tickets = g_db->get_all_tickets();
            for (const auto& ticket : tickets) {
                if (ticket.status != "open" || ticket.channel_id.empty() || ticket.welcome_message_id.empty()) continue;
                auto tc_opt = g_db->get_ticket_config(ticket.server_id, ticket.panel_name);
                if (!tc_opt) continue;

                auto res = bot.rest().get_message(ticket.channel_id, ticket.welcome_message_id);
                if (!res.success() && res.status_code == 404) {
                    std::cout << "[bronx tickets] startup scan found ticket channel " << ticket.channel_id << " was deleted, removing from db.\n";
                    g_db->delete_ticket(ticket.channel_id);
                } else if (res.success()) {
                    if (res.body.contains("reactions") && res.body["reactions"].is_object()) {
                        auto reactions = res.body["reactions"];
                        std::string target_close_emoji = clean_emoji(tc_opt->ticket_close_emoji);
                        for (auto& [emo_id, uids] : reactions.items()) {
                            if (clean_emoji(emo_id) == target_close_emoji && uids.is_array()) {
                                bool reacted = false;
                                for (auto& uid : uids) {
                                    if (uid.is_string() && uid.get<std::string>() == ticket.user_id) {
                                        reacted = true;
                                        break;
                                    }
                                }
                                if (reacted) {
                                    std::cout << "[bronx tickets] startup scan found reaction from creator " << ticket.user_id << " to close ticket channel " << ticket.channel_id << "\n";
                                    execute_close_ticket(bot, ticket.channel_id);
                                }
                            }
                        }
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            std::cout << "[bronx tickets] startup scan for offline reactions finished.\n";
        }).detach();
    }
};

} // namespace bronx::cogs
