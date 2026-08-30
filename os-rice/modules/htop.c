/* modules/htop.c -- htop process viewer. ONE copy, POSIX, distro-agnostic
 * (was .../apps/htop.sh). Native everywhere, no config.
 *
 * Was modules/htop.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_htop(void) {
    static const char *const pkgs[] = { "htop", NULL };
    return osr_pkg_install_step("Installing htop", pkgs);
}
