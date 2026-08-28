/* modules/helvum.c -- Helvum PipeWire patchbay (GTK). ONE copy, POSIX
 * (was .../modules/helvum.sh). Native, no config. Available module (qpwgraph is
 * the default patchbay in this rice).
 *
 * Port of modules/helvum.sh, kept as the reference at
 * test/ref/helvum_sh_ref.sh. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_helvum(void) {
    static const char *const pkgs[] = { "helvum", NULL };
    return osr_pkg_install_step("Installing Helvum", pkgs);
}
