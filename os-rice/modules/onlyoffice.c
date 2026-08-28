/* modules/onlyoffice.c -- ONLYOFFICE Desktop Editors (AUR). ONE copy, POSIX
 * (was .../apps/onlyoffice.sh). Available module (not in default rice.list).
 *
 * Port of modules/onlyoffice.sh, kept as the reference at
 * test/ref/onlyoffice_sh_ref.sh. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_onlyoffice(void) {
    static const char *const pkgs[] = { "onlyoffice", NULL };
    return osr_pkg_install_step("Installing ONLYOFFICE (AUR)", pkgs);
}
