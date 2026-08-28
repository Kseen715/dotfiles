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
 *   osr test-run …  the test suite runner
 *
 * The remaining .sh files (lib/ui.sh, lib/log.sh, lib/state.sh, lib/user.sh,
 * lib/detect.sh, lib/theme.sh, install.sh, osr) are NOT implementations any
 * more: they are the shell-callable surface this binary cannot have, because
 * ~120 module scripts call `run_step`, `info`, `as_root` and friends as shell
 * functions, and install.sh SOURCES each module. Every one of them is a few
 * lines of delegation; the logic is here.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include "lib/common.h"
#include "lib/cmds.h"

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

int main(int argc, char **argv) {
    size_t i;
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
