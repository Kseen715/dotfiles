/* modules/obs-studio.c -- OBS Studio. ONE copy, POSIX (was .../apps/obs-studio.sh).
 *
 * Port of modules/obs-studio.sh, kept as the reference at
 * test/ref/obs-studio_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_obs_studio(void) {
    static const char *const pkgs[] = { "obs-studio", NULL };
    Str dir;
    int ok;

    ok = osr_pkg_install_step("Installing OBS Studio", pkgs);
    /* The config dir, so the first launch writes its profile into a directory
     * the riced account owns rather than one root created for it. */
    str_init(&dir);
    str_addz(&dir, osr_mod_home());
    str_addz(&dir, "/.config/obs-studio");
    ok = osr_mkdir_p(str_text(&dir)) && ok;
    str_free(&dir);
    return ok;
}
