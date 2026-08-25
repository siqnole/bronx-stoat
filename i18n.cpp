#include "i18n.h"
#include <fstream>
#include <iostream>
#include <algorithm>

namespace bronx {

std::unordered_map<std::string, nlohmann::json> i18n::translations_;
std::vector<SupportedLanguage> i18n::languages_ = {
    {"en", "English", "🇺🇸"},
    {"pt-BR", "Português (Brasil)", "🇧🇷"},
    {"es", "Español", "🇪🇸"},
    {"fr", "Français", "🇫🇷"},
    {"de", "Deutsch", "🇩🇪"}
};

std::string i18n::normalize_code(const std::string& input) {
    std::string code = input;
    std::transform(code.begin(), code.end(), code.begin(), ::tolower);
    if (code == "pt" || code == "pt-br" || code == "pt_br" || code == "portuguese" || code == "portugues") return "pt-BR";
    if (code == "en" || code == "english" || code == "en-us") return "en";
    if (code == "es" || code == "spanish" || code == "es-es") return "es";
    if (code == "fr" || code == "french" || code == "fr-fr") return "fr";
    if (code == "de" || code == "german" || code == "de-de") return "de";
    return input;
}

bool i18n::is_supported(const std::string& lang) {
    std::string norm = normalize_code(lang);
    for (const auto& l : languages_) {
        if (l.code == norm) return true;
    }
    return false;
}

std::vector<SupportedLanguage> i18n::get_supported_languages() {
    return languages_;
}

void i18n::init(const std::string& locales_dir) {
    translations_.clear();
    for (const auto& lang : languages_) {
        std::string filepath = locales_dir + "/" + lang.code + ".json";
        std::ifstream file(filepath);
        if (file.is_open()) {
            try {
                nlohmann::json j;
                file >> j;
                translations_[lang.code] = j;
            } catch (const std::exception& e) {
                std::cerr << "[i18n] Failed to parse " << filepath << ": " << e.what() << "\n";
            }
        }
    }
}

std::string i18n::resolve_lang(Database* db, const std::string& user_id, const std::string& server_id) {
    if (db) {
        if (!user_id.empty()) {
            auto u_lang = db->get_user_language(user_id);
            if (u_lang && is_supported(*u_lang)) {
                return normalize_code(*u_lang);
            }
        }
        if (!server_id.empty()) {
            auto s_lang = db->get_server_language(server_id);
            if (s_lang && is_supported(*s_lang)) {
                return normalize_code(*s_lang);
            }
        }
    }
    return "en";
}

std::string i18n::t(const std::string& lang, const std::string& key, const std::string& fallback, const std::unordered_map<std::string, std::string>& tokens) {
    std::string norm_lang = normalize_code(lang);
    std::string result = fallback;

    auto it = translations_.find(norm_lang);
    if (it != translations_.end() && it->second.contains(key) && it->second[key].is_string()) {
        result = it->second[key].get<std::string>();
    }

    for (const auto& [k, v] : tokens) {
        std::string placeholder = "{" + k + "}";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), v);
            pos += v.length();
        }
    }

    return result;
}

} // namespace bronx
