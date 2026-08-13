/* modules/fastfetch.c -- port of windows-rice/modules/fastfetch.ps1:
 * package + theme-rendered config.jsonc. C89.
 */
#include "src/common.h"

#include "../lib/winpkg.h"
#include "../lib/theme_render.h"
#include "../lib/config_copy.h"
#include "../lib/ui.h"

#ifdef _WIN32

int osrm_fastfetch(const char *repo_root, const char *themes_root, const char *map_path,
                   const char *theme, int theme_only) {
    char dest[600];
    char layer_src[700];
    int is_temp;

    if (!theme_only) osr_winpkg_install(map_path, "fastfetch", NULL);

    osr_expand_home("~/.config/fastfetch/config.jsonc", dest, sizeof(dest));
    if (osr_theme_layer_source(themes_root, repo_root, "fastfetch", "config.jsonc", theme,
                                layer_src, sizeof(layer_src), &is_temp)) {
        int ok = osr_copy_file(layer_src, dest);
        osr_theme_layer_cleanup(layer_src, is_temp);
        if (ok) { osr_success("fastfetch: config.jsonc themed as '%s'", theme); return 1; }
        osr_warn("fastfetch: could not write %s", dest);
        return 0;
    }
    osr_warn("fastfetch: no config.jsonc.tmpl or theme '%s'; leaving fastfetch's own default", theme);
    return 1; /* matches Install-Fastfetch: a missing theme layer warns, not fails */
}

#else /* !_WIN32 */

int osrm_fastfetch(const char *repo_root, const char *themes_root, const char *map_path,
                   const char *theme, int theme_only) {
    (void)repo_root; (void)themes_root; (void)map_path; (void)theme; (void)theme_only;
    return 0;
}

#endif /* _WIN32 */
