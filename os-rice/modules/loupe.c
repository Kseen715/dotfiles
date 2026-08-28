/* modules/loupe.c -- Loupe image viewer. ONE copy, POSIX
 * (was .../modules/loupe.sh). Native, no config. Available module.
 *
 * Port of modules/loupe.sh, kept as the reference at
 * test/ref/loupe_sh_ref.sh. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_loupe(void) {
    static const char *const pkgs[] = { "loupe", NULL };
    return osr_pkg_install_step("Installing Loupe", pkgs);
}
