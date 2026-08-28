/* modules/vlc.c -- VLC. The "plays anything, including the broken file" player;
 * modules/celluloid.sh (mpv front-end) is the lighter one and they coexist fine.
 *
 * VLC's Qt interface follows QT_QPA_PLATFORMTHEME (modules/theming.sh sets the
 * rice's Qt palette), so it inherits the desktop colors without a skin. Its own
 * skin engine is a binary .vlt format and is deliberately not vendored here.
 *
 * The rice-owned vlcrc layer sets the interface to dark and turns off the
 * playlist art fetching that otherwise phones home on every file.
 *
 * Port of modules/vlc.sh, kept as the reference at
 * test/ref/vlc_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_vlc(void) {
    static const char *const pkgs[] = { "vlc", NULL };
    Str dst;
    int ok;

    ok = osr_pkg_install_step("Installing VLC", pkgs);
    str_init(&dst);
    str_addz(&dst, osr_mod_home());
    str_addz(&dst, "/.config/vlc/vlcrc");
    (void)osr_install_theme_layer("vlc", "vlcrc", str_text(&dst));
    str_free(&dst);
    return ok;
}
