/* modules/fcitx5.c -- input method for CJK, Cyrillic and anything else that
 * needs composition (i3-sugg §5). fcitx5 over ibus: lighter, better Wayland
 * support, and its Qt/GTK bridges are separate packages you can pick.
 *
 * The three toolkit bridges are what make it actually work — without
 * fcitx5-gtk/fcitx5-qt an app falls back to raw XIM and you get no candidate
 * window. The env vars matter just as much and live in the session layer
 * (~/.config/xprofile.d/10-session.sh): GTK_IM_MODULE, QT_IM_MODULE, XMODIFIERS.
 *
 * Engines are opt-in per language; the three below cover Japanese, Chinese and
 * Korean. `ibus` (+ ibus-anthy) is the packaged alternative — never run both.
 * Emoji picking without an IME switch: rofimoji drives the rofi launcher this
 * rice already ships, so Super+. gets emoji with no extra daemon.
 * Candidate-window theme (theme-owned, §6b). An input method draws its own popup,
 * so without this it is stock grey over a themed desktop - the one piece of UI no
 * GTK/Qt theme reaches.
 *
 * Was modules/fcitx5.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_fcitx5(void) {
    static const char *const base[]    = {
        "fcitx5", "fcitx5-gtk", "fcitx5-qt", "fcitx5-configtool", NULL
    };
    static const char *const engines[] = {
        "fcitx5-mozc", "fcitx5-chinese-addons", "fcitx5-hangul", NULL
    };
    static const char *const emoji[]   = { "rofimoji", NULL };
    Str dst;
    int ok;

    ok = osr_pkg_install_step("Installing fcitx5", base);
    ok = osr_pkg_install_step("Installing fcitx5 engines", engines) && ok;
    ok = osr_pkg_install_step("Installing emoji picker", emoji) && ok;

    /* Two files in two different trees: the theme is data under
     * .local/share, the classicui conf that selects it is config. */
    str_init(&dst);
    str_addz(&dst, osr_mod_home());
    str_addz(&dst, "/.local/share/fcitx5/themes/osr/theme.conf");
    (void)osr_install_theme_layer("fcitx5", "theme.conf", str_text(&dst));
    str_reset(&dst);
    str_addz(&dst, osr_mod_home());
    str_addz(&dst, "/.config/fcitx5/conf/classicui.conf");
    (void)osr_install_theme_layer("fcitx5", "classicui.conf", str_text(&dst));
    str_free(&dst);
    return ok;
}
