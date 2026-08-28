/* modules/htop.c -- htop process viewer. ONE copy, POSIX, distro-agnostic
 * (was .../apps/htop.sh). Native everywhere, no config.
 *
 * Port of modules/htop.sh, kept as the reference at
 * test/ref/htop_sh_ref.sh. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_htop(void) {
    static const char *const pkgs[] = { "htop", NULL };
    return osr_pkg_install_step("Installing htop", pkgs);
}
