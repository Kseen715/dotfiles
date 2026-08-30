/* modules/inxi.c -- inxi system information tool. ONE copy, POSIX,
 * distro-agnostic (was linux-debian/modules/inxi.sh). Native on every target.
 *
 * Was modules/inxi.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_inxi(void) {
    static const char *const pkgs[] = { "inxi", NULL };
    return osr_pkg_install_step("Installing inxi", pkgs);
}
