/* modules/easyeffects.c -- EasyEffects audio effects + its LADSPA/LV2 plugin set.
 * ONE copy, POSIX (was .../modules/easyeffects.sh). Two AUR plugins (mda-lv2,
 * libdeep-filter-ladspa) come through the aur: rows in pacman.map.
 *
 * Was modules/easyeffects.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_easyeffects(void) {
    static const char *const pkgs[] = { "easyeffects", NULL };
    static const char *const plugins[] = {
        "lsp-plugins", "lsp-plugins-ladspa", "calf", "libebur128", "zam-plugins",
        "zita-convolver", "speex", "soundtouch", "rnnoise", "libsamplerate",
        "libsndfile", "libbs2b", "fftw", "speexdsp", "nlohmann-json", "onetbb", NULL
    };
    static const char *const aur[] = { "mda-lv2", "libdeep-filter-ladspa", NULL };
    Str effects, dconf;
    const char *dirs[3];
    int ok;

    ok = osr_pkg_install_step("Installing EasyEffects", pkgs);
    ok = osr_pkg_install_step("Installing EasyEffects plugins", plugins) && ok;
    ok = osr_pkg_install_step("Installing EasyEffects AUR plugins", aur) && ok;

    /* One mkdir, not two: the dconf dir is here because EasyEffects writes its
     * presets through dconf, and a first launch that finds neither directory
     * makes root create them. */
    str_init(&effects); str_init(&dconf);
    str_addz(&effects, osr_mod_home()); str_addz(&effects, "/.config/easyeffects");
    str_addz(&dconf,   osr_mod_home()); str_addz(&dconf,   "/.config/dconf");
    dirs[0] = str_text(&effects); dirs[1] = str_text(&dconf); dirs[2] = NULL;
    ok = osr_mkdir_p_all(dirs) && ok;
    str_free(&effects); str_free(&dconf);
    return ok;
}
