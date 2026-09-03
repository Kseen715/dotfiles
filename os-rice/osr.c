/* osr.c -- the harness core: one binary holding what used to be
 * lib/{ui,log,state,user,detect,theme}.sh, install.sh's text and decisions,
 * and the test runner. Built as build/osr on POSIX and build/osr.exe on
 * Windows, from the same sources: nob.c compiles each lib unit and links it
 * here, and this file only dispatches on the command word.
 *
 *   osr ui …        the live step window, the palette, the step counter
 *   osr log …       the five log lines
 *   osr state …     ~/.config/osr/state
 *   osr user …      the target-user model and the config-file primitives
 *   osr detect …    the distro/hardware facts, as shell assignments
 *   osr theme …     themes as objects: discovery, manifest, palette
 *   osr install …   install.sh's help, listings, option loop, manifest, report
 *   osr module …    the modules written in C
 *   osr pkg …       package resolution, the native installer, the providers
 *   osr net …       downloads, redirect resolution, github_latest
 *   osr build …     the source: provider builders
 *   osr config …    layered config, owned blocks, composed files
 *   osr benchmark … CPU throughput/power measurement (no .sh ancestor)
 *   osr undervolt … CPU voltage offsets (no .sh ancestor: new here)
 *   osr wallpaper … set or query the current theme's wallpaper (wallpaper.sh)
 *   osr test-run …  the test suite runner
 *
 * ONE BINARY, TWO SYSTEMS. There used to be two cores: this one, and
 * install.exe -- a separate program at the repository root with its own module
 * table, its own package map, its own log lines and its own option loop. They
 * are the same program now. What differs between the systems is inside the lib
 * units (lib/pkg.c dispatches to scoop/choco/winget instead of apt/dnf,
 * lib/ui.c paints with the console API instead of ANSI), never in the shape of
 * the tool, and the command table below is guarded only where a command has
 * nothing to do on a system -- see lib/cmds.h.
 *
 * The remaining .sh files are `osr`, `install.sh` and `wallpaper.sh` -- entry
 * points people, scripts, pickers and hotkeys already type -- plus osr.ps1 and
 * osr.bat, which are the same two lines for a Windows shell. `osr` is also the
 * one file that runs before a compiler is a given: its self-bootstrap block is
 * what `bootstrap.sh` used to be. Nothing in lib/ is sourced by any of them any
 * more: `startup_env` below is what lib/ui.sh's shell-level state became, and
 * it belongs here because the process that has to make those decisions once,
 * for every child it forks, is this one.
 *
 * C89 + POSIX, and C89 + Win32.
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "lib/common.h"
#include "lib/cmds.h"
#include "lib/elevate.h"

typedef struct {
    const char *name;
    int (*fn)(int argc, char **argv);
    const char *blurb;
} Command;

static const Command commands[] = {
    { "ui",       osr_ui_main,      "live step window, palette, step counter" },
    { "log",      osr_log_main,     "info / debug / warn / success / error lines" },
    { "state",    osr_state_main,   "~/.config/osr/state: what is applied" },
    { "user",     osr_user_main,    "target user, login shell, config-file writes" },
    { "detect",   osr_detect_main,  "distro + hardware facts, as shell assignments" },
    { "theme",    osr_theme_main,   "theme discovery, manifest, palette" },
    { "install",  osr_install_main, "install.sh's text, option loop and manifest" },
    { "module",   osr_module_main,  "the modules written in C" },
    { "pkg",      osr_pkg_main,     "resolve, install and probe packages" },
    { "net",      osr_net_main,     "fetch a URL, resolve a GitHub tag" },
    { "build",    osr_build_main,   "the source: builders (lib/build.sh)" },
    { "config",   osr_config_main,  "layered config: seeds, blocks, composed files" },
    { "git",      osr_git_main,     "clone/update a repo, oh-my-zsh and its plugins" },
    { "service",  osr_service_main, "enable/disable a service on any init" },
    { "preflight", osr_preflight_main, "rice preconditions, before any mutation" },
    { "fonts",    osr_fonts_main,   "install a Nerd Font" },
    { "migrate",  osr_migrate_main, "patch a seeded, user-owned layer in place" },
    { "apply",    osr_apply_main,   "the lists a theme-only apply is built out of" },
    { "reload",   osr_reload_main,  "tell the running apps to re-read their config" },
    { "wallpaper", osr_wallpaper_main, "set or query the current theme's wallpaper" },
#ifndef _WIN32
    /* The four that have no Windows answer -- a GNOME session, MSRs, sysfs
     * hwmon, and a suite that drives this binary under sh. lib/cmds.h says
     * why each. */
    { "gnome",    osr_gnome_main,   "GNOME session probe and custom keybindings" },
    { "benchmark", osr_benchmark_main, "measure CPU throughput, power and thermals" },
    { "undervolt", osr_undervolt_main, "CPU voltage offsets: probe, set, auto-tune" },
    { "test-run", osr_testrun_main, "run the test suite" }
#endif
};
#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

static int usage(void) {
    size_t i;
    fputs("usage: osr <command> [args]\n\n", stderr);
    for (i = 0; i < COMMAND_COUNT; i++) {
        fprintf(stderr, "  %-9s %s\n", commands[i].name, commands[i].blurb);
    }
    fputs("\nThis is the harness core, not the CLI: the user-facing front end\n", stderr);
    fputs("is ./osr (osr.ps1 on Windows), which calls into these.\n", stderr);
    return 2;
}

/* resolve_roots -- OSR_ROOT, OSR_LIB and OSR_DOTFILES, when nothing set them.
 *
 * The `osr` launcher sets all three before running this, and on that path
 * nothing here happens. What this covers is the binary run directly --
 * `build/osr install …`, a test driving it, and every Windows invocation,
 * where the launcher is a two-line .ps1 that has no reason to know the layout.
 *
 * The layout is the one nob.c writes: every binary lands in <os-rice>/build/,
 * so the tree root is the parent of the directory holding this executable, and
 * the dotfiles checkout is the parent of that. Derived rather than compiled in,
 * because a clone can sit anywhere.
 */
static void resolve_roots(const char *argv0) {
    char exe_dir[OSR_PATH_MAX];
    char root[OSR_PATH_MAX];
    char buf[OSR_PATH_MAX];

    if (env_is_set("OSR_ROOT") && env_is_set("OSR_LIB") && env_is_set("OSR_DOTFILES")) return;

    osr_dirname(argv0, exe_dir, sizeof(exe_dir));
    osr_dirname(exe_dir, root, sizeof(root));
    /* Run from the tree itself rather than from build/ (a test, a developer):
     * there is no parent to climb to, so take the directory as it stands. */
    if (!osr_path_join(buf, sizeof(buf), root, "lib") || !dir_exists(buf)) {
        osr_copy_bounded(root, sizeof(root), exe_dir);
    }

    if (!env_is_set("OSR_ROOT")) osr_setenv("OSR_ROOT", root);
    if (!env_is_set("OSR_LIB")) {
        if (osr_path_join(buf, sizeof(buf), env_str("OSR_ROOT", root), "lib")) {
            osr_setenv("OSR_LIB", buf);
        }
    }
    if (!env_is_set("OSR_DOTFILES")) {
        osr_dirname(env_str("OSR_ROOT", root), buf, sizeof(buf));
        osr_setenv("OSR_DOTFILES", buf);
    }
}

/* startup_env -- the shell-level state lib/ui.sh used to establish before any
 * shim ran, established here instead, once, at the top of the process.
 *
 * All of it is inherited rather than recomputed, and that is the whole point:
 * a module runs as a forked child whose stdout is the step log, not a
 * terminal, so a palette decided per process would come out colorless in
 * every module while the runner around it was colored. ui.sh made the
 * decision once against the real terminal and exported it; so does this.
 *
 * Every value is set only when the environment does not already carry one, so
 * a caller (a test, a CI job, `NO_COLOR=1`) still wins.
 */
static void startup_env(void) {
    const char *const *pal;
    int i;

    /* The palette. query_fd() is ui.sh's `exec 3>&1` trick: inside a
     * `$(...)` fd 1 is the capture pipe, so the real terminal is on fd 3 when
     * one was handed over. With no fd 3 it is plain fd 1, which is the
     * ordinary case now that no shell wraps this. */
    pal = osr_palette_values(query_fd());
    for (i = 0; i < OSR_PALETTE_COUNT; i++) {
        if (!env_is_set(osr_palette_names[i])) {
            osr_setenv(osr_palette_names[i], pal[i]);
        }
    }

    /* The per-run logfile a step's silent output is captured into. ui.sh
     * spelled it ${TMPDIR:-/tmp}/os-rice-$$.log. */
    if (!env_is_set("OSR_LOG")) {
        Str log;
        str_init(&log);
        str_addz(&log, osr_tmpdir());
        str_addz(&log, "/os-rice-");
        str_addl(&log, osr_pid());
        str_addz(&log, ".log");
        osr_setenv("OSR_LOG", str_text(&log));
        str_free(&log);
    }

    /* The step counter and the live window's height. The installer sets the
     * total before its loop and bumps N per module; these are the floors that
     * make `osr log info` print no prefix rather than "[0/0] " when nothing
     * set them. */
    if (!env_is_set("OSR_STEP_N")) osr_setenv("OSR_STEP_N", "0");
    if (!env_is_set("OSR_STEP_TOTAL")) osr_setenv("OSR_STEP_TOTAL", "0");
    if (!env_is_set("OSR_TAIL_LINES")) osr_setenv("OSR_TAIL_LINES", "5");
}

int main(int argc, char **argv) {
    size_t i;

    resolve_roots(argv[0]);
    startup_env();
    /* Remember how this run was invoked, before any work, so that a step
     * needing more privilege than it has can relaunch this exact run with it
     * rather than failing (lib/elevate.h). On POSIX that costs nothing and
     * changes nothing; on Windows it is what makes one UAC prompt cover a
     * whole install. */
    osr_elevate_init(argc, argv);

    if (argc < 2) return usage();
    for (i = 0; i < COMMAND_COUNT; i++) {
        if (strcmp(argv[1], commands[i].name) == 0) {
            /* Each command sees the vector from its own word onward, so its
             * argv[0] is the command name -- the shape every one of them
             * already had when it was a separate program. */
            return commands[i].fn(argc - 1, argv + 1);
        }
    }
    return usage();
}
