/* modules/nwg-displays.c -- nwg-displays monitor layout tool. ONE copy, POSIX
 * (was .../modules/nwg-displays.sh). Native, no config.
 *
 * Port of modules/nwg-displays.sh, kept as the reference at
 * test/ref/nwg-displays_sh_ref.sh. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_nwg_displays(void) {
    static const char *const pkgs[] = { "nwg-displays", NULL };
    return osr_pkg_install_step("Installing nwg-displays", pkgs);
}
