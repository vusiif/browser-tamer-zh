#pragma once
#include <functional>
#include <string>

/**
 * Resident (system tray) mode.
 *
 * The first Browser Tamer process becomes resident: it owns a hidden window and a notification-area icon and keeps
 * running after its UI closes. Every later launch (a link click, the start menu shortcut) is a short-lived client that
 * hands its command line to the resident process over WM_COPYDATA and exits, so the picker opens without paying for a
 * cold start (process creation, Lua, config parsing, font files, Direct3D) each time.
 *
 * Only implemented on Windows; elsewhere every function is a no-op and the app behaves as before.
 */
namespace bt::resident {

    struct host {
        /**
         * Executes one command (the same string a cold start would build from its command line). May block while UI is shown.
         */
        std::function<void(const std::string& data)> execute;

        /**
         * Called when a command arrives while another one is still running (its UI is open).
         * Return true to queue it until the current one finishes, false if it was dealt with.
         */
        std::function<bool(const std::string& data)> on_busy;
    };

    /**
     * Client side: hands `data` to an already running resident instance.
     * @param data Command to forward. Empty only checks that a resident instance exists.
     * @return true if a resident instance accepted it.
     */
    bool forward(const std::string& data);

    /**
     * Becomes the resident instance, runs `first_data` (if not empty) and keeps processing forwarded commands until
     * the user exits from the tray menu. If another process is already becoming resident, forwards `first_data` to it
     * instead; if that fails as well, executes `first_data` in this process without staying resident.
     */
    int run(const host& h, const std::string& first_data);

    /**
     * True while this process is the resident instance.
     */
    bool is_resident();

    /**
     * Asks the resident loop to quit once the current command finishes. Also requests cancellation of the open UI.
     */
    void request_exit();

    /**
     * Asks whatever UI is currently open to close as soon as possible (checked by the UI on each frame).
     */
    void request_cancel();

    /**
     * True when the open UI should close (see request_cancel()).
     */
    bool cancel_requested();

    /**
     * Brings a visible window owned by this process to the foreground. Returns false if there is none.
     */
    bool bring_own_window_to_front();

    /**
     * Starts a separate, non-resident Browser Tamer process for `data`. Used when the resident UI is busy.
     */
    bool spawn_direct(const std::string& data);

    /**
     * Command line switch for spawn_direct().
     */
    constexpr const char* DirectSwitch = "--direct ";

    /**
     * Command line switch that starts the resident instance with no UI (used by "start with Windows").
     */
    constexpr const char* TraySwitch = "--tray";

    bool is_autostart_enabled();
    void set_autostart(bool enabled);
}
