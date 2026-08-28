/* modules/mako.c -- mako notification daemon + rice-owned config. ONE copy, POSIX
 * (was .../modules/mako.sh).
 *
 * Port of modules/mako.sh, kept as the reference at
 * test/ref/mako_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_mako(void) {
    static const char *const pkgs[] = { "mako", NULL };
    Str dst;
    int ok;

    ok = osr_pkg_install_step("Installing mako", pkgs);
    /* `|| :` in the sh: a theme with no mako layer is not a failure, mako has
     * its own defaults. */
    str_init(&dst);
    str_addz(&dst, osr_mod_home());
    str_addz(&dst, "/.config/mako/config");
    (void)osr_install_theme_layer("mako", "config", str_text(&dst));
    str_free(&dst);
    return ok;
}
