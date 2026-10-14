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
                    payload.content = "user info for **" + user.username + "**:";
                    
                    std::string presence_str = user.presence.value_or("Offline");
                    std::string status_msg = user.status_text.value_or("");
                    std::string status_full = presence_str + (status_msg.empty() ? "" : ": " + status_msg);

                    uint64_t account_created_ms = stoatpp::utils::decode_ulid_timestamp(user.id);
                    std::string stoat_join = helpers::format_epoch_ms(account_created_ms);
                    
                    std::string server_name = "server";
                    if (!msg.server_id.empty()) {
                        auto srv = bot.get_server(msg.server_id);
                        if (srv) {
                            server_name = srv->name;
                        }
                    }

                    std::string server_join = "none";
                    if (!member.joined_at.empty()) {
                        server_join = helpers::format_iso_string(member.joined_at);
                    }
                    
                    std::string discrim = user.discriminator.empty() ? "" : "#" + user.discriminator;

                    // Use a short custom bio for the bot itself to avoid
                    // blowing past Stoat's 2000-char embed description limit.
                    std::string bio_text;
                    if (user.id == bot.current_user().id) {
                        bio_text = "stoat's premier all-in-one bot — moderation, roles, levels, economy, logging, and 150+ more commands. [learn more](https://bronx.siqnole.dev/)";
                    } else {
                        bio_text = user.bio.value_or("none");
                    }

                    std::string desc = "• **id**: `" + user.id + "`\n";
                    desc += "• **display name**: " + user.display_name.value_or("none") + "\n";
                    desc += "• **type**: " + std::string(user.bot ? "bot" : "user") + "\n";
                    desc += "• **status**: " + status_full + "\n";
                    desc += "• **bio**: " + bio_text + "\n\n";
                    desc += "• **joined stoat**: " + stoat_join + "\n";
                    desc += "• **joined " + server_name + "**: " + server_join + "\n";
                    
                    stoatpp::models::Embed embed;
                    embed.title       = user.username + discrim + (user.bot ? " [bot]" : "");
                    embed.description = desc;
                    embed.colour      = "#5865f2";
                    embed.icon_url    = user.avatar_url(bot.config().cdn_url);
                    // upload banner (or avatar fallback) to Autumn for the embed image
                    std::string img_src = user.banner
                        ? user.banner_url(bot.config().cdn_url)
                        : user.avatar_url(bot.config().cdn_url);
                    std::string file_id = bot.rest().upload_from_url(img_src);
                    if (!file_id.empty()) embed.set_image(file_id);
                    
                    payload.embeds.push_back(embed.to_json());
                    bot.send_message(msg.channel_id, payload);
                });
            },
            "Users"
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
                    payload.content = "avatar for **" + user.username + "**:";
                    
                    stoatpp::models::Embed embed;
                    embed.title  = user.username + "'s avatar";
                    embed.url    = av_url;
                    embed.colour = "#5865f2";
                    // upload avatar to Autumn to get a file ID for the media field
                    std::string file_id = bot.rest().upload_from_url(av_url);
                    if (!file_id.empty()) embed.set_image(file_id);
                    
                    payload.embeds.push_back(embed.to_json());
                    bot.send_message(msg.channel_id, payload);
                });
            },
            "Users"
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
                    payload.content = "banner for **" + user.username + "**:";
                    
                    stoatpp::models::Embed embed;
                    embed.title  = user.username + "'s banner";
                    embed.url    = bn_url;
                    embed.colour = "#5865f2";
                    std::string file_id = bot.rest().upload_from_url(bn_url);
                    if (!file_id.empty()) embed.set_image(file_id);
                    
                    payload.embeds.push_back(embed.to_json());
                    bot.send_message(msg.channel_id, payload);
                });
            },
            "Users"
        });
    }
};

} // namespace bronx::cogs
