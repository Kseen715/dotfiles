/* modules/wlogout.c -- wlogout logout menu (AUR) + rice-owned config dir. ONE
 * copy, POSIX (was .../modules/wlogout.sh). Not in the default rice.list (wleave
 * is used), kept as an available alternative module.
 *
 * Was modules/wlogout.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"

#include <stddef.h>

int osrm_wlogout(void) {
    static const char *const pkgs[] = { "wlogout", NULL };
    int ok;

    ok = osr_pkg_install_step("Installing wlogout (AUR)", pkgs);
    if (*osr_mod_theme_dir() != '\0') ok = osr_apply_config("wlogout") && ok;
    return ok;
}
