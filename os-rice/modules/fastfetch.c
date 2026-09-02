/* modules/fastfetch.c -- fastfetch: install the package, then paint the one
 * config.jsonc it reads.
 *
 * ONE FUNCTION, and almost all of it shared. That is the point of the layout
 * rule -- modules/<name>.c, never modules/<os>/<name>.c -- and now that
 * lib/module.h has a body on both systems it is what a module both can have
 * actually looks like: the package comes from osr_pkg_install_step whichever
 * map is answering, the theme layer from osr_install_theme_layer whichever
 * resolver is behind it, and the only #ifdef in the file is the one place the
 * two genuinely disagree.
 *
 * The module is short because the program is: the package is whatever the
 * platform's map resolves `fastfetch` to (a bare passthrough on
 * arch/fedora/void/alpine/gentoo, the official prebuilt .deb on Debian/Ubuntu
 * where it is packaged only on very recent releases, scoop's package on
 * Windows), and fastfetch reads exactly ONE config file, so the theme owns the
 * whole installed file -- it is nothing but presentation (section 5).
 *
 * WHERE THEY DIFFER is the fallback when the current theme ships no version of
 * that file. POSIX installs the dotfiles base file, matching install_layer in
 * the .sh. Windows leaves fastfetch's own built-in default and warns, matching
 * Install-Fastfetch -- because the dotfiles tree it reads has no base
 * config.jsonc to fall back TO, only the .tmpl every theme is rendered from.
 * That is a difference in what exists, not a difference of opinion, which is
 * why it is the one thing the compiler picks between.
 *
 * https://github.com/fastfetch-cli/fastfetch
 *
 * C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_fastfetch(void) {
    static const char *const pkgs[] = { "fastfetch", NULL };
    Str dst;
    int ok;

    ok = osr_pkg_install_step("Installing fastfetch", pkgs);

    str_init(&dst);
    str_addz(&dst, osr_mod_home());
    str_addz(&dst, "/.config/fastfetch/config.jsonc");

    if (!osr_install_theme_layer("fastfetch", "config.jsonc", str_text(&dst))) {
#ifndef _WIN32
        /* No theme version of it: the dotfiles base is the fallback. */
        Str fallback;
        str_init(&fallback);
        str_addz(&fallback, osr_mod_dotfiles());
        str_addz(&fallback, "/fastfetch/config.jsonc");
        if (file_exists(str_text(&fallback))) {
            osr_install_layer(str_text(&fallback), str_text(&dst));
        }
        str_free(&fallback);
#else
        osr_warnf("fastfetch: theme '%s' renders no config.jsonc; leaving "
                  "fastfetch's own default", osr_mod_theme());
#endif
    }
    str_free(&dst);
    return ok;
}
