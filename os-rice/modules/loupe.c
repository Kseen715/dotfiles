/* modules/loupe.c -- Loupe image viewer. ONE copy, POSIX
 * (was .../modules/loupe.sh). Native, no config. Available module.
 *
 * Was modules/loupe.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_loupe(void) {
    static const char *const pkgs[] = { "loupe", NULL };
    return osr_pkg_install_step("Installing Loupe", pkgs);
}
