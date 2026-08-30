/* modules/onlyoffice.c -- ONLYOFFICE Desktop Editors (AUR). ONE copy, POSIX
 * (was .../apps/onlyoffice.sh). Available module (not in default rice.list).
 *
 * Was modules/onlyoffice.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_onlyoffice(void) {
    static const char *const pkgs[] = { "onlyoffice", NULL };
    return osr_pkg_install_step("Installing ONLYOFFICE (AUR)", pkgs);
}
