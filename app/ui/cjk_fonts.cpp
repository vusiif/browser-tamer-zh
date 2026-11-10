#include "cjk_fonts.h"
#include "../i18n.h"
#include "common/platform.h"
#include "fonts/font_loader.h"
#include "imgui.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#if PLATFORM_WINDOWS
#include "common/os.h"
#elif PLATFORM_LINUX
#include <fontconfig/fontconfig.h>
#endif

using namespace std;

namespace bt::ui {

    namespace {
        // marks our merged sources so a second call on the same atlas is a no-op
        constexpr const char* SourceTag = "bt-cjk";

        // must match grey's font_loader so merged glyphs line up with the Latin ones
        constexpr float FontSize = 18.0f;

        struct font_file {
            string path;
            int index{0};   // face index inside .ttc collections
        };

        bool is_traditional() {
            return i18n::current() == "zh_TW";
        }

#if PLATFORM_WINDOWS
        vector<font_file> candidates(bool bold) {
            const string dir = grey::common::os::get_system_fonts_path() + "\\";
            // Microsoft YaHei (simplified) and Microsoft JhengHei (traditional) ship with every Windows since Vista.
            vector<string> names = is_traditional()
                ? vector<string>{"msjh", "msyh"}
                : vector<string>{"msyh", "msjh"};

            vector<font_file> r;
            for(const string& n: names) {
                if(bold) r.push_back({dir + n + "bd.ttc"});
                r.push_back({dir + n + ".ttc"});
            }
            r.push_back({dir + "simsun.ttc"});   // last resort
            return r;
        }
#elif PLATFORM_LINUX
        vector<font_file> candidates(bool bold) {
            vector<font_file> r;
            FcConfig* config = ::FcInitLoadConfigAndFonts();
            if(!config) return r;

            string query = string{"sans-serif:lang="} + (is_traditional() ? "zh-tw" : "zh-cn");
            if(bold) query += ":bold";

            if(FcPattern* pattern = ::FcNameParse(reinterpret_cast<const FcChar8*>(query.c_str()))) {
                ::FcConfigSubstitute(config, pattern, FcMatchPattern);
                ::FcDefaultSubstitute(pattern);

                FcResult result;
                if(FcPattern* match = ::FcFontMatch(config, pattern, &result)) {
                    FcChar8* file = nullptr;
                    FcCharSet* charset = nullptr;
                    int index = 0;
                    // fontconfig always returns *something*; make sure it really has Chinese glyphs ("中")
                    bool has_cjk = ::FcPatternGetCharSet(match, FC_CHARSET, 0, &charset) == FcResultMatch &&
                                   ::FcCharSetHasChar(charset, 0x4E2D);
                    if(has_cjk && ::FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file) {
                        ::FcPatternGetInteger(match, FC_INDEX, 0, &index);
                        r.push_back({reinterpret_cast<const char*>(file), index});
                    }
                    ::FcPatternDestroy(match);
                }
                ::FcPatternDestroy(pattern);
            }
            ::FcConfigDestroy(config);
            return r;
        }
#elif PLATFORM_MACOS
        vector<font_file> candidates(bool) {
            // PingFang covers both simplified and traditional; the others are fallbacks for older systems
            return {
                {"/System/Library/Fonts/PingFang.ttc"},
                {"/System/Library/Fonts/Hiragino Sans GB.ttc"},
                {"/System/Library/Fonts/STHeiti Light.ttc"},
                {"/Library/Fonts/Arial Unicode.ttf"}
            };
        }
#else
        vector<font_file> candidates(bool) { return {}; }
#endif

        /**
         * Font files are big (Microsoft YaHei is ~20MB), so each is read once per process and shared
         * between regular/monospace and between successive ImGui contexts. ImGui 1.92 requires the data
         * to outlive the atlas, which a process-lifetime cache guarantees.
         */
        const vector<char>* load(const string& path) {
            static map<string, vector<char>> cache;
            if(auto it = cache.find(path); it != cache.end()) {
                return it->second.empty() ? nullptr : &it->second;
            }

            vector<char> data;
            error_code ec;
            if(filesystem::is_regular_file(path, ec)) {
                ifstream in{filesystem::path{path}, ios::binary};
                data.assign(istreambuf_iterator<char>{in}, istreambuf_iterator<char>{});
            }
            auto& slot = cache[path] = std::move(data);
            return slot.empty() ? nullptr : &slot;
        }

        void merge_into(ImFont* dst, bool bold) {
            if(!dst) return;

            for(const font_file& f: candidates(bold)) {
                const vector<char>* data = load(f.path);
                if(!data) continue;

                ImFontConfig cfg;
                cfg.MergeMode = true;
                cfg.DstFont = dst;
                cfg.FontNo = static_cast<ImU32>(f.index);
                cfg.FontDataOwnedByAtlas = false;   // shared, process-lifetime buffer
                snprintf(cfg.Name, sizeof(cfg.Name), "%s", SourceTag);

                ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
                    const_cast<char*>(data->data()), static_cast<int>(data->size()), FontSize, &cfg);
                return;
            }
        }
    }

    void prewarm_cjk_fonts() {
        // mirror merge_cjk_fonts(): the first existing candidate for regular and for bold
        for(bool bold: {false, true}) {
            for(const font_file& f: candidates(bold)) {
                if(load(f.path)) break;
            }
        }
    }

    void merge_cjk_fonts() {
        ImFontAtlas* atlas = ImGui::GetIO().Fonts;
        if(!atlas || atlas->Fonts.empty()) return;

        for(const ImFontConfig& src: atlas->Sources) {
            if(strcmp(src.Name, SourceTag) == 0) return;
        }

        // grey adds the regular system font first
        merge_into(atlas->Fonts[0], false);
        merge_into(grey::fonts::font_loader::get_font(grey::font_weight::bold), true);
        merge_into(grey::fonts::font_loader::get_font(grey::font_weight::fixed_size), false);
    }
}
