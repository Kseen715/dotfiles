/* modules/nwg-displays.c -- nwg-displays monitor layout tool. ONE copy, POSIX
 * (was .../modules/nwg-displays.sh). Native, no config.
 *
 * Was modules/nwg-displays.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_nwg_displays(void) {
    static const char *const pkgs[] = { "nwg-displays", NULL };
    return osr_pkg_install_step("Installing nwg-displays", pkgs);
}
