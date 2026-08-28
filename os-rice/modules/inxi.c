/* modules/inxi.c -- inxi system information tool. ONE copy, POSIX,
 * distro-agnostic (was linux-debian/modules/inxi.sh). Native on every target.
 *
 * Port of modules/inxi.sh, kept as the reference at
 * test/ref/inxi_sh_ref.sh. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_inxi(void) {
    static const char *const pkgs[] = { "inxi", NULL };
    return osr_pkg_install_step("Installing inxi", pkgs);
}
