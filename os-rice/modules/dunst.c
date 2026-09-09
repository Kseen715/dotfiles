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
     * beside it. dunst reads the *.conf files in dunstrc.d/ after dunstrc, so
     * the palette can
     * be a separate file and the base one stays the rice's. */
    str_initv(&src, &dst, (Str *)NULL);
    str_addzz(&src, osr_mod_dotfiles(), "/dunst/dunstrc", (const char *)NULL);
    str_addzz(&dst, osr_mod_home(), "/.config/dunst/dunstrc", (const char *)NULL);
    if (file_exists(str_text(&src)))
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
    str_setz(&dst, osr_mod_home(), "/.config/dunst/dunstrc.d/90-theme.conf", (const char *)NULL);
    (void)osr_install_theme_layer("dunst", "90-theme.conf", str_text(&dst));
    str_freev(&src, &dst, (Str *)NULL);
    return ok;
}
