/* modules/wezterm.c -- port of windows-rice/modules/wezterm.ps1: package +
 * font + dotfiles-owned .wezterm.lua + theme-rendered colors/osr-rice.toml.
 * C89.
 */
#include "src/common.h"

#include "../lib/winpkg.h"
#include "../lib/fonts.h"
#include "../lib/theme_render.h"
#include "../lib/config_copy.h"
#include "../lib/winui.h"

#include <stddef.h>

#ifdef _WIN32

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

int osrm_wezterm(const char *repo_root, const char *themes_root, const char *map_path,
                 const char *theme, int theme_only) {
    (void)repo_root; (void)themes_root; (void)map_path; (void)theme; (void)theme_only;
    return 0;
}

#endif /* _WIN32 */
