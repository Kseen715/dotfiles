/* modules/oh-my-posh.c -- port of windows-rice/modules/oh-my-posh.ps1:
 * package + font + theme-owned M365Princess++.omp.json (literal only, falls
 * back to the 'osr-rice' theme's copy when the requested theme ships none of
 * its own -- same fallback Install-OhMyPosh already has). C89.
 */
#include "src/common.h"

#include "../lib/winpkg.h"
#include "../lib/fonts.h"
#include "../lib/theme_render.h"
#include "../lib/config_copy.h"
#include "../lib/ui.h"

#include <string.h>

#ifdef _WIN32

int osrm_oh_my_posh(const char *repo_root, const char *themes_root, const char *map_path,
                    const char *theme, int theme_only) {
    char themes_path[600];
    char dest[700];
    char layer_src[700];
    int is_temp;
    char use_theme[128];

    if (!theme_only) {
        osr_winpkg_install(map_path, "oh-my-posh", NULL);
        osr_install_nerd_font("JetBrainsMono");
    }

    if (!osrm_resolve_posh_themes_path(themes_path, sizeof(themes_path))) {
        osr_warn("oh-my-posh: could not resolve oh-my-posh's themes directory; is oh-my-posh installed?");
        return 0;
    }

    osrm_copy_bounded(use_theme, sizeof(use_theme), theme);
    if (!osr_theme_layer_source(themes_root, repo_root, "oh-my-posh", "M365Princess++.omp.json",
                                 use_theme, layer_src, sizeof(layer_src), &is_temp)) {
        if (strcmp(use_theme, "osr-rice") == 0) {
            osr_warn("oh-my-posh: theme 'osr-rice' ships no oh-my-posh config (themes/osr-rice/config/oh-my-posh/)");
            return 0;
        }
        osr_warn("oh-my-posh: theme '%s' ships no oh-my-posh config; using 'osr-rice' -- the only prompt defined so far", use_theme);
        osrm_copy_bounded(use_theme, sizeof(use_theme), "osr-rice");
        if (!osr_theme_layer_source(themes_root, repo_root, "oh-my-posh", "M365Princess++.omp.json",
                                     use_theme, layer_src, sizeof(layer_src), &is_temp)) {
            osr_warn("oh-my-posh: theme 'osr-rice' ships no oh-my-posh config either");
            return 0;
        }
    }

    osrm_path_join(dest, sizeof(dest), themes_path, "M365Princess++.omp.json");
    {
        int ok = osr_copy_file(layer_src, dest);
        osr_theme_layer_cleanup(layer_src, is_temp);
        if (!ok) { osr_warn("oh-my-posh: could not write %s", dest); return 0; }
    }
    osr_success("oh-my-posh: themed as '%s' -> %s", use_theme, dest);
    return 1;
}

#else /* !_WIN32 */

int osrm_oh_my_posh(const char *repo_root, const char *themes_root, const char *map_path,
                    const char *theme, int theme_only) {
    (void)repo_root; (void)themes_root; (void)map_path; (void)theme; (void)theme_only;
    return 0;
}

#endif /* _WIN32 */
