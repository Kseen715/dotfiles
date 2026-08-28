/* modules/flatpak.c -- Flatpak + the Flathub remote. ONE copy, POSIX
 * (was .../apps/flatpack.sh). Adding the remote is idempotent (--if-not-exists).
 * The remote must be added AS ROOT with an explicit --system: `flatpak
 * remote-add` defaults to the system installation, and a non-root user touching
 * it goes through polkit - which has no agent (and no session) under the
 * installer's sudo -u, so it dies with "operation EnsureRepo not allowed for
 * user". Root writes /var/lib/flatpak directly, no polkit involved, and every
 * user on the box gets Flathub.
 *
 * Port of modules/flatpak.sh, kept as the reference at
 * test/ref/flatpak_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_flatpak(void) {
    static const char *const pkgs[] = { "flatpak", NULL };
    char *argv[8];
    int ok;

    ok = osr_pkg_install_step("Installing Flatpak", pkgs);
    argv[0] = (char *)"flatpak"; argv[1] = (char *)"remote-add";
    argv[2] = (char *)"--system"; argv[3] = (char *)"--if-not-exists";
    argv[4] = (char *)"flathub";
    argv[5] = (char *)"https://flathub.org/repo/flathub.flatpakrepo";
    argv[6] = NULL;
    return osr_run_step_root("Adding Flathub remote", argv) && ok;
}
