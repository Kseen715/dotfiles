/* modules/wezterm.c -- WezTerm: package, the Nerd Font its glyphs need, the
 * dotfiles-owned .wezterm.lua, and the theme-owned palette.
 *
 * ONE FUNCTION. What used to be two -- a port of wezterm.ps1 above an #ifdef
 * and a port of wezterm.sh below it -- is one body, because the two were
 * already doing the same four things in the same order and only spelled them
 * differently.
 *
 * WHERE THE PACKAGE COMES FROM is the map's business, not this file's, and the
 * two maps say different things on purpose. Every POSIX target builds WezTerm
 * from source (lib/pkgmap/any.map -> source:provide_wezterm, upstream's
 * documented route; there is no AppImage or flatpak path), so a rice must list
 * `rust` before `wezterm` -- manifest order is the dependency graph (section
 * 4). Windows takes winget's build on x86_64 and reaches the same builder only
 * on arm64, where upstream publishes nothing at all. Neither of those
 * decisions is visible here, which is the whole value of the map.
 *
 * CONFIG IS SPLIT BY OWNERSHIP (section 5), the same shape as foot and ghostty:
 *
 *   .wezterm.lua           dotfiles-owned (10-layer) -- overwritten on update
 *   colors/osr-rice.toml   theme-owned palette (90-layer) -- swapped on a
 *                          rice or theme switch (section 6), falling back to
 *                          the dotfiles default when a theme ships none
 *
 * The base .wezterm.lua selects `color_scheme = "osr-rice"` when that file is
 * present, so the palette swaps independently of the base -- the section 5
 * split applied to a DE config through WezTerm's own custom-color-scheme
 * directory, since its config is Lua and has no include directive the way
 * foot.ini does.
 *
 * What it must install and configure is stated in test/unit_c/terminals_test.c.
 *
 * C89.
 */
#include "../lib/module.h"
#include "../lib/fonts.h"

#include <stddef.h>

static int nerd_font(void *ctx) { return osr_install_nerd_font((const char *)ctx); }

int osrm_wezterm(void) {
    /* unzip and fontconfig are the font install's own dependencies on POSIX;
     * on Windows the font goes through a package manager that brings its own,
     * and the map has no row for either -- so the list is trimmed there rather
     * than asking for two packages that do not exist. */
#ifndef _WIN32
    static const char *const pkgs[] = { "wezterm", "unzip", "fontconfig", NULL };
#else
    static const char *const pkgs[] = { "wezterm", NULL };
#endif
    Str src, dst;
    int ok;

    ok = osr_pkg_install_step("Installing WezTerm", pkgs);
    ok = osr_step("Installing JetBrains Mono Nerd Font", nerd_font,
                  (void *)"JetBrainsMono") && ok;

    str_init(&src); str_init(&dst);
    str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/wezterm/.wezterm.lua");
    str_addz(&dst, osr_mod_home());     str_addz(&dst, "/.wezterm.lua");
    if (file_exists(str_text(&src)))
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;

    /* Palette. The theme's version wins; the dotfiles default covers a theme
     * that ships none. In --module mode OSR_THEME_DIR is whatever the theme
     * picker resolved (section 6). */
    str_reset(&dst);
    str_addz(&dst, osr_mod_home());
    str_addz(&dst, "/.config/wezterm/colors/osr-rice.toml");
    if (!osr_install_theme_layer("wezterm", "wezterm-theme.toml", str_text(&dst))) {
        str_reset(&src);
        str_addz(&src, osr_mod_dotfiles());
        str_addz(&src, "/wezterm/wezterm-theme.toml");
        if (file_exists(str_text(&src)))
            ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
    }
    str_free(&src); str_free(&dst);

    if (ok) osr_successf("wezterm: themed as '%s'", osr_mod_theme());
    return ok;
}
