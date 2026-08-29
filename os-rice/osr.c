/* osr.c -- the POSIX harness core: one binary holding what used to be
 * lib/{ui,log,state,user,detect,theme}.sh, install.sh's text and decisions,
 * and the test runner.
 *
 * Same shape as the Windows core (install.c + its lib units linked into one
 * build/install.exe): nob.c compiles each lib/osr_*.c and links them here,
 * and this file only dispatches on the command word.
 *
 *   osr ui …        the live step window, the palette, the step counter
 *   osr log …       the five log lines
 *   osr state …     ~/.config/osr/state
 *   osr user …      the target-user model and the config-file primitives
 *   osr detect …    the distro/hardware facts, as shell assignments
 *   osr theme …     themes as objects: discovery, manifest, palette
 *   osr install …   install.sh's help, listings, option loop, manifest, report
 *   osr pkg …       package resolution, the native installer, the providers
 *   osr net …       downloads, redirect resolution, github_latest
 *   osr build …     the source: provider builders
 *   osr config …    layered config, owned blocks, composed files
 *   osr benchmark … CPU throughput/power measurement (no .sh ancestor)
 *   osr undervolt … CPU voltage offsets (no .sh ancestor: new here)
 *   osr wallpaper … set or query the current theme's wallpaper (wallpaper.sh)
 *   osr test-run …  the test suite runner
 *
 * The remaining .sh files are `osr`, `install.sh`, `wallpaper.sh` and
 * `bootstrap.sh` -- entry points people, scripts, pickers and hotkeys already
 * type, plus the one file that runs before a compiler is a given. Nothing in
 * lib/ is sourced by any of them any more: `startup_env` below is what
 * lib/ui.sh's shell-level state became, and it belongs here because the
 * process that has to make those decisions once, for every child it forks,
 * is this one.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include "lib/common.h"
#include "lib/cmds.h"

#include <unistd.h>

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
    { "module",   osr_module_main,  "the Linux modules written in C" },
    { "pkg",      osr_pkg_main,     "resolve, install and probe packages" },
    { "net",      osr_net_main,     "fetch a URL, resolve a GitHub tag" },
    { "build",    osr_build_main,   "the source: builders (lib/build.sh)" },
    { "config",   osr_config_main,  "layered config: seeds, blocks, composed files" },
    { "git",      osr_git_main,     "clone/update a repo, oh-my-zsh and its plugins" },
    { "service",  osr_service_main, "enable/disable a service on any init" },
    { "preflight", osr_preflight_main, "rice preconditions, before any mutation" },
    { "fonts",    osr_fonts_main,   "install a Nerd Font" },
    { "gnome",    osr_gnome_main,   "GNOME session probe and custom keybindings" },
    { "migrate",  osr_migrate_main, "patch a seeded, user-owned layer in place" },
    { "apply",    osr_apply_main,   "the lists a theme-only apply is built out of" },
    { "reload",   osr_reload_main,  "tell the running apps to re-read their config" },
    { "benchmark", osr_benchmark_main, "measure CPU throughput, power and thermals" },
    { "undervolt", osr_undervolt_main, "CPU voltage offsets: probe, set, auto-tune" },
    { "wallpaper", osr_wallpaper_main, "set or query the current theme's wallpaper" },
    { "test-run", osr_testrun_main, "run the test suite" }
};
#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

static int usage(void) {
    size_t i;
    fputs("usage: osr <command> [args]\n\n", stderr);
    for (i = 0; i < COMMAND_COUNT; i++) {
        fprintf(stderr, "  %-9s %s\n", commands[i].name, commands[i].blurb);
    }
    fputs("\nThis is the harness core, not the CLI: the user-facing front end\n", stderr);
    fputs("is ./osr, which calls into these.\n", stderr);
    return 2;
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
            setenv(osr_palette_names[i], pal[i], 1);
        }
    }

    /* The per-run logfile a step's silent output is captured into. ui.sh
     * spelled it ${TMPDIR:-/tmp}/os-rice-$$.log. */
    if (!env_is_set("OSR_LOG")) {
        Str log;
        const char *tmpdir = env_str("TMPDIR", "/tmp");
        str_init(&log);
        str_addz(&log, tmpdir);
        str_addz(&log, "/os-rice-");
        str_addl(&log, (long)getpid());
        str_addz(&log, ".log");
        setenv("OSR_LOG", str_text(&log), 1);
        str_free(&log);
    }

    /* The step counter and the live window's height. The installer sets the
     * total before its loop and bumps N per module; these are the floors that
     * make `osr log info` print no prefix rather than "[0/0] " when nothing
     * set them. */
    if (!env_is_set("OSR_STEP_N")) setenv("OSR_STEP_N", "0", 1);
    if (!env_is_set("OSR_STEP_TOTAL")) setenv("OSR_STEP_TOTAL", "0", 1);
    if (!env_is_set("OSR_TAIL_LINES")) setenv("OSR_TAIL_LINES", "5", 1);
}

int main(int argc, char **argv) {
    size_t i;
    startup_env();
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
