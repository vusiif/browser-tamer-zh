#pragma once
#include <format>
#include <string>
#include <string_view>
#include <vector>

/**
 * Minimal UI localisation.
 *
 * The English source text is the lookup key, so untranslated strings simply stay English.
 * Translations live in i18n/<code>.json (flat "English": "translation" objects) and are embedded at build time.
 * Keys starting with '@' are metadata:
 *   "@name"     - native language name shown in the language menu, e.g. "简体中文"
 *   "@fallback" - language code to consult when a key is missing, e.g. "zh_CN" for zh_TW
 */
namespace bt::i18n {

    struct language {
        std::string code;           // "en", "zh_CN", ...
        std::string native_name;    // "English", "简体中文", ...
    };

    /**
     * @brief Selects the UI language.
     * @param preferred Language code from settings. Empty (or "auto") follows the operating system language.
     */
    void init(const std::string& preferred);

    /**
     * @brief Resolved language code currently in use, e.g. "zh_CN". "en" when no translation applies.
     */
    const std::string& current();

    /**
     * @brief OS UI language, normalised to the form used by translation files ("zh_CN", "zh_TW", "de", ...).
     */
    std::string detect_os_language();

    /**
     * @brief Normalises a locale identifier ("zh-Hans-CN", "zh_TW.UTF-8", "en-US") to "lang[_REGION]".
     *        Chinese is always mapped to either zh_CN (simplified) or zh_TW (traditional).
     */
    std::string normalise(std::string_view locale);

    /**
     * @brief All selectable languages: English first, then every embedded translation.
     */
    const std::vector<language>& languages();

    /**
     * @brief Translates English source text. Returns the input unchanged when there is no translation.
     */
    std::string tr(std::string_view en);

    /**
     * @brief Translates a std::format pattern and formats it.
     *        Translations may reorder arguments with indexed placeholders ("{1} ... {0}").
     *        A malformed translation falls back to the English pattern instead of throwing.
     */
    template<typename... Args>
    std::string trf(std::string_view en_fmt, const Args&... args) {
        auto store = std::make_format_args(args...);
        try {
            return std::vformat(tr(en_fmt), store);
        } catch(const std::format_error&) {
            return std::vformat(en_fmt, store);
        }
    }
}

namespace bt {
    using i18n::tr;
    using i18n::trf;
}
