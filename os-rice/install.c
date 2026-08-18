/* install.c -- C port of install.sh, scoped to what a compiled Windows
 * binary can do without a shell.
 *
 * Covers today:
 *   - rice.list package resolution + real module installs (lib/manifest.c,
 *     modules.c) for the finite set of Windows modules modules.c knows
 *     (fastfetch, wezterm, pwsh, oh-my-posh -- see modules.c's header)
 *   - the Windows OS passes ported from the retired windows-11-x86_64/
 *     ps1 tree (win-tweaks, win-update, win-debloat, win-winutil --
 *     modules/windows/, lib/wintweak.c), reached the same way any module is:
 *     `install --module win-tweaks`
 *   - theme rendering + apply, including --theme-only (lib/theme_render.c,
 *     modules.c's osr_apply_module_theme)
 *   - wallpaper apply as part of a rice/theme apply (lib/wallpaper.c);
 *     wallpaper.exe (a separate program, next to this one, mirrors
 *     wallpaper.sh being separate from install.sh) is the direct
 *     show/list/next/set CLI
 *
 * Still NOT done, on purpose (see PLAN_UNIVERSAL.md "Scope"):
 *   - the ~70 Linux modules folder (*.sh) (not applicable to Windows --
 *     see modules.h's header) or any DE/session logic
 *   - --user (multi-user installs) and --verbose from install.sh's full
 *     option set
 *   - the interactive theme picker (install.sh has one when no --theme is
 *     given and a TTY is attached); this always falls back to the
 *     manifest's own `theme:` or the default, matching install.sh's own
 *     non-interactive fallback
 * Every module name this can't act on is reported as such, never claimed
 * as done.
 *
 * C89.
 */
#include "lib/elevate.h"
#include "lib/net.h"
#include "lib/winpkg.h"
#include "lib/manifest.h"
#include "lib/wallpaper.h"
#include "lib/winstate.h"
#include "lib/winui.h"
#include "modules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>

#define OSR_MAX_PATH_C   1024
#define OSR_DEFAULT_THEME "xin" /* matches lib/theme.sh's OSR_DEFAULT_THEME */

/* -------------------------------------------------------------------------
 * small portable string/path helpers (no snprintf: msvcrt.dll on the XP
 * target this repo eventually builds for doesn't reliably have it, see
 * PLAN_UNIVERSAL.md's toolchain matrix -- plain sprintf/memcpy do instead).
 * ---------------------------------------------------------------------- */

/* copy_bounded -- dst = src, truncated to dst_sz - 1 bytes, always
 * null-terminated (unlike strncpy(), which GCC's -Wstringop-truncation
 * flags whenever the size argument doubles as a copy length).
 */
static void copy_bounded(char *dst, unsigned long dst_sz, const char *src) {
    unsigned long len = (unsigned long)strlen(src);
    if (len >= dst_sz) len = dst_sz - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* path_join -- out = a + separator + b, bounded. Leaves out empty on overflow
 * so a truncated path fails a later fopen() loudly instead of silently
 * pointing somewhere wrong.
 */
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

/* dirname_of -- directory containing path, "." if path has no separator.
 * Checks both '/' and '\\' since argv[0] on Windows can be either,
 * depending on how the .exe was launched.
 */
static void dirname_of(const char *path, char *out, unsigned long out_sz) {
    const char *slash_fwd = strrchr(path, '/');
    const char *slash_back = strrchr(path, '\\');
    const char *slash = slash_fwd;
    unsigned long len;

    if (slash_back != NULL && (slash == NULL || slash_back > slash)) slash = slash_back;

    if (slash == NULL) {
        if (out_sz >= 2) { out[0] = '.'; out[1] = '\0'; }
        return;
    }
    len = (unsigned long)(slash - path);
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

/* -------------------------------------------------------------------------
 * directory listing -- opendir/readdir (mingw-w64 ships <dirent.h> for
 * Windows; it's also the native POSIX call), so this is already the same
 * code a future native os_linux.c branch would use for the same job.
 * ---------------------------------------------------------------------- */

static void list_rices(const char *rices_dir) {
    DIR *d = opendir(rices_dir);
    struct dirent *ent;

    if (d == NULL) { osr_error("cannot open %s", rices_dir); }

    printf("Available rices:\n");
    while ((ent = readdir(d)) != NULL) {
        char rice_dir[OSR_MAX_PATH_C];
        char rice_list[OSR_MAX_PATH_C];
        FILE *fp;

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        path_join(rice_dir, sizeof(rice_dir), rices_dir, ent->d_name);
        path_join(rice_list, sizeof(rice_list), rice_dir, "rice.list");

        fp = fopen(rice_list, "r");
        if (fp == NULL) continue;
        fclose(fp);

        printf("  %s\n", ent->d_name);
    }
    closedir(d);
}

static void list_modules(const char *modules_dir) {
    DIR *d = opendir(modules_dir);
    struct dirent *ent;

    if (d == NULL) { osr_error("cannot open %s", modules_dir); }

    printf("Available modules (Linux, sh -- listed for reference; see below for what this C core can run):\n");
    while ((ent = readdir(d)) != NULL) {
        unsigned long len = (unsigned long)strlen(ent->d_name);
        if (len > 3 && strcmp(ent->d_name + len - 3, ".sh") == 0) {
            printf("  %.*s\n", (int)(len - 3), ent->d_name);
        }
    }
    closedir(d);

    printf("\nModules this C core can actually run (package + config, see modules.c):\n");
    printf("  fastfetch\n  wezterm\n  pwsh\n  oh-my-posh\n");

    /* Listed apart because they are a different kind of thing: these change
     * the operating system rather than installing an app, so nothing here
     * belongs in a rice.list by habit -- you ask for them on purpose. */
    printf("\nWindows OS passes (no package, no theme -- see modules/windows/):\n");
    printf("  win-tweaks   debloat services + Explorer/taskbar/snap settings + sudo\n");
    printf("  win-update   ask Windows Update to run now\n");
    printf("  win-debloat  Raphire's Win11Debloat (third-party, fetched at run time)\n");
    printf("  win-winutil  Chris Titus WinUtil (third-party, interactive)\n");
}

/* -------------------------------------------------------------------------
 * CLI
 * ---------------------------------------------------------------------- */

static void usage(void) {
    printf(
        "Usage:\n"
        "  install [--root <path>] [--theme <name>] <rice>\n"
        "                              install a rice\n"
        "  install --module [--theme <name>] <name>...\n"
        "                              install module(s) directly, no rice\n"
    );
    printf(
        "  install --theme-only --theme <name>\n"
        "                              re-theme what's already installed, no\n"
        "                              packages (safe to bind to a hotkey)\n"
        "  install --list              list available rices\n"
        "  install --list-modules      list available modules\n"
        "  install -h | --help         this help\n"
        "\n"
    );
    printf(
        "  <rice>        name of a directory under rices/\n"
        "  --theme <name> which theme paints module configs (default: the rice's\n"
        "                own `theme:`, else '" OSR_DEFAULT_THEME "')\n"
        "  --root <path> os-rice root (default: the parent of this program's own\n"
        "                directory -- it is built into <os-rice>/build/)\n"
        "\n"
    );
    printf(
        "modules.c knows how to fully install+theme fastfetch, wezterm, pwsh, and\n"
        "oh-my-posh (ports of windows-rice's own modules); every other rice.list\n"
        "entry falls back to a plain windows.map package install, package only,\n"
        "no config. See modules.c and PLAN_UNIVERSAL.md for the exact scope line.\n"
    );
    printf(
        "\n"
        "It also knows four Windows OS passes -- win-tweaks, win-update,\n"
        "win-debloat, win-winutil (--list-modules describes them) -- which\n"
        "change the system itself rather than installing anything, and which are\n"
        "meant to be asked for by name rather than listed in a rice.\n"
    );
}

/* run_one_module -- dispatch a single module name: modules.c's real
 * install if known, else a plain windows.map package resolution (package
 * only, no config -- this is the fallback for any rice.list entry that
 * names a Linux-only module modules.c has no port of). Always executes
 * for real, same as install.sh's own run_module -- there is no dry-run
 * concept on the sh side, so there isn't one here either (see
 * PLAN_UNIVERSAL.md decision 9).
 */
static void run_one_module(const char *repo_root, const char *map_path, const char *name,
                            const char *theme) {
    if (osr_known_module(name)) {
        if (osr_run_module(repo_root, name, theme)) {
            /* module itself already printed an osr_success/osr_warn line */
        } else {
            osr_warn("module '%s' failed -- skipped", name);
        }
        return;
    }

    if (osr_winpkg_install(map_path, name, NULL)) {
        osr_success("  %-14s installed", name);
    } else {
        osr_warn("  %-14s FAILED (see the windows.map warning above)", name);
    }
}

static void record_applied(const char *rice_name, const char *theme) {
    char ts[32];
    if (rice_name != NULL) osr_state_set("rice", rice_name);
    osr_state_set("theme", theme);
    sprintf(ts, "%ld", (long)time(NULL));
    osr_state_set("applied", ts);
}

static int run_rice(const char *root, const char *repo_root, const char *rice_name,
                     const char *theme_override) {
    char rices_dir[OSR_MAX_PATH_C];
    char rice_dir[OSR_MAX_PATH_C];
    char rice_list_path[OSR_MAX_PATH_C];
    char map_path[OSR_MAX_PATH_C];
    char themes_dir[OSR_MAX_PATH_C];
    char theme_dir[OSR_MAX_PATH_C];
    const char *theme;
    osr_manifest m;
    unsigned long i;

    path_join(rices_dir, sizeof(rices_dir), root, "rices");
    path_join(rice_dir, sizeof(rice_dir), rices_dir, rice_name);
    path_join(rice_list_path, sizeof(rice_list_path), rice_dir, "rice.list");
    path_join(map_path, sizeof(map_path), root, "windows.map");
    path_join(themes_dir, sizeof(themes_dir), root, "themes");

    if (!osr_parse_rice_list(rice_list_path, &m)) {
        osr_error("rice not found: %s (try --list)", rice_name);
    }

    theme = theme_override;
    if (theme == NULL || theme[0] == '\0') theme = m.theme[0] != '\0' ? m.theme : OSR_DEFAULT_THEME;
    path_join(theme_dir, sizeof(theme_dir), themes_dir, theme);

    osr_info("rice=%s theme=%s", rice_name, theme);
    if (m.themes[0] != '\0') osr_info("themes offered=%s", m.themes);
    if (m.requires_list[0] != '\0') osr_info("requires=%s (not preflighted yet)", m.requires_list);
    osr_info("modules=%lu", m.module_count);

    /* install.sh warms the sudo credential at the top of a run so that no
     * escalating step prompts mid-loop; this is that, for UAC. Asked once,
     * before any work, and only when some package genuinely has no route
     * that avoids Administrator -- see osr_winpkg_run_needs_admin. */
    {
        char *names[OSR_MANIFEST_MAX_MODULES];
        for (i = 0; i < m.module_count; i++) names[i] = m.modules[i];
        if (osr_winpkg_run_needs_admin(map_path, names, (int)m.module_count)) {
            osr_elevate_now("some packages need a package manager that only an "
                            "Administrator can install.");
        }
    }

    for (i = 0; i < m.module_count; i++) {
        run_one_module(repo_root, map_path, m.modules[i], theme);
    }

    osr_apply_theme_wallpaper(theme_dir, theme);
    record_applied(rice_name, theme);
    osr_success("rice '%s' installed", rice_name);

    return 0;
}

static int run_modules_direct(const char *repo_root, const char *map_path,
                               char **names, int name_count, const char *theme) {
    int i;
    if (osr_winpkg_run_needs_admin(map_path, names, name_count)) {
        osr_elevate_now("some packages need a package manager that only an "
                        "Administrator can install.");
    }
    for (i = 0; i < name_count; i++) {
        run_one_module(repo_root, map_path, names[i], theme);
    }
    osr_success("module(s) processed");
    return 0;
}

/* run_theme_only -- re-theme whatever of the four known modules is
 * currently installed, plus the wallpaper. No packages touched -- the
 * hotkey-safe path, mirrors install.sh's --theme-only (osr_apply_theme).
 * Narrowed by "is it installed" rather than by a recorded rice's module
 * list (install.sh's osr_theme_modules): simpler, and still never writes
 * a config for a program this machine doesn't have.
 */
static int run_theme_only(const char *root, const char *repo_root, const char *theme) {
    static const char *known[] = { "fastfetch", "wezterm", "oh-my-posh", "pwsh" };
    unsigned long total = sizeof(known) / sizeof(known[0]);
    unsigned long i;
    char themes_dir[OSR_MAX_PATH_C];
    char theme_dir[OSR_MAX_PATH_C];

    path_join(themes_dir, sizeof(themes_dir), root, "themes");
    path_join(theme_dir, sizeof(theme_dir), themes_dir, theme);

    osr_info("applying theme '%s'", theme);

    for (i = 0; i < total; i++) {
        const char *name = known[i];
        int carries_theme_layer = strcmp(name, "pwsh") != 0;

        if (carries_theme_layer && !osr_winpkg_have_command(name)) continue; /* not installed */

        osr_info_step(i + 1, total, "layer: %s", name);
        if (!osr_apply_module_theme(repo_root, name, theme)) {
            osr_warn("layer '%s' failed -- skipped", name);
        }
    }

    osr_apply_theme_wallpaper(theme_dir, theme);
    record_applied(NULL, theme);
    osr_success("theme '%s' applied", theme);
    return 0;
}

int main(int argc, char **argv) {
    int i;
    int do_list = 0;
    int do_list_modules = 0;
    int do_help = 0;
    int module_mode = 0;
    int theme_only = 0;
    char *rice_name = NULL;
    char *theme_arg = NULL;
    char *user_home = NULL;
    char *module_names[64];
    int module_count = 0;
    char root[OSR_MAX_PATH_C];
    char repo_root[OSR_MAX_PATH_C];
    char rices_dir[OSR_MAX_PATH_C];
    char modules_dir[OSR_MAX_PATH_C];
    char map_path[OSR_MAX_PATH_C];

    root[0] = '\0';

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            do_help = 1;
        } else if (strcmp(argv[i], "--list") == 0) {
            do_list = 1;
        } else if (strcmp(argv[i], "--list-modules") == 0) {
            do_list_modules = 1;
        } else if (strcmp(argv[i], "--module") == 0) {
            module_mode = 1;
        } else if (strcmp(argv[i], "--theme-only") == 0) {
            theme_only = 1;
        } else if (strcmp(argv[i], "--theme") == 0) {
            i++;
            if (i >= argc) { fprintf(stderr, "error: --theme needs a name\n"); return 1; }
            theme_arg = argv[i];
        } else if (strcmp(argv[i], "--root") == 0) {
            i++;
            if (i >= argc) { fprintf(stderr, "error: --root needs a path\n"); return 1; }
            copy_bounded(root, sizeof(root), argv[i]);
        } else if (strcmp(argv[i], "--user-home") == 0) {
            /* Set by lib/elevate.c when it relaunches this run elevated:
             * the profile to rice, carried across the elevation boundary
             * the way sudo carries $SUDO_USER (see elevate.h). Configs must
             * still land in the user's home, not the admin's. */
            i++;
            if (i >= argc) { fprintf(stderr, "error: --user-home needs a path\n"); return 1; }
            user_home = argv[i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr,
                "error: unknown option: %s (this C port only implements --list, "
                "--list-modules, --module, --theme, --theme-only, --root, "
                "--user-home -- see install.sh for the full set)\n",
                argv[i]);
            return 1;
        } else if (module_mode) {
            if (module_count >= (int)(sizeof(module_names) / sizeof(module_names[0]))) {
                fprintf(stderr, "error: too many module names\n");
                return 1;
            }
            module_names[module_count++] = argv[i];
        } else if (rice_name == NULL) {
            rice_name = argv[i];
        } else {
            fprintf(stderr, "error: only one rice may be given (got '%s' and '%s')\n", rice_name, argv[i]);
            return 1;
        }
    }

    if (do_help) { usage(); return 0; }

    /* Before any work: remember how we were invoked, so a step that needs
     * Administrator can relaunch this exact run elevated instead of failing
     * (lib/elevate.h -- the port of install.sh's sudo warm-up). */
    osr_elevate_init(argc, argv);
    if (user_home != NULL) osr_set_user_home(user_home);

    /* Default root: not the directory holding this exe, but its parent --
     * nob.c links every binary into <os-rice>/build/ rather than next to the
     * sources, so rices/, themes/, modules/ and windows.map sit one level up
     * from argv[0]. --root overrides it when the tree lives somewhere else. */
    if (root[0] == '\0') {
        char exe_dir[OSR_MAX_PATH_C];
        dirname_of(argv[0], exe_dir, sizeof(exe_dir));
        dirname_of(exe_dir, root, sizeof(root));
    }
    dirname_of(root, repo_root, sizeof(repo_root));

    path_join(rices_dir, sizeof(rices_dir), root, "rices");
    path_join(modules_dir, sizeof(modules_dir), root, "modules");
    path_join(map_path, sizeof(map_path), root, "windows.map");

    if (do_list) { list_rices(rices_dir); return 0; }
    if (do_list_modules) { list_modules(modules_dir); return 0; }

    if (theme_only) {
        if (theme_arg == NULL || theme_arg[0] == '\0') {
            fprintf(stderr, "error: --theme-only needs --theme <name>\n");
            return 1;
        }
        return run_theme_only(root, repo_root, theme_arg);
    }

    if (module_mode) {
        if (module_count == 0) {
            usage();
            fprintf(stderr, "error: no module specified\n");
            return 1;
        }
        return run_modules_direct(repo_root, map_path, module_names, module_count,
                                   theme_arg != NULL ? theme_arg : OSR_DEFAULT_THEME);
    }

    if (rice_name == NULL) {
        usage();
        fprintf(stderr, "error: no rice specified\n");
        return 1;
    }

    return run_rice(root, repo_root, rice_name, theme_arg);
}
