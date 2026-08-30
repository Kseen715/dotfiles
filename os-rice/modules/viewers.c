/* modules/viewers.c -- the small openers ~/.config/mimeapps.list points at
 * (modules/xdg.sh seeds that file). Without them "Open With" names applications
 * that are not installed, which is worse than an empty menu: the double-click
 * does nothing and no error is shown.
 *
 * zathura + zathura-pdf-mupdf   PDF, keyboard-driven, themed from a plain rc
 * nsxiv                         images; reads its colors from X resources, so
 * modules/theming.sh already themed it
 * imv                           the Wayland-capable image viewer
 * mpv                           video/audio, and what celluloid wraps
 *
 * zathura is the only one with a config file worth owning: its rc is a flat
 * `set option value` list, so the rice ships the color half as a layer that the
 * base rc includes.
 *
 * Was modules/viewers.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_viewers(void) {
    static const char *const pkgs[] = {
        "zathura", "zathura-pdf-mupdf", "nsxiv", "imv", "mpv", NULL
    };
    /* app, the dotfiles file, the theme drop-in, where each lands. */
    static const char *const layers[] = {
        "zathura", "/zathura/zathurarc", "90-theme.rc",
        "/.config/zathura/zathurarc", "/.config/zathura/90-theme.rc",
        "mpv", "/mpv/mpv.conf", "90-theme.conf",
        "/.config/mpv/mpv.conf", "/.config/mpv/90-theme.conf",
        NULL
    };
    Str src, dst;
    size_t i;
    int ok;

    ok = osr_pkg_install_step("Installing document + media viewers", pkgs);

    str_init(&src); str_init(&dst);
    for (i = 0; layers[i] != NULL; i += 5) {
        str_reset(&src); str_reset(&dst);
        str_addz(&src, osr_mod_dotfiles()); str_addz(&src, layers[i + 1]);
        str_addz(&dst, osr_mod_home());     str_addz(&dst, layers[i + 3]);
        if (file_exists(str_text(&src)))
            ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
        str_reset(&dst);
        str_addz(&dst, osr_mod_home()); str_addz(&dst, layers[i + 4]);
        (void)osr_install_theme_layer(layers[i], layers[i + 2], str_text(&dst));
    }
    str_free(&src); str_free(&dst);
    return ok;
}
