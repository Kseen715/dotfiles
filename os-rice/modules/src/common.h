/* modules/src/common.h -- shared internals of the Windows module implementations
 * (one file per module next to this one, plus the windows/ group, see
 * modules.h/modules.c for the public surface and the ps1-to-C mapping).
 * Not a public header: nothing outside modules.c, the per-module files and
 * the unit tests that inspect the windows tweak tables should include it.
 *
 * C89.
 */
#ifndef OSR_MODULES_COMMON_H
#define OSR_MODULES_COMMON_H

#include "../../lib/wintweak.h"

void osrm_path_join(char *out, unsigned long out_sz, const char *a, const char *b);
void osrm_copy_bounded(char *dst, unsigned long dst_sz, const char *src);

#ifdef _WIN32
/* osrm_capture_command_output -- run cmd, trim trailing CR/LF, return 1 if
 * anything came back. Used for the two answers only the installed tool
 * itself can give correctly (pwsh's own profile path; scoop's own prefix
 * for a package) -- see Resolve-PwshProfilePath / Resolve-PoshThemesPath's
 * own comments for why these must not be guessed.
 */
int osrm_capture_command_output(const char *cmd, char *out, unsigned long out_sz);

/* osrm_resolve_pwsh_profile_path -- C port of pwsh.ps1's
 * Resolve-PwshProfilePath: asked FROM pwsh itself, never assembled from
 * %USERPROFILE%\Documents -- a redirected/OneDrive-moved Documents folder
 * makes those disagree, silently. 0 if pwsh is not installed/answers nothing.
 */
int osrm_resolve_pwsh_profile_path(char *out, unsigned long out_sz);

/* osrm_resolve_posh_themes_path -- C port of oh-my-posh.ps1's
 * Resolve-PoshThemesPath: POSH_THEMES_PATH env var, else scoop's own prefix
 * for the package, else the installed binary's own directory. 0 if nothing
 * resolves to a directory that exists.
 */
int osrm_resolve_posh_themes_path(char *out, unsigned long out_sz);
#endif

/* One entry point per module, all with the same signature so modules.c's
 * dispatch stays a plain table of calls. theme_only skips every install
 * verb (package, font, dotfiles-owned config) and reinstalls just the
 * theme-owned layer. themes_root is unused by modules that carry no theme
 * layer (pwsh). Each returns 1 on success, 0 after warning.
 */
int osrm_fastfetch(const char *repo_root, const char *themes_root, const char *map_path,
                   const char *theme, int theme_only);
int osrm_wezterm(const char *repo_root, const char *themes_root, const char *map_path,
                 const char *theme, int theme_only);
int osrm_pwsh(const char *repo_root, const char *themes_root, const char *map_path,
              const char *theme, int theme_only);
int osrm_oh_my_posh(const char *repo_root, const char *themes_root, const char *map_path,
                    const char *theme, int theme_only);

/* The windows/ group -- OS-level passes over a Windows machine rather than
 * app modules: no package, no font, no config file, and so no theme layer
 * either (all three ignore themes_root/theme and treat theme_only as a
 * successful no-op). Ported from the retired windows-11-x86_64/ ps1 tree;
 * see modules/windows/tweaks.c's header for the file-by-file mapping.
 */
int osrm_win_tweaks(const char *repo_root, const char *themes_root, const char *map_path,
                      const char *theme, int theme_only);
int osrm_win_update(const char *repo_root, const char *themes_root, const char *map_path,
                      const char *theme, int theme_only);
int osrm_win_debloat(const char *repo_root, const char *themes_root, const char *map_path,
                       const char *theme, int theme_only);
int osrm_win_winutil(const char *repo_root, const char *themes_root, const char *map_path,
                       const char *theme, int theme_only);

/* The two policy tables behind osrm_win_tweaks, exposed so they can be
 * asserted on without applying anything -- see test/unit_c/wintweak_test.c.
 * They are the whole of what setup.ps1 used to say, so a test that reads
 * them is testing the port itself, and it is the only part of this module
 * that CAN be tested: every other line changes the machine it runs on.
 */
const osr_wintweak_reg *osrm_win_reg_tweaks(unsigned long *count);
const osr_wintweak_service *osrm_win_service_tweaks(unsigned long *count);

#endif /* OSR_MODULES_COMMON_H */
