/* modules/paru.c -- bootstrap the paru AUR helper. Listed first in an Arch rice
 * so every later aur: package can dispatch through it (manifest order is the
 * dependency graph, §4). paru resolves via pacman.map to source:provide_paru, so
 * pkg_install builds it from the AUR once and skips on rerun (command -v probe).
 * Arch-only; a no-op elsewhere (nothing maps `paru` on non-pacman hosts).
 *
 * Was modules/paru.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <string.h>

int osrm_paru(void) {
    static const char *const pkgs[] = { "paru", NULL };

    if (strcmp(osr_mod_pkg(), "pacman") != 0) {
        osr_info("paru (AUR helper) is Arch-only - skipping");
        return 1;
    }
    return osr_pkg_install_step("Bootstrapping paru (AUR helper)", pkgs);
}
