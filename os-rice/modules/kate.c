/* modules/kate.c -- Kate text editor + dotfiles config. ONE copy, POSIX
 * (was .../apps/kate.sh). katerc is dotfiles-owned config (§5).
 * KDE/Qt palette (theme-owned, §6b). Kate reads the color scheme every KDE app
 * reads, so this file is what stops Kate being stock Breeze on a themed desktop;
 * the Konsole scheme rides along because it is the same 16 colors in the same
 * format, and lands harmlessly when Konsole is not installed.
 * The SAME file again as ~/.config/kdeglobals, which is the palette KDE apps read
 * when nothing selected a scheme for them. Off Plasma there is no Colors KCM to
 * do the selecting, and a scheme sitting unselected in color-schemes/ changes
 * nothing - this second copy is what actually colors Kate, Dolphin and Okular
 * here. It also covers every KDE app that has no katerc-style setting of its own.
 * Pre-rename copy: the scheme used to be osr.colors with a per-theme Name, which
 * no static katerc could select. Left behind it just clutters the picker.
 * The text area is a SECOND theme system (KSyntaxHighlighting), independent of
 * the Qt palette above: without this file Kate paints the chrome in the rice's
 * colors and the code in stock Breeze. katerc selects both by the same name,
 * `os-rice`: the KDE scheme's file id, this one's metadata name.
 *
 * Port of modules/kate.sh, kept as the reference at
 * test/ref/kate_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <unistd.h>

int osrm_kate(void) {
    static const char *const pkgs[] = { "kate", NULL };
    Str src, dst;
    int ok;

    ok = osr_pkg_install_step("Installing Kate", pkgs);

    str_init(&src); str_init(&dst);
    str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/kate/katerc");
    str_addz(&dst, osr_mod_home());     str_addz(&dst, "/.config/katerc");
    if (file_exists(str_text(&src)))
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;

    /* The SAME colour scheme file, twice: KDE reads the palette from the
     * scheme in .local/share and the ACTIVE colours from kdeglobals, and an
     * app started outside a Plasma session only ever consults the latter. */
    str_reset(&dst);
    str_addz(&dst, osr_mod_home());
    str_addz(&dst, "/.local/share/color-schemes/os-rice.colors");
    (void)osr_install_theme_layer("kde", "color-scheme.colors", str_text(&dst));
    str_reset(&dst);
    str_addz(&dst, osr_mod_home()); str_addz(&dst, "/.config/kdeglobals");
    (void)osr_install_theme_layer("kde", "color-scheme.colors", str_text(&dst));

    /* An earlier name for the same file; left behind it shows up as a second,
     * stale entry in KDE's scheme picker. */
    str_reset(&dst);
    str_addz(&dst, osr_mod_home());
    str_addz(&dst, "/.local/share/color-schemes/osr.colors");
    (void)unlink(str_text(&dst));
    str_addz(&dst, ".bak");
    (void)unlink(str_text(&dst));

    str_reset(&dst);
    str_addz(&dst, osr_mod_home());
    str_addz(&dst, "/.local/share/konsole/osr.colorscheme");
    (void)osr_install_theme_layer("konsole", "osr.colorscheme", str_text(&dst));
    str_reset(&dst);
    str_addz(&dst, osr_mod_home());
    str_addz(&dst, "/.local/share/org.kde.syntax-highlighting/themes/osr.theme");
    (void)osr_install_theme_layer("kate", "osr.theme", str_text(&dst));

    str_free(&src); str_free(&dst);
    return ok;
}
