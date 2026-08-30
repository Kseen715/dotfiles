/* modules/git-base.c -- git + core CLI tools (git, wget, editors, man). The base
 * every later module and the user relies on. Native everywhere; ONE copy, POSIX
 * (was linux-arch-x86_64-hyprland-glass/modules/git.sh, bash+pacman).
 *
 * Was modules/git-base.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_git_base(void) {
    static const char *const pkgs[] = {
        "git", "wget", "nano", "vim", "man-db", NULL
    };
    return osr_pkg_install_step("Installing git and base CLI tools", pkgs);
}
