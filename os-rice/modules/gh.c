/* modules/gh.c -- GitHub CLI. ONE copy, POSIX, distro-agnostic (was
 * linux-debian/modules/gh.sh). Native-first: the package is `github-cli` on
 * arch/alpine/void and `gh` on fedora/Debian/Ubuntu (resolved by pkgmap). Only
 * Debian 11 (bullseye) lacks it -> upstream release tarball via an apt.map row.
 *
 * Was modules/gh.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_gh(void) {
    static const char *const pkgs[] = { "gh", "git", NULL };
    return osr_pkg_install_step("Installing GitHub CLI", pkgs);
}
