/* modules/gtklock.c -- gtklock GTK screen locker + rice-owned config. ONE copy,
 * POSIX (was .../modules/gtklock.sh). style.css carries a {{WALLPAPER_PATH}}
 * placeholder the legacy sed-substituted at install; we resolve it to the rice's
 * wallpaper. .face (lockscreen avatar) is seeded once and then left to the user.
 *
 * Was modules/gtklock.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"

#include <stddef.h>

int osrm_gtklock(void) {
    static const char *const pkgs[] = { "gtklock", "gtklock-userinfo-module", NULL };
    Str src, dst;
    int ok;

    ok = osr_pkg_install_step("Installing gtklock", pkgs);
    if (*osr_mod_theme_dir() == '\0') return ok;

    str_init(&src); str_init(&dst);
    str_addz(&src, osr_mod_theme_dir()); str_addz(&src, "/config/gtklock/config.ini");
    str_addz(&dst, osr_mod_home());      str_addz(&dst, "/.config/gtklock/config.ini");
    if (file_exists(str_text(&src)))
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;

    /* {{WALLPAPER_PATH}} -> the installed wallpaper (cosmetic bg). Shared with
     * hyprpaper/hyprland so all three paint the same file. */
    str_reset(&src); str_reset(&dst);
    str_addz(&src, osr_mod_theme_dir()); str_addz(&src, "/config/gtklock/style.css");
    str_addz(&dst, osr_mod_home());      str_addz(&dst, "/.config/gtklock/style.css");
    if (file_exists(str_text(&src)))
        ok = osr_install_wallpaper_layer(str_text(&src), str_text(&dst)) && ok;

    /* The lockscreen avatar is seeded once - user territory afterwards. */
    str_reset(&src); str_reset(&dst);
    str_addz(&src, osr_mod_theme_dir()); str_addz(&src, "/config/gtklock/.face");
    str_addz(&dst, osr_mod_home());      str_addz(&dst, "/.face");
    if (file_exists(str_text(&src)))
        ok = osr_seed_once(str_text(&src), str_text(&dst)) && ok;

    str_free(&src); str_free(&dst);
    return ok;
}
