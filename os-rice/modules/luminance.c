/* modules/luminance.c -- Luminance brightness controller (AUR). ONE copy, POSIX
 * (was .../modules/luminance.sh).
 *
 * Port of modules/luminance.sh, kept as the reference at
 * test/ref/luminance_sh_ref.sh. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_luminance(void) {
    static const char *const pkgs[] = { "luminance", NULL };
    return osr_pkg_install_step("Installing Luminance (AUR)", pkgs);
}
