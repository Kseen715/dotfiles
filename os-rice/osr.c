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
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "lib/common.h"
#include "lib/cmds.h"
#include "lib/elevate.h"
#include "lib/git.h"
#include "lib/module.h"

typedef struct {
    const char *name;
    int (*fn)(int argc, char **argv);
    /* Does this command read the checkout -- themes/, rices/, modules/*.sh,
     * lib/pkgmap, lib/servicemap, or the dotfiles configs beside it? Those are
     * data files, not compiled-in text, so a binary standing alone somewhere
     * (the release artifact, a copy in ~/wd) has none of them and the command
     * quietly does half its job. Marked here rather than probed, because the
     * answer is fixed per command and provision_tree() may CLONE: `osr log
     * info hi` must never reach for the network. */
    int needs_tree;
    const char *blurb;
} Command;

static const Command commands[] = {
    { "ui",       osr_ui_main,         0, "live step window, palette, step counter" },
    { "log",      osr_log_main,        0, "info / debug / warn / success / error lines" },
    { "state",    osr_state_main,      0, "~/.config/osr/state: what is applied" },
    { "user",     osr_user_main,       0, "target user, login shell, config-file writes" },
    { "detect",   osr_detect_main,     0, "distro + hardware facts, as shell assignments" },
    { "theme",    osr_theme_main,      1, "theme discovery, manifest, palette" },
    { "install",  osr_install_main,    1, "install.sh's text, option loop and manifest" },
    { "module",   osr_module_main,     1, "the modules written in C" },
    { "pkg",      osr_pkg_main,        1, "resolve, install and probe packages" },
    { "net",      osr_net_main,        0, "fetch a URL, resolve a GitHub tag" },
    { "build",    osr_build_main,      1, "the source: builders (lib/build.sh)" },
    { "config",   osr_config_main,     0, "layered config: seeds, blocks, composed files" },
    { "git",      osr_git_main,        0, "clone/update a repo, oh-my-zsh and its plugins" },
    { "service",  osr_service_main,    1, "enable/disable a service on any init" },
    { "preflight", osr_preflight_main,  0, "rice preconditions, before any mutation" },
    { "fonts",    osr_fonts_main,      0, "install a Nerd Font" },
    { "migrate",  osr_migrate_main,    0, "patch a seeded, user-owned layer in place" },
    { "apply",    osr_apply_main,      1, "the lists a theme-only apply is built out of" },
    { "reload",   osr_reload_main,     0, "tell the running apps to re-read their config" },
    { "wallpaper", osr_wallpaper_main,  1, "set or query the current theme's wallpaper" },
#ifndef _WIN32
    /* The four that have no Windows answer -- a GNOME session, MSRs, sysfs
     * hwmon, and a suite that drives this binary under sh. lib/cmds.h says
     * why each. */
    { "gnome",    osr_gnome_main,      0, "GNOME session probe and custom keybindings" },
    { "benchmark", osr_benchmark_main,  0, "measure CPU throughput, power and thermals" },
    { "undervolt", osr_undervolt_main,  0, "CPU voltage offsets: probe, set, auto-tune" },
    { "test-run", osr_testrun_main,    0, "run the test suite" }
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

/* The checkout provision_tree() fetches when there is none around the binary.
 * Same defaults as the `osr` launcher's self-bootstrap block, and the same two
 * env overrides, because it is the same job done from the other side: that
 * script clones because it has no binary, this clones because it has no tree.
 */
#define OSR_REPO_URL_DEFAULT "https://github.com/Kseen715/dotfiles.git"
#define OSR_TREE_DIR_DEFAULT "os-rice-dotfiles"

/* tree_at -- is <root> an os-rice tree, rather than whatever directory the
 * binary happens to be sitting in?
 *
 * Either lib/ or themes/ answers yes, because a tree is not always the whole
 * checkout: the test sandboxes assemble a root out of exactly the part the
 * scenario needs -- a lib/ and a modules/ for the module tier, a themes/ for
 * the theme tier -- and each of those is a real tree for what runs against it.
 * A downloaded binary's directory has neither, which is the case this exists
 * to catch, so demanding both would only make sandboxes clone.
 */
static int tree_at(const char *root) {
    char buf[OSR_PATH_MAX];
    if (*root == '\0') return 0;
    if (osr_path_join(buf, sizeof(buf), root, "lib") && dir_exists(buf)) return 1;
    return osr_path_join(buf, sizeof(buf), root, "themes") && dir_exists(buf);
}

/* provision_tree -- make OSR_ROOT/OSR_LIB/OSR_DOTFILES point at a real
 * checkout, cloning one when this binary has none around it.
 *
 * The released binary is a single file: people download osr-<version>-<arch>
 * and run it from wherever it landed. Everything compiled into it works there,
 * and everything that is a FILE in the repository does not -- themes/, rices/,
 * lib/pkgmap, lib/servicemap, and the dotfiles configs a module copies. Before
 * this, resolve_roots() above still pointed OSR_DOTFILES at the download
 * directory's parent, so `module run zsh` installed the packages and then
 * warned its way past every layer it was supposed to write:
 *
 *     [WARN] install: source not found: ./zsh/rc.d/10-omz.zsh
 *
 * The clone is shallow and lands in $TMPDIR (OSR_DEST overrides), and a tree
 * already sitting there is used as it is -- no fetch, so a second run costs
 * nothing and works offline. Only the commands marked needs_tree in the table
 * above reach this, so no `osr log` or `osr detect` ever touches the network.
 */
static void provision_tree(void) {
    const char *url = env_str("OSR_REPO_URL", OSR_REPO_URL_DEFAULT);
    char dest[OSR_PATH_MAX];
    char root[OSR_PATH_MAX];
    char buf[OSR_PATH_MAX];

    if (tree_at(env_str("OSR_ROOT", ""))) return;

    if (env_is_set("OSR_DEST")) {
        osr_copy_bounded(dest, sizeof(dest), env_str("OSR_DEST", ""));
    } else if (!osr_path_join(dest, sizeof(dest), osr_tmpdir(), OSR_TREE_DIR_DEFAULT)) {
        return;
    }
    /* The tree is os-rice/ INSIDE the dotfiles repo, and the configs the
     * modules copy are the repo itself -- the same two roots the launcher
     * exports from a checkout. */
    if (!osr_path_join(root, sizeof(root), dest, "os-rice")) return;

    if (!tree_at(root)) {
        char *clone_args[3];
        const char *git_pkg[2];

        osr_infof("no os-rice tree beside this binary - fetching %s into %s", url, dest);
        git_pkg[0] = "git"; git_pkg[1] = NULL;
        /* The package half detects the box on its way through (osr_mod_pkg),
         * so nothing has to be exported before this point. */
        if (!osr_have_cmd("git")) (void)osr_pkg_install(git_pkg);
        /* osr_git_repo writes the tree as OSR_USER (§8), so that account has
         * to be resolved first. A later --user still wins: the runner calls
         * osr_resolve_user again with the name it was given. */
        osr_resolve_user(NULL);
        clone_args[0] = (char *)"--depth"; clone_args[1] = (char *)"1"; clone_args[2] = NULL;
        (void)osr_git_repo("os-rice dotfiles", url, dest, clone_args);
        if (!tree_at(root)) osr_die("cloned %s but found no tree at %s", url, root);
    }

    osr_setenv("OSR_DOTFILES", dest);
    osr_setenv("OSR_ROOT", root);
    if (osr_path_join(buf, sizeof(buf), root, "lib")) osr_setenv("OSR_LIB", buf);
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
            if (commands[i].needs_tree) provision_tree();
            /* Each command sees the vector from its own word onward, so its
             * argv[0] is the command name -- the shape every one of them
             * already had when it was a separate program. */
            return commands[i].fn(argc - 1, argv + 1);
        }
    }
    return usage();
}
