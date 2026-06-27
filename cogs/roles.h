#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"

namespace bronx::cogs {

class RolesModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "role",
            {},
            "batch toggle multiple roles on multiple members.",
            "",
            {"<user1,user2,...>", "<role1,role2,...>"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling role (unified/batch)...\n";
                if (args.size() < 2) {
                    bot.send_message(msg.channel_id, "usage: `!role <user1,user2,...> <role1,role2,...>`");
                    return;
                }

                std::vector<std::string> user_queries;
                {
                    std::stringstream ss(args[0]);
                    std::string item;
                    while (std::getline(ss, item, ',')) {
                        item.erase(0, item.find_first_not_of(" \t\r\n"));
                        item.erase(item.find_last_not_of(" \t\r\n") + 1);
                        if (!item.empty()) user_queries.push_back(item);
                    }
                }

                std::vector<std::string> role_queries;
                {
                    std::stringstream ss(args[1]);
                    std::string item;
                    while (std::getline(ss, item, ',')) {
                        item.erase(0, item.find_first_not_of(" \t\r\n"));
                        item.erase(item.find_last_not_of(" \t\r\n") + 1);
                        if (!item.empty()) role_queries.push_back(item);
                    }
                }

                auto server_opt = bot.get_server(msg.server_id);
                if (!server_opt) {
                    bot.send_message(msg.channel_id, "failed to retrieve server settings.");
                    return;
                }

                std::vector<stoatpp::models::Role> resolved_roles;
                std::vector<std::string> missing_roles;
                for (const auto& rq : role_queries) {
                    bool found = false;
                    for (const auto& r : server_opt->roles) {
                        if (r.id == rq || r.name == rq) {
                            resolved_roles.push_back(r);
                            found = true;
                            break;
                        }
                    }
                    if (!found) missing_roles.push_back(rq);
                }

                if (resolved_roles.empty()) {
                    bot.send_message(msg.channel_id, "could not find any of the specified roles.");
                    return;
                }

                bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, user_queries, resolved_roles, missing_roles](stoatpp::models::Member exec_member, bool exec_ok) {
                    if (!exec_ok) {
                        bot.send_message(msg.channel_id, "failed to verify executor member settings.");
                        return;
                    }

                    struct ResolvedTarget {
                        stoatpp::models::User user;
                        stoatpp::models::Member member;
                        bool success = false;
                    };
                    
                    auto resolved_ptr = std::make_shared<std::vector<ResolvedTarget>>();
                    
                    struct UserResolver {
                        static void resolve(stoatpp::cluster& bot, const std::string& server_id,
                                            const std::vector<std::string>& queries, size_t index,
                                            std::shared_ptr<std::vector<ResolvedTarget>> resolved,
                                            std::function<void()> callback) {
                            if (index >= queries.size()) {
                                callback();
                                return;
                            }
                            helpers::resolve_user(bot, server_id, queries[index], [&bot, server_id, queries, index, resolved, callback](stoatpp::models::User u, stoatpp::models::Member m, bool ok) {
                                ResolvedTarget target;
                                target.user = u;
                                target.member = m;
                                target.success = ok;
                                resolved->push_back(target);
                                resolve(bot, server_id, queries, index + 1, resolved, callback);
                            });
                        }
                    };

                    UserResolver::resolve(bot, msg.server_id, user_queries, 0, resolved_ptr, [&bot, msg, resolved_ptr, resolved_roles, exec_member, missing_roles]() {
                        struct TargetResult {
                            std::string id;
                            std::string username;
                            std::string user_mention;
                            std::string status_err = "";
                            std::vector<std::string> added_roles;
                            std::vector<std::string> removed_roles;
                        };

                        auto results = std::make_shared<std::vector<TargetResult>>();
                        results->resize(resolved_ptr->size());

                        struct ActionItem {
                            size_t target_index;
                            std::string role_id;
                            std::string role_name;
                            bool add;
                        };

                        auto actions = std::make_shared<std::vector<ActionItem>>();

                        for (size_t i = 0; i < resolved_ptr->size(); ++i) {
                            const auto& target = (*resolved_ptr)[i];
                            (*results)[i].id = target.success ? target.user.id : "";
                            (*results)[i].username = target.success ? target.user.username : "";
                            (*results)[i].user_mention = target.success ? "<@" + target.user.id + ">" : "";

                            if (!target.success) {
                                (*results)[i].status_err = "failed to resolve user";
                                continue;
                            }

                            int exec_highest = helpers::get_highest_rank(bot, msg.server_id, exec_member);
                            int target_highest = helpers::get_highest_rank(bot, msg.server_id, target.member);
                            if (exec_highest >= target_highest && exec_highest != -1) {
                                (*results)[i].status_err = "hierarchy error: you cannot modify this user";
                                continue;
                            }

                            for (const auto& role : resolved_roles) {
                                int role_rank = helpers::get_role_rank(bot, msg.server_id, role.id);
                                if (exec_highest >= role_rank && exec_highest != -1) {
                                    continue;
                                }

                                bool has_role = false;
                                for (const auto& r_id : target.member.roles) {
                                    if (r_id == role.id) {
                                        has_role = true;
                                        break;
                                    }
                                }

                                ActionItem act;
                                act.target_index = i;
                                act.role_id = role.id;
                                act.role_name = role.name;
                                act.add = !has_role;
                                actions->push_back(act);
                            }
                        }

                        if (actions->empty()) {
                            stoatpp::models::MessagePayload payload;
                            nlohmann::json embed = nlohmann::json::object();
                            embed["title"] = "unified role toggle summary";
                            embed["colour"] = "#4eb3ec";
                            
                            std::string desc = "";
                            for (const auto& res : *results) {
                                if (!res.user_mention.empty()) {
                                    desc += res.user_mention + "\n";
                                    if (!res.status_err.empty()) {
                                        desc += "⚠️ " + res.status_err + "\n";
                                    } else {
                                        desc += "no changes applied.\n";
                                    }
                                    desc += "\n";
                                }
                            }
                            if (!missing_roles.empty()) {
                                desc += "missing/invalid roles ignored: ";
                                for (size_t i = 0; i < missing_roles.size(); ++i) {
                                    desc += "`" + missing_roles[i] + "`" + (i + 1 < missing_roles.size() ? ", " : "");
                                }
                                desc += "\n";
                            }
                            embed["description"] = desc;
                            payload.embeds.push_back(embed);
                            bot.send_message(msg.channel_id, payload);
                            return;
                        }

                        auto actions_completed = std::make_shared<std::atomic<size_t>>(0);
                        size_t total_actions = actions->size();

                        for (const auto& act : *actions) {
                            auto target_id = (*results)[act.target_index].id;
                            auto cb = [&bot, act, actions_completed, total_actions, results, missing_roles, msg](bool action_ok) {
                                if (action_ok) {
                                    if (act.add) {
                                        (*results)[act.target_index].added_roles.push_back(act.role_name);
                                    } else {
                                        (*results)[act.target_index].removed_roles.push_back(act.role_name);
                                    }
                                }

                                size_t completed = ++(*actions_completed);
                                if (completed == total_actions) {
                                    stoatpp::models::MessagePayload payload;
                                    nlohmann::json embed = nlohmann::json::object();
                                    embed["title"] = "unified role toggle summary";
                                    embed["colour"] = "#4eb3ec";

                                    std::string desc = "";
                                    for (const auto& res : *results) {
                                        if (!res.user_mention.empty()) {
                                            desc += res.user_mention + "\n";
                                            if (!res.status_err.empty()) {
                                                desc += "⚠️ " + res.status_err + "\n";
                                            } else {
                                                if (!res.added_roles.empty()) {
                                                    desc += "+ ";
                                                    for (size_t r = 0; r < res.added_roles.size(); ++r) {
                                                        desc += res.added_roles[r] + (r + 1 < res.added_roles.size() ? ", " : "");
                                                    }
                                                    desc += "\n";
                                                }
                                                if (!res.removed_roles.empty()) {
                                                    desc += "- ";
                                                    for (size_t r = 0; r < res.removed_roles.size(); ++r) {
                                                        desc += res.removed_roles[r] + (r + 1 < res.removed_roles.size() ? ", " : "");
                                                    }
                                                    desc += "\n";
                                                }
                                                if (res.added_roles.empty() && res.removed_roles.empty()) {
                                                    desc += "no changes applied.\n";
                                                }
                                            }
                                            desc += "\n";
                                        }
                                    }
                                    if (!missing_roles.empty()) {
                                        desc += "missing/invalid roles ignored: ";
                                        for (size_t i = 0; i < missing_roles.size(); ++i) {
                                            desc += "`" + missing_roles[i] + "`" + (i + 1 < missing_roles.size() ? ", " : "");
                                        }
                                        desc += "\n";
                                    }
                                    embed["description"] = desc;
                                    payload.embeds.push_back(embed);
                                    bot.send_message(msg.channel_id, payload);
                                }
                            };

                            if (act.add) {
                                bot.add_role_to_member(msg.server_id, target_id, act.role_id, cb);
                            } else {
                                bot.remove_role_from_member(msg.server_id, target_id, act.role_id, cb);
                            }
                        }
                    });
                });
            }
        });

        bot.register_command({
            "roleinfo",
            {},
            "display role metadata and role hierarchy details.",
            "",
            {},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling roleinfo...\n";
                auto server_opt = bot.get_server(msg.server_id);
                if (!server_opt) {
                    bot.send_message(msg.channel_id, "failed to get server info.");
                    return;
                }

                auto sorted_roles = server_opt->roles;
                std::sort(sorted_roles.begin(), sorted_roles.end(), [](const auto& a, const auto& b) {
                    return a.rank < b.rank;
                });

                std::string out = "roles for **" + server_opt->name + "** (sorted by hierarchy, highest first):\n";
                for (const auto& r : sorted_roles) {
                    out += "• **" + r.name + "** (rank: " + std::to_string(r.rank) + ", id: `" + r.id + "`";
                    if (r.colour) {
                        out += ", color: " + *r.colour;
                    }
                    out += ")\n";
                }

                bot.send_message(msg.channel_id, out);
            }
        });
    }
};

} // namespace bronx::cogs
