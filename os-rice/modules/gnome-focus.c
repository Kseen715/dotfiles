/* modules/gnome-focus.c -- make notification clicks raise the window (DRAFT).
 *
 * GNOME Shell's focus-stealing prevention: when an app asks for focus without a
 * fresh user-interaction timestamp (Telegram, Thunderbird, anything raising a
 * window from a tray/notification), the Shell refuses and shows a second
 * "<App> is ready" notification instead. Clicking that one finally raises it.
 * Two clicks for every message.
 *
 * There is no gsettings key for this on Wayland -- the behaviour lives in
 * MetaDisplay's focus policy, so the only fix is a Shell extension that catches
 * `demands-attention` and activates the window itself. Fetching and enabling it
 * is lib/gnome.c's osr_gnome_extension_install, which is where the per-Shell-
 * major download and the "log out for it to load" warning live. Best-effort
 * (§9): an unreachable extensions site reports and the run continues.
 *
 * Was modules/gnome-focus.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/gnome.h"

#define GF_UUID "stealmyfocus@kleinernik.gmail.com"

int osrm_gnome_focus(void) {
    return osr_gnome_extension_install("Installing Steal My Focus Window",
                                       GF_UUID);
}
