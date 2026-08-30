/* modules/wezterm.c -- port of windows-rice/modules/wezterm.ps1: package +
 * font + dotfiles-owned .wezterm.lua + theme-rendered colors/osr-rice.toml.
 * C89.
 */
#ifdef _WIN32

#include "src/common.h"

#include "../lib/winpkg.h"
#include "../lib/fonts.h"
#include "../lib/theme_render.h"
#include "../lib/config_copy.h"
#include "../lib/winui.h"

#include <stddef.h>

int osrm_wezterm(const char *repo_root, const char *themes_root, const char *map_path,
                 const char *theme, int theme_only) {
    char dotfiles_dir[600];
    char dest_theme[600];
    char layer_src[700];
    int is_temp;
    int ok = 1;

    osrm_path_join(dotfiles_dir, sizeof(dotfiles_dir), repo_root, "wezterm");

    if (!theme_only) {
        char src_lua[700];
        char dest_lua[600];
        osr_winpkg_install(map_path, "wezterm", NULL);
        osr_install_nerd_font("JetBrainsMono");

        osrm_path_join(src_lua, sizeof(src_lua), dotfiles_dir, ".wezterm.lua");
        osr_expand_home("~/.wezterm.lua", dest_lua, sizeof(dest_lua));
        if (!osr_copy_file(src_lua, dest_lua)) { osr_warn("wezterm: could not write %s", dest_lua); ok = 0; }
    }

    osr_expand_home("~/.config/wezterm/colors/osr-rice.toml", dest_theme, sizeof(dest_theme));
    if (osr_theme_layer_source(themes_root, repo_root, "wezterm", "wezterm-theme.toml", theme,
                                layer_src, sizeof(layer_src), &is_temp)) {
        if (!osr_copy_file(layer_src, dest_theme)) ok = 0;
        osr_theme_layer_cleanup(layer_src, is_temp);
    } else {
        /* Linux's own dotfiles-level default, same fallback wezterm.ps1 uses
         * when no theme.list resolves at all. */
        char fallback[700];
        osrm_path_join(fallback, sizeof(fallback), dotfiles_dir, "wezterm-theme.toml");
        if (!osr_copy_file(fallback, dest_theme)) ok = 0;
    }

    if (ok) osr_success("wezterm: themed as '%s'", theme);
    else osr_warn("wezterm: one or more config files failed to write");
    return ok;
}

#else /* !_WIN32 */

/* The POSIX branch, once modules/wezterm.sh. What it must install and
 * configure is stated in test/unit_c/terminals_test.c.
 *
 * WezTerm is BUILT FROM SOURCE on every target (any.map -> source:provide_wezterm,
 * upstream's documented route); there is no AppImage/flatpak path. The build
 * needs a Rust toolchain, so list `rust` before `wezterm` in a rice (manifest
 * order is the dependency graph, §4).
 *
 * Config is split by ownership (§5), same shape as foot/ghostty:
 *
 *   .wezterm.lua           dotfiles-owned (10-layer) -- overwritten on update
 *   colors/osr-rice.toml   rice-owned palette (90-layer) -- swapped on rice
 *                          switch (§6), falling back to the dotfiles default
 *                          when a rice ships none
 *
 * The base .wezterm.lua selects `color_scheme = "osr-rice"` when that file is
 * present, so the palette swaps independently of the base -- the §5 split
 * applied to a DE config, via WezTerm's own custom-color-scheme directory (its
 * config is Lua and has no include directive like foot.ini).
 */
#include "../lib/module.h"
#include "../lib/nerdfont.h"

static int nerd_font(void *ctx) { return osr_install_nerd_font((const char *)ctx); }

int osrm_wezterm(void) {
    static const char *const pkgs[] = { "wezterm", "unzip", "fontconfig", NULL };
    Str src, dst;
    int ok;

    ok = osr_pkg_install_step("Installing WezTerm (source build)", pkgs);
    ok = osr_step("Installing JetBrains Mono Nerd Font", nerd_font,
                  (void *)"JetBrainsMono") && ok;

    str_init(&src); str_init(&dst);
    str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/wezterm/.wezterm.lua");
    str_addz(&dst, osr_mod_home());     str_addz(&dst, "/.wezterm.lua");
    if (file_exists(str_text(&src)))
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;

    /* Palette. The rice override wins; the dotfiles default covers a rice that
     * ships none. In --module mode OSR_THEME_DIR is whatever rice the theme
     * picker resolved (§6). */
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
    return ok;
}

#endif /* _WIN32 */
