/* modules.c -- see modules.h. Dispatch only: one file per module lives in
 * modules/ next to this one. C89.
 *
 * Mapping from windows-rice/modules folder (*.ps1) to those files (each ps1
 * file's own header comment is the fuller design rationale; this is the same
 * behavior, not a redesign):
 *
 *   fastfetch.ps1   -> modules/fastfetch.c    package + theme-rendered config.jsonc
 *   wezterm.ps1     -> modules/wezterm.c      package + font + dotfiles .wezterm.lua
 *                                             + theme-rendered colors/osr-rice.toml
 *   pwsh.ps1        -> modules/pwsh.c         package + dotfiles-owned profile
 *                                             (no theme layer)
 *   oh-my-posh.ps1  -> modules/oh-my-posh.c   package + font + theme-owned
 *                                             M365Princess++.omp.json
 */
#include "modules.h"

#include "modules/src/common.h"

#include <string.h>

int osr_known_module(const char *name) {
    return strcmp(name, "fastfetch") == 0
        || strcmp(name, "wezterm") == 0
        || strcmp(name, "pwsh") == 0
        || strcmp(name, "oh-my-posh") == 0;
}

#ifdef _WIN32

static int dispatch(const char *repo_root, const char *name, const char *theme, int theme_only) {
    char os_rice_root[600];
    char themes_root[600];
    char map_path[700];

    osrm_path_join(os_rice_root, sizeof(os_rice_root), repo_root, "os-rice");
    osrm_path_join(themes_root, sizeof(themes_root), os_rice_root, "themes");
    osrm_path_join(map_path, sizeof(map_path), os_rice_root, "windows.map");

    if (strcmp(name, "fastfetch") == 0) return osrm_fastfetch(repo_root, themes_root, map_path, theme, theme_only);
    if (strcmp(name, "wezterm") == 0) return osrm_wezterm(repo_root, themes_root, map_path, theme, theme_only);
    if (strcmp(name, "pwsh") == 0) return osrm_pwsh(repo_root, themes_root, map_path, theme, theme_only);
    if (strcmp(name, "oh-my-posh") == 0) return osrm_oh_my_posh(repo_root, themes_root, map_path, theme, theme_only);

    return 0;
}

int osr_run_module(const char *repo_root, const char *name, const char *theme) {
    return dispatch(repo_root, name, theme, 0);
}

int osr_apply_module_theme(const char *repo_root, const char *name, const char *theme) {
    if (strcmp(name, "pwsh") == 0) return 1; /* no theme layer to reapply */
    return dispatch(repo_root, name, theme, 1);
}

#else /* !_WIN32 */

int osr_run_module(const char *repo_root, const char *name, const char *theme) {
    (void)repo_root;
    (void)name;
    (void)theme;
    return 0;
}

int osr_apply_module_theme(const char *repo_root, const char *name, const char *theme) {
    (void)repo_root;
    (void)name;
    (void)theme;
    return 0;
}

#endif /* _WIN32 */
