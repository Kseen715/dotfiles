/* modules/flameshot.c -- screenshots, the X11 replacement for
 * grim+slurp (i3-sugg §2). flameshot is the annotate-and-share GUI; maim+slop
 * are the scriptable pair the i3 bindings use for "region straight to
 * clipboard", which flameshot cannot do without opening its editor.
 *
 * Port of modules/flameshot.sh, kept as the reference at
 * test/ref/flameshot_sh_ref.sh. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_flameshot(void) {
    static const char *const pkgs[] = { "flameshot", "maim", "slop", "xclip", NULL };
    return osr_pkg_install_step("Installing screenshot tools", pkgs);
}
