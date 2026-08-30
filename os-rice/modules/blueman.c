/* modules/blueman.c -- Bluetooth stack + tray applet (i3-sugg §7.2).
 * blueman-applet is what the i3 config execs; without it there is no pairing UI
 * and no way to answer a pairing request.
 *
 * The service name differs per init (bluetooth.service vs /etc/sv/bluetoothd) —
 * that is a servicemap `@init` row, not a case here (§8).
 * bluez-obex is file transfer to/from the phone; without it "Send file" in the
 * blueman menu is greyed out. mpris-proxy (ships inside bluez) is what makes the
 * play/pause button on a headset reach playerctl and the bar.
 *
 * Was modules/blueman.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_blueman(void) {
    static const char *const pkgs[] = { "bluez", "bluez-obex", "blueman", NULL };
    int ok;

    ok = osr_pkg_install_step("Installing Bluetooth", pkgs);
    /* A warning, not a failure: a container has no init to enable it under, and
     * the packages are still worth having. */
    if (!osr_service_enable("bluetooth"))
        osr_warn("could not enable bluetooth (needs a real init)");
    return ok;
}
