#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include <algorithm>
#include <iostream>

namespace bronx::cogs {

class TupperModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "tupper",
            {"t"},
            "manage your masquerade profiles (Tuppers) for proxying messages (subcommands: list, add, remove, avatar, brackets, rename).",
            "",
            {"<subcommand>", "[args...]"},
            [](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::cout << "[bronx command] handling tupper...\n";

                if (!g_db) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Database connection is not initialized.");
                    return;
                }

                std::string pfx = msg.prefix.empty() ? bot.config().command_prefix : msg.prefix;

                if (args.empty() || args[0] == "list") {
                    std::vector<Database::Tupper> tuppers = g_db->get_user_tuppers(msg.author.id);
                    stoatpp::models::MessagePayload payload;
                    nlohmann::json embed = nlohmann::json::object();
                    embed["title"] = "👥 Your Tuppers";
                    embed["colour"] = "#2ecc71";

                    std::string desc = "";
                    if (tuppers.empty()) {
                        desc = "You have no tuppers configured. Create one using:\n`" + pfx + "tupper add <name> <brackets> <avatar_url>`\n\n"
                               "Example:\n`" + pfx + "tupper add Cat [text] https://example.com/cat.png`";
                    } else {
                        desc = "Here are your configured tuppers:\n\n";
                        for (const auto& t : tuppers) {
                            desc += "• **" + t.name + "**:\n"
                                    "  - Brackets: `" + t.prefix + "text" + t.suffix + "`\n"
                                    "  - Avatar: [Link](" + t.avatar_url + ")\n\n";
                        }
                    }
                    embed["description"] = desc;
                    payload.embeds.push_back(embed);
                    bot.send_message(msg.channel_id, payload);
                    return;
                }

                std::string subcommand = args[0];
                std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);

                if (subcommand == "add" || subcommand == "create") {
                    if (args.size() < 3 || (args.size() < 4 && msg.attachments.empty())) {
                        bot.send_message(msg.channel_id, "Usage: `" + pfx + "tupper add <name> <brackets> [avatar_url]` or upload an image attachment.");
                        return;
                    }

                    std::string name = args[1];
                    if (name.length() < 1 || name.length() > 32) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Tupper name must be between 1 and 32 characters.");
                        return;
                    }

                    std::string brackets = args[2];
                    size_t pos = brackets.find("text");
                    if (pos == std::string::npos) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Brackets must contain the placeholder 'text' (e.g. `[text]` or `t:text`).");
                        return;
                    }

                    std::string prefix = brackets.substr(0, pos);
                    std::string suffix = brackets.substr(pos + 4);

                    if (prefix.empty() && suffix.empty()) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Prefix and suffix cannot both be empty.");
                        return;
                    }

                    std::string input_avatar = "";
                    if (!msg.attachments.empty()) {
                        input_avatar = bot.config().autumn_url + "/attachments/" + msg.attachments[0];
                    } else {
                        input_avatar = args[3];
                    }

                    if (input_avatar.length() < 1 || input_avatar.length() > 256) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Avatar URL must be between 1 and 256 characters.");
                        return;
                    }
                    std::string avatar_url = helpers::upload_avatar(bot, input_avatar);

                    // Check if name already exists for this user
                    std::vector<Database::Tupper> current = g_db->get_user_tuppers(msg.author.id);
                    for (const auto& t : current) {
                        if (t.name == name) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " You already have a tupper named '" + name + "'. Please choose a unique name.");
                            return;
                        }
                    }

                    if (g_db->add_tupper(msg.author.id, name, avatar_url, prefix, suffix)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Successfully created tupper **" + name + "**.");
                    } else {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to save tupper. Make sure the name is unique.");
                    }
                    return;
                }

                if (subcommand == "remove" || subcommand == "delete") {
                    if (args.size() < 2) {
                        bot.send_message(msg.channel_id, "Usage: `" + pfx + "tupper remove <name>`");
                        return;
                    }

                    std::string name = args[1];
                    if (g_db->remove_tupper(msg.author.id, name)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Successfully removed tupper **" + name + "**.");
                    } else {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to remove tupper. Check if the name exists.");
                    }
                    return;
                }

                if (subcommand == "avatar") {
                    if (args.size() < 2 || (args.size() < 3 && msg.attachments.empty())) {
                        bot.send_message(msg.channel_id, "Usage: `" + pfx + "tupper avatar <name> [new_avatar_url]` or upload an image attachment.");
                        return;
                    }

                    std::string name = args[1];
                    std::string url = "";
                    if (!msg.attachments.empty()) {
                        url = bot.config().autumn_url + "/attachments/" + msg.attachments[0];
                    } else {
                        url = args[2];
                    }

                    if (url.length() < 1 || url.length() > 256) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Avatar URL must be between 1 and 256 characters.");
                        return;
                    }
                    std::string uploaded_url = helpers::upload_avatar(bot, url);

                    if (g_db->update_tupper_avatar(msg.author.id, name, uploaded_url)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Successfully updated avatar for **" + name + "**.");
                    } else {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to update avatar. Make sure the tupper exists.");
                    }
                    return;
                }

                if (subcommand == "brackets") {
                    if (args.size() < 3) {
                        bot.send_message(msg.channel_id, "Usage: `" + pfx + "tupper brackets <name> <new_brackets>`");
                        return;
                    }

                    std::string name = args[1];
                    std::string brackets = args[2];
                    size_t pos = brackets.find("text");
                    if (pos == std::string::npos) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Brackets must contain the placeholder 'text' (e.g. `[text]` or `t:text`).");
                        return;
                    }

                    std::string prefix = brackets.substr(0, pos);
                    std::string suffix = brackets.substr(pos + 4);

                    if (prefix.empty() && suffix.empty()) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Prefix and suffix cannot both be empty.");
                        return;
                    }

                    if (g_db->update_tupper_brackets(msg.author.id, name, prefix, suffix)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Successfully updated brackets for **" + name + "**.");
                    } else {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to update brackets. Make sure the tupper exists.");
                    }
                    return;
                }

                if (subcommand == "rename") {
                    if (args.size() < 3) {
                        bot.send_message(msg.channel_id, "Usage: `" + pfx + "tupper rename <name> <new_name>`");
                        return;
                    }

                    std::string old_name = args[1];
                    std::string new_name = args[2];
                    if (new_name.length() < 1 || new_name.length() > 32) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " New name must be between 1 and 32 characters.");
                        return;
                    }

                    if (g_db->rename_tupper(msg.author.id, old_name, new_name)) {
                        bot.send_message(msg.channel_id, "" + bronx::emj_yes + " Successfully renamed tupper **" + old_name + "** to **" + new_name + "**.");
                    } else {
                        bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Failed to rename. Check if the tupper exists and the new name is unique.");
                    }
                    return;
                }

                bot.send_message(msg.channel_id, "" + bronx::emj_warning + " Unknown subcommand. Use `" + pfx + "tupper <list|add|remove|avatar|brackets|rename>`.");
            },
            "Tupper",
            0,
            0,
            {"list", "add <name> <brackets> <avatar_url>", "remove <name>", "avatar <name> <url>", "brackets <name> <brackets>", "rename <name> <new_name>"}
        });
    }
};

} // namespace bronx::cogs
