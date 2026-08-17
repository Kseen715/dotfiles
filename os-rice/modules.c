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
 *                                             M365Princess++.omp.json, PLUS
 *                                             Starship (package + font +
 *                                             composed starship.toml, same
 *                                             as modules/starship.sh on
 *                                             Linux) -- the active pwsh
 *                                             prompt engine; oh-my-posh's
 *                                             own theme is kept as a
 *                                             switch-back-able fallback.
 *
 * And from the also-retired windows-11-x86_64/ tree (the OS debloat/tweak
 * side, which never had a C tier at all) to modules/windows/ -- grouped in
 * their own folder because they are not app modules: no package, no font,
 * no config, no theme layer, just one OS-level pass each:
 *
 *   setup.ps1 +           -> modules/windows/tweaks.c   12 registry rows + 7
 *   microscripts/reg-*.ps1                            service rows, as two
 *   microscripts/disable-*.ps1                        tables driving
 *   src/common.ps1                                    lib/wintweak.c's three
 *                                                     verbs
 *   win-update.ps1 +      -> modules/windows/update.c   trigger an update run
 *   microscripts/update-windows.ps1
 *
 *   winutils.ps1 +        -> modules/windows/debloat.c  the two third-party
 *   microscripts/raphire-win11debloat.ps1             vendor scripts, opt-in
 *
 * src/common.ps1's other half needed no port at all: its EchoInfo/
 * EchoWarning/EchoError/InvokeEcho are lib/ui.c, and its Test-IsElevated/
 * Invoke-ElevatedScript are lib/elevate.c, both of which already existed.
 */
#include "modules.h"

#include "modules/src/common.h"

#include <string.h>

int osr_known_module(const char *name) {
    return strcmp(name, "fastfetch") == 0
        || strcmp(name, "wezterm") == 0
        || strcmp(name, "pwsh") == 0
        || strcmp(name, "oh-my-posh") == 0
        || strcmp(name, "win-tweaks") == 0
        || strcmp(name, "win-update") == 0
        || strcmp(name, "win-debloat") == 0
        || strcmp(name, "win-winutil") == 0;
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

    if (strcmp(name, "win-tweaks") == 0) return osrm_win_tweaks(repo_root, themes_root, map_path, theme, theme_only);
    if (strcmp(name, "win-update") == 0) return osrm_win_update(repo_root, themes_root, map_path, theme, theme_only);
    if (strcmp(name, "win-debloat") == 0) return osrm_win_debloat(repo_root, themes_root, map_path, theme, theme_only);
    if (strcmp(name, "win-winutil") == 0) return osrm_win_winutil(repo_root, themes_root, map_path, theme, theme_only);

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
