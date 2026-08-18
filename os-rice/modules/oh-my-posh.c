/* modules/oh-my-posh.c -- port of windows-rice/modules/oh-my-posh.ps1:
 * package + font + theme-owned M365Princess++.omp.json (literal only, falls
 * back to the 'osr-rice' theme's copy when the requested theme ships none of
 * its own -- same fallback Install-OhMyPosh already has), PLUS Starship --
 * the same prompt engine modules/starship.sh installs on Linux, wired into
 * PowerShell7-profile's profile.ps1 (`starship init powershell`) so the pwsh
 * prompt looks like the zsh one: same shared starship/starship.toml base,
 * same rice palette (starship/starship.palette.toml.tmpl rendered against
 * the theme's theme.list -- every theme already carries the four colors it
 * needs, see starship.sh's own header). oh-my-posh itself is kept installed
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

/* compose_starship_toml -- C port of lib/config.sh's compose_starship_config:
 * the shared base (its own trailing `[palettes.theme]` table, which starts
 * the first line matching that header, stripped) followed by the rice's
 * palette fragment. starship.toml has no include mechanism, so composition
 * is how the base/theme split (§5/§6) is realized for it -- same reason the
 * sh side does it this way. Returns 1 on success.
 */
static int compose_starship_toml(const char *base_path, const char *frag_path, const char *dest_path) {
    FILE *bfp, *ffp, *ofp;
    long bsize;
    char *btext;
    char *cut;
    char fragbuf[8192];
    size_t n;
    int ok = 1;

    bfp = fopen(base_path, "rb");
    if (bfp == NULL) return 0;
    fseek(bfp, 0, SEEK_END);
    bsize = ftell(bfp);
    fseek(bfp, 0, SEEK_SET);
    if (bsize < 0) { fclose(bfp); return 0; }
    btext = (char *)malloc((size_t)bsize + 1);
    if (btext == NULL) { fclose(bfp); return 0; }
    if (bsize > 0 && fread(btext, 1, (size_t)bsize, bfp) != (size_t)bsize) {
        fclose(bfp); free(btext); return 0;
    }
    btext[bsize] = '\0';
    fclose(bfp);

    cut = strstr(btext, "\n[palettes.theme]");
    if (cut != NULL) cut[1] = '\0'; /* keep the newline before it, drop the rest */

    ofp = fopen(dest_path, "wb");
    if (ofp == NULL) { free(btext); return 0; }
    if (fwrite(btext, 1, strlen(btext), ofp) != strlen(btext)) ok = 0;
    free(btext);

    ffp = fopen(frag_path, "rb");
    if (ffp == NULL) { fclose(ofp); return 0; }
    while ((n = fread(fragbuf, 1, sizeof(fragbuf), ffp)) > 0) {
        if (fwrite(fragbuf, 1, n, ofp) != n) { ok = 0; break; }
    }
    fclose(ffp);
    fclose(ofp);
    return ok;
}

/* install_starship -- package + Nerd Font + ~/.config/starship.toml, themed
 * with whichever rice palette `theme` resolves to. Warns and returns 0 on
 * failure but never aborts oh-my-posh's own (still-primary) install.
 */
static int install_starship(const char *repo_root, const char *themes_root,
                             const char *map_path, const char *theme, int theme_only) {
    char base[700];
    char dest[700];
    char layer_src[700];
    int is_temp;
    int ok;

    if (!theme_only) {
        osr_winpkg_install(map_path, "starship", NULL);
        osr_install_nerd_font("JetBrainsMono");
    }

    osrm_path_join(base, sizeof(base), repo_root, "starship");
    osrm_path_join(base, sizeof(base), base, "starship.toml");
    osr_expand_home("~/.config/starship.toml", dest, sizeof(dest));

    if (osr_theme_layer_source(themes_root, repo_root, "starship", "starship.palette.toml",
                                theme, layer_src, sizeof(layer_src), &is_temp)) {
        ok = compose_starship_toml(base, layer_src, dest);
        osr_theme_layer_cleanup(layer_src, is_temp);
    } else {
        /* No rice palette resolves -> install the base as-is, same fallback
         * modules/starship.sh uses for a rice that ships none. */
        ok = osr_copy_file(base, dest);
    }

    if (ok) osr_success("starship: themed as '%s' -> %s", theme, dest);
    else osr_warn("starship: could not write %s", dest);
    return ok;
}

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

    if (!install_starship(repo_root, themes_root, map_path, theme, theme_only)) ok = 0;

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
