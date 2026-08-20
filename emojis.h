#pragma once
// emojis.h — Configurable bot emoji strings.
//
// Set these in your .env file to use custom Revolt emoji IDs:
//   YES_EMJ=01M0EHR5E6J2CFG6GM95WFNHHH
//   NO_EMJ=01M0EHVNFBBKF4WRCE3SC5PYRQ
//   WARNING_EMJ=01M0EJ6E6NV94SKB6X9ZR752W0
//   ERROR_EMJ=01M0EJJ34THV9GBD5ZKMRJEMG9
//
// Values are formatted as :ID: for inline use.
// Falls back to unicode emojis if env vars are not set.

#include <cstdlib>
#include <string>

namespace bronx {

inline std::string emj_yes;      // ✅ check / approved / affirmative
inline std::string emj_no;       // ❌ denied / cancelled / no
inline std::string emj_warning;  // ⚠️ warning / caution
inline std::string emj_error;    // 🛑 error / permission failure

// Call once after load_env() in main().
inline void load_emojis() {
    auto from_env = [](const char* key, const std::string& unicode_fallback) -> std::string {
        const char* val = std::getenv(key);
        if (val && val[0] != '\0') {
            // Wrap raw ID in :ID: syntax expected by Revolt
            return std::string(":") + val + ":";
        }
        return unicode_fallback;
    };

    emj_yes     = from_env("YES_EMJ",     "✅");
    emj_no      = from_env("NO_EMJ",      "❌");
    emj_warning = from_env("WARNING_EMJ", "⚠️");
    emj_error   = from_env("ERROR_EMJ",   "🛑");
}

}  // namespace bronx
