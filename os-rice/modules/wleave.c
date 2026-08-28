/* modules/wleave.c -- wleave logout menu (AUR) + rice-owned config dir. ONE copy,
 * POSIX (was .../modules/wleave.sh). scdoc is a native build/man dep. The config
 * dir (layout, style.css, icons) is rice-owned (§6), copied whole.
 *
 * Port of modules/wleave.sh, kept as the reference at
 * test/ref/wleave_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"

#include <stddef.h>

int osrm_wleave(void) {
    static const char *const pkgs[] = { "wleave", "scdoc", NULL };
    int ok;

    ok = osr_pkg_install_step("Installing wleave (AUR)", pkgs);
    if (*osr_mod_theme_dir() != '\0') ok = osr_apply_config("wleave") && ok;
    return ok;
}
