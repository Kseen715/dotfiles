/* modules/hyprpaper.c -- hyprpaper wallpaper daemon + config. ONE copy, POSIX
 * (was .../modules/hyprpaper.sh). Setting the live wallpaper is apply_wallpaper's
 * job (§6); this module installs the daemon and its rice-owned config.
 *
 * hyprpaper.conf's `preload`/`wallpaper` need a real path, and hyprpaper does NOT
 * read the session's env - the legacy config's bare `$WALLPAPER_PATH` resolved
 * against nothing. It is a {{WALLPAPER_PATH}} placeholder now, filled by
 * install_wallpaper_layer with the same installed path gtklock and hyprland's
 * `env =` line get, so the daemon, the locker and the session agree on one file.
 *
 * Port of modules/hyprpaper.sh, kept as the reference at
 * test/ref/hyprpaper_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"

#include <stddef.h>

int osrm_hyprpaper(void) {
    static const char *const pkgs[] = { "hyprpaper", NULL };
    Str src, dst;
    int ok;

    ok = osr_pkg_install_step("Installing hyprpaper", pkgs);
    /* The wallpaper layer, not a plain one: hyprpaper.conf names the image, so
     * {{WALLPAPER_PATH}} has to be filled in as it is installed. */
    if (*osr_mod_theme_dir() != '\0') {
        str_init(&src); str_init(&dst);
        str_addz(&src, osr_mod_theme_dir());
        str_addz(&src, "/config/hypr/hyprpaper.conf");
        str_addz(&dst, osr_mod_home());
        str_addz(&dst, "/.config/hypr/hyprpaper.conf");
        if (file_exists(str_text(&src)))
            ok = osr_install_wallpaper_layer(str_text(&src), str_text(&dst)) && ok;
        str_free(&src); str_free(&dst);
    }
    return ok;
}
