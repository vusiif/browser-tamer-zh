#include "config_app.h"
#include "common/platform.h"
#include <format>
#include "common/process.h"
#include "common/str.h"
#include "common/stl.hpp"
#include "common/fss.h"
#include "../rule_hit_log.h"
#include <filesystem>
#include "../url_opener.h"
#include "../discovery.h"
#include "../pipeline/replacer.h"
#include "globals.h"
#include "../../res.h"
#include "../strings.h"
#include "../i18n.h"
#include "extra_widgets.hpp"
#include "common/datetime.h"
#include "common/os.h"
#include "magic_enum/magic_enum.hpp"
#include <algorithm>

#include "btwidgets.h"
#include "picker_app.h"
#include "common/desktop_shell.h"

#if PLATFORM_WINDOWS
#include "common/win32/user.h"
#include "common/win32/shell.h"
#endif

using namespace std;
using namespace grey;
using namespace grey::common;
namespace w = widgets;

namespace bt::ui {
    config_app::config_app() : app{app::make(string{APP_LONG_NAME} + " " + APP_VERSION, {900, 600})} {
        app->initial_theme_id = g_state.ui_theme;
        app->can_resize = true;
        app->center_on_screen = true;
        app->fonts.load_all();
        auto& opts = app->main_window_opts();
        opts.has_menu_bar = true;
        opts.open_ptr = &is_open;

        app->on_initialised = [this]() {
            app->preload_texture("logo", icon_png, icon_png_len);
            app->preload_texture("incognito", incognito_icon_png, incognito_icon_png_len);
            app->preload_texture("bt_chromium", chromium_icon_png, chromium_icon_png_len);
            app->preload_texture("bt_gecko", gecko64_icon_png, gecko64_icon_png_len);
            btw_on_app_initialised(*app);
            if(g_state.rediscover_on_config_startup) {
                rediscover_browsers();
            }
        };

        check_health();
    }

    config_app::~config_app() {
    }

    void config_app::run() {
        app->run([this]() { return run_frame(); });
    }

    void config_app::refresh_sys_info() {
        si_frame_time += ImGui::GetIO().DeltaTime;
        if(si_frame_time >= 1) {
            // update every second
            si_frame_time = 0;

            // fetch new info and convert to string to avoid conversion in every frame
            si_fps = format("{:.1f}", ImGui::GetIO().Framerate);
            si_scale = format("{:.1f}", w::scale);
            si_dpi = format("{}", grey::common::desktop_shell::get_current_monitor_dpi());
        }
    }

    void config_app::check_health(bool force) {
        health_checked_time_sec += w::frame_delta();

        // check health every 5 seconds
        if(health_checked_time_sec < 5.0f && !force)
            return;

        health_checked_time_sec = 0;
        health_checks = setup::get_checks();

        health_succeeded = health_failed = 0;

        for(auto& sc: health_checks) {
            sc.recheck();

            if(sc.is_ok) {
                health_succeeded += 1;
            } else {
                health_failed += 1;
            }
        }
    }

    bool config_app::run_frame() {
        if(g_state.browsers.empty()) {
            render_no_browsers();
        } else {
            render_menu_bar();
            if(g_state.browsers.empty()) return is_open;    // safety check
            render_browsers();

#if _DEBUG
            if(show_demo) {
                ImGui::ShowDemoWindow(&show_demo);
            }
#endif

            render_status_bar();

            if(show_subs)
                render_subs_window();

            if(show_scripting)
                render_scripting_window();

            if(pv_show)
                render_pipe_visualiser_window();

            if(add_browser_show)
                render_add_browser_window();

            if(g_config.tick(ImGui::GetIO().DeltaTime)) {
                //w::notify_info("state changed");
            }
        }

        check_health();
        w::toast_render_frame();

        // end of frame actions
        if(exec_rediscover) {
            rediscover_browsers();
            exec_rediscover = false;
        }

        return is_open;
    }

    void config_app::render_menu_bar() {
        //w::menu_bar menu{menu_items, [this](const string& id) { handle_menu_click(id); }};

        if(w::menu_bar menu; menu) {
            if(w::menu m_file{tr("File")}; m_file) {
                if(w::mi(tr("Add custom browser"), true, ICON_MD_ADD_CIRCLE)) {
                    add_browser_show = true;
                }

                if(w::mi(tr("Open configuration directory"), true, ICON_MD_FOLDER_OPEN)) {
                    desktop_shell::open(grey::common::fss::get_config_dir(CONFIG_NAME));
                }

                w::sep();
                if(w::mi(tr("Exit"), true, ICON_MD_LOGOUT)) {
                    is_open = false;
                }
            }

            if(w::menu m_tools{tr(strings::MenuTools)}; m_tools) {
#if PLATFORM_WINDOWS
                if(w::mi(tr("Open Windows Defaults"), true, ICON_MD_PSYCHOLOGY)) {
                    win32::shell::open_default_apps(APP_LONG_NAME, true);
                }
#endif
                if(w::mi(tr(strings::PipelineDebugger), true, ICON_MD_DIRECTIONS_RUN)) {
                    pv_show = !pv_show;
                }
                if(w::mi(tr(strings::ScriptEditor), true, ICON_MD_CODE)) {
                    show_scripting = !show_scripting;
                }
                if(w::mi(tr("Re-check health"), true, ICON_MD_MEDICAL_SERVICES)) {
                    check_health(true);
                    w::toast(emphasis::info, tr("Health check finished"));
                }
                w::sep(tr("Discovery"));
                if(w::mi(tr("Rediscover"), true, ICON_MD_REFRESH)) {
                    exec_rediscover = true;
                }
                w::small_checkbox(tr("Discover Black Hole browser"), g_state.discover_black_hole_browser);
                //w::small_checkbox("Discover classic Firefox profiles", g_state.discover_classic_gecko_profiles);
                //w::small_checkbox("Discover Firefox containers", g_state.discover_gecko_containers);
            }

            if(w::menu m{tr("General")}; m) {
                w::small_checkbox(tr("Log to clicks.csv"), g_state.log_rule_hits);
                w::small_checkbox(tr("Rediscover on startup"), g_state.rediscover_on_config_startup);

                if(w::menu m_toast{tr("Toast"), true, ICON_MD_NOTIFICATIONS}; m_toast) {
                    w::small_checkbox(tr("Enabled"), g_state.toast.enabled);
                    w::slider(g_state.toast.visible_seconds, 0, 30, tr("stays visible (seconds)"));
                    w::slider(g_state.toast.border_width, 0, 6, tr("border width"));
                    w::slider(g_state.toast.opacity, 50, 255, tr("window opacity"));
                    w::slider(g_state.toast.anim_duration, 0.1f, 5.0f, tr("animation duration"), 0.1f);
                }

                render_language_menu();

                w::mi_themes([this](const string& theme_id) {
                    app->set_theme(theme_id);
                    g_state.ui_theme = theme_id;
                });
            }

            if(w::menu m{tr("Picker")}; m) {
                w::small_checkbox(tr("Close on focus loss"), g_state.picker.close_on_focus_loss);
                w::small_checkbox(tr("Always on top"), g_state.picker.always_on_top);
                w::small_checkbox(tr("Show on no rule"), g_state.picker.invoke.on_no_rule);

                string sep_title = ICON_MD_KEYBOARD " " + tr("Keyboard");
                if(picker_app::is_hotkey_down(false)) sep_title += " " ICON_MD_CHECK;
                w::sep(sep_title);
                w::small_checkbox(tr("Ctrl + Shift + Left Click"), g_state.picker.invoke.on_key_control_shift);
                w::small_checkbox(tr("Ctrl + Alt    + Left Click"), g_state.picker.invoke.on_key_control_alt);
                w::small_checkbox(tr("Alt  + Shift + Left Click"), g_state.picker.invoke.on_key_alt_shift);
                w::small_checkbox(tr("CAPS LOCKS"), g_state.picker.invoke.on_key_caps_locks);
            }

            if(w::menu m{tr("Transforms")}; m) {
                if(w::small_checkbox(tr("Unwrap Office 365 links"), g_state.transforms.unwrap_o365)) {
                    g_pipeline.load();
                }
                if(w::small_checkbox(tr("Unshorten links"), g_state.transforms.unshorten)) {
                    g_pipeline.load();
                }
                if(w::small_checkbox(tr("ClearURLs"), g_state.transforms.clearurls)) {
                    g_pipeline.load();
                }
                if(w::small_checkbox(tr("Substitute substrings"), g_state.transforms.substitute)) {
                    g_pipeline.load();
                }
                if(g_state.transforms.substitute) {
                    if(w::mi(tr("Substitutions..."), true, ICON_MD_FIND_REPLACE)) {
                        show_subs = !show_subs;
                    }
                }
                if(w::small_checkbox(tr("Scripting"), g_state.transforms.scripting)) {
                    g_pipeline.load();
                }
            }

            if(w::menu m{tr("Help")}; m) {
                if(w::mi(tr("All Releases"), true, ICON_MD_OPEN_IN_NEW)) {
                    url_opener::open(APP_GITHUB_RELEASES_URL);
                }
                if(w::mi(tr("Documentation"), true, ICON_MD_OPEN_IN_NEW)) {
                    url_opener::open(APP_DOCS_URL);
                }

#if _DEBUG
                if(w::mi("ImGui demo", true)) {
                    show_demo = !show_demo;
                }
#endif

                // display sys info
                refresh_sys_info();
                w::sep(ICON_MD_MONITOR " " + tr("system info"));
                {
                    float offset = 100 * w::scale;
                    w::lbl("FPS");
                    w::sl(offset);
                    w::lbl(si_fps);
                    w::lbl(tr("Scale"));
                    w::sl(offset);
                    w::lbl(si_scale);
                    w::lbl("DPI");
                    w::sl(offset);
                    w::lbl(si_dpi);
                }
            }
        }
    }

    void config_app::render_language_menu() {
        if(w::menu m_lang{tr("Language"), true, ICON_MD_TRANSLATE}; m_lang) {
            auto select = [](const string& code) {
                g_state.ui_language = code;
                i18n::init(code);
            };

            if(w::mi(tr("Follow system"), true, g_state.ui_language.empty() ? ICON_MD_CHECK : "")) {
                select("");
            }
            w::sep();
            for(const i18n::language& lang: i18n::languages()) {
                if(w::mi(lang.native_name, true, g_state.ui_language == lang.code ? ICON_MD_CHECK : "")) {
                    select(lang.code);
                }
            }
        }
    }

    void config_app::startup_health_warning() {
        if(health_failed > 0) {
            // "###" keeps the popup id stable when the language changes
            const string title = tr("Health warning") + "###health_warning";

            if(!startup_health_opened) {
                ImGui::OpenPopup(title.c_str());
                startup_health_opened = true;
            }

            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if(ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                string msg = trf("There are {} issues preventing {} from working properly.",
                    health_failed, APP_SHORT_NAME);

                if(w::button(tr("Close"), emphasis::error)) {
                    ImGui::CloseCurrentPopup();
                    startup_health_warned = true;
                }

                ImGui::EndPopup();
            }
        } else {
            startup_health_warned = true;
        }
    }

    void config_app::render_subs_window() {
        w::wnd wsubs{
            tr("Substitutions") + "###substitutions", {
                .open_ptr = &show_subs,
                .size = {600, 300},
                .size_cond = act_condition::once,
                .border = 1
            }
        };

        bool recompute{false};

        if(w::button(ICON_MD_ADD " " + tr("add"), emphasis::primary)) {
            g_state.transforms.substitutions.push_back({});
            g_pipeline.load();
            recompute = true;
        }

        w::sl();
        ww::help_link("#substitutions");

        // testing stuff
        w::sep(tr("Test"));
        if(w::input(url_subs_in, ICON_MD_LOGIN)) recompute = true;
        w::tt(tr("Input a value to test substitutions."));
        if(w::input(url_subs_up.url, ICON_MD_LOGOUT, true, 0, true)) recompute = true;
        w::tt(tr("Substitution result are populated here."));

        w::sep(tr("Substitutions"));
        // scrollable container of subs
        {
            w::div subs_div{"subs_div"};

            for(int i = 0; i < g_state.transforms.substitutions.size(); i++) {
                if(i > 0) w::sep();
                string suffix = "##" + to_string(i);

                auto replacer = g_pipeline.get_replacer_step(i);

                float pad = 100.0f * w::scale + 10 * w::scale;
                float iw = 250.0f * w::scale;

                w::combo("##kind" + suffix,
                         {tr(replacer_kinds[0]), tr(replacer_kinds[1])},
                         (unsigned int&) replacer->kind,
                         100);

                w::sl(pad);
                if(w::input(replacer->find, tr("find") + suffix, true, iw)) recompute = true;

                // "delete" button to the right
                w::sl(pad + iw + 70 * w::scale);
                if(w::button(ICON_MD_DELETE + suffix, emphasis::error)) {
                    g_state.transforms.substitutions.erase(g_state.transforms.substitutions.begin() + i);
                    g_pipeline.load();
                    recompute = true;
                    break;
                }

                w::lbl("");
                w::sl(pad);
                if(w::input(replacer->replace, tr("replace") + suffix, true, iw)) recompute = true;
            }
        }

        if(recompute) {
            // sync state back to config
            g_state.transforms.substitutions.clear();
            for(auto& r: g_pipeline.get_steps()) {
                if(r->type == url_pipeline_step_type::find_replace) {
                    auto rr = std::static_pointer_cast<pipeline::replacer>(r);
                    g_state.transforms.substitutions.emplace_back(string{magic_enum::enum_name(rr->kind)}, rr->find,
                                                                  rr->replace);
                }
            }

            // recompute
            url_subs_up.clear(true);
            url_subs_up.url = url_subs_in;
            g_pipeline.process(url_subs_up);
            recompute = false;
        }
    }

    void config_app::render_scripting_window() {
        w::wnd w{
            tr(strings::ScriptEditor) + "###script_editor", {
                .open_ptr = &show_scripting,
                .size = {800, 600},
                .size_cond = act_condition::once,
                .border = 1
            }
        };

        {
            if(!script_initialised) {
                script_editor.set_text(g_script.get_code());
                script_initialised = true;
            }

            bool do_run = false;
            bool do_save = false;
            string code_append;

            // toolbar
            if(w::div tb_div{"tb_div", {.auto_resize_y = true}}; tb_div) {
                do_save = w::button(ICON_MD_SAVE);
                w::tt(tr("save to disk"));

                w::sl();
                w::lbl("|", {.emp = emphasis::disabled});

                w::sl();
                if(w::button(ICON_MD_RULE)) {
                    code_append = strings::LuaScriptNewRule;
                }
                w::tt(tr("Insert new rule template"));
                w::sl();
                if(w::button(ICON_MD_DIRECTIONS_RUN)) {
                    code_append = strings::LuaScriptNewPipeline;
                }
                w::tt(tr("Insert new pipeline template"));

                w::sl();
                w::lbl("|", {.emp = emphasis::disabled});

                w::sl();
                w::combo("##fn", g_script.bt_function_names, script_fn_selected, 250);
                w::tt(tr("function to execute"));

                w::sl();
                do_run = w::button(ICON_MD_PLAY_ARROW, emphasis::primary);
                w::tt(tr("run function"));

                w::sl();
                ww::help_link("#scripting");
            }

            if(!code_append.empty()) {
                string new_code = script_editor.get_text() + "\n\n" + code_append;
                script_editor.set_text(new_code);
                do_save = true;
            }

            string func_name = g_script.bt_function_names.empty() ? "" : g_script.bt_function_names[script_fn_selected];
            bool is_ppl = func_name.starts_with(LuaPipelinePrefix);

            if(do_run || do_save) {
                g_script.set_code(script_editor.get_text());
                g_pipeline.load();

                if(do_run && g_script.get_error().empty()) {
                    // test it
                    script_show_terminal = true;
                    script_terminal.clear();

                    click_payload up;
                    up.url = g_state.pipevis.url;
                    up.window_title = g_state.pipevis.window_title;
                    up.process_name = g_state.pipevis.process_name;

                    if(is_ppl) {
                        script_terminal = trf(
                            "invoking pipeline for:\n  url: [{}]\n  window title: [{}]\n  process name: [{}]\n\n",
                            up.url,
                            up.window_title,
                            up.process_name);
                        string out_url = g_script.call_ppl(up, func_name);
                        script_terminal += g_script.print_buffer;
                        script_terminal += trf("URL transformed to:\n  [{}]\n", out_url);
                    } else {
                        script_terminal = trf(
                            "invoking rule match for:\n  url: [{}]\n  window title: [{}]\n  process name: [{}]\n\n",
                            up.url,
                            up.window_title,
                            up.process_name);
                        g_pipeline.process(up);
                        if(up.url != g_state.pipevis.url) {
                            script_terminal += trf("pipeline changed URL to [{}]\n", up.url);
                        }

                        g_script.print_buffer.clear();
                        bool matched = g_script.call_rule(up, func_name);
                        script_terminal += g_script.print_buffer;

                        script_terminal += trf("rule matched: {}\n", matched ? tr("yes") : tr("no"));
                    }
                }
            }

            // test input
            if(!func_name.empty() && w::accordion(tr("Test data") + "###test_data")) {
                w::input(g_state.pipevis.url, ICON_MD_LINK " URL", true);
                if(!is_ppl) {
                    w::input(g_state.pipevis.window_title, ICON_MD_WINDOW " " + tr("window"), true);
                    w::input(g_state.pipevis.process_name, ICON_MD_MEMORY " " + tr("process"), true);
                }
            }

            if(!g_script.get_error().empty()) {
                w::div err_div{"err_div", {.auto_resize_y = true}};
                w::lbl(g_script.get_error(), {.emp = emphasis::error});
            }

            script_editor.render();
        }

        if(script_show_terminal) {
            w::wnd wt{
                tr("Terminal") + "###terminal", {
                    .open_ptr = &script_show_terminal,
                    .size = {800, 400},
                    .size_cond = act_condition::once
                }
            };

            // toolbar
            if(w::div tb_div{"tb_div", {.auto_resize_y = true}}; tb_div) {
                w::tt(tr("clear"));
                w::sl();
                w::icon_checkbox(ICON_MD_ARROW_DOWNWARD, script_terminal_autoscroll);
                w::tt(tr("auto-scroll"));
            }

            // terminal itself
            static w::code_editor term{w::code_editor::language::none, false, false};
            term.set_text(script_terminal);
            term.render();
        }
    }

    void config_app::render_pipe_visualiser_window() {
        w::wnd w{
            tr(strings::PipelineDebugger) + "###pipeline_debugger", {
                .open_ptr = &pv_show,
                .size = {800, 500},
                .size_cond = act_condition::once,
                .border = 1
            }
        };

        bool i0 = w::input(g_state.pipevis.url, ICON_MD_LINK " URL");
        bool i1 = w::input(g_state.pipevis.window_title, ICON_MD_WINDOW " " + tr("window"), true, 300.0f * w::scale);
        w::sl();
        bool i2 = w::input(g_state.pipevis.process_name, ICON_MD_MEMORY " " + tr("process"), true, 150.0f * w::scale);

        if(w::button(ICON_MD_CLEAR_ALL " " + tr("clear"), emphasis::error)) {
            g_state.pipevis.url = g_state.pipevis.window_title = g_state.pipevis.process_name = "";
        }

        w::sl();
        bool refresh = w::button(tr("refresh")) || pv_pipeline_steps.size() != g_pipeline.get_steps().size();
        w::tt(tr("Refresh calculations"));

        w::sl();
        w::checkbox(tr("matching only"), pv_only_matching);

        if(pv_pipeline_steps.empty() || refresh || i0 || i1 || i2) {
            pv_cp.clear();
            pv_cp.url = g_state.pipevis.url;
            pv_cp.window_title = g_state.pipevis.window_title;
            pv_cp.process_name = g_state.pipevis.process_name;
            pv_pipeline_steps = g_pipeline.process_debug(pv_cp);
        }

        // do it continuously
        recalculate_test_url_matches(pv_cp);

        if(w::table pv{"pv", {tr("Key"), tr("Value")}, .0f, .0f, true}; pv) {
            // input row
            pv.begin_row();
            w::lbl("URL");
            pv.next_column();
            w::lbl(g_state.pipevis.url);

            // pipeline steps
            if(!pv_pipeline_steps.empty()) {
                pv.begin_row();
                {
                    w::tree_node node_pipeline{tr("Pipeline") + "###pipeline", true, false, true};
                    pv.next_column();
                    w::lbl(" ");
                    if(node_pipeline) {
                        int i = 0;
                        for(auto& s: pv_pipeline_steps) {
                            pv.begin_row();
                            w::id_frame idf{i++};
                            string text = url_pipeline_step::to_string(s.step->type);
                            {
                                w::tree_node step_node{text, true, true, true};
                                pv.next_column();
                                if(s.before.url == s.after.url) {
                                    w::lbl(ICON_MD_BRIGHTNESS_1);
                                    w::tt(tr("no change"));
                                } else {
                                    w::lbl(ICON_MD_ADJUST, {.emp = emphasis::primary});
                                    w::tt(tr("URL was modified"));
                                }
                                w::sl();
                                w::lbl(s.after.url);
                            } // step_node
                        }
                    }
                } // node_pipeline
            }

            // browsers
            pv.begin_row();
            if(w::tree_node node_browsers{tr("Browsers") + "###browsers", true, false, true}; node_browsers) {
                int fid = 0;
                for(auto& b: g_state.browsers) {
                    w::id_frame idf{fid++};
                    if(pv_only_matching && !b.ui_test_url_matches) continue;

                    pv.begin_row();
                    auto emp = b.ui_test_url_matches ? emphasis::primary : emphasis::none;
                    if(w::tree_node node_browser{b.name, true, false, true, emp}; node_browser) {
                        for(auto& p: b.profiles) {
                            w::id_frame idf{fid++};
                            if(pv_only_matching && !p.ui_test_url_matches) continue;

                            pv.begin_row();
                            auto emp = p.ui_test_url_matches ? emphasis::primary : emphasis::none;
                            w::tree_node node_profile(p.name, true, false, true, emp);
                            pv.next_column();
                            if(p.rules.empty()) {
                                w::lbl(tr("no rules"), {.emp = emphasis::disabled});
                            } else {
                                w::lbl(trf("{} rule(s)", p.rules.size()), {.emp = emphasis::disabled});
                            }

                            // rules
                            if(node_profile) {
                                int idx = 0;
                                for(auto r: p.rules) {
                                    if(pv_only_matching && !r.ui_test_url_matches) continue;
                                    pv.begin_row();
                                    auto emp = r.ui_test_url_matches ? emphasis::primary : emphasis::none;
                                    w::tree_node node_rule{
                                        format("{}##{}", r.get_type_string(), idx++),
                                        false, true, true, emp
                                    };
                                    pv.next_column();
                                    w::lbl(r.to_string(false), {.emp = emp});
                                }
                            }
                        }
                    }
                }
            }
        }


        //    for(auto b : g_config.browsers) {
        //        if(pv_only_matching && !b->ui_test_url_matches) continue;
        //        ImGui::TableNextRow();
        //        ImGui::TableSetColumnIndex(0);
        //        {
        //        w::tree_node b_node(b->name, true, false, true, b->ui_test_url_matches ? emphasis::primary : emphasis::none);
        //        ImGui::TableSetColumnIndex(1);
        //        ImGui::TextDisabled(" ");
        //        if(b_node) {
        //            for(auto i : b->profiles) {
        //                if(pv_only_matching && !i->ui_test_url_matches) continue;
        //                ImGui::TableNextRow();
        //                ImGui::TableSetColumnIndex(0);
        //                auto emp = i->ui_test_url_matches ? emphasis::primary : emphasis::none;
        //                bool i_open = w::tree_node(i->name, true, false, true, emp);
        //                ImGui::TableSetColumnIndex(1);

        //                if(i->rules.empty()) {
        //                    w::lbl("no rules", 0, false);
        //                } else {
        //                    w::lbl(format("{} rule(s)", i->rules.size()), 0, false);
        //                }

        //                if(i_open) {

        //                    // rules
        //                    int idx = 0;
        //                    for(auto r : i->rules) {
        //                        if(pv_only_matching && !r->ui_test_url_matches) continue;
        //                        ImGui::TableNextRow();
        //                        ImGui::TableSetColumnIndex(0);
        //                        auto emp = r->ui_test_url_matches ? emphasis::primary : emphasis::none;
        //                        w::tree_node(
        //                            format("{}##{}", r->get_type_string(), idx++),
        //                            false, true, true, emp);
        //                        ImGui::TableSetColumnIndex(1);
        //                        w::lbl(r->to_string(false), emp);
        //                    }
        //                }
        //            }

        //            ImGui::TreePop();
        //        }
        //        }
        //    }
    }

    void config_app::render_status_bar() {
        w::status_bar sb;
        w::lbl("");

        bool recheck{false};
        int i = 0;
        for(system_check& hc: health_checks) {
            w::sl();
            w::id_frame idf{i++};

            if(hc.is_ok) {
                w::lbl(ICON_MD_CHECK, {.emp = emphasis::primary});
                w::tt(tr(hc.name));
            } else {
                w::spinner(spinner_type::rotated_heart, {.emp = emphasis::error, .radius = 9, .thickness = 2.0f, .speed = 8.0f});
                if(w::is_hovered()) {
                    w::mouse_cursor(w::mouse_cursor_type::hand);
                    if(w::rich_tt rt; rt) {
                        w::lbl(tr(hc.name), {.emp = emphasis::primary, .font_w = font_weight::bold});
                        w::lbl(tr(hc.description), {.emp = emphasis::secondary});
                        w::sep();
                        w::lbl(hc.error_message, {.emp = emphasis::error});
                        w::lbl(trf("click to {}", tr(hc.fix_description)));
                    }
                }
                if(w::is_leftclicked()) {
                    hc.fix();
                    recheck = true;
                }
            }
        }
        if(recheck) {
            check_health();
        }

        size_t ipc{0};
        size_t irc{0};
        for(const auto& b: g_state.browsers) {
            ipc += b.profiles.size();
            for(const auto& profile: b.profiles) {
                irc += profile.rules.size();
            }
        }

        w::slh();
        w::lbl(format("{} {}", ICON_MD_WEB, g_state.browsers.size()), {.emp = emphasis::disabled});
        w::tt(tr("Browser count"));

        w::sl();
        w::lbl(format("{} {}", ICON_MD_PERSON, ipc), {.emp = emphasis::disabled});
        w::tt(tr("Profile count"));

        w::sl();
        w::lbl(format("{} {}", ICON_MD_RULE, irc), {.emp = emphasis::disabled});
        w::tt(tr("Configured rule count"));

        w::slh();

        static bool donate_hovered{false};
        {
            w::group g;
            w::lbl(ICON_MD_COFFEE, {.emp = emphasis::disabled});
            if(donate_hovered) {
                w::sl();
                w::hyperlink(tr("donate"));
            }
        }
        donate_hovered = w::is_hovered();
        if(donate_hovered) {
            w::mouse_cursor(widgets::mouse_cursor_type::hand);
        }
        if(w::is_leftclicked()) {
            url_opener::open(APP_DONATE_URL);
        }

        if(!g_state.browsers.empty()) {
            w::slh();
            optional<profile_selection> sel = browser::get_default(g_state.browsers);
            if(sel) {
                w::lbl(ICON_MD_LAPTOP, {.emp = emphasis::disabled});
                w::sl();
                w::lbl(sel->b().name, {.emp = emphasis::disabled});
                w::tt(tr("Default browser"));

                if(sel->b().engine != browser_engine::generic) {
                    w::slh();
                    w::lbl(ICON_MD_TAB, {.emp = emphasis::disabled});
                    w::sl();
                    w::lbl(sel->p().name, {.emp = emphasis::disabled});
                    w::tt(tr("Default profile"));
                }
            }
        }
    }

    void config_app::render_no_browsers() {
        for(int i = 0; i < 5; i++)
            w::lbl("");

        w::lbl(tr("Currently there are no browsers registered."),
               {.emp = emphasis::primary, .center_x = true, .font_size = 5});
        w::spc();
        w::lbl(tr("Press the button below to scan your system for installed browsers."),
               {.emp = emphasis::none, .center_x = true});

        string label = ICON_MD_REFRESH + tr("discover system browsers");
        float button_width = 300 * w::scale;
        auto cur = w::cur_get();
        cur.x += w::avail_x() / 2 - button_width / 2;
        w::cur_set(cur);

        w::texter fs{5};
        if(w::button(label, emphasis::error, true, false, "", button_width)) {
            exec_rediscover = true;
        }
    }

    void config_app::render_browsers() {
        if(w::div l_div{
            "l_div", {
                .size = sz{250, 0} * w::scale,
                .user_resizeable_horizontal = true,
            }
        }; l_div) {
            if(w::div l_div_tb{
                "l_div_tb", {
                    .auto_resize_y = true,
                    .style_like_widget = true,
                }
            }; l_div_tb) {
                if(w::button(ICON_MD_ADD_CIRCLE " " + tr("Add"), emphasis::primary)) {
                    add_browser_show = true;
                }
                w::tt(tr("Add custom browser definition"));
                w::sl();
                if(w::button(ICON_MD_REFRESH)) {
                    exec_rediscover = true;
                }
                w::tt(tr("Rediscover system browsers"));
                w::sl();
                w::icon_checkbox(ICON_MD_VISIBILITY, g_state.show_hidden_browsers);
                w::tt(tr("Show hidden browsers"));
            }

            if(w::div l_div_list{
                "l_div_list", {
                    .has_background = false
                }
            }; l_div_list) {
                for(int i = 0; i < g_state.browsers.size(); i++) {
                    auto& br = g_state.browsers[i];
                    if(!g_state.show_hidden_browsers && br.is_hidden) {
                        continue;
                    }

                    render_card(br, i == selected_browser_idx);

                    // we can now react on click
                    if(w::is_leftclicked()) {
                        selected_browser_idx = i;
                    }
                }
            }
        }
        w::sl();
        {
            if(w::div r_div{"r_div"}; r_div) {
                if(selected_browser_idx < g_state.browsers.size()) {
                    if(w::div r_div_tb{
                        "r_div_tb", {
                            .auto_resize_y = true,
                            .style_like_widget = true,
                        }
                    }; r_div_tb) {
                        render_browser_toolbar(g_state.browsers[selected_browser_idx]);
                    }

                    if(!g_state.browsers.empty() && selected_browser_idx < g_state.browsers.size()) {
                        if(w::div r_div_detail{"r_div_detail"}; r_div_detail) {
                            render_detail(g_state.browsers[selected_browser_idx]);
                        }
                    }
                }
            }
        }
    }

    browser_profile config_app::get_selected_browser_instance() const {
        auto& browser = g_state.browsers[selected_browser_idx];
        if(browser.engine != browser_engine::generic) {
            return browser.profiles[selected_profile_idx];
        } else {
            return browser.profiles[0];
        }
    }

    void config_app::render_card(browser& b, bool is_selected) const {
        w::draw_splitter dsp;

        {
            w::group g{true};

            float padding = 10 * w::scale;
            float icon_size = 32 * w::scale;
            float left_pad = icon_size + padding * 2;

            // render icon and come back to starting position
            auto pos = w::cur_get();
            w::cur_set({pos.x + padding, pos.y + padding});

            string path = b.get_best_icon_path();
            if(app->preload_texture(path, fss::get_full_path(path))) {
                w::image(*app, path, sz::square(icon_size));
            } else {
                w::image(*app, "logo", sz::square(icon_size));
            }

            // elements
            w::cur_set({pos.x + left_pad, pos.y + padding});
            w::lbl(b.name);

            //w::spc();
            auto pos2 = w::cur_get();
            w::cur_set({pos2.x + left_pad, pos2.y});
            //w::move_pos(left_pad, 0);

            // management level
            switch(b.management) {
                case management_extent::full:
                    w::lbl(ICON_MD_AUTO_FIX_HIGH);
                    break;
                case management_extent::profiles:
                    w::lbl(ICON_MD_AUTO_FIX_NORMAL);
                    break;
                default:
                    w::lbl(ICON_MD_AUTO_FIX_OFF);
                    break;
            }

            if(b.engine != browser_engine::generic) {
                w::sl();
                w::lbl(format("{} {}", ICON_MD_FACE, b.profiles.size()), {.emp = emphasis::disabled});

                switch(b.engine) {
                    case browser_engine::chromium:
                        w::sl();
                        w::icon_image(*app, "bt_chromium");
                        break;
                    case browser_engine::gecko:
                        w::sl();
                        w::icon_image(*app, "bt_gecko");
                        break;
                }
            }

            if(b.is_hidden) {
                w::sl();
                w::lbl(ICON_MD_VISIBILITY_OFF, {.emp = emphasis::disabled});
                w::tt(tr("Hidden"));
            }

            if(b.supports_frameless_windows()) {
                w::sl();
                w::lbl(ICON_MD_TAB_UNSELECTED, {.emp = emphasis::disabled});
                w::tt(tr("Supports frameless windows"));
            }

            if(b.is_default()) {
                w::sl();
                w::lbl(ICON_MD_FAVORITE, {.emp = emphasis::primary});
                w::tt(tr("Default browser"));
            }


            w::spc();
            w::cur_set(pos);
        } // group end

        const auto& style = ImGui::GetStyle();
        const rect item_rect = w::item_rect_get();
        float ax = w::avail_x();
        const rect bounding_rect = rect{item_rect.lt(), point{item_rect.x_min + ax, item_rect.y_max}};

        if(w::is_hovered()) {
            w::draw_rect(bounding_rect, rgb_colour{ImGuiCol_Border}, style.WindowBorderSize, style.FrameRounding);
        }

        dsp.swap(); // draw in background

        if(is_selected) {
            w::draw_rect_filled(bounding_rect, rgb_colour{ImGuiCol_WindowBg}, style.FrameRounding);
        }
    }

    void config_app::render_browser_toolbar(browser& b) {
        // hide/show button rendered as a button due to wrong looks if rendered as a checkbox
        w::icon_checkbox(ICON_MD_VISIBILITY, b.is_hidden, true, tr("Show in browser list and picker"));

        bool can_move_up = b != *g_state.browsers.begin();
        bool can_move_down = b != *g_state.browsers.rbegin();

        w::sl();
        if(w::button(ICON_MD_ARROW_UPWARD, emphasis::none, can_move_up)) {
            // move up one position inside g_config.browsers
            size_t idx = browser::index_of(g_state.browsers, b);
            if(idx != string::npos && idx > 0) {
                std::swap(g_state.browsers[idx], g_state.browsers[idx - 1]);
                selected_browser_idx = idx - 1;
                return;
            }
        }
        w::tt(tr(strings::BrowserMoveUpTooltip));

        w::sl();
        if(w::button(ICON_MD_ARROW_DOWNWARD, emphasis::none, can_move_down)) {
            // move down one position inside g_state.browsers
            size_t idx = browser::index_of(g_state.browsers, b);
            if(idx != string::npos && idx < g_state.browsers.size() - 1) {
                std::swap(g_state.browsers[idx], g_state.browsers[idx + 1]);
                selected_browser_idx = idx + 1;
            }
        }
        w::tt(tr(strings::BrowserMoveDownTooltip));

        if(b.engine == browser_engine::gecko) {
            w::sl();
            if(w::button(ICON_MD_SUPERVISOR_ACCOUNT)) {
                desktop_shell::open(b.open_cmd, "-P");
            }
            w::tt(tr("open Firefox Profile Manager (-P flag)"));

            w::sl();
            if(w::button(ICON_MD_SUPERVISED_USER_CIRCLE)) {
                desktop_shell::open(b.open_cmd, "about:profiles");
            }
            w::tt(tr("open Firefox Profile Manager in Firefox itself"));

            w::slh(); // ---

            w::icon_checkbox(ICON_MD_ACCOUNT_CIRCLE, b.discover_classic_gecko_profiles,
                false, tr("discovery legacy profiles"));

            w::sl();
            w::icon_checkbox(ICON_MD_INVENTORY_2, b.discover_containers,
                false, tr("discover containers"));
        } else if(b.engine == browser_engine::chromium) {
            /*
            w::sl();
            if(w::button(ICON_MD_FLAG, "open chromium flags page")) {
                desktop_shell::open(b.open_cmd, "\"chrome://flags\"");
            }
        */
        }

        // all the browsers can be deleted except the last one
        w::slh();

        if(w::button(ICON_MD_DELETE, emphasis::error)) {
            const size_t idx = browser::index_of(g_state.browsers, b);

            // erase and save
            std::erase_if(g_state.browsers, [b](const auto& i) { return i == b; });
            selected_browser_idx = std::clamp<size_t>(idx, 0, g_state.browsers.size() - 1);
        }
        w::tt(tr("Completely deletes this browser, no questions asked"));
    }

    void config_app::render_detail(browser& b) {
        // --- browser properties

        if(b.management != management_extent::full) {
            w::input(b.name, tr("name"));
        }

        if (!b.is_internal()) {
            w::input(b.open_cmd, tr("cmd"), true, 0, b.management == management_extent::full);
            w::tt(tr("Browser executable or command"));
            w::sl();
            if(w::button(ICON_MD_LAUNCH "##open_install", emphasis::none, true, true, tr("open"))) {
                const std::filesystem::path p{b.open_cmd};
                const string path = p.parent_path().string();
                desktop_shell::open(path);
            }

            if(!b.data_path.empty()) {
                w::input(b.data_path, tr("data"), true, 0, b.management == management_extent::full);
                w::tt(tr("Location of the data directory"));

                w::sl();
                if(w::button(ICON_MD_LAUNCH "##open_data", emphasis::none, true, true, tr("open"))) {
                    desktop_shell::open(b.data_path);
                }
            }
        }

        // --- browser properties end

        // --- profiles start

        w::spc();

        if(b.management == bt::management_extent::none) {
            // single profile, the first one, no tab bar
            selected_profile_idx = 0;
            if(!b.profiles.empty()) {
                render_profile(b, *b.profiles.begin(), 0);
            }
        } else {
            w::tab_bar tabs{"tabs", true, true};

            int idx{0};
            for(browser_profile& bi: b.profiles) {
                w::id_frame idf{idx};
                if(!g_state.show_hidden_browsers && bi.is_hidden) {
                    idx++;
                    continue;
                }

                string tab_icon;
                if(bi.is_incognito) {
                    tab_icon = format("{} ", ICON_MD_SECURITY);
                }
                string tab_title = format(" {}{} ", tab_icon, bi.name);

                {
                    auto t = tabs.next_tab(tab_title, bi.is_default,
                                           set_selected_profile_idx == -1
                                               ? false
                                               : idx == set_selected_profile_idx);
                    if(t) {
                        selected_profile_idx = idx;
                        render_profile(b, bi, idx);
                    }
                }
                idx++;
            }
        }

        set_selected_profile_idx = -1;
    }

    void config_app::render_profile(browser& b, browser_profile& bi, int idx) {
        w::spc();

        // mini toolbar

        if(!bi.is_default) {
            if(w::button(ICON_MD_FAVORITE, emphasis::primary)) {
                browser::set_default(g_state.browsers, bi);
            }
        }
        w::tt(tr("Set as default profile"));

        // hide/show button rendered as a button due to wrong looks if rendered as a checkbox
        w::sl();
        w::icon_checkbox(ICON_MD_VISIBILITY, bi.is_hidden, true, tr("Show in profile list and picker"));

        bool can_move_left = idx != 0;
        bool can_move_right = idx != b.profiles.size() - 1;

        w::sl();
        if(w::button(ICON_MD_WEST, emphasis::none, can_move_left)) {
            // move up one position inside b->profiles
            std::swap(b.profiles[idx], b.profiles[idx - 1]);
            set_selected_profile_idx = idx - 1;
            return;
        }
        w::tt(tr(strings::ProfileMoveUpTooltip));

        w::sl();
        if(w::button(ICON_MD_EAST, emphasis::none, can_move_right)) {
            // move down one position inside b->profiles
            std::swap(b.profiles[idx], b.profiles[idx + 1]);
            set_selected_profile_idx = idx + 1;
            return;
        }
        w::tt(tr(strings::ProfileMoveDownTooltip));

        w::sl();
        if(w::button(ICON_MD_LAUNCH)) {
            url_opener::open(profile_selection(b, idx), APP_TEST_URL);
        }
        w::tt(tr("test by opening a link"));

        if(b.supports_frameless_windows()) {
            w::sl();
            if(w::button(ICON_MD_TAB_UNSELECTED)) {
                click_payload up{APP_TEST_URL};
                up.app_mode = true;
                url_opener::open(profile_selection(b, idx), up);
            }
            w::tt(tr(strings::ProfileTestLink));
        }

        w::sl();
        w::icon_checkbox(ICON_MD_PALETTE, bi.use_user_color, false, tr("set custom highlight color"));

        if(bi.use_user_color) {
            w::sl();
            w::colour("##bi_user_color", bi.user_color);
            w::tt(tr("highlight color"));
        }

        if(!b.open_cmd.empty()) {
            if(b.engine == bt::browser_engine::gecko) {
                if(b.discover_containers) {
                    w::sl();
                    ww::help_link("#mozilla-firefox");
                }
            }
        }

        // end of mini toolbar

        if (!b.is_internal()) {
            render_icon(b, bi);
            w::sl();
            {
                w::group g;

                if(b.management == bt::management_extent::none) {
                    w::input(bi.launch_arg, tr("arg"));
                    w::tt(tr("Arguments to pass"));

                    w::checkbox(tr("hide user interface"), bi.no_window);
                } else {
                    w::input(bi.launch_arg, tr("arg"), true, 0, true);
                    w::tt(tr("Discovered arguments (read-only)"));

                    w::input(bi.user_arg, tr("extra arg"));
                    w::tt(tr("Any extra arguments to pass.\nIf you break it, you fix it ;)"));
                }
            }
        }

        w::spc();
        render_rules(b, bi);
    }

    void config_app::render_add_browser_window() {
        w::wnd w{
            tr("Add browser") + "###add_browser", {
                .size = {400, 0},
                .size_cond = act_condition::once,
                .resizeable = false
            }
        };
        browser& b = new_browser;

        w::input(b.name, tr("name"));
        w::tt(tr("display name to show in the browser list"));

        w::enum_combo<browser_engine>(tr("engine"), b.engine);
        b.set_management(b.engine);

        w::input(b.open_cmd, tr("path"));
        w::tt(tr("executable path"));

        if(desktop_shell::file_open_dialog_supported()) {
            w::sl();
            if(w::button(ICON_MD_MORE_HORIZ "##pick_exe", emphasis::none, true, true, tr("pick executable"))) {
                string exe_path_selected = desktop_shell::file_open_dialog(tr("Executable"), "*.exe");
                if(!exe_path_selected.empty()) {
                    b.open_cmd = exe_path_selected;
#if PLATFORM_WINDOWS
                    wstring w_product_name = win32::user::get_file_version_info_string(b.open_cmd, "ProductName");
                    if(!w_product_name.empty()) b.name = str::to_str(w_product_name);
#endif
                }
            }
        }

        bool needs_data_path = (b.engine == browser_engine::chromium || b.engine == browser_engine::gecko);

        w::input(b.data_path, tr("data path"), needs_data_path);
        w::tt(tr("path to data folder"));
        if(desktop_shell::directory_open_dialog_supported()) {
            w::sl();
            if(w::button(ICON_MD_MORE_HORIZ "##pick_dir", emphasis::none, needs_data_path, true, tr("pick directory"))) {
                string path = desktop_shell::directory_open_dialog();
                if(!path.empty()) b.data_path = path;
            }
        }

        if(w::button(ICON_MD_ADD_CIRCLE " " + tr("add"), emphasis::primary, true, false, "", 100 * w::scale)) {
            b.management = b.engine == browser_engine::generic ? management_extent::none : management_extent::profiles;

            // validation
            b.ui_validation_error.clear();
            if(b.open_cmd.empty()) {
                b.ui_validation_error += tr("- path is required\n");
            } else if(!std::filesystem::exists(b.open_cmd)) {
                b.ui_validation_error += tr("- path does not exist\n");
            }
            if(b.name.empty()) {
                b.ui_validation_error += tr("- name is required\n");
            }
            if(needs_data_path) {
                if(b.data_path.empty()) {
                    b.ui_validation_error += tr("- data path is required\n");
                } else {
                    b.profiles.clear();
                    discovery::discover_managed_profiles(b);
                    if(b.profiles.size() < 2) {
                        // there needs to be at least two valid profiles (incognito + at least the default)
                        b.ui_validation_error += tr("- invalid data path\n");
                    }
                }
            }

            // final addition
            if(b.ui_validation_error.empty()) {
                b.profiles.clear();
                if(b.management == management_extent::none) {
                    discovery::discover_other_profiles(b);
                } else {
                    discovery::discover_managed_profiles(b);
                }
                g_state.browsers.push_back(b);
                selected_browser_idx = g_state.browsers.size() - 1;

                // reset browser before next operation
                b = browser{"", ""};
                add_browser_show = false;
            }
        }

        if(!b.ui_validation_error.empty()) {
            w::sl();
            w::lbl(ICON_MD_ERROR, {.emp = emphasis::error});
            w::tt(b.ui_validation_error);
        }
    }

    void config_app::render_icon(browser& b, browser_profile& p) {
        {
            w::group g;
            btw_icon(*app, b, p, icon_overlay_mode::profile_only, 0, 0, 40 * w::scale);
        }
        if(w::is_hovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            w::tt(tr("Click to override the built-in icon\nRight-click to reset to default"));
        }

        if(w::is_leftclicked()) {
            string icon_path = desktop_shell::file_open_dialog(tr("Icon"), "*.png;*.ico");
            if(!icon_path.empty()) {
                p.user_icon_path = icon_path;
                app->preload_texture(icon_path, icon_path);
            }
        }

        if(w::is_rightclicked()) {
            p.user_icon_path.clear();
            w::toast(emphasis::info, tr("Icon reset to default"));
        }
    }

    void config_app::render_rules(browser& b, browser_profile& bi) {
        w::sep(tr("Rules"));

        if(w::button(ICON_MD_ADD " " + tr("add"), emphasis::primary)) {
            // placeholder rule value (user data), intentionally not translated
            bi.add_rule(format("rule {}", bi.rules.size()));
        }

        w::sl();
        if(w::button(ICON_MD_DELETE " " + tr("clear all"), emphasis::error)) {
            bi.rules.clear();
        }

        w::sl();
        ww::help_link("#rules");

        // scrollable area with list of rules
        {
            if(w::div rules_div{"rules_div"}; rules_div) {
                for(int i = 0; i < bi.rules.size(); i++) {
                    auto& rule = bi.rules[i];
                    w::id_frame idg{i};
                    btw_rule(b, bi, rule, i);
                }
            }
        }
    }

    void config_app::rediscover_browsers() const {
        vector<browser> fresh_browsers = discovery::discover_all_browsers();
        // rediscover profiles in partially managed browsers
        for(browser& b: g_state.browsers) {
            if(b.management != management_extent::profiles) continue;
            browser nb = b; // create a copy
            nb.profiles.clear();
            discovery::discover_managed_profiles(nb);
            fresh_browsers.push_back(nb);
        }

        fresh_browsers = browser::merge(fresh_browsers, g_state.browsers);
        g_state.browsers = fresh_browsers;
        btw_on_app_initialised(*app);

        w::toast(emphasis::info, trf("Discovered {} browser(s)", g_state.browsers.size()));
    }

    void config_app::recalculate_test_url_matches(const click_payload& cp) {
        for(auto& b: g_state.browsers) {
            b.ui_test_url_matches = false;
            for(auto& bi: b.profiles) {
                bi.ui_test_url_matches = false;

                for(auto& r: bi.rules) {
                    r.ui_test_url_matches = false;
                    if(r.is_match(cp, g_script)) {
                        r.ui_test_url_matches = true;
                        bi.ui_test_url_matches = true;
                        b.ui_test_url_matches = true;
                    }
                }
            }
        }
    }
}
