/* modules/arandr.c -- display layout, the X11 replacement for nwg-displays
 * (i3-sugg §2). Two halves that belong together: arandr is the GUI you drag
 * monitors around in, autorandr is what remembers the result and re-applies it
 * on hotplug.
 *
 * After arranging a layout once, save it with `autorandr --save <name>`; the i3
 * config runs `autorandr --change` at startup so docking picks the profile up.
 * The udev hotplug hook ships with the package.
 *
 * Was modules/arandr.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_arandr(void) {
    static const char *const pkgs[] = { "arandr", "autorandr", NULL };
    return osr_pkg_install_step("Installing arandr + autorandr", pkgs);
}
