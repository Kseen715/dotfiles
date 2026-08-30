/* modules/ncdu.c -- ncdu
 *
 * Was modules/ncdu.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_ncdu(void) {
    static const char *const pkgs[] = { "ncdu", NULL };
    return osr_pkg_install_step("Installing ncdu", pkgs);
}
