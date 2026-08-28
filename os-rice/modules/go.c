/* modules/go.c -- Go toolchain. ONE copy, POSIX, distro-agnostic (was
 * linux-debian/modules/go.sh, which fetched go.dev tarballs). Native-first: the
 * distro package is used everywhere (updatable via the package manager). The
 * name differs (dnf/apt call it `golang`, others `go`), resolved by pkgmap.
 *
 * Port of modules/go.sh, kept as the reference at
 * test/ref/go_sh_ref.sh. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_go(void) {
    static const char *const pkgs[] = { "go", NULL };
    return osr_pkg_install_step("Installing Go", pkgs);
}
