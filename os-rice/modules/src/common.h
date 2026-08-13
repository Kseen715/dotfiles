/* modules/src/common.h -- shared internals of the Windows module implementations
 * (one file per module next to this one, see modules.h/modules.c for the
 * public surface and the ps1-to-C mapping). Not a public header: nothing
 * outside modules.c and the per-module files should include it.
 *
 * C89.
 */
#ifndef OSR_MODULES_COMMON_H
#define OSR_MODULES_COMMON_H

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

#endif /* OSR_MODULES_COMMON_H */
