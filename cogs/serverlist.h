#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "command_config.h"
#include "../database.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace bronx::cogs {

class ServerListModule : public stoatpp::bot_module {
public:
    std::string get_staff_channel_id() const {
        const char* env_chan = std::getenv("STAFF_CHANNEL_ID");
        if (!env_chan || std::string(env_chan).empty()) {
            env_chan = std::getenv("SERVERLIST_STAFF_CHANNEL_ID");
        }
        if (env_chan && !std::string(env_chan).empty()) {
            return std::string(env_chan);
        }
        return "01KYBC5KZG865D8BNYA2CTW7XS";
    }

    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "list",
            {"serverlist", "sl"},
            "P2P server listing and advertising system.",
            "",
            {"<subcommand>", "[args...]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                if (msg.server_id.empty()) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " This command can only be used in a server.");
                    return;
                }
                if (!g_db) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Database is unavailable.");
                    return;
                }

                std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;
                if (pfx.rfind("<@", 0) == 0) pfx = bot.config().command_prefix;

                // ── Default view: display current configuration and listing commands ──
                if (args.empty()) {
                    bot.fetch_member(msg.server_id, msg.author.id, [this, &bot, msg, pfx](stoatpp::models::Member member, bool ok) {
                        bool is_admin = ok && is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, member);
                        
                        auto listing_opt = g_db->get_server_listing(msg.server_id);
                        Database::ServerListing listing;
                        bool exists = false;
                        if (listing_opt) {
                            listing = *listing_opt;
                            exists = true;
                        } else {
                            listing.server_id = msg.server_id;
                            listing.mode = "disabled";
                            listing.status = "none";
                        }
                        
                        auto srv_opt = bot.get_server(msg.server_id);
                        std::string srv_name = (srv_opt && !srv_opt->name.empty()) ? srv_opt->name : "This Server";

                        stoatpp::models::MessagePayload payload;
                        nlohmann::json embed = nlohmann::json::object();
                        embed["title"] = "🌐 Server Listing Configuration: " + srv_name;
                        embed["colour"] = "#a78bfa";

                        std::string desc = "";
                        if (!exists) {
                            desc += "ℹ️ **This server is not listed yet.**\n"
                                    "To list your server on the public web directory and participate in the P2P advertising network, run:\n"
                                    "👉 `" + pfx + "list setup` *(Requires Manage Server permission)*\n\n";
                        }

                        desc += "**Current Configuration:**\n"
                                "• **Mode:** `" + listing.mode + "` (website, bump, both, disabled)\n"
                                "• **Ad Channel:** " + (listing.advertising_channel_id.empty() ? "*not set*" : "<#" + listing.advertising_channel_id + ">") + "\n"
                                "• **Tags:** " + (listing.tags.empty() ? "*not set*" : "`" + listing.tags + "`") + "\n"
                                "• **Invite URL:** " + (listing.invite_url.empty() ? "*not set*" : listing.invite_url) + "\n"
                                "• **Status:** `" + listing.status + "`\n"
                                "• **Description:** " + (listing.description.empty() ? "*not set*" : listing.description) + "\n"
                                "• **Custom Embed JSON:** " + (listing.embed_json.empty() ? "*none*" : "\n```json\n" + listing.embed_json + "\n```") + "\n\n";

                        desc += "**Available Commands:**\n"
                                "• `" + pfx + "list setup` — Interactive wizard to list your server.\n"
                                "• `" + pfx + "list set <key> <value>` — Set specific settings (mode, channel, tags, desc, invite, embed).\n"
                                "• `" + pfx + "list config` — View your current server configuration.\n"
                                "• `" + pfx + "list submit` — Submit your server for staff review.\n"
                                "• `" + pfx + "list bump` — Bump your server and broadcast its ad embed.\n";

                        if (is_admin || bot.is_owner(msg.author.id)) {
                            desc += "\n*You have permission to edit this configuration.*";
                        } else {
                            desc += "\n*Only users with Manage Server permission can edit this configuration.*";
                        }

                        embed["description"] = desc;
                        payload.embeds.push_back(embed);
                        bot.send_message(msg.channel_id, payload);
                    });
                    return;
                }

                std::string subcommand = args[0];
                std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);

                // ── Help check ──
                if (subcommand == "help") {
                    bot.send_message(msg.channel_id,
                        "📋 **Server List / P2P Ad Commands:**\n"
                        "• `" + pfx + "list` — View paginated list of approved servers.\n"
                        "• `" + pfx + "list setup` — Interactive wizard to list your server.\n"
                        "• `" + pfx + "list set <key> <value>` — Set specific settings (mode, channel, tags, desc, invite, embed).\n"
                        "• `" + pfx + "list config` — View your current server configuration.\n"
                        "• `" + pfx + "list submit` — Submit your server for staff review.\n"
                        "• `" + pfx + "list bump` — Bump your server and broadcast its ad embed.\n"
                        "• `" + pfx + "list approve <server_id>` — [Staff Only] Approve a submission.\n"
                        "• `" + pfx + "list deny <server_id> [reason]` — [Staff Only] Deny a submission."
                    );
                    return;
                }

                // ── Administrative/Configuration Commands (Requires Manage Server) ──
                if (subcommand == "setup" || subcommand == "set" || subcommand == "config" || subcommand == "submit" || subcommand == "bump") {
                    bot.fetch_member(msg.server_id, msg.author.id, [this, &bot, msg, subcommand, args, pfx](stoatpp::models::Member member, bool ok) {
                        if (!ok) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to verify permissions.");
                            return;
                        }
                        if (!is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, member)) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " You need **Manage Server** permission to modify listing configuration.");
                            return;
                        }

                        // Retrieve or initialize listing config
                        auto listing_opt = g_db->get_server_listing(msg.server_id);
                        Database::ServerListing listing;
                        if (listing_opt) {
                            listing = *listing_opt;
                        } else {
                            listing.server_id = msg.server_id;
                            listing.created_at = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                        }
                        
                        // We also need server name
                        auto srv_opt = bot.get_server(msg.server_id);
                        if (srv_opt && !srv_opt->name.empty()) {
                            listing.name = srv_opt->name;
                        } else {
                            listing.name = "Unknown Server";
                        }

                        if (subcommand == "config") {
                            stoatpp::models::MessagePayload payload;
                            nlohmann::json embed = nlohmann::json::object();
                            embed["title"] = "🎫 Listing Config: " + listing.name;
                            embed["colour"] = "#a78bfa";
                            
                            std::string desc = 
                                "• **Mode:** `" + listing.mode + "` (website, bump, both, disabled)\n"
                                "• **Ad Channel:** " + (listing.advertising_channel_id.empty() ? "*not set*" : "<#" + listing.advertising_channel_id + ">") + "\n"
                                "• **Tags:** " + (listing.tags.empty() ? "*not set*" : "`" + listing.tags + "`") + "\n"
                                "• **Invite URL:** " + (listing.invite_url.empty() ? "*not set*" : listing.invite_url) + "\n"
                                "• **Status:** `" + listing.status + "`\n"
                                "• **Description:** " + (listing.description.empty() ? "*not set*" : listing.description) + "\n"
                                "• **Custom Embed JSON:** " + (listing.embed_json.empty() ? "*none*" : "\n```json\n" + listing.embed_json + "\n```");
                                
                            embed["description"] = desc;
                            payload.embeds.push_back(embed);
                            bot.send_message(msg.channel_id, payload);
                            return;
                        }

                        if (subcommand == "submit") {
                            if (listing.mode == "disabled") {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Listing is currently disabled. Use `.list set mode <website|bump|both>` before submitting.");
                                return;
                            }
                            if (listing.description.empty()) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Please provide a description before submitting.");
                                return;
                            }
                            if (listing.invite_url.empty()) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Please configure an invite URL before submitting.");
                                return;
                            }

                            listing.status = "pending";
                            g_db->save_server_listing(listing);

                            stoatpp::models::MessagePayload staff_payload;
                            nlohmann::json embed = nlohmann::json::object();
                            embed["title"] = "📥 New Server Listing Submission";
                            embed["colour"] = "#fdcb6e";
                            std::string desc = "• **Server Name:** " + listing.name + "\n"
                                               "• **Server ID:** `" + listing.server_id + "`\n"
                                               "• **Mode:** `" + listing.mode + "`\n"
                                               "• **Tags:** `" + listing.tags + "`\n"
                                               "• **Invite:** [Join](" + listing.invite_url + ")\n"
                                               "• **Description:** " + listing.description + "\n\n"
                                               "To approve: `.list approve " + listing.server_id + "`\n"
                                               "To deny: `.list deny " + listing.server_id + " [reason]`";
                            embed["description"] = desc;
                            staff_payload.embeds.push_back(embed);
                            
                            if (!listing.embed_json.empty()) {
                                try {
                                    nlohmann::json custom_embed = nlohmann::json::parse(listing.embed_json);
                                    staff_payload.embeds.push_back(custom_embed);
                                } catch(...) {}
                            }
                            
                            std::string staff_chan = get_staff_channel_id();
                            if (!staff_chan.empty()) {
                                try {
                                    bot.send_message(staff_chan, staff_payload);
                                } catch (...) {}
                            }
                            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " **Submission received!** Staff will review it shortly.");
                            return;
                        }

                        if (subcommand == "set") {
                            if (args.size() < 3) {
                                bot.send_message(msg.channel_id, "usage: `.list set <mode|channel|tags|desc|invite|embed> <value...>`");
                                return;
                            }
                            std::string key = args[1];
                            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
                            
                            // Combine arguments for value
                            std::string val = "";
                            for (size_t i = 2; i < args.size(); ++i) {
                                val += args[i] + " ";
                            }
                            if (!val.empty()) val.pop_back(); // remove trailing space

                            bool needs_reapproval = false;

                            if (key == "mode") {
                                std::transform(val.begin(), val.end(), val.begin(), ::tolower);
                                if (val != "website" && val != "bump" && val != "both" && val != "disabled") {
                                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid mode. Must be one of: `website`, `bump`, `both`, `disabled`.");
                                    return;
                                }
                                listing.mode = val;
                            } else if (key == "channel") {
                                std::string chan_id = val;
                                chan_id.erase(0, chan_id.find_first_not_of(" \t\r\n"));
                                chan_id.erase(chan_id.find_last_not_of(" \t\r\n") + 1);
                                if (chan_id.rfind("<#", 0) == 0 && chan_id.back() == '>') {
                                    chan_id = chan_id.substr(2, chan_id.size() - 3);
                                } else if (chan_id == "none" || chan_id == "clear") {
                                    chan_id = "";
                                }
                                // Validate: must be empty (clear) or a 26-char ULID
                                if (!chan_id.empty() && !(chan_id.size() == 26 &&
                                    chan_id.find_first_not_of("0123456789ABCDEFGHJKMNPQRSTVWXYZabcdefghjkmnpqrstvwxyz") == std::string::npos)) {
                                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid channel. Please **mention** the channel (e.g. `<#channel_id>`), or use `none` to clear it.");
                                    return;
                                }
                                listing.advertising_channel_id = chan_id;
                            } else if (key == "tags") {
                                listing.tags = val;
                                needs_reapproval = true;
                            } else if (key == "desc" || key == "description") {
                                listing.description = val;
                                needs_reapproval = true;
                            } else if (key == "invite") {
                                if (val.rfind("http://", 0) != 0 && val.rfind("https://", 0) != 0) {
                                    val = "https://" + val;
                                }
                                listing.invite_url = val;
                                needs_reapproval = true;
                            } else if (key == "embed") {
                                if (val == "none" || val == "clear") {
                                    listing.embed_json = "";
                                } else {
                                    bool is_json = false;
                                    try {
                                        nlohmann::json parsed = nlohmann::json::parse(val);
                                        is_json = true;
                                    } catch (...) {}

                                    if (is_json) {
                                        listing.embed_json = val;
                                    } else {
                                        std::string title = "";
                                        std::string description = "";
                                        std::string colour = "";
                                        std::string image_url = "";

                                        std::stringstream ss(val);
                                        std::string line;
                                        while (std::getline(ss, line)) {
                                            size_t colon = line.find(':');
                                            if (colon == std::string::npos) continue;
                                            std::string k = line.substr(0, colon);
                                            std::string v = line.substr(colon + 1);

                                            k.erase(0, k.find_first_not_of(" \t\r\n"));
                                            k.erase(k.find_last_not_of(" \t\r\n") + 1);
                                            v.erase(0, v.find_first_not_of(" \t\r\n"));
                                            v.erase(v.find_last_not_of(" \t\r\n") + 1);

                                            std::transform(k.begin(), k.end(), k.begin(), ::tolower);
                                            if (k == "title") title = v;
                                            else if (k == "description") description = v;
                                            else if (k == "colour" || k == "color") colour = v;
                                            else if (k == "image") image_url = v;
                                        }

                                        nlohmann::json embed = nlohmann::json::object();
                                        if (!title.empty()) embed["title"] = title;
                                        if (!description.empty()) embed["description"] = description;
                                        if (!colour.empty()) {
                                            std::string clean_col = colour;
                                            std::transform(clean_col.begin(), clean_col.end(), clean_col.begin(), ::tolower);
                                            if (clean_col == "random") {
                                                std::vector<std::string> cols = {"#a78bfa", "#3b82f6", "#10b981", "#f59e0b", "#ef4444"};
                                                embed["colour"] = cols[std::rand() % cols.size()];
                                            } else if (clean_col == "green") {
                                                embed["colour"] = "#10b981";
                                            } else if (clean_col == "blue") {
                                                embed["colour"] = "#3b82f6";
                                            } else if (clean_col == "red") {
                                                embed["colour"] = "#ef4444";
                                            } else if (clean_col == "yellow") {
                                                embed["colour"] = "#f59e0b";
                                            } else if (clean_col == "purple") {
                                                embed["colour"] = "#8b5cf6";
                                            } else {
                                                if (colour.rfind("#", 0) != 0 && (colour.length() == 6 || colour.length() == 8)) {
                                                    embed["colour"] = "#" + colour;
                                                } else {
                                                    embed["colour"] = colour;
                                                }
                                            }
                                        }

                                        std::string final_img = "";
                                        if (!image_url.empty() && image_url != "none" && image_url != "attachment") {
                                            final_img = image_url;
                                        } else if (!msg.attachments.empty()) {
                                            final_img = msg.attachments[0];
                                        }

                                        if (!final_img.empty()) {
                                            if (final_img.rfind("http://", 0) == 0 || final_img.rfind("https://", 0) == 0) {
                                                std::string uploaded_id = bot.rest().upload_from_url(final_img);
                                                if (!uploaded_id.empty()) {
                                                    embed["media"] = uploaded_id;
                                                } else {
                                                    embed["icon_url"] = final_img;
                                                }
                                            } else {
                                                embed["media"] = final_img;
                                            }
                                        }

                                        if (embed.empty()) {
                                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid format. Paste raw JSON, or type fields as:\ntitle:text\ndescription:text\ncolour:hex\nimage:link");
                                            return;
                                        }
                                        listing.embed_json = embed.dump();
                                    }
                                }
                                needs_reapproval = true;
                            } else {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Unknown setting key. Use: `mode`, `channel`, `tags`, `desc`, `invite`, `embed`.");
                                return;
                            }

                            if (needs_reapproval && listing.status == "approved") {
                                listing.status = "pending";
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " **Notice:** Changing tags, description, invite, or custom embed requires re-approval. Status reset to **pending**.");
                            }

                            if (g_db->save_server_listing(listing)) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Setting `" + key + "` successfully updated.");
                            } else {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save setting.");
                            }
                            return;
                        }

                        if (subcommand == "bump") {
                            if (listing.status != "approved") {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Your server listing is not approved yet. Use `.list submit` to submit it for review.");
                                return;
                            }
                            if (listing.mode != "bump" && listing.mode != "both" && listing.mode != "website") {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Bumping is not enabled for your server. Set your listing mode to `website`, `bump`, or `both` using `.list set mode both`.");
                                return;
                            }

                            uint64_t now_secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                            if (now_secs - listing.last_bumped_at < 7200) {
                                uint64_t diff = 7200 - (now_secs - listing.last_bumped_at);
                                uint64_t hours = diff / 3600;
                                uint64_t mins = (diff % 3600) / 60;
                                uint64_t secs = diff % 60;
                                std::string cd_str = "";
                                if (hours) cd_str += std::to_string(hours) + "h ";
                                if (mins) cd_str += std::to_string(mins) + "m ";
                                cd_str += std::to_string(secs) + "s";
                                bot.send_message(msg.channel_id, "⏱️ **Cooldown!** You can bump this server again in **" + cd_str + "**.");
                                return;
                            }

                            listing.last_bumped_at = now_secs;
                            listing.bump_count += 1;
                            g_db->save_server_listing(listing);

                            if (listing.mode == "website") {
                                bot.send_message(msg.channel_id, "" + bronx::emj_yes + " **Bumped!** Your server listing has been bumped on the website.");
                                return;
                            }

                            stoatpp::models::MessagePayload ad_payload;
                            nlohmann::json custom_embed;
                            bool has_custom_embed = false;
                            if (!listing.embed_json.empty()) {
                                try {
                                    custom_embed = nlohmann::json::parse(listing.embed_json);
                                    has_custom_embed = true;
                                } catch (...) {}
                            }

                            if (has_custom_embed) {
                                // If media field is an HTTP URL, upload it to Autumn CDN now
                                if (custom_embed.contains("media") && custom_embed["media"].is_string()) {
                                    std::string m = custom_embed["media"].get<std::string>();
                                    if (m.rfind("http://", 0) == 0 || m.rfind("https://", 0) == 0) {
                                        std::string uploaded_id = bot.rest().upload_from_url(m);
                                        if (!uploaded_id.empty()) {
                                            custom_embed["media"] = uploaded_id;
                                            listing.embed_json = custom_embed.dump();
                                            g_db->save_server_listing(listing);
                                        } else {
                                            custom_embed.erase("media");
                                            if (!custom_embed.contains("icon_url")) {
                                                custom_embed["icon_url"] = m;
                                            }
                                        }
                                    }
                                }
                                // Append invite URL to the custom embed so the join card always appears
                                if (!listing.invite_url.empty()) {
                                    // Add a URL field — Revolt renders this as a clickable server invite card
                                    if (!custom_embed.contains("url") || custom_embed["url"].is_null() || custom_embed["url"].get<std::string>().empty()) {
                                        custom_embed["url"] = listing.invite_url;
                                    }
                                    // Also append a "👉 Join here:" line at the end of description if not already present
                                    std::string cur_desc = custom_embed.contains("description") && custom_embed["description"].is_string()
                                        ? custom_embed["description"].get<std::string>() : "";
                                    if (cur_desc.find(listing.invite_url) == std::string::npos) {
                                        cur_desc += "\n\n👉 **Join here:** " + listing.invite_url;
                                        custom_embed["description"] = cur_desc;
                                    }
                                }
                                ad_payload.embeds.push_back(custom_embed);
                            } else {
                                nlohmann::json embed = nlohmann::json::object();
                                embed["title"] = "📢 Server Bump: " + listing.name;
                                embed["colour"] = "#a78bfa";
                                std::string desc = listing.description;
                                if (!listing.tags.empty()) desc += "\n\n**Tags:** " + listing.tags;
                                if (!listing.invite_url.empty()) desc += "\n\n👉 **Join here:** " + listing.invite_url;
                                embed["description"] = desc;
                                ad_payload.embeds.push_back(embed);
                            }

                            auto all_approved = g_db->get_approved_server_listings();
                            int sent = 0;
                            stoatpp::models::MessageQuery query;
                            query.limit = 1;

                            for (const auto& other : all_approved) {
                                if (other.server_id == msg.server_id) continue;
                                if (other.mode != "bump" && other.mode != "both") continue;
                                if (other.advertising_channel_id.empty()) continue;

                                // Check for consecutive bumps
                                auto res = bot.rest().get_messages(other.advertising_channel_id, query);
                                if (!res.success()) {
                                    continue;
                                }

                                bool is_consecutive = false;
                                if (res.body.is_array() && !res.body.empty()) {
                                    const auto& last_msg = res.body[0];
                                    if (last_msg.contains("embeds") && last_msg["embeds"].is_array()) {
                                        for (const auto& embed : last_msg["embeds"]) {
                                            if (embed.contains("url") && embed["url"].is_string() && 
                                                embed["url"].get<std::string>() == listing.invite_url) {
                                                is_consecutive = true;
                                                break;
                                            }
                                            if (embed.contains("description") && embed["description"].is_string() && 
                                                embed["description"].get<std::string>().find(listing.invite_url) != std::string::npos) {
                                                is_consecutive = true;
                                                break;
                                            }
                                            if (embed.contains("title") && embed["title"].is_string() && 
                                                embed["title"].get<std::string>().find(listing.name) != std::string::npos) {
                                                is_consecutive = true;
                                                break;
                                            }
                                        }
                                    }
                                }

                                if (is_consecutive) {
                                    continue;
                                }

                                bot.send_message(other.advertising_channel_id, ad_payload);
                                sent++;
                            }

                            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " **Bumped!** Your server ad has been broadcast to **" + std::to_string(sent) + "** other participating servers' advertising channels.");
                            return;
                        }

                        if (subcommand == "setup") {
                            bot.send_message(msg.channel_id, "server list setup [1/6]: specify mode (`website`, `bump`, `both`, or `disabled`).");
                            bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(60), [this, &bot, msg, listing](std::optional<stoatpp::events::Message> r1) mutable {
                                if (!r1 || r1->content == "cancel") {
                                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Setup canceled.");
                                    return;
                                }
                                std::string mode = r1->content;
                                std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
                                if (mode != "website" && mode != "bump" && mode != "both" && mode != "disabled") {
                                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Invalid mode. Setup canceled.");
                                    return;
                                }
                                listing.mode = mode;

                                bot.send_message(msg.channel_id, "💬 **Server List Setup Wizard**\n**Step 2/6**: Mention the channel where other servers' bump ads should be sent, or reply `skip` to skip/none.");
                                bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(60), [this, &bot, msg, listing](std::optional<stoatpp::events::Message> r2) mutable {
                                    if (!r2 || r2->content == "cancel") {
                                        bot.send_message(msg.channel_id, "" + bronx::emj_no + " Setup canceled.");
                                        return;
                                    }
                                    if (r2->content != "skip") {
                                        std::string raw_chan = r2->content;
                                        raw_chan.erase(0, raw_chan.find_first_not_of(" \t\r\n"));
                                        raw_chan.erase(raw_chan.find_last_not_of(" \t\r\n") + 1);
                                        std::string chan_id = raw_chan;
                                        if (raw_chan.rfind("<#", 0) == 0 && raw_chan.back() == '>') {
                                            chan_id = raw_chan.substr(2, raw_chan.size() - 3);
                                        }
                                        // Validate the resolved ID is a proper ULID
                                        bool valid_chan = (chan_id.size() == 26 &&
                                            chan_id.find_first_not_of("0123456789ABCDEFGHJKMNPQRSTVWXYZabcdefghjkmnpqrstvwxyz") == std::string::npos);
                                        if (!valid_chan) {
                                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " That doesn't look like a valid channel. Please **mention** the channel (e.g. `<#channel_id>`). Setup canceled.");
                                            return;
                                        }
                                        listing.advertising_channel_id = chan_id;
                                    }

                                    bot.send_message(msg.channel_id, "💬 **Server List Setup Wizard**\n**Step 3/6**: Enter a few tags for your server, separated by spaces (e.g. `gaming programming fun`), or reply `skip`.");
                                    bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(60), [this, &bot, msg, listing](std::optional<stoatpp::events::Message> r3) mutable {
                                        if (!r3 || r3->content == "cancel") {
                                            bot.send_message(msg.channel_id, "" + bronx::emj_no + " Setup canceled.");
                                            return;
                                        }
                                        if (r3->content != "skip") {
                                            listing.tags = r3->content;
                                        }

                                        bot.send_message(msg.channel_id, "💬 **Server List Setup Wizard**\n**Step 4/6**: Type a short description of your server (1-2 sentences), or reply `skip`.");
                                        bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(60), [this, &bot, msg, listing](std::optional<stoatpp::events::Message> r4) mutable {
                                            if (!r4 || r4->content == "cancel") {
                                                bot.send_message(msg.channel_id, "" + bronx::emj_no + " Setup canceled.");
                                                return;
                                            }
                                            if (r4->content != "skip") {
                                                listing.description = r4->content;
                                            }

                                            bot.send_message(msg.channel_id, "💬 **Server List Setup Wizard**\n**Step 5/6**: Paste a public invite link for your server (e.g. `https://stoat.gg/...`), or reply `skip`.");
                                            bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(60), [this, &bot, msg, listing](std::optional<stoatpp::events::Message> r5) mutable {
                                                if (!r5 || r5->content == "cancel") {
                                                    bot.send_message(msg.channel_id, "" + bronx::emj_no + " Setup canceled.");
                                                    return;
                                                }
                                                if (r5->content != "skip") {
                                                    std::string invite = r5->content;
                                                    if (invite.rfind("http://", 0) != 0 && invite.rfind("https://", 0) != 0) {
                                                        invite = "https://" + invite;
                                                    }
                                                    listing.invite_url = invite;
                                                }

                                                bot.send_message(msg.channel_id, "💬 **Server List Setup Wizard**\n**Step 6/6**: If you want a custom ad embed, type its details now using this format (or upload an image attachment, or reply `skip` for none):\n```\ntitle: title text here\ndescription: description and stuff\ncolour: random/green/#00ff00/00ff00\nimage: link/attachment/none\n```");
                                                bot.await_message(msg.channel_id, msg.author.id, std::chrono::seconds(120), [this, &bot, msg, listing](std::optional<stoatpp::events::Message> r6) mutable {
                                                    if (!r6 || r6->content == "cancel") {
                                                        bot.send_message(msg.channel_id, "" + bronx::emj_no + " Setup canceled.");
                                                        return;
                                                    }
                                                    if (r6->content != "skip") {
                                                        std::string title = "";
                                                        std::string description = "";
                                                        std::string colour = "";
                                                        std::string image_url = "";

                                                        std::stringstream ss(r6->content);
                                                        std::string line;
                                                        while (std::getline(ss, line)) {
                                                            size_t colon = line.find(':');
                                                            if (colon == std::string::npos) continue;
                                                            std::string k = line.substr(0, colon);
                                                            std::string v = line.substr(colon + 1);

                                                            k.erase(0, k.find_first_not_of(" \t\r\n"));
                                                            k.erase(k.find_last_not_of(" \t\r\n") + 1);
                                                            v.erase(0, v.find_first_not_of(" \t\r\n"));
                                                            v.erase(v.find_last_not_of(" \t\r\n") + 1);

                                                            std::transform(k.begin(), k.end(), k.begin(), ::tolower);
                                                            if (k == "title") title = v;
                                                            else if (k == "description") description = v;
                                                            else if (k == "colour" || k == "color") colour = v;
                                                            else if (k == "image") image_url = v;
                                                        }

                                                        nlohmann::json embed = nlohmann::json::object();
                                                        if (!title.empty()) embed["title"] = title;
                                                        if (!description.empty()) embed["description"] = description;
                                                        if (!colour.empty()) {
                                                            std::string clean_col = colour;
                                                            std::transform(clean_col.begin(), clean_col.end(), clean_col.begin(), ::tolower);
                                                            if (clean_col == "random") {
                                                                std::vector<std::string> cols = {"#a78bfa", "#3b82f6", "#10b981", "#f59e0b", "#ef4444"};
                                                                embed["colour"] = cols[std::rand() % cols.size()];
                                                            } else if (clean_col == "green") {
                                                                embed["colour"] = "#10b981";
                                                            } else if (clean_col == "blue") {
                                                                embed["colour"] = "#3b82f6";
                                                            } else if (clean_col == "red") {
                                                                embed["colour"] = "#ef4444";
                                                            } else if (clean_col == "yellow") {
                                                                embed["colour"] = "#f59e0b";
                                                            } else if (clean_col == "purple") {
                                                                embed["colour"] = "#8b5cf6";
                                                            } else {
                                                                if (colour.rfind("#", 0) != 0 && (colour.length() == 6 || colour.length() == 8)) {
                                                                    embed["colour"] = "#" + colour;
                                                                } else {
                                                                    embed["colour"] = colour;
                                                                }
                                                            }
                                                        }

                                                        std::string final_img = "";
                                                        if (!image_url.empty() && image_url != "none" && image_url != "attachment") {
                                                            final_img = image_url;
                                                        } else if (!r6->attachments.empty()) {
                                                            final_img = r6->attachments[0];
                                                        }

                                                        if (!final_img.empty()) {
                                                            if (final_img.rfind("http://", 0) == 0 || final_img.rfind("https://", 0) == 0) {
                                                                std::string uploaded_id = bot.rest().upload_from_url(final_img);
                                                                if (!uploaded_id.empty()) {
                                                                    embed["media"] = uploaded_id;
                                                                } else {
                                                                    embed["icon_url"] = final_img;
                                                                }
                                                            } else {
                                                                embed["media"] = final_img;
                                                            }
                                                        }

                                                        if (!embed.empty()) {
                                                            listing.embed_json = embed.dump();
                                                        }
                                                    }

                                                    listing.status = "pending"; // Reset to pending after complete setup
                                                    if (g_db->save_server_listing(listing)) {
                                                        bot.send_message(msg.channel_id, "" + bronx::emj_yes + " **Setup complete!** Run `.list submit` to submit your listing for staff review.");
                                                    } else {
                                                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save listing configuration.");
                                                    }
                                                });
                                            });
                                        });
                                    });
                                });
                            });
                            return;
                        }
                    });
                    return;
                }

                // ── Review Commands (Staff only) ──
                if (subcommand == "approve" || subcommand == "deny") {
                    std::string staff_chan = get_staff_channel_id();
                    if (!staff_chan.empty() && msg.channel_id != staff_chan && !bot.is_owner(msg.author.id)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Review commands can only be run by staff in the designated review channel.");
                        return;
                    }

                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "usage: `.list <approve|deny> <server_id> [reason...]`");
                        return;
                    }

                    std::string target_srv = args[1];

                    auto verify_staff_and_run = [this, &bot, msg, subcommand, target_srv, args]() {
                        auto listing_opt = g_db->get_server_listing(target_srv);
                        if (!listing_opt) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Server listing for ID `" + target_srv + "` not found.");
                            return;
                        }
                        auto listing = *listing_opt;

                        if (subcommand == "approve") {
                            listing.status = "approved";
                            if (g_db->save_server_listing(listing)) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Server listing for **" + listing.name + "** has been approved!");
                                
                                auto target_srv_details = bot.get_server(target_srv);
                                if (target_srv_details && !target_srv_details->owner.empty()) {
                                    std::thread([&bot, target_srv_details, listing]() {
                                        auto dm_res = bot.rest().open_dm(target_srv_details->owner);
                                        if (dm_res.success()) {
                                            std::string dm_chan = dm_res.body["_id"].get<std::string>();
                                            bot.send_message(dm_chan, "" + bronx::emj_yes + " Your server listing/ad for **" + listing.name + "** has been approved!");
                                        }
                                    }).detach();
                                }
                            } else {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save status update.");
                            }
                        } else {
                            listing.status = "denied";
                            std::string reason = "No reason provided.";
                            if (args.size() >= 3) {
                                reason = "";
                                for (size_t i = 2; i < args.size(); ++i) {
                                    reason += args[i] + " ";
                                }
                                if (!reason.empty()) reason.pop_back();
                            }
                            if (g_db->save_server_listing(listing)) {
                                bot.send_message(msg.channel_id, "" + bronx::emj_no + " Server listing for **" + listing.name + "** has been denied.");
                                
                                auto target_srv_details = bot.get_server(target_srv);
                                if (target_srv_details && !target_srv_details->owner.empty()) {
                                    std::thread([&bot, target_srv_details, listing, reason]() {
                                        auto dm_res = bot.rest().open_dm(target_srv_details->owner);
                                        if (dm_res.success()) {
                                            std::string dm_chan = dm_res.body["_id"].get<std::string>();
                                            bot.send_message(dm_chan, "" + bronx::emj_no + " Your server listing/ad for **" + listing.name + "** has been denied. Reason: " + reason);
                                        }
                                    }).detach();
                                }
                            } else {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save status update.");
                            }
                        }
                    };

                    if (bot.is_owner(msg.author.id)) {
                        verify_staff_and_run();
                    } else {
                        bot.fetch_member(msg.server_id, msg.author.id, [&bot, msg, verify_staff_and_run](stoatpp::models::Member member, bool ok) {
                            if (ok && is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, member)) {
                                verify_staff_and_run();
                            } else {
                                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Permission error: you need Manage Server permission to review listings.");
                            }
                        });
                    }
                    return;
                }

                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Unknown subcommand. Use setup, set, config, submit, bump.");
            },
            "Utility"
        });
    }
};

} // namespace bronx::cogs
