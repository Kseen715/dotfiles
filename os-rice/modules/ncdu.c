/* modules/ncdu.c -- ncdu
 *
 * Port of modules/ncdu.sh, kept as the reference at
 * test/ref/ncdu_sh_ref.sh. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_ncdu(void) {
    static const char *const pkgs[] = { "ncdu", NULL };
    return osr_pkg_install_step("Installing ncdu", pkgs);
}
