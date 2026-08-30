/* modules/celluloid.c -- Celluloid (mpv GTK frontend). ONE copy, POSIX
 * (was .../apps/celluloid.sh). Native, no config.
 *
 * Was modules/celluloid.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_celluloid(void) {
    static const char *const pkgs[] = { "celluloid", NULL };
    return osr_pkg_install_step("Installing Celluloid", pkgs);
}
