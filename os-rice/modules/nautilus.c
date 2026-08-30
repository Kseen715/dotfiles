/* modules/nautilus.c -- GNOME Files (Nautilus) file manager. ONE copy, POSIX
 * (was .../modules/nautilus.sh). Native, no config.
 *
 * Was modules/nautilus.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_nautilus(void) {
    static const char *const pkgs[] = { "nautilus", NULL };
    return osr_pkg_install_step("Installing Nautilus", pkgs);
}
