#pragma once
#include "stoatpp/bot_module.h"
#include "helpers.h"
#include "../database.h"
#include "../i18n.h"
#include <algorithm>
#include <string>
#include <vector>

namespace bronx::cogs {

class LanguageModule : public stoatpp::bot_module {
public:
    void register_handlers(stoatpp::cluster& bot) override {
        bot.register_command({
            "language",
            {"lang", "idioma", "locale"},
            "Configure your preferred language or server default language.",
            "",
            {"[code|server|reset]", "[code]"},
            [this](stoatpp::cluster& bot, const stoatpp::events::Message& msg, const std::vector<std::string>& args) {
                std::string current_locale = i18n::resolve_lang(g_db.get(), msg.author.id, msg.server_id);

                if (args.empty()) {
                    stoatpp::models::MessagePayload payload;
                    nlohmann::json embed = nlohmann::json::object();
                    embed["title"] = i18n::t(current_locale, "lang_available_title", "🌐 Language Settings");
                    embed["colour"] = "#5865f2";

                    std::string user_lang = (g_db && g_db->get_user_language(msg.author.id)) ? *g_db->get_user_language(msg.author.id) : "default (en)";
                    std::string server_lang = (g_db && g_db->get_server_language(msg.server_id)) ? *g_db->get_server_language(msg.server_id) : "default (en)";

                    std::string desc = i18n::t(current_locale, "lang_available_desc",
                        "Use `.lang <code>` to set your language, or `.lang server <code>` for server default.\n\n"
                        "**Supported Languages:**\n"
                        "• 🇺🇸 `en` — English\n"
                        "• 🇧🇷 `pt-BR` — Português (Brasil)\n"
                        "• 🇪🇸 `es` — Español\n"
                        "• 🇫🇷 `fr` — Français\n"
                        "• 🇩🇪 `de` — Deutsch");

                    desc += "\n\n**Current Preferences:**\n"
                            "• User Language: `" + user_lang + "`\n"
                            "• Server Default: `" + server_lang + "`";

                    embed["description"] = desc;
                    payload.embeds.push_back(embed);
                    bot.send_message(msg.channel_id, payload);
                    return;
                }

                std::string arg0 = args[0];
                std::string arg0_lower = arg0;
                std::transform(arg0_lower.begin(), arg0_lower.end(), arg0_lower.begin(), ::tolower);

                // Handle server setting: .lang server <code>
                if (arg0_lower == "server") {
                    bot.fetch_member(msg.server_id, msg.author.id, [this, &bot, msg, args, current_locale](stoatpp::models::Member member, bool ok) {
                        if (!ok || !is_user_trusted_or_owner(bot, msg.server_id, msg.author.id, member)) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " " + i18n::t(current_locale, "lang_perm_error", "Permission error: You need Manage Server permission to change server language."));
                            return;
                        }

                        if (args.size() < 2) {
                            bot.send_message(msg.channel_id, "usage: `.lang server <code|reset>`");
                            return;
                        }

                        std::string target_code = args[1];
                        std::string norm_target = i18n::normalize_code(target_code);

                        if (target_code == "reset" || target_code == "default" || target_code == "clear") {
                            if (g_db) g_db->save_server_language(msg.server_id, "");
                            bot.send_message(msg.channel_id, "" + bronx::emj_yes + " " + i18n::t(current_locale, "lang_server_reset", "Server default language reset to default."));
                            return;
                        }

                        if (!i18n::is_supported(norm_target)) {
                            bot.send_message(msg.channel_id, "" + bronx::emj_warning + " " + i18n::t(current_locale, "lang_invalid", "Invalid language code. Supported: `en`, `pt-BR`, `es`, `fr`, `de`."));
                            return;
                        }

                        if (g_db) g_db->save_server_language(msg.server_id, norm_target);
                        bot.send_message(msg.channel_id, "" + bronx::emj_yes + " " + i18n::t(norm_target, "lang_server_updated", "Server default language updated to **{lang}**.", {{"lang", norm_target}, {"name", norm_target}}));
                    });
                    return;
                }

                // Handle user setting: .lang <code> or .lang reset
                if (arg0_lower == "reset" || arg0_lower == "default" || arg0_lower == "clear") {
                    if (g_db) g_db->save_user_language(msg.author.id, "");
                    bot.send_message(msg.channel_id, "" + bronx::emj_yes + " " + i18n::t(current_locale, "lang_reset", "Language preference reset to default."));
                    return;
                }

                std::string norm_code = i18n::normalize_code(arg0);
                if (!i18n::is_supported(norm_code)) {
                    bot.send_message(msg.channel_id, "" + bronx::emj_warning + " " + i18n::t(current_locale, "lang_invalid", "Invalid language code. Supported: `en`, `pt-BR`, `es`, `fr`, `de`."));
                    return;
                }

                if (g_db) g_db->save_user_language(msg.author.id, norm_code);
                bot.send_message(msg.channel_id, "" + bronx::emj_yes + " " + i18n::t(norm_code, "lang_updated", "Language preference updated to **{lang}**.", {{"lang", norm_code}, {"name", norm_code}}));
            },
            "Utility"
        });
    }
};

} // namespace bronx::cogs
