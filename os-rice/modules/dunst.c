/* modules/dunst.c -- dunst notification daemon, the X11 replacement for mako
 * (i3-sugg §2). Config split (§5) uses dunst's own drop-in dir: the base dunstrc
 * is dotfiles-owned (geometry, behaviour, mouse actions) and the rice drops
 * ~/.config/dunst/dunstrc.d/90-theme.conf on top (colors, font, frame), which
 * dunst merges in lexical order after the main file.
 *
 * Was modules/dunst.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_dunst(void) {
    static const char *const pkgs[] = { "dunst", "libnotify", NULL };
    Str src, dst;
    int ok;

    ok = osr_pkg_install_step("Installing dunst", pkgs);

    /* Two layers, in order: the dotfiles base dunstrc, then the theme's drop-in
     * beside it. dunst reads dunstrc.d/*.conf after dunstrc, so the palette can
     * be a separate file and the base one stays the rice's. */
    str_init(&src); str_init(&dst);
    str_addz(&src, osr_mod_dotfiles());
    str_addz(&src, "/dunst/dunstrc");
    str_addz(&dst, osr_mod_home());
    str_addz(&dst, "/.config/dunst/dunstrc");
    if (file_exists(str_text(&src)))
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
    str_reset(&dst);
    str_addz(&dst, osr_mod_home());
    str_addz(&dst, "/.config/dunst/dunstrc.d/90-theme.conf");
    (void)osr_install_theme_layer("dunst", "90-theme.conf", str_text(&dst));
    str_free(&src); str_free(&dst);
    return ok;
}
