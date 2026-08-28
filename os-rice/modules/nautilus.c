/* modules/nautilus.c -- GNOME Files (Nautilus) file manager. ONE copy, POSIX
 * (was .../modules/nautilus.sh). Native, no config.
 *
 * Port of modules/nautilus.sh, kept as the reference at
 * test/ref/nautilus_sh_ref.sh. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_nautilus(void) {
    static const char *const pkgs[] = { "nautilus", NULL };
    return osr_pkg_install_step("Installing Nautilus", pkgs);
}
