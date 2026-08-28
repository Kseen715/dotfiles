/* modules/curseforge.c -- CurseForge (AUR). POSIX port of .../apps/curseforge.sh.
 * CurseForge's PKGBUILD ships broken checksums, so it needs helper flags the
 * generic aur: provider doesn't carry (--nosign + makepkg --skipchecksums) —
 * hence a direct helper call rather than a pacman.map aur: row. Available module.
 *
 * Port of modules/curseforge.sh, kept as the reference at
 * test/ref/curseforge_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_curseforge(void) {
    const char *helper;
    char *argv[10];

    {
        char *q[4];
        q[0] = (char *)"pacman"; q[1] = (char *)"-Q"; q[2] = (char *)"curseforge"; q[3] = NULL;
        if (osr_run_quiet(q) == 0) {
            osr_info("curseforge already installed (aur) - skipping");
            return 1;
        }
    }
    helper = osr_pkg_aur_helper();
    if (*helper == '\0')
        osr_die("no AUR helper (paru/yay) - install 'paru' before curseforge");
    /* Not an `aur:` row: this one needs makepkg flags no pkgmap row can carry. */
    osr_warn("skipping checksum verification for curseforge (upstream ships broken sums)");
    argv[0] = (char *)helper; argv[1] = (char *)"-S"; argv[2] = (char *)"--needed";
    argv[3] = (char *)"--noconfirm"; argv[4] = (char *)"--nosign";
    argv[5] = (char *)"curseforge"; argv[6] = (char *)"--mflags";
    argv[7] = (char *)"--skipchecksums"; argv[8] = NULL;
    return osr_run_step_user("Installing CurseForge (AUR)", argv);
}
