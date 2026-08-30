/* modules/luminance.c -- Luminance brightness controller (AUR). ONE copy, POSIX
 * (was .../modules/luminance.sh).
 *
 * Was modules/luminance.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_luminance(void) {
    static const char *const pkgs[] = { "luminance", NULL };
    return osr_pkg_install_step("Installing Luminance (AUR)", pkgs);
}
