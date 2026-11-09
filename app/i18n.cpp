#include "i18n.h"
#include "common/platform.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <ranges>
#include <unordered_map>

#if PLATFORM_WINDOWS
#include <windows.h>
#elif PLATFORM_MACOS
#include <CoreFoundation/CoreFoundation.h>
#endif

// defined in the generated i18n_data.cpp (see cmake/embed_i18n.cmake)
namespace bt::i18n::embedded {
    struct file {
        std::string_view code;
        std::string_view json;
    };

    extern const file files[];
    extern const std::size_t files_count;
}

using namespace std;

namespace bt::i18n {

    namespace {
        struct sv_hash {
            using is_transparent = void;

            size_t operator()(string_view s) const noexcept { return hash<string_view>{}(s); }
        };

        using table = unordered_map<string, string, sv_hash, equal_to<>>;

        struct catalogue {
            string name;
            string fallback;
            table entries;
        };

        const string English{"en"};

        // ordered by code, so the language menu is stable
        const map<string, catalogue>& catalogues() {
            static const map<string, catalogue> all = [] {
                map<string, catalogue> r;
                for(size_t i = 0; i < embedded::files_count; i++) {
                    const auto& f = embedded::files[i];
                    if(f.code.empty()) continue;

                    // no exceptions: a broken file just means "no translation"
                    nlohmann::json j = nlohmann::json::parse(f.json.begin(), f.json.end(), nullptr, false);
                    if(!j.is_object()) continue;

                    catalogue c;
                    for(auto& [key, value]: j.items()) {
                        if(!value.is_string()) continue;
                        string v = value.get<string>();
                        if(key == "@name") c.name = v;
                        else if(key == "@fallback") c.fallback = v;
                        else if(!key.starts_with('@') && !v.empty()) c.entries.emplace(key, std::move(v));
                    }
                    if(c.name.empty()) c.name = string{f.code};
                    r.emplace(string{f.code}, std::move(c));
                }
                return r;
            }();
            return all;
        }

        string active_code{English};
        vector<const table*> active_chain;

        string resolve(const string& wanted) {
            const auto& all = catalogues();
            if(wanted.empty() || wanted == English) return English;
            if(all.contains(wanted)) return wanted;

            string lang = wanted.substr(0, wanted.find('_'));
            if(all.contains(lang)) return lang;

            // same language, other region (e.g. "de_AT" -> "de_DE")
            for(const auto& code: all | views::keys) {
                if(code.starts_with(lang + "_")) return code;
            }
            return English;
        }

        string to_lower(string s) {
            ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
            return s;
        }

        string to_upper(string s) {
            ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(toupper(c)); });
            return s;
        }
    }

    string normalise(string_view locale) {
        string s{locale};

        // strip encoding and modifier: "zh_CN.UTF-8", "sr_RS@latin"
        if(size_t pos = s.find_first_of(".@"); pos != string::npos) s.resize(pos);
        ranges::replace(s, '-', '_');

        vector<string> parts;
        for(auto part: s | views::split('_')) {
            parts.emplace_back(part.begin(), part.end());
        }
        if(parts.empty()) return English;

        string lang = to_lower(parts[0]);
        if(lang.empty() || lang == "c" || lang == "posix") return English;

        if(lang == "zh") {
            // script or region decides between simplified and traditional
            for(size_t i = 1; i < parts.size(); i++) {
                string p = to_upper(parts[i]);
                if(p == "HANT" || p == "TW" || p == "HK" || p == "MO") return "zh_TW";
            }
            return "zh_CN";
        }

        for(size_t i = 1; i < parts.size(); i++) {
            if(parts[i].size() == 2 && ranges::all_of(parts[i], [](unsigned char c) { return isalpha(c); })) {
                return lang + "_" + to_upper(parts[i]);
            }
        }
        return lang;
    }

    string detect_os_language() {
#if PLATFORM_WINDOWS
        wchar_t buf[LOCALE_NAME_MAX_LENGTH]{};
        LANGID id = ::GetUserDefaultUILanguage();
        if(::LCIDToLocaleName(MAKELCID(id, SORT_DEFAULT), buf, LOCALE_NAME_MAX_LENGTH, 0) > 0) {
            // locale names are plain ASCII ("zh-CN")
            string name;
            for(const wchar_t* p = buf; *p; p++) name += static_cast<char>(*p);
            return normalise(name);
        }
        return English;
#elif PLATFORM_MACOS
        string result{English};
        if(CFArrayRef langs = ::CFLocaleCopyPreferredLanguages()) {
            if(::CFArrayGetCount(langs) > 0) {
                auto first = static_cast<CFStringRef>(::CFArrayGetValueAtIndex(langs, 0));
                char buf[64]{};
                if(::CFStringGetCString(first, buf, sizeof(buf), kCFStringEncodingUTF8)) {
                    result = normalise(buf);
                }
            }
            ::CFRelease(langs);
        }
        return result;
#else
        // gettext order of precedence
        for(const char* var: {"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"}) {
            const char* v = getenv(var);
            if(!v || !*v) continue;
            string value{v};
            // LANGUAGE is a priority list: "zh_CN:en"
            value = value.substr(0, value.find(':'));
            string n = normalise(value);
            if(n != English || to_lower(value).starts_with("en")) return n;
        }
        return English;
#endif
    }

    void init(const string& preferred) {
        string wanted = (preferred.empty() || preferred == "auto") ? detect_os_language() : normalise(preferred);
        active_code = resolve(wanted);

        // build the lookup chain: language itself, then its declared fallbacks
        active_chain.clear();
        const auto& all = catalogues();
        string code = active_code;
        for(int depth = 0; depth < 4 && code != English; depth++) {
            auto it = all.find(code);
            if(it == all.end()) break;
            if(ranges::find(active_chain, &it->second.entries) != active_chain.end()) break; // cycle
            active_chain.push_back(&it->second.entries);
            code = it->second.fallback;
        }
    }

    const string& current() {
        return active_code;
    }

    const vector<language>& languages() {
        static const vector<language> r = [] {
            vector<language> v{{English, "English"}};
            for(const auto& [code, c]: catalogues()) {
                v.push_back({code, c.name});
            }
            return v;
        }();
        return r;
    }

    string tr(string_view en) {
        for(const table* t: active_chain) {
            if(auto it = t->find(en); it != t->end()) return it->second;
        }
        return string{en};
    }
}
