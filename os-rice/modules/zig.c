/* modules/zig.c -- Zig toolchain. ONE copy, POSIX, distro-agnostic (was
 * linux-debian/modules/zig.sh, which added the debian.griffo.io apt repo).
 * Native-first: native on arch/fedora/alpine/void and recent Ubuntu; Debian and
 * older Ubuntu get the official ziglang.org tarball (source:provide_zig via apt.map).
 *
 * Port of modules/zig.sh, kept as the reference at
 * test/ref/zig_sh_ref.sh. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_zig(void) {
    static const char *const pkgs[] = { "zig", NULL };
    return osr_pkg_install_step("Installing Zig", pkgs);
}
