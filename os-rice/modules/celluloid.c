/* modules/celluloid.c -- Celluloid (mpv GTK frontend). ONE copy, POSIX
 * (was .../apps/celluloid.sh). Native, no config.
 *
 * Port of modules/celluloid.sh, kept as the reference at
 * test/ref/celluloid_sh_ref.sh. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_celluloid(void) {
    static const char *const pkgs[] = { "celluloid", NULL };
    return osr_pkg_install_step("Installing Celluloid", pkgs);
}
