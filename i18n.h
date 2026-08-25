#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>
#include "database.h"

namespace bronx {

struct SupportedLanguage {
    std::string code;
    std::string name;
    std::string flag;
};

class i18n {
public:
    static void init(const std::string& locales_dir = "locales");
    static std::string t(const std::string& lang, const std::string& key, const std::string& fallback, const std::unordered_map<std::string, std::string>& tokens = {});
    static std::string resolve_lang(Database* db, const std::string& user_id, const std::string& server_id);
    static bool is_supported(const std::string& lang);
    static std::vector<SupportedLanguage> get_supported_languages();
    static std::string normalize_code(const std::string& code);

private:
    static std::unordered_map<std::string, nlohmann::json> translations_;
    static std::vector<SupportedLanguage> languages_;
};

} // namespace bronx
