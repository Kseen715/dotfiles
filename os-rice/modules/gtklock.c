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

    str_initv(&src, &dst, (Str *)NULL);
    str_addzz(&src, osr_mod_theme_dir(), "/config/gtklock/config.ini", (const char *)NULL);
    str_addzz(&dst, osr_mod_home(), "/.config/gtklock/config.ini", (const char *)NULL);
    if (file_exists(str_text(&src)))
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;

    /* {{WALLPAPER_PATH}} -> the installed wallpaper (cosmetic bg). Shared with
     * hyprpaper/hyprland so all three paint the same file. */
    str_reset(&src);
    str_reset(&dst);
    str_addzz(&src, osr_mod_theme_dir(), "/config/gtklock/style.css", (const char *)NULL);
    str_addzz(&dst, osr_mod_home(), "/.config/gtklock/style.css", (const char *)NULL);
    if (file_exists(str_text(&src)))
        ok = osr_install_wallpaper_layer(str_text(&src), str_text(&dst)) && ok;

    /* The lockscreen avatar is seeded once - user territory afterwards. */
    str_reset(&src);
    str_reset(&dst);
    str_addzz(&src, osr_mod_theme_dir(), "/config/gtklock/.face", (const char *)NULL);
    str_addzz(&dst, osr_mod_home(), "/.face", (const char *)NULL);
    if (file_exists(str_text(&src)))
        ok = osr_seed_once(str_text(&src), str_text(&dst)) && ok;

    str_freev(&src, &dst, (Str *)NULL);
    return ok;
}
