/* modules/zig.c -- Zig toolchain. ONE copy, POSIX, distro-agnostic (was
 * linux-debian/modules/zig.sh, which added the debian.griffo.io apt repo).
 * Native-first: native on arch/fedora/alpine/void and recent Ubuntu; Debian and
 * older Ubuntu get the official ziglang.org tarball (source:provide_zig via apt.map).
 *
 * Was modules/zig.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_zig(void) {
    static const char *const pkgs[] = { "zig", NULL };
    return osr_pkg_install_step("Installing Zig", pkgs);
}
