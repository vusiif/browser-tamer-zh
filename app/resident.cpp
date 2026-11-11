#include "resident.h"
#include "common/platform.h"

#if PLATFORM_WINDOWS
#include <deque>
#include <vector>
#include "globals.h"
#include "common/str.h"
#include "i18n.h"
#include <windows.h>
#include <shellapi.h>
#endif

using namespace std;

namespace bt::resident {

#if PLATFORM_WINDOWS
    namespace {
        // names include APP_LONG_NAME so the Dev build never talks to an installed release build
        const wstring WindowClass = grey::common::str::to_wstr(APP_LONG_NAME " Resident");
        const wstring MutexName = grey::common::str::to_wstr("Local\\" APP_LONG_NAME " Resident");
        const wstring RunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
        const wstring RunValue = grey::common::str::to_wstr(APP_LONG_NAME);

        constexpr ULONG_PTR CopyDataMagic = 0x42544D31;  // "BTM1"
        constexpr UINT WmTray = WM_APP + 1;
        constexpr UINT WmDrain = WM_APP + 2;
        constexpr UINT TrayIconId = 1;

        enum menu_cmd : UINT {
            cmd_open_settings = 1,
            cmd_autostart,
            cmd_exit
        };

        const host* g_host{nullptr};
        HWND g_hwnd{nullptr};
        HANDLE g_mutex{nullptr};
        UINT g_taskbar_created{0};
        deque<string> g_queue;
        bool g_busy{false};
        bool g_exit{false};
        bool g_cancel{false};

        wstring exe_path() {
            wstring path(MAX_PATH, L'\0');
            for(;;) {
                DWORD n = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
                if(n < path.size()) {
                    path.resize(n);
                    return path;
                }
                path.resize(path.size() * 2);
            }
        }

        HWND find_resident() {
            return ::FindWindowW(WindowClass.c_str(), nullptr);
        }

        bool send(HWND target, const string& data) {
            DWORD pid{0};
            ::GetWindowThreadProcessId(target, &pid);
            // we were started by the foreground app (browser etc.), so we may pass the foreground right on;
            // without it the picker would open behind the window the link was clicked in
            ::AllowSetForegroundWindow(pid);

            COPYDATASTRUCT cds{
                .dwData = CopyDataMagic,
                .cbData = static_cast<DWORD>(data.size()),
                .lpData = const_cast<char*>(data.data())
            };
            DWORD_PTR result{0};
            LRESULT ok = ::SendMessageTimeoutW(target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds),
                                               SMTO_ABORTIFHUNG, 5000, &result);
            return ok != 0 && result == TRUE;
        }

        void add_tray_icon() {
            NOTIFYICONDATAW nid{.cbSize = sizeof(nid)};
            nid.hWnd = g_hwnd;
            nid.uID = TrayIconId;
            nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
            nid.uCallbackMessage = WmTray;
            nid.hIcon = static_cast<HICON>(::LoadImageW(::GetModuleHandleW(nullptr), L"IDI_ICON1", IMAGE_ICON,
                                                        ::GetSystemMetrics(SM_CXSMICON),
                                                        ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
            wstring tip = grey::common::str::to_wstr(APP_LONG_NAME);
            wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
            ::Shell_NotifyIconW(NIM_ADD, &nid);

            nid.uVersion = NOTIFYICON_VERSION_4;
            ::Shell_NotifyIconW(NIM_SETVERSION, &nid);
        }

        void remove_tray_icon() {
            NOTIFYICONDATAW nid{.cbSize = sizeof(nid)};
            nid.hWnd = g_hwnd;
            nid.uID = TrayIconId;
            ::Shell_NotifyIconW(NIM_DELETE, &nid);
        }

        void enqueue(string data) {
            g_queue.push_back(std::move(data));
            ::PostMessageW(g_hwnd, WmDrain, 0, 0);
        }

        // An empty command line (or one starting with the splitter) opens the configuration UI.
        string open_settings_command() {
            return ArgSplitter "0";
        }

        void drain() {
            // UI runs a nested message loop, which dispatches our messages too: never re-enter.
            if(g_busy) {
                // let the host decide whether newly arrived commands wait or are handled elsewhere
                for(auto it = g_queue.begin(); it != g_queue.end();) {
                    if(g_host->on_busy && !g_host->on_busy(*it)) {
                        it = g_queue.erase(it);
                    } else {
                        ++it;
                    }
                }
                return;
            }

            g_busy = true;
            while(!g_queue.empty() && !g_exit) {
                string data = std::move(g_queue.front());
                g_queue.pop_front();

                // A UI window posts WM_QUIT when it is destroyed; a stale one would close the next UI instantly.
                MSG stale;
                while(::PeekMessageW(&stale, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}

                g_cancel = false;
                g_host->execute(data);
                g_cancel = false;
            }
            g_busy = false;

            if(g_exit) ::PostMessageW(g_hwnd, WM_NULL, 0, 0);   // wake the main loop
        }

        void show_menu(POINT pt) {
            HMENU menu = ::CreatePopupMenu();
            auto add = [menu](UINT id, const string& text, UINT flags = MF_STRING) {
                ::AppendMenuW(menu, flags, id, grey::common::str::to_wstr(text).c_str());
            };
            add(cmd_open_settings, tr("Open settings"));
            add(cmd_autostart, tr("Start with Windows"), MF_STRING | (is_autostart_enabled() ? MF_CHECKED : 0));
            ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            add(cmd_exit, tr("Exit"));
            ::SetMenuDefaultItem(menu, cmd_open_settings, FALSE);

            // required for the menu to close when the user clicks elsewhere
            ::SetForegroundWindow(g_hwnd);
            UINT cmd = ::TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                                          pt.x, pt.y, g_hwnd, nullptr);
            ::PostMessageW(g_hwnd, WM_NULL, 0, 0);
            ::DestroyMenu(menu);

            switch(cmd) {
                case cmd_open_settings:
                    enqueue(open_settings_command());
                    break;
                case cmd_autostart:
                    set_autostart(!is_autostart_enabled());
                    break;
                case cmd_exit:
                    request_exit();
                    break;
                default:
                    break;
            }
        }

        LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
            switch(msg) {
                case WM_COPYDATA: {
                    auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lparam);
                    if(!cds || cds->dwData != CopyDataMagic) return FALSE;
                    // queue and return at once: the client is blocked in SendMessage until we do
                    string data{static_cast<const char*>(cds->lpData), cds->cbData};
                    enqueue(std::move(data));
                    return TRUE;
                }
                case WmDrain:
                    drain();
                    return 0;
                case WmTray:
                    switch(LOWORD(lparam)) {
                        case NIN_SELECT:
                        case NIN_KEYSELECT:
                            enqueue(open_settings_command());
                            break;
                        case WM_CONTEXTMENU: {
                            POINT pt{static_cast<short>(LOWORD(wparam)), static_cast<short>(HIWORD(wparam))};
                            show_menu(pt);
                        }
                        break;
                        default:
                            break;
                    }
                    return 0;
                case WM_ENDSESSION:
                    // logoff/shutdown, or the installer's Restart Manager asking us to let go of bt.exe
                    if(wparam) request_exit();
                    return 0;
                case WM_CLOSE:
                    // e.g. "taskkill /im bt.exe" from the uninstaller: quit properly instead of
                    // destroying the window and leaving the process running without it
                    request_exit();
                    return 0;
                default:
                    if(msg == g_taskbar_created && g_taskbar_created != 0) {
                        // Explorer restarted and forgot every tray icon
                        add_tray_icon();
                        return 0;
                    }
                    break;
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        }

        bool create_window() {
            WNDCLASSEXW wc{.cbSize = sizeof(wc)};
            wc.lpfnWndProc = wnd_proc;
            wc.hInstance = ::GetModuleHandleW(nullptr);
            wc.lpszClassName = WindowClass.c_str();
            if(!::RegisterClassExW(&wc)) return false;

            // a real (never shown) top-level window rather than a message-only one: those miss "TaskbarCreated"
            g_hwnd = ::CreateWindowExW(WS_EX_TOOLWINDOW, WindowClass.c_str(), WindowClass.c_str(), WS_POPUP,
                                       0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
            if(!g_hwnd) return false;

            g_taskbar_created = ::RegisterWindowMessageW(L"TaskbarCreated");
            // let a non-elevated Explorer reach us when we run elevated
            ::ChangeWindowMessageFilterEx(g_hwnd, g_taskbar_created, MSGFLT_ALLOW, nullptr);
            ::ChangeWindowMessageFilterEx(g_hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
            return true;
        }

        bool wait_and_forward(const string& data, DWORD timeout_ms) {
            const ULONGLONG deadline = ::GetTickCount64() + timeout_ms;
            while(::GetTickCount64() < deadline) {
                if(HWND h = find_resident()) {
                    return data.empty() || send(h, data);
                }
                ::Sleep(20);
            }
            return false;
        }
    }

    bool forward(const string& data) {
        HWND h = find_resident();
        if(!h) return false;
        return data.empty() || send(h, data);
    }

    int run(const host& h, const string& first_data) {
        g_mutex = ::CreateMutexW(nullptr, TRUE, MutexName.c_str());
        if(g_mutex && ::GetLastError() == ERROR_ALREADY_EXISTS) {
            // another process won the race and is starting up right now
            ::CloseHandle(g_mutex);
            g_mutex = nullptr;
            if(wait_and_forward(first_data, 5000)) return 0;
            if(!first_data.empty()) h.execute(first_data);
            return 0;
        }

        // UI windows created later must be per-monitor DPI aware; the setting is process wide and only applies
        // when made before the first window, which is our hidden one
        ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        if(!create_window()) {
            if(!first_data.empty()) h.execute(first_data);
            return 0;
        }

        g_host = &h;
        add_tray_icon();

        if(!first_data.empty()) enqueue(first_data);

        MSG msg;
        while(!g_exit) {
            BOOL r = ::GetMessageW(&msg, nullptr, 0, 0);
            if(r == -1) break;
            if(r == 0) continue;    // WM_QUIT posted by a closed UI window, not meant for us
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }

        remove_tray_icon();
        ::DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
        g_host = nullptr;
        if(g_mutex) {
            ::ReleaseMutex(g_mutex);
            ::CloseHandle(g_mutex);
            g_mutex = nullptr;
        }
        return 0;
    }

    bool is_resident() {
        return g_hwnd != nullptr;
    }

    void request_exit() {
        g_exit = true;
        g_cancel = true;
        if(g_hwnd) ::PostMessageW(g_hwnd, WM_NULL, 0, 0);
    }

    void request_cancel() {
        g_cancel = true;
    }

    bool cancel_requested() {
        return g_cancel;
    }

    static BOOL CALLBACK find_visible_proc(HWND hwnd, LPARAM lp) {
        if(hwnd != g_hwnd && ::IsWindowVisible(hwnd) && !::GetWindow(hwnd, GW_OWNER)) {
            *reinterpret_cast<HWND*>(lp) = hwnd;
            return FALSE;
        }
        return TRUE;
    }

    bool bring_own_window_to_front() {
        HWND found{nullptr};
        ::EnumThreadWindows(::GetCurrentThreadId(), find_visible_proc, reinterpret_cast<LPARAM>(&found));

        if(!found) return false;
        if(::IsIconic(found)) ::ShowWindow(found, SW_RESTORE);
        ::SetForegroundWindow(found);
        return true;
    }

    bool spawn_direct(const string& data) {
        wstring exe = exe_path();
        wstring cmd = L"\"" + exe + L"\" " + grey::common::str::to_wstr(DirectSwitch) + grey::common::str::to_wstr(data);
        vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(L'\0');

        STARTUPINFOW si{.cb = sizeof(si)};
        PROCESS_INFORMATION pi{};
        if(!::CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            return false;
        }
        ::AllowSetForegroundWindow(pi.dwProcessId);
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
        return true;
    }

    bool quit_running(unsigned long timeout_ms) {
        HWND h = find_resident();
        if(!h) return true;

        DWORD pid{0};
        ::GetWindowThreadProcessId(h, &pid);
        HANDLE process = ::OpenProcess(SYNCHRONIZE, FALSE, pid);
        ::PostMessageW(h, WM_CLOSE, 0, 0);
        if(!process) return find_resident() == nullptr;

        const bool exited = ::WaitForSingleObject(process, timeout_ms) == WAIT_OBJECT_0;
        ::CloseHandle(process);
        return exited;
    }

    bool is_autostart_enabled() {
        HKEY key;
        if(::RegOpenKeyExW(HKEY_CURRENT_USER, RunKey.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
        wchar_t value[MAX_PATH * 2]{};
        DWORD size = sizeof(value) - sizeof(wchar_t);
        DWORD type{0};
        LSTATUS st = ::RegQueryValueExW(key, RunValue.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(value), &size);
        ::RegCloseKey(key);
        if(st != ERROR_SUCCESS || type != REG_SZ) return false;
        // only count it if it points at this executable (a stale entry from a moved install does not count)
        wstring v{value};
        return v.find(exe_path()) != wstring::npos;
    }

    void set_autostart(bool enabled) {
        HKEY key;
        if(::RegCreateKeyExW(HKEY_CURRENT_USER, RunKey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)
           != ERROR_SUCCESS) return;
        if(enabled) {
            wstring v = L"\"" + exe_path() + L"\" " + grey::common::str::to_wstr(TraySwitch);
            ::RegSetValueExW(key, RunValue.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()),
                             static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
        } else {
            ::RegDeleteValueW(key, RunValue.c_str());
        }
        ::RegCloseKey(key);
    }

#else

    bool forward(const string&) { return false; }

    int run(const host& h, const string& first_data) {
        if(!first_data.empty()) h.execute(first_data);
        return 0;
    }

    bool is_resident() { return false; }
    void request_exit() {}
    void request_cancel() {}
    bool cancel_requested() { return false; }
    bool bring_own_window_to_front() { return false; }
    bool spawn_direct(const string&) { return false; }
    bool quit_running(unsigned long) { return true; }
    bool is_autostart_enabled() { return false; }
    void set_autostart(bool) {}

#endif
}
