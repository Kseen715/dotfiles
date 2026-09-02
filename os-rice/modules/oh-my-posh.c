/* modules/oh-my-posh.c -- port of windows-rice/modules/oh-my-posh.ps1:
 * package + font + theme-owned M365Princess++.omp.json (literal only, falls
 * back to the 'osr-rice' theme's copy when the requested theme ships none of
 * its own -- same fallback Install-OhMyPosh already has), PLUS a call into
 * modules/starship.c, which is the active pwsh prompt engine.
 *
 * That call is the whole of the Starship relationship: what Starship needs
 * (package, font, ccver, the composed starship.toml) belongs to the starship
 * module and lives in its file, on both operating systems -- Install-Starship
 * only ever sat inside oh-my-posh.ps1 because the .ps1 tree had no starship
 * module of its own. Windows has no dispatch row for starship (modules.c), so
 * this is where it gets run; the profile wires the result up with `starship
 * init powershell` so the pwsh prompt looks like the zsh one.
 *
 * oh-my-posh itself is kept installed
 * and still gets its own theme file so a user can switch back to it (see the
 * commented init line in the profile); it just isn't the active engine.
 * PSReadLine's ListView prediction dropdown is untouched by any of this --
 * that's history/readline config, not prompt-engine config.
 * C89.
 */
#include "src/common.h"

#include "../lib/winpkg.h"
#include "../lib/fonts.h"
#include "../lib/theme_render.h"
#include "../lib/config_copy.h"
#include "../lib/winui.h"

#include <string.h>

#ifdef _WIN32

int osrm_oh_my_posh(const char *repo_root, const char *themes_root, const char *map_path,
                    const char *theme, int theme_only) {
    char themes_path[600];
    char dest[700];
    char layer_src[700];
    int is_temp;
    char use_theme[128];
    int ok = 1;

    if (!theme_only) {
        osr_winpkg_install(map_path, "oh-my-posh", NULL);
        osr_install_nerd_font("JetBrainsMono");
    }

    /* The prompt engine itself, and everything it needs, is modules/starship.c
     * -- the same module the POSIX side runs as `osr module starship`. It has
     * no dispatch row of its own here, so this is where Windows runs it. */
    if (!osrm_starship(repo_root, themes_root, map_path, theme, theme_only)) ok = 0;

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
        int copy_ok = osr_copy_file(layer_src, dest);
        osr_theme_layer_cleanup(layer_src, is_temp);
        if (!copy_ok) { osr_warn("oh-my-posh: could not write %s", dest); return 0; }
    }
    osr_success("oh-my-posh: themed as '%s' -> %s", use_theme, dest);
    return ok;
}

#else /* !_WIN32 */

int osrm_oh_my_posh(const char *repo_root, const char *themes_root, const char *map_path,
                    const char *theme, int theme_only) {
    (void)repo_root; (void)themes_root; (void)map_path; (void)theme; (void)theme_only;
    return 0;
}

#endif /* _WIN32 */
