#include <format>
#include <thread>
#include "globals.h"
#include "common/str.h"
#include "common/process.h"
#include "app/url_pipeline.h"
#include "app/rule_hit_log.h"
#include "app/url_opener.h"
#include "cmdline.h"
#include "app/discovery.h"
#include "app/i18n.h"
#include "app/resident.h"
#include <filesystem>
#include <limits>
#include "common/fss.h"

#if PLATFORM_WINDOWS
#include <windows.h>
#include <shellapi.h>
#include "common/win32/os.h"
#include "common/win32/window.h"
#endif

//ui
#include "app/ui/config_app.h"
#include "app/ui/picker_app.h"
#include "app/ui/toast_app.h"
#include "app/ui/cjk_fonts.h"

using namespace std;
using namespace grey::common;
using namespace bt;

// What the resident process is showing right now; decides what happens to commands arriving meanwhile.
enum class activity { idle, picker, toast, config };
static activity g_activity{activity::idle};

struct activity_scope {
    activity prev;
    explicit activity_scope(activity a) : prev{g_activity} { g_activity = a; }
    ~activity_scope() { g_activity = prev; }
};

// Marker appended to forwarded commands: the hotkey must be read by the process the click started,
// because a long-running process only sees its own (stale) keyboard state.
static const string HotkeyMarker = "hk:";

void open(click_payload cp, bool force_picker, bool picker_hotkey_down) {
    g_pipeline.process(cp);

    // decision whether to show picker or not
    bool show_picker{force_picker};
    optional<picker_invoked_reason> picker_reason;

    vector<browser_match_result> rule_matches;
    if(!force_picker) {
        if(picker_hotkey_down) {
            show_picker = true;
            picker_reason = picker_invoked_reason::hotkey_down;
        } else {
            rule_matches = browser::match(g_state.browsers, cp, g_script);
            if(rule_matches.size() > 1) {
                show_picker = true;
                picker_reason = picker_invoked_reason::rule_conflict;
            } else if(rule_matches.size() == 1) {
                if(g_state.picker.invoke.on_no_rule && rule_matches[0].rule.is_fallback) {
                    show_picker = true;
                    picker_reason = picker_invoked_reason::no_rule_defined;
                }
            }
        }
    } else {
        picker_reason = picker_invoked_reason::forced;
    }

    if(show_picker) {
        // choice narrows down only on rule conflict
        vector<profile_selection> limited_choices;
        if(picker_reason == picker_invoked_reason::rule_conflict) {
            for(auto& match : rule_matches) {
                limited_choices.push_back(match.profile);
            }
        }
        activity_scope as{activity::picker};
        ui::picker_app app{cp, limited_choices.empty() ? std::nullopt : std::make_optional(limited_choices)};
        const auto pr = app.run();
        if(pr) {
            cp.url = pr.url;
            url_opener::open(*pr.choice, cp);
            if(g_state.log_rule_hits) {
                rule_hit_log::i.write(cp, *pr.choice, picker_reason, nullopt);
            }
        }
    } else {
        auto matches = browser::match(g_state.browsers, cp, g_script);
        if(matches.empty()) {
            url_opener::open(cp);
            return;
        }

        browser_match_result& first_match = matches[0];
        first_match.rule.apply_to(cp);
        url_opener::open(first_match.profile, cp);
        if(g_state.log_rule_hits) {
            rule_hit_log::i.write(cp, first_match.profile, std::nullopt, matches[0]);
        }

        if(g_state.toast.enabled) {
            activity_scope as{activity::toast};
            ui::toast_app app{cp, first_match};
            app.run();
        }
    }
}

static string get_command(const string& data, string& command_data) {
    // Find the position of the first space character
    size_t pos = data.find(' ');

    // If no space is found, return the entire string
    if(pos == std::string::npos) {
        return "";
    }

    // Return the substring from the beginning up to the space character
    command_data = data.substr(pos + 1);
    return data.substr(0, pos);
}

static void execute(const string& data) {
    // settings are already loaded by g_config at this point
    i18n::init(g_state.ui_language);

    // will be set to "true" if "pick" command is detected
    bool force_picker{false};
    string clean_data = data; // copy of the data, will be used for further processing

    if(data.empty() || data.starts_with(ArgSplitter)) {
        // if data starts with ArgSplitter that means command line is empty, so we invoke the configuration util
        activity_scope as{activity::config};
        ui::config_app app;
        app.run();
        return;
    }

    // try to extract command from the data, which is the first word in the data string
    string command_data;
    string command = get_command(data, command_data);
    if(!command.empty()) {
        if(command == "pick") {
            // force-invoke the picker
            force_picker = true;
            clean_data = command_data;
        } else if(command == "browser") {
            cmdline c;
            c.exec(command, command_data);
            return;
        } else if(command == "discover") {
            vector<browser> fresh_browsers = discovery::discover_all_browsers();
            fresh_browsers = browser::merge(fresh_browsers, g_state.browsers);
            g_state.browsers = fresh_browsers;
            g_config.serialize();
            return;
        }
    }

    // if we reached this point, it's a normal operation mode
    // 0 - url
    // 1 - HWND
    auto parts = str::split(clean_data, ArgSplitter, true);
    if(parts.size() < 2) return;
    string url = parts[0];

    // forwarded commands carry the hotkey state of the process the click started
    bool hotkey_down;
    if(parts.size() > 2 && parts[2].starts_with(HotkeyMarker)) {
        hotkey_down = parts[2] == HotkeyMarker + "1";
    } else {
        hotkey_down = ui::picker_app::is_hotkey_down();
    }
    click_payload cp{.url = url, .raw_url = url};


#if PLATFORM_WINDOWS
    cp.source_window_handle = reinterpret_cast<HWND>(str::to_ulong(parts[1], 16));
    win32::window win{cp.source_window_handle};
    cp.window_title = win.get_text();
    auto pid = win.get_pid();
    cp.process_id = std::to_string(pid);
    process proc{pid};
    cp.process_path = proc.get_path();
    cp.process_name = proc.get_name();
    cp.process_description = proc.get_description();
#endif

#if PLATFORM_LINUX
    int pid = str::to_int(parts[1]);
    process proc{pid};
    cp.process_id = std::to_string(pid);
    cp.process_name = proc.get_name();
    cp.process_path = proc.get_path();
    cp.process_description = proc.get_description();
#endif

#ifndef NDEBUG
    if(command == "toast") {
        cp.url = command_data;
        browser_match_result bmr{browser::get_default(g_state.browsers).value(), match_rule{"a rule description"}};
        ui::toast_app app{cp, bmr};
        app.run();
        return;
    }
#endif

    cp.prettify();
    open(cp, force_picker, hotkey_down);   // open-up hahaha
}

static bool is_config_command(const string& data) {
    return data.empty() || data.starts_with(ArgSplitter);
}

/**
 * Commands that print to a console or rewrite the whole configuration run in the process that was started for them.
 */
static bool is_local_command(const string& data) {
    string command_data;
    string command = get_command(data, command_data);
    return command == "browser" || command == "discover" || command == "toast";
}

#if PLATFORM_WINDOWS

static std::filesystem::file_time_type g_script_write_time{};

/**
 * The resident process lives for a long time: pick up configuration and script edits made by other processes
 * and flush its own in-memory changes (picker settings, rules created from the picker).
 */
static void sync_state() {
    if(g_config.tick(numeric_limits<float>::max())) {
        g_pipeline.load();
    }

    std::error_code ec;
    auto script_path = fss::get_config_file_path(CONFIG_NAME, "scripts.lua");
    auto t = std::filesystem::last_write_time(script_path, ec);
    if(!ec && t != g_script_write_time) {
        if(g_script_write_time != std::filesystem::file_time_type{}) g_script.reload();
        g_script_write_time = t;
    }
}

static void resident_execute(const string& data) {
    sync_state();
    execute(data);
    sync_state();
}

static bool resident_on_busy(const string& data) {
    const bool wants_config = is_config_command(data);
    switch(g_activity) {
        case activity::toast:
            // the toast is purely informational: dismiss it and handle the new click right away
            resident::request_cancel();
            return true;
        case activity::config:
            if(wants_config) {
                resident::bring_own_window_to_front();
                return false;
            }
            break;
        case activity::picker:
            // a settings request waits for the picker; a new link must not
            if(wants_config) return true;
            break;
        case activity::idle:
            return true;
    }

    // the single UI thread is taken: open this link in a separate (cold) process instead of making it wait
    if(!resident::spawn_direct(data)) return true;
    return false;
}

#endif

string get_parent_arg() {
#if PLATFORM_WINDOWS
    return format("{:x}", (DWORD)(win32::window::get_foreground().get_handle()));
#else
    // on Linux we can use parent process ID
    process proc;
    auto parent = proc.get_parent();
    return format("{}", parent.get_pid());
#endif
}

/**
 * @brief Parses incoming arguments and shapes them into universal command format.
 * @param args
 * @return 
 */
string parse_args(const vector<string>& args) {
    string arg;

    for(int i = 0; i < args.size(); i++) {
        const string pt = args[i];
        if(!arg.empty()) arg += ' ';
        arg += pt;
    }

    string parent_arg = get_parent_arg();

    return format("{}{}{}",
      arg,
      ArgSplitter,
      parent_arg);
}

string parse_args(string arg) {
    str::trim(arg, "\"");   // arg can be quoted
    string parent_arg = get_parent_arg();

    return format("{}{}{}",
      arg,
      ArgSplitter,
      parent_arg);
}

#if PLATFORM_WINDOWS

int wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    // ::MessageBox(NULL, pCmdLine, NULL, MB_OK);
    string cmd_line = str::to_str(pCmdLine);

    // started by a busy resident instance: run once, exactly as handed over
    if(cmd_line.starts_with(resident::DirectSwitch)) {
        execute(cmd_line.substr(string{resident::DirectSwitch}.size()));
        return 0;
    }

    if(cmd_line == resident::QuitSwitch) {
        return resident::quit_running() ? 0 : 1;
    }

    const bool tray_only = cmd_line == resident::TraySwitch;
    const string data = tray_only ? string{} : parse_args(cmd_line);

    if(!g_state.run_in_tray) {
        if(!tray_only) execute(data);
        return 0;
    }

    if(!tray_only && is_local_command(data)) {
        execute(data);
        return 0;
    }

    // read the hotkey here, in the process the click started (see HotkeyMarker)
    string forwarded = data;
    if(!tray_only && !is_config_command(data)) {
        forwarded += ArgSplitter + HotkeyMarker + (ui::picker_app::is_hotkey_down() ? "1" : "0");
    }

    if(resident::forward(forwarded)) return 0;

    if(tray_only) {
        // nothing is waiting on us (e.g. started with Windows): get the slow parts out of the way now
        i18n::init(g_state.ui_language);
        grey::app::prewarm();
        ui::prewarm_cjk_fonts();
    }

    resident::host h{
        .execute = [](const string& d) {
            resident_execute(d);
            // turning tray mode off in settings ends the resident process once its UI closes
            if(!g_state.run_in_tray) resident::request_exit();
        },
        .on_busy = resident_on_busy
    };
    return resident::run(h, forwarded);
}

#else

int main(int argc, char* argv[]) {
    vector<string> args;
    for (int i = 1; i < argc; i++) {
        args.push_back(string(argv[i]));
    }
    execute(parse_args(args));
    return 0;
}

#endif