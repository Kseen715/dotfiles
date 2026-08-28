/* modules/wlogout.c -- wlogout logout menu (AUR) + rice-owned config dir. ONE
 * copy, POSIX (was .../modules/wlogout.sh). Not in the default rice.list (wleave
 * is used), kept as an available alternative module.
 *
 * Port of modules/wlogout.sh, kept as the reference at
 * test/ref/wlogout_sh_ref.sh. C89.
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
