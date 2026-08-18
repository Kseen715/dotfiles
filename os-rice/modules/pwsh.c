/* modules/pwsh.c -- port of windows-rice/modules/pwsh.ps1: package +
 * dotfiles-owned profile. No theme layer. C89.
 */
#include "src/common.h"

#include "../lib/winpkg.h"
#include "../lib/config_copy.h"
#include "../lib/winui.h"

#include <stddef.h>

#ifdef _WIN32

int osrm_pwsh(const char *repo_root, const char *themes_root, const char *map_path,
              const char *theme, int theme_only) {
    char profile_path[600];
    char src[700];

    (void)themes_root;
    (void)theme; /* pwsh carries no theme layer -- see osr_apply_module_theme */
    if (theme_only) return 1;

    osr_winpkg_install(map_path, "pwsh", NULL);

    if (!osrm_resolve_pwsh_profile_path(profile_path, sizeof(profile_path))) {
        osr_warn("pwsh: could not resolve pwsh's own profile path; is pwsh installed?");
        return 0;
    }

    osrm_path_join(src, sizeof(src), repo_root, "PowerShell7-profile");
    osrm_path_join(src, sizeof(src), src, "Microsoft.PowerShell_profile.ps1");

    if (!osr_copy_file(src, profile_path)) {
        osr_warn("pwsh: could not write %s", profile_path);
        return 0;
    }
    osr_success("pwsh: profile installed -> %s", profile_path);
    return 1;
}

#else /* !_WIN32 */

int osrm_pwsh(const char *repo_root, const char *themes_root, const char *map_path,
              const char *theme, int theme_only) {
    (void)repo_root; (void)themes_root; (void)map_path; (void)theme; (void)theme_only;
    return 0;
}

#endif /* _WIN32 */
