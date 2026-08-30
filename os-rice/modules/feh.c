/* modules/feh.c -- wallpaper setter, the X11 replacement for hyprpaper
 * (i3-sugg §2). feh has no daemon: it paints the root window once and exits, so
 * the i3 config re-runs it on every start. xcolor is the X11 hyprpicker.
 *
 * The wallpaper itself is resolved and installed by the shared §6 helper
 * (apply_wallpaper at the end of a rice run) — this module only provides the
 * setter, so nothing here hard-codes a path.
 *
 * Was modules/feh.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_feh(void) {
    static const char *const pkgs[] = { "feh", "xcolor", NULL };
    return osr_pkg_install_step("Installing feh + xcolor", pkgs);
}
