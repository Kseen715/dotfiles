/* modules/flameshot.c -- screenshots, the X11 replacement for
 * grim+slurp (i3-sugg §2). flameshot is the annotate-and-share GUI; maim+slop
 * are the scriptable pair the i3 bindings use for "region straight to
 * clipboard", which flameshot cannot do without opening its editor.
 *
 * Was modules/flameshot.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_flameshot(void) {
    static const char *const pkgs[] = { "flameshot", "maim", "slop", "xclip", NULL };
    return osr_pkg_install_step("Installing screenshot tools", pkgs);
}
