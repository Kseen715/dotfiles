/* modules.c -- see modules.h. C89.
 *
 * Mapping from windows-rice/modules folder (*.ps1) to the functions below (each
 * ps1 file's own header comment is the fuller design rationale; this is
 * the same behavior, not a redesign):
 *
 *   fastfetch.ps1   -> module_fastfetch    package + theme-rendered config.jsonc
 *   wezterm.ps1     -> module_wezterm      package + font + dotfiles .wezterm.lua
 *                                          + theme-rendered colors/osr-rice.toml
 *   pwsh.ps1        -> module_pwsh         package + dotfiles-owned profile
 *                                          (no theme layer)
 *   oh-my-posh.ps1  -> module_oh_my_posh   package + font + theme-owned
 *                                          M365Princess++.omp.json (literal only,
 *                                          falls back to the 'osr-rice' theme's
 *                                          copy when the requested theme ships
 *                                          none of its own -- same fallback
 *                                          Install-OhMyPosh already has)
 */
#include "modules.h"

#include "lib/winpkg.h"
#include "lib/fonts.h"
#include "lib/theme_render.h"
#include "lib/config_copy.h"
#include "lib/ui.h"

#include <stdio.h>
#include <string.h>

static void path_join(char *out, unsigned long out_sz, const char *a, const char *b) {
    unsigned long len_a = (unsigned long)strlen(a);
    unsigned long len_b = (unsigned long)strlen(b);
    unsigned long need;
    int has_sep = (len_a > 0 && (a[len_a - 1] == '/' || a[len_a - 1] == '\\'));

    need = len_a + (has_sep ? 0 : 1) + len_b;
    if (out_sz == 0) return;
    if (need >= out_sz) { out[0] = '\0'; return; }

    memcpy(out, a, len_a);
    if (!has_sep) out[len_a] = '/';
    memcpy(out + len_a + (has_sep ? 0 : 1), b, len_b);
    out[need] = '\0';
}

static void copy_bounded(char *dst, unsigned long dst_sz, const char *src) {
    unsigned long len = (unsigned long)strlen(src);
    if (dst_sz == 0) return;
    if (len >= dst_sz) len = dst_sz - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

int osr_known_module(const char *name) {
    return strcmp(name, "fastfetch") == 0
        || strcmp(name, "wezterm") == 0
        || strcmp(name, "pwsh") == 0
        || strcmp(name, "oh-my-posh") == 0;
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dirent.h>

static int find_on_path(const char *name, char *out, unsigned long out_sz) {
    char buf[MAX_PATH];
    DWORD len = SearchPathA(NULL, name, NULL, (DWORD)sizeof(buf), buf, NULL);
    if (len == 0 || len >= sizeof(buf)) return 0;
    copy_bounded(out, out_sz, buf);
    return 1;
}

static void dirname_of(const char *path, char *out, unsigned long out_sz) {
    const char *slash_fwd = strrchr(path, '/');
    const char *slash_back = strrchr(path, '\\');
    const char *slash = slash_fwd;
    unsigned long len;

    if (slash_back != NULL && (slash == NULL || slash_back > slash)) slash = slash_back;
    if (slash == NULL) { copy_bounded(out, out_sz, "."); return; }
    len = (unsigned long)(slash - path);
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

static int dir_exists(const char *path) {
    DIR *d = opendir(path);
    if (d == NULL) return 0;
    closedir(d);
    return 1;
}

/* capture_command_output -- run cmd, trim trailing CR/LF, return 1 if
 * anything came back. Used for the two answers only the installed tool
 * itself can give correctly (pwsh's own profile path; scoop's own prefix
 * for a package) -- see Resolve-PwshProfilePath / Resolve-PoshThemesPath's
 * own comments for why these must not be guessed.
 */
static int capture_command_output(const char *cmd, char *out, unsigned long out_sz) {
    FILE *fp;
    unsigned long len;

    if (out_sz > 0) out[0] = '\0';
    fp = _popen(cmd, "r");
    if (fp == NULL) return 0;

    len = 0;
    if (out_sz > 0) {
        len = (unsigned long)fread(out, 1, out_sz - 1, fp);
        out[len] = '\0';
    }
    _pclose(fp);

    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) { out[--len] = '\0'; }
    return len > 0;
}

/* resolve_pwsh_profile_path -- C port of pwsh.ps1's
 * Resolve-PwshProfilePath: asked FROM pwsh itself, never assembled from
 * %USERPROFILE%\Documents -- a redirected/OneDrive-moved Documents folder
 * makes those disagree, silently. */
static int resolve_pwsh_profile_path(char *out, unsigned long out_sz) {
    if (!osr_winpkg_have_command("pwsh")) return 0;
    return capture_command_output(
        "pwsh -NoLogo -NoProfile -Command \"$PROFILE.CurrentUserCurrentHost\"", out, out_sz);
}

/* resolve_posh_themes_path -- C port of oh-my-posh.ps1's
 * Resolve-PoshThemesPath: POSH_THEMES_PATH env var, else scoop's own
 * prefix for the package, else the installed binary's own directory. */
static int resolve_posh_themes_path(char *out, unsigned long out_sz) {
    char buf[600];
    DWORD n;

    if (out_sz > 0) out[0] = '\0';

    n = GetEnvironmentVariableA("POSH_THEMES_PATH", buf, (DWORD)sizeof(buf));
    if (n > 0 && n < sizeof(buf)) {
        copy_bounded(out, out_sz, buf);
    }

    if (out[0] == '\0' && osr_winpkg_have_command("scoop")) {
        char prefix[600];
        if (capture_command_output("scoop prefix oh-my-posh", prefix, sizeof(prefix)) && prefix[0] != '\0') {
            path_join(out, out_sz, prefix, "themes");
        }
    }

    if (out[0] == '\0') {
        char exe_path[600];
        char dir[600];
        if (find_on_path("oh-my-posh.exe", exe_path, sizeof(exe_path))
            || find_on_path("oh-my-posh", exe_path, sizeof(exe_path))) {
            dirname_of(exe_path, dir, sizeof(dir));
            path_join(out, out_sz, dir, "themes");
        }
    }

    return out[0] != '\0' && dir_exists(out);
}

static int module_fastfetch(const char *repo_root, const char *themes_root, const char *map_path,
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

static int module_wezterm(const char *repo_root, const char *themes_root, const char *map_path,
                           const char *theme, int theme_only) {
    char dotfiles_dir[600];
    char dest_theme[600];
    char layer_src[700];
    int is_temp;
    int ok = 1;

    path_join(dotfiles_dir, sizeof(dotfiles_dir), repo_root, "wezterm");

    if (!theme_only) {
        char src_lua[700];
        char dest_lua[600];
        osr_winpkg_install(map_path, "wezterm", NULL);
        osr_install_nerd_font("JetBrainsMono");

        path_join(src_lua, sizeof(src_lua), dotfiles_dir, ".wezterm.lua");
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
        path_join(fallback, sizeof(fallback), dotfiles_dir, "wezterm-theme.toml");
        if (!osr_copy_file(fallback, dest_theme)) ok = 0;
    }

    if (ok) osr_success("wezterm: themed as '%s'", theme);
    else osr_warn("wezterm: one or more config files failed to write");
    return ok;
}

static int module_pwsh(const char *repo_root, const char *map_path, const char *theme, int theme_only) {
    char profile_path[600];
    char src[700];

    (void)theme; /* pwsh carries no theme layer -- see osr_apply_module_theme */
    if (theme_only) return 1;

    osr_winpkg_install(map_path, "pwsh", NULL);

    if (!resolve_pwsh_profile_path(profile_path, sizeof(profile_path))) {
        osr_warn("pwsh: could not resolve pwsh's own profile path; is pwsh installed?");
        return 0;
    }

    path_join(src, sizeof(src), repo_root, "PowerShell7-profile");
    path_join(src, sizeof(src), src, "Microsoft.PowerShell_profile.ps1");

    if (!osr_copy_file(src, profile_path)) {
        osr_warn("pwsh: could not write %s", profile_path);
        return 0;
    }
    osr_success("pwsh: profile installed -> %s", profile_path);
    return 1;
}

static int module_oh_my_posh(const char *repo_root, const char *themes_root, const char *map_path,
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

    if (!resolve_posh_themes_path(themes_path, sizeof(themes_path))) {
        osr_warn("oh-my-posh: could not resolve oh-my-posh's themes directory; is oh-my-posh installed?");
        return 0;
    }

    copy_bounded(use_theme, sizeof(use_theme), theme);
    if (!osr_theme_layer_source(themes_root, repo_root, "oh-my-posh", "M365Princess++.omp.json",
                                 use_theme, layer_src, sizeof(layer_src), &is_temp)) {
        if (strcmp(use_theme, "osr-rice") == 0) {
            osr_warn("oh-my-posh: theme 'osr-rice' ships no oh-my-posh config (themes/osr-rice/config/oh-my-posh/)");
            return 0;
        }
        osr_warn("oh-my-posh: theme '%s' ships no oh-my-posh config; using 'osr-rice' -- the only prompt defined so far", use_theme);
        copy_bounded(use_theme, sizeof(use_theme), "osr-rice");
        if (!osr_theme_layer_source(themes_root, repo_root, "oh-my-posh", "M365Princess++.omp.json",
                                     use_theme, layer_src, sizeof(layer_src), &is_temp)) {
            osr_warn("oh-my-posh: theme 'osr-rice' ships no oh-my-posh config either");
            return 0;
        }
    }

    path_join(dest, sizeof(dest), themes_path, "M365Princess++.omp.json");
    {
        int ok = osr_copy_file(layer_src, dest);
        osr_theme_layer_cleanup(layer_src, is_temp);
        if (!ok) { osr_warn("oh-my-posh: could not write %s", dest); return 0; }
    }
    osr_success("oh-my-posh: themed as '%s' -> %s", use_theme, dest);
    return 1;
}

static int dispatch(const char *repo_root, const char *name, const char *theme, int theme_only) {
    char os_rice_root[600];
    char themes_root[600];
    char map_path[700];

    path_join(os_rice_root, sizeof(os_rice_root), repo_root, "os-rice");
    path_join(themes_root, sizeof(themes_root), os_rice_root, "themes");
    path_join(map_path, sizeof(map_path), os_rice_root, "windows.map");

    if (strcmp(name, "fastfetch") == 0) return module_fastfetch(repo_root, themes_root, map_path, theme, theme_only);
    if (strcmp(name, "wezterm") == 0) return module_wezterm(repo_root, themes_root, map_path, theme, theme_only);
    if (strcmp(name, "pwsh") == 0) return module_pwsh(repo_root, map_path, theme, theme_only);
    if (strcmp(name, "oh-my-posh") == 0) return module_oh_my_posh(repo_root, themes_root, map_path, theme, theme_only);

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
