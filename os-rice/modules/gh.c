/* modules/gh.c -- GitHub CLI. ONE copy, POSIX, distro-agnostic (was
 * linux-debian/modules/gh.sh). Native-first: the package is `github-cli` on
 * arch/alpine/void and `gh` on fedora/Debian/Ubuntu (resolved by pkgmap). Only
 * Debian 11 (bullseye) lacks it -> upstream release tarball via an apt.map row.
 *
 * Port of modules/gh.sh, kept as the reference at
 * test/ref/gh_sh_ref.sh. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_gh(void) {
    static const char *const pkgs[] = { "gh", "git", NULL };
    return osr_pkg_install_step("Installing GitHub CLI", pkgs);
}
