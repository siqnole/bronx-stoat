#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"

namespace bronx::cogs {

class UsersModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "userinfo",
            {"user", "profile", "ui"},
            "fetch information about a user.",
            "",
            {"[user]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling userinfo...\n";
                
                std::string query = args.empty() ? msg.author.id : args[0];
                
                helpers::resolve_user(bot, msg.server_id, query, [&bot, msg](stoatpp::models::User user, stoatpp::models::Member member, bool success) {
                    if (!success) {
                        bot.send_message(msg.channel_id, "could not find user");
                        return;
                    }
                    
                    stoatpp::models::MessagePayload payload;
                    std::string content = "user info for **" + user.username + "**:";
                    if (user.banner) {
                        content += "\n" + user.banner_url(bot.config().cdn_url);
                    }
                    payload.content = content;
                    
                    std::string presence_str = user.presence.value_or("Offline");
                    std::string status_msg = user.status_text.value_or("");
                    std::string status_full = presence_str + (status_msg.empty() ? "" : ": " + status_msg);

                    uint64_t account_created_ms = stoatpp::utils::decode_ulid_timestamp(user.id);
                    std::string stoat_join = helpers::format_epoch_ms(account_created_ms);
                    
                    std::string server_join = "none";
                    if (!member.joined_at.empty()) {
                        server_join = helpers::format_iso_string(member.joined_at);
                    }
                    
                    std::string discrim = user.discriminator.empty() ? "" : "#" + user.discriminator;

                    std::string desc = "• **id**: `" + user.id + "`\n";
                    desc += "• **display name**: " + user.display_name.value_or("none") + "\n";
                    desc += "• **type**: " + std::string(user.bot ? "bot" : "user") + "\n";
                    desc += "• **status**: " + status_full + "\n";
                    desc += "• **bio**: " + user.bio.value_or("none") + "\n\n";
                    desc += "• **joined stoat**: " + stoat_join + "\n";
                    desc += "• **joined stoat++**: " + server_join + "\n";
                    
                    nlohmann::json embed = {
                        {"title", user.username + discrim + (user.bot ? " [bot]" : "")},
                        {"description", desc},
                        {"colour", "#5865f2"},
                        {"icon_url", user.avatar_url(bot.config().cdn_url)}
                    };
                    
                    payload.embeds.push_back(embed);
                    bot.send_message(msg.channel_id, payload);
                });
            }
        });

        bot.register_command({
            "avatar",
            {"av"},
            "fetch the avatar of a user.",
            "",
            {"[user]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling avatar...\n";
                
                std::string query = args.empty() ? msg.author.id : args[0];
                
                helpers::resolve_user(bot, msg.server_id, query, [&bot, msg](stoatpp::models::User user, stoatpp::models::Member member, bool success) {
                    if (!success) {
                        bot.send_message(msg.channel_id, "could not find user");
                        return;
                    }
                    
                    std::string av_url = user.avatar_url(bot.config().cdn_url);
                    
                    stoatpp::models::MessagePayload payload;
                    payload.content = "avatar for **" + user.username + "**:\n" + av_url;
                    
                    nlohmann::json embed = {
                        {"title", user.username + "'s avatar"},
                        {"url", av_url},
                        {"colour", "#5865f2"}
                    };
                    
                    payload.embeds.push_back(embed);
                    bot.send_message(msg.channel_id, payload);
                });
            }
        });

        bot.register_command({
            "banner",
            {"bn"},
            "fetch the banner profile image of a user.",
            "",
            {"[user]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling banner...\n";
                
                std::string query = args.empty() ? msg.author.id : args[0];
                
                helpers::resolve_user(bot, msg.server_id, query, [&bot, msg](stoatpp::models::User user, stoatpp::models::Member member, bool success) {
                    if (!success) {
                        bot.send_message(msg.channel_id, "could not find user");
                        return;
                    }
                    
                    if (!user.banner) {
                        bot.send_message(msg.channel_id, "this user does not have a banner set");
                        return;
                    }
                    
                    std::string bn_url = user.banner_url(bot.config().cdn_url);
                    
                    stoatpp::models::MessagePayload payload;
                    payload.content = "banner for **" + user.username + "**:\n" + bn_url;
                    
                    nlohmann::json embed = {
                        {"title", user.username + "'s banner"},
                        {"url", bn_url},
                        {"colour", "#5865f2"}
                    };
                    
                    payload.embeds.push_back(embed);
                    bot.send_message(msg.channel_id, payload);
                });
            }
        });
    }
};

} // namespace bronx::cogs
