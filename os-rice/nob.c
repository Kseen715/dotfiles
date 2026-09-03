/* nob.c -- build script for the os-rice Windows C core. Replaces the old
 * Makefile: this only needs a C compiler, never a separate `make` binary
 * (the actual complaint that started this file: `make` on Windows meant
 * one more thing to install/PATH-manage besides gcc -- see
 * PLAN_UNIVERSAL.md decision 6). Bootstrap once, then just run it:
 *
 *   mkdir build
 *   cc -o build/nob nob.c   (build\nob.exe on Windows)
 *   ./build/nob             (builds the full static programs)
 *   ./build/nob static      (same as the default)
 *   ./build/nob runtime     (builds build/osr-runtime, modules on demand)
 *   ./build/nob both        (builds static and runtime outputs)
 *   ./build/nob test        (builds both + runs the test suite)
 *   ./build/nob clean
 *   ./build/nob -v          (any of the above, with full command lines)
 *   ./build/nob -t          (any of the above, timed: how long each unit
 *                            took to compile and each binary to link)
 *
 * Commands are echoed the way an autoconf build with silent rules prints
 * them -- "TCC      build/obj/lib_net.o", "LD       build/install" -- so a
 * full build reads as one line per output instead of one wrapped
 * paragraph, and the tag names the compiler that actually ran (GCC, TCC,
 * ZIG CC, ...); -v/--verbose (or NOB_VERBOSE=1, for the `make` wrapper,
 * which forwards no arguments) prints the full command lines instead. See
 * "autoconf-style command echo" near main().
 *
 * Every binary this script produces -- nob itself included -- lands under
 * build/, never next to the sources: build/install, build/wallpaper, the
 * test binaries in build/test/, the objects in build/obj/. So the source
 * tree stays clean, one .gitignore line (build/) covers the lot, and
 * `clean` has a single place to look.
 *
 * The compiler is chosen for you: with $CC unset, the fastest of a short
 * preferred list that actually works on this host wins (tcc, then "zig cc",
 * then gcc, then cc -- see "picking the compiler" below), and the answer is
 * cached in build/cc.detected. Set $CC to override it outright (CC=clang
 * ./nob); `./build/nob clean` re-runs the detection. Both flag dialects are
 * handled: a compiler named cl/clang-cl gets MSVC's spelling (/W4, /c, /Fo,
 * *.lib), everything else gets gcc/clang's. Switching compilers forces a
 * full rebuild, because objects are not portable between them.
 *
 * Builds are incremental: a source is recompiled only when its object is
 * older than the source, and a binary is relinked only when it is older
 * than the objects it is made of, so a second run in a row does nothing
 * (see needs_compile/needs_link below). `./build/nob clean` forces the
 * next build to be a full one.
 *
 * After the first bootstrap you never type that gcc line again -- nob.h's
 * "Go Rebuild Urself" technology (NOB_GO_REBUILD_URSELF below) recompiles
 * nob on the spot whenever nob.c itself changes, before doing anything
 * else. osr.ps1/osr.bat lean on exactly this: they just run
 * `build\nob.exe`, even the very first time it doesn't exist yet (see
 * osr.ps1, which creates build/ and bootstraps into it).
 *
 * nob.c/nob.h are build-time tooling, run only on the developer/CI host --
 * unlike install.c/lib/*.c they are never cross-compiled for the XP
 * target, so unlike those files this one is free to use C99 (nob.h itself
 * requires it).
 */
/* Pick the backend from the language mode the compiler is actually in:
 * __STDC_VERSION__ is defined only from C99 on (199901L, 201112L, ...); in
 * C89 -- and on compilers like MSVC that never define it -- it is absent,
 * which is exactly when nob.h (a C99 header) cannot be used. NOB89 forces
 * the fallback regardless. */
#if defined(NOB89) || !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 199901L)
#  define NOB89_IMPLEMENTATION
#  include "nob89.h"
#else
#  define NOB_IMPLEMENTATION
#  include "nob.h"
#endif

#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#ifndef _WIN32
#include <sys/time.h>   /* gettimeofday, for `nob -t`; see now_secs() */
#endif

/* EXE -- the host's executable suffix. Windows needs ".exe"; on a Linux/CI
 * host the produced binaries (and the tests we actually run there) carry no
 * suffix. */
#ifdef _WIN32
#define EXE ".exe"
#else
#define EXE ""
#endif

/* Everything this script writes goes under BUILD_DIR: programs directly in
 * it, test binaries in its test/ subdirectory, objects in obj/. Nothing is
 * ever written next to a source file, so the tree a developer reads stays
 * free of build output and `clean` (plus .gitignore) has one place to look.
 * install.c/wallpaper.c know about this layout too -- being one level down
 * from the os-rice root is exactly why they resolve rices/themes/modules
 * from their exe's *parent* directory (see install.c's main). */
#define BUILD_DIR "build"
#define OBJ_DIR BUILD_DIR "/obj"
#define TEST_BIN_DIR BUILD_DIR "/test"

/* mkdir_if_needed -- nob_mkdir_if_not_exists() logs "directory `x` already
 * exists" on every single run; now that an up-to-date build prints nothing
 * else, those two lines would be the whole output. Only call it when there
 * is actually a directory to create, so the "created directory" line still
 * shows up the one time it matters. */
static bool mkdir_if_needed(const char *path) {
    if (nob_file_exists(path) > 0) return true;
    return nob_mkdir_if_not_exists(path);
}

/* --- per-command timing (`nob -t`) -----------------------------------
 *
 * `nob -t` / `--time` (or NOB_TIME=1, for the `make` wrapper, which forwards
 * no arguments) appends the wall-clock cost of each compile and link to the
 * line that already names it:
 *
 *   TCC      build/obj/lib_yaml.o                    0.412s
 *   LD       build/osr                               0.088s
 *   total                                            3.907s
 *
 * The numbers are only meaningful if one command runs at a time, so timing
 * also serialises the build: an async batch shares the machine, and each
 * member's wall clock would then measure the contention rather than the
 * file. That makes a timed build slower than a real one -- it is a profile
 * of the tree, not a stopwatch on the build you would actually run, and the
 * total printed at the end is the serial total, not what `nob` costs you.
 *
 * Only commands nob decided to issue are timed; an up-to-date tree runs
 * nothing and reports nothing, so `nob clean && nob -t` is the way to time
 * every unit.
 */
static bool timing = false;
static double timed_total = 0.0;

/* now_secs -- a monotonic-ish clock in seconds. Not clock(), which counts
 * this process's CPU time and would report ~0 for a compiler that runs in a
 * child; not time(), whose one-second resolution is coarser than most of the
 * units here. nob.h has nob_nanos_since_unspecified_epoch(), but nob89.h
 * (the C89 backend) does not, and this file has to build against both. */
#ifdef _WIN32
static double now_secs(void) {
    return (double)GetTickCount() / 1000.0;
}
#else
static double now_secs(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}
#endif

/* pending_echo -- the brief line for the command now running, parked by the
 * log handler instead of printed, so the duration can be appended to it and
 * one line still describes one command. Empty when the handler did not
 * produce one (that is `nob -v`, which prints full command lines itself). */
static char pending_echo[512];

/* report_timed_total -- the last line of a timed run, whichever subcommand
 * ran; registered with atexit() because main() returns from a dozen places
 * and the total is worth printing after every one of them. Silent when
 * nothing was rebuilt, so an up-to-date tree still prints nothing. */
static void report_timed_total(void) {
    if (timed_total > 0.0) fprintf(stderr, "  %-8s %-37s %8.3fs\n", "total", "", timed_total);
}

/* run_timed -- run one command, print what it cost. Synchronous by
 * construction; see the note above on why. */
static bool run_timed(Nob_Cmd *cmd, const char *what) {
    double start;
    bool ok;
    pending_echo[0] = '\0';
    start = now_secs();
    ok = nob_cmd_run(cmd);
    {
        double secs = now_secs() - start;
        timed_total += secs;
        if (pending_echo[0] != '\0') {
            fprintf(stderr, "%-48s %8.3fs\n", pending_echo, secs);
        } else {
            fprintf(stderr, "  %-8s %-38s %8.3fs\n", "TIME", what, secs);
        }
    }
    return ok;
}

/* cmd_append_args -- append a NULL-terminated list of arguments. C89 has no
 * variadic macros, so this plain variadic *function* stands in for nob.h's
 * variadic nob_cmd_append() wherever several arguments are pushed at once;
 * nob.c then works unchanged against both nob.h and the C89 nob89.h. */
static void cmd_append_args(Nob_Cmd *cmd, ...) {
    va_list ap;
    const char *arg;
    va_start(ap, cmd);
    for (;;) {
        arg = va_arg(ap, const char *);
        if (arg == NULL) break;
        nob_cmd_append(cmd, arg);
    }
    va_end(ap);
}

/* --- what gets compiled, and where it goes -------------------------------
 *
 * Three lists, and which one a unit is in says what kind of unit it is.
 *
 * core_srcs      compiled on every host. Either the unit is plain C89 (the
 *                string kit, the manifest and map parsers, the renderers), or
 *                it holds both systems' bodies under one #ifdef -- which is
 *                what makes it ONE unit rather than two files with the same
 *                job. lib/pkg.c, lib/module.c, lib/ui.c and lib/detect.c are
 *                all of the second kind.
 * posix_srcs     the subsystems that exist only on POSIX: a GNOME session,
 *                MSRs and sysfs hwmon, the fork-based fetch layer, the suite
 *                runner, and the ~120 modules that install X11/Wayland
 *                programs.
 * win_srcs       the subsystems that exist only on Windows: process
 *                elevation, and the modules whose program or whose OS pass is
 *                Windows-only.
 *
 * There used to be two lists linking two different programs -- install.exe
 * from lib_srcs, build/osr from posix_srcs -- with their own module tables and
 * their own log lines. One program now, one command table, one registry.
 */
static const char *core_srcs[] = {
    "lib/common.c",
    "lib/log.c",
    "lib/ui.c",
    "lib/state.c",
    "lib/user.c",
    "lib/detect.c",
    "lib/theme.c",
    "lib/render.c",
    "lib/install.c",
    "lib/module.c",
    "lib/modules.c",
    "lib/pkg.c",
    "lib/build.c",
    "lib/config.c",
    "lib/apply.c",
    "lib/preflight.c",
    "lib/reload.c",
    "lib/migrate.c",
    "lib/git.c",
    "lib/service.c",
    "lib/fonts.c",
    "lib/wallpaper.c",
    "lib/elevate.c",
    /* lib/fetch.c holds both transports and the two URL parsers above them;
     * it was lib/net.c and lib/fetch.c, one per system, publishing different
     * names for the same three acts. */
    "lib/fetch.c",
    /* The vendored YAML parser's implementation (thirdparty/yaml.h). Core
     * rather than static-only: config parsing is what it is here for, and
     * that lives in the runtime host too. */
    "lib/yaml.c",
    /* Modules both systems have. */
    "modules/fastfetch.c",
    "modules/starship.c",
    "modules/wezterm.c",
};
#define CORE_SRCS_COUNT (sizeof(core_srcs) / sizeof(core_srcs[0]))

static const char *posix_srcs[] = {
    "lib/gnome.c",
    "lib/testrun.c",
    "lib/benchmark.c",
    /* lib/bench/ -- CPU measurement. Unconditional within this list: every
     * architecture has a throughput number worth taking, and the power layer
     * degrades to "no sensor" rather than to a wrong reading. */
    "lib/bench/cpu.c",
    "lib/bench/power.c",
    "lib/bench/util.c",
    "lib/undervolt.c",
    /* lib/uv/ -- the undervolting backends. backend.c and generic_opp.c are
     * unconditional: the probe has to work everywhere, including on an arch
     * with no voltage control at all, because "what does this machine expose"
     * is the first question and the one most likely to be answered "nothing".
     * The vendor mailboxes are added under arch guards as they land. */
    "lib/uv/backend.c",
    "lib/uv/generic_opp.c",
    "lib/uv/journal.c",
    "modules/alacritty.c",
    "modules/amnezia-vpn.c",
    "modules/arandr.c",
    "modules/archives.c",
    "modules/audio.c",
    "modules/avahi.c",
    "modules/benchmark.c",
    "modules/blueman.c",
    "modules/brightnessctl.c",
    "modules/btop.c",
    "modules/celluloid.c",
    "modules/cliphist.c",
    "modules/codecs.c",
    "modules/copyq.c",
    "modules/cpu-microcodes.c",
    "modules/curseforge.c",
    "modules/datagrip.c",
    "modules/discord.c",
    "modules/disks.c",
    "modules/dkms.c",
    "modules/dnscrypt.c",
    "modules/docker.c",
    "modules/dunst.c",
    "modules/easyeffects.c",
    "modules/evolution.c",
    "modules/fcitx5.c",
    "modules/feh.c",
    "modules/firefox.c",
    "modules/flameshot.c",
    "modules/flatpak.c",
    "modules/foot.c",
    "modules/gh.c",
    "modules/ghostty.c",
    "modules/git-base.c",
    "modules/gnome-focus.c",
    "modules/gnome-overview.c",
    "modules/gnome-panel.c",
    "modules/go.c",
    "modules/gpaste.c",
    "modules/gpu-drivers.c",
    "modules/gtklock.c",
    "modules/gvfs.c",
    "modules/helpers.c",
    "modules/helvum.c",
    "modules/htop.c",
    "modules/hyprcursor.c",
    "modules/hypridle.c",
    "modules/hyprland.c",
    "modules/hyprlock.c",
    "modules/hyprpaper.c",
    "modules/hyprpicker.c",
    "modules/i3.c",
    "modules/i3lock.c",
    "modules/input.c",
    "modules/inxi.c",
    "modules/kate.c",
    "modules/kdeconnect.c",
    "modules/keyring.c",
    "modules/lcc.c",
    "modules/lightdm.c",
    "modules/loupe.c",
    "modules/luminance.c",
    "modules/mako.c",
    "modules/micro.c",
    "modules/mirrors.c",
    "modules/nautilus.c",
    "modules/ncdu.c",
    "modules/networkmanager.c",
    "modules/nwg-displays.c",
    "modules/obs-studio.c",
    "modules/onlyoffice.c",
    "modules/openssh.c",
    "modules/pacman-multilib.c",
    "modules/paru.c",
    "modules/picom.c",
    "modules/pipewire.c",
    "modules/polkit-agent.c",
    "modules/polybar.c",
    "modules/power.c",
    "modules/printer.c",
    "modules/proteus.c",
    "modules/pulseaudio.c",
    "modules/qbittorrent.c",
    "modules/qpwgraph.c",
    "modules/redshift.c",
    "modules/rofi.c",
    "modules/rust.c",
    "modules/sddm.c",
    "modules/serie.c",
    "modules/steam.c",
    "modules/swap.c",
    "modules/swaylock.c",
    "modules/tcc.c",
    "modules/telegram.c",
    "modules/theming.c",
    "modules/thumbnails.c",
    "modules/thunar.c",
    "modules/thunderbird.c",
    "modules/ufw.c",
    "modules/viewers.c",
    "modules/vlc.c",
    "modules/vmware-init.c",
    "modules/vscode-insiders.c",
    "modules/vscode.c",
    "modules/waybar.c",
    "modules/waydroid.c",
    "modules/wayland.c",
    "modules/waylock.c",
    "modules/wleave.c",
    "modules/wlogout.c",
    "modules/wofi.c",
    "modules/xdg.c",
    "modules/xorg.c",
    "modules/yandex-browser.c",
    "modules/yazi.c",
    "modules/zen-browser.c",
    "modules/zig.c",
    "modules/zip.c",
    "modules/zsh.c",
};
#define POSIX_SRCS_COUNT (sizeof(posix_srcs) / sizeof(posix_srcs[0]))

static const char *win_srcs[] = {
    "modules/oh-my-posh.c",
    "modules/pwsh.c",
    "modules/win-debloat.c",
    "modules/win-tweaks.c",
    "modules/win-update.c",
};
#define WIN_SRCS_COUNT (sizeof(win_srcs) / sizeof(win_srcs[0]))

/* HOST_SRCS -- the list for the system being built for. nob.c has always
 * assumed the host it runs on is the system it builds for (see the -DWINVER
 * flags and the -lwininet line further down), and this is the same
 * assumption. */
#ifdef _WIN32
#define HOST_SRCS win_srcs
#define HOST_SRCS_COUNT WIN_SRCS_COUNT
#else
#define HOST_SRCS posix_srcs
#define HOST_SRCS_COUNT POSIX_SRCS_COUNT
#endif

/* The runtime host (D-4a) compiles every unit EXCEPT the modules, plus
 * lib/module_runtime.c, and loads a module's object on demand instead. So the
 * two counts below are "how many entries of each list come before its modules"
 * -- the modules are last in both, which is what makes that a count rather
 * than a filter.
 *
 * Keep them at the boundary when adding a unit: a non-module added after the
 * modules would be silently left out of the runtime host, and the failure is a
 * link error naming whatever osr.c dispatches to it. */
#define CORE_NO_MODULES_COUNT  (CORE_SRCS_COUNT - 3)
#define POSIX_NO_MODULES_COUNT (POSIX_SRCS_COUNT - 119)
#define POSIX_RUNTIME_SRC "lib/module_runtime.c"

/* test_names -- tests that LINK against the built objects, and so run on
 * whichever host builds. Everything here is a pure unit reached through a
 * public header: a URL parser, an asset matcher. Nothing in this list touches
 * a registry, a network or a package manager.
 *
 * unity_test_names -- tests that link nothing and INCLUDE what they read
 * instead, because what they read is static to its unit. They run everywhere
 * too; the difference is only how they are put together, and it is forced --
 * linking them against the object built from the same source would define it
 * twice. */
static const char *test_names[] = {
    "net_parse_test", "artifact_test",
};
#define TEST_COUNT (sizeof(test_names) / sizeof(test_names[0]))

static const char *unity_test_names[] = {
    "wintweak_test",
};
#define UNITY_TEST_COUNT (sizeof(unity_test_names) / sizeof(unity_test_names[0]))

/* posix_test_names -- tests of the POSIX-only units, which cannot be linked
 * the way the ones above are.
 *
 * A test of lib/uv/* includes the .c files it needs directly (a unity build)
 * and links nothing else at all, which is also why these tests can reach
 * static helpers the header does not export. The behaviour tests below do the
 * same for a different reason: they drive build/osr as a subprocess, so they
 * must not be linked to any of it.
 */
static const char *posix_test_names[] = {
    "uv_journal_test", "bench_test",
    /* The behaviour tests (test/harness.c). They link nothing at all -- they
     * drive build/osr as a subprocess and assert what it did to a sandboxed
     * box -- which is why they belong here rather than with the tests that
     * link the lib objects: a black-box test of what a unit must do should
     * not break when the unit is renamed or split. */
    "service_test", "preflight_test", "apply_test", "pkg_test", "nerdfont_test", "net_test", "git_test", "reload_test", "migrate_test", "zsh_test", "gnome_test", "gnome_modules_test", "detect_test", "gpu_drivers_test", "audio_test", "swap_test", "log_test", "ui_test", "state_test", "testrun_test", "user_test", "theme_test", "theme_layers_test", "wallpaper_test", "config_test", "terminals_test", "build_test", "apps_test", "desktop_test", "install_test", "modules_test", "yaml_test"
};
#define POSIX_TEST_COUNT (sizeof(posix_test_names) / sizeof(posix_test_names[0]))

/* --- picking the compiler --------------------------------------------
 *
 * $CC still wins outright, and is still a command line rather than a bare
 * program name: "zig cc", "ccache gcc" and "gcc -m32" are all ordinary
 * values, so it gets split on spaces once and kept as words.
 *
 * What is new is what happens when $CC is unset. It used to mean "the same
 * family of compiler that built nob", which is safe but slow: gcc takes
 * about two seconds over this tree where tcc takes a tenth of one, and for
 * a build that runs on the way to every `osr` invocation that gap is the
 * whole cost of the build. So instead, walk cc_ladder in order and take the
 * first entry that can actually compile and link a program on this host.
 *
 * The probe is deliberately shallow -- it proves a candidate exists, runs,
 * and speaks our flag dialect, not that it can digest every header this
 * tree includes. A compiler that passes the probe and then fails the real
 * build is exactly what $CC is there to override.
 *
 * A probe costs a process spawn, which is more than an already-up-to-date
 * build otherwise spends, so the answer is remembered in build/cc.detected
 * and reused from then on. `nob clean` throws it away, which is also how
 * you get a newly installed compiler noticed.
 */
#if defined(_MSC_VER)
#define DEFAULT_CC "cl.exe"
#elif defined(__clang__)
#define DEFAULT_CC "clang"
#elif defined(__GNUC__)
#define DEFAULT_CC "gcc"
#else
#define DEFAULT_CC "cc"
#endif

/* cc_ladder -- fastest first, and DEFAULT_CC last. That tail matters on a
 * host where none of the names ahead of it resolve -- an MSVC-only Windows
 * box, say: whatever compiled nob demonstrably exists here, so it is the
 * one candidate that cannot leave us with nothing. */
static const char *cc_ladder[] = {"tcc", "clang", "gcc", "zig cc", "cc", DEFAULT_CC};
#define CC_LADDER_COUNT (sizeof(cc_ladder) / sizeof(cc_ladder[0]))

#define CC_DETECTED BUILD_DIR "/cc.detected"
#define CC_STAMP BUILD_DIR "/cc.stamp"
#define CC_PROBE_SRC BUILD_DIR "/cc_probe.c"
#define CC_PROBE_OBJ BUILD_DIR "/cc_probe.o"
#define CC_PROBE_BIN BUILD_DIR "/cc_probe" EXE

/* DEV_NULL -- where a probe's own diagnostics go. A candidate that fails is
 * the expected case here, not something to report. */
#ifdef _WIN32
#define DEV_NULL "NUL"
#else
#define DEV_NULL "/dev/null"
#endif

/* split_words -- "zig cc" -> {"zig", "cc"}, returning the count. The words
 * point into a copy of s that is leaked on purpose: it has to outlive the
 * call, and it lives exactly as long as the run does. */
#define CC_MAX_WORDS 16
static size_t split_words(const char *s, const char **out, size_t max) {
    char *buf = (char *)malloc(strlen(s) + 1);
    char *p;
    size_t n = 0;
    NOB_ASSERT(buf != NULL);
    strcpy(buf, s);
    for (p = buf; *p;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        NOB_ASSERT(n < max);
        out[n++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    return n;
}

/* is_msvc_name -- does this program name speak MSVC's flag dialect (/W4,
 * /c, /Fo) rather than gcc/clang's? True for "cl", "clang-cl" and cross
 * spellings ending in "-cl"; deliberately false for plain "clang", which
 * takes gcc flags.
 */
static bool is_msvc_name(const char *c) {
    const char *base = c;
    const char *p;
    size_t len;
    for (p = c; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    len = strlen(base);
    if (len > 4 && strcmp(base + len - 4, ".exe") == 0) len -= 4;
    if (len < 2 || strncmp(base + len - 2, "cl", 2) != 0) return false;
    return len == 2 || base[len - 3] == '-';
}

/* cc_probe -- can `candidate` build a program at all? Compile *and* link,
 * because the interesting failure is not "no such program": a compiler
 * whose runtime library is missing compiles a translation unit perfectly
 * well and only falls over at the link. */
static bool cc_probe(const char *candidate) {
    const char *words[CC_MAX_WORDS];
    size_t n = split_words(candidate, words, CC_MAX_WORDS);
    Nob_Log_Level saved = nob_minimal_log_level;
    Nob_Cmd cmd = {0};
    bool ok;
    size_t i;
    if (n == 0) return false;
    for (i = 0; i < n; i++) nob_cmd_append(&cmd, words[i]);
    if (is_msvc_name(words[0])) {
        cmd_append_args(&cmd, "/nologo", CC_PROBE_SRC, "/Fo" CC_PROBE_OBJ, "/Fe" CC_PROBE_BIN, NULL);
    } else {
        cmd_append_args(&cmd, CC_PROBE_SRC, "-o", CC_PROBE_BIN, NULL);
    }
    /* the "CMD: ..." line for a candidate we are only trying out is noise */
    nob_minimal_log_level = NOB_WARNING;
    {
        Nob_Cmd_Opt opt = {0};
        opt.stdout_path = DEV_NULL;
        opt.stderr_path = DEV_NULL;
        ok = nob_cmd_run_opt(&cmd, opt);
    }
    nob_minimal_log_level = saved;
    return ok;
}

/* cc_probe_cleanup -- the scratch files are the probe's, not the build's, so
 * drop them once the walk is over and leave build/ holding only real output.
 * Silently: three "deleting ..." lines about files the caller never asked
 * for would be most of what a first build prints. */
static void cc_probe_cleanup(void) {
    Nob_Log_Level saved = nob_minimal_log_level;
    nob_minimal_log_level = NOB_WARNING;
    if (nob_file_exists(CC_PROBE_SRC) > 0) nob_delete_file(CC_PROBE_SRC);
    if (nob_file_exists(CC_PROBE_OBJ) > 0) nob_delete_file(CC_PROBE_OBJ);
    if (nob_file_exists(CC_PROBE_BIN) > 0) nob_delete_file(CC_PROBE_BIN);
    nob_minimal_log_level = saved;
}

/* cc_detect -- the cached ladder walk. Returns a string that lives for the
 * rest of the run. */
static const char *cc_detect(void) {
    Nob_String_Builder sb = {0};
    size_t i;

    if (nob_file_exists(CC_DETECTED) > 0 && nob_read_entire_file(CC_DETECTED, &sb) && sb.count > 0) {
        while (sb.count > 0 && (unsigned char)sb.items[sb.count - 1] <= ' ') sb.count--;
        nob_sb_append_null(&sb);
        if (sb.count > 1) return sb.items; /* leaked on purpose, as above */
    }
    nob_sb_free(sb);

    /* the probe writes a source file and a binary, so it needs build/ --
     * which on a first-ever run does not exist yet. */
    if (!mkdir_if_needed(BUILD_DIR)) return DEFAULT_CC;
    if (!nob_write_entire_file(CC_PROBE_SRC, "int main(void) { return 0; }\n", 29)) return DEFAULT_CC;

    for (i = 0; i < CC_LADDER_COUNT; i++) {
        if (!cc_probe(cc_ladder[i])) continue;
        cc_probe_cleanup();
        nob_log(NOB_INFO, "compiler: %s (set $CC to override, `nob clean` to re-detect)", cc_ladder[i]);
        return cc_ladder[i];
    }
    cc_probe_cleanup();
    nob_log(NOB_WARNING, "no compiler on the preferred list works here; trying %s anyway", DEFAULT_CC);
    return DEFAULT_CC;
}

/* cc_autodetected -- false when $CC named the compiler. Only a detected one
 * is worth writing to build/cc.detected; caching an explicit $CC there would
 * make a one-off "CC=gcc nob" stick around for every later run. */
static bool cc_autodetected = false;

static const char *cc(void) {
    static const char *cached = NULL;
    const char *env;
    if (cached != NULL) return cached;
    env = getenv("CC");
    if (env != NULL && *env != '\0') {
        cached = env;
        return cached;
    }
    cc_autodetected = true;
    cached = cc_detect();
    return cached;
}

/* $CC is a command line, not just a program name, so split it once and keep
 * the words -- exec takes one program plus separate arguments. */
static const char *cc_words[CC_MAX_WORDS];
static size_t cc_word_count = 0;

static void cc_split(void) {
    if (cc_word_count > 0) return;
    cc_word_count = split_words(cc(), cc_words, CC_MAX_WORDS);
    NOB_ASSERT(cc_word_count > 0);
}

/* cc_prog -- the program $CC names, without its arguments. */
static const char *cc_prog(void) {
    cc_split();
    return cc_words[0];
}

static void append_cc(Nob_Cmd *cmd) {
    size_t i;
    cc_split();
    for (i = 0; i < cc_word_count; i++) nob_cmd_append(cmd, cc_words[i]);
}

static bool is_msvc(void) { return is_msvc_name(cc_prog()); }

/* is_chibicc -- does $CC name chibicc, with or without a path and flags?
 * chibicc is deliberately absent from cc_ladder: it is a self-hosting toy
 * compiler that searches /usr/include but not the compiler-private
 * directory where stddef.h actually lives on a glibc host, and it cannot
 * parse GCC's own stdarg.h (`typedef __builtin_va_list __gnuc_va_list;`).
 * Left alone, `CC=chibicc` dies on the first real system header with a
 * "stddef.h: cannot open file" that reads like a missing file, not a
 * missing capability -- so say so up front. */
static bool is_chibicc(void) {
    const char *prog = cc_prog();
    const char *base = prog;
    const char *p;
    size_t len;
    for (p = prog; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    len = strlen(base);
    return len == 7 && memcmp(base, "chibicc", 7) == 0;
}

/* is_faucc -- does $CC name faucc (FAUcc, the FAU machine's C compiler),
 * with or without a path? faucc emits only 16-bit (i286) or 32-bit (i386)
 * Intel code -- there is no 64-bit backend -- and its cc1 predates most of
 * what a modern glibc header does, so it needs its own flag dialect (see
 * append_common_flags) and its own early warning (see check_cc_detection). */
static bool is_faucc(void) {
    const char *prog = cc_prog();
    const char *base = prog;
    const char *p;
    size_t len;
    for (p = prog; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    len = strlen(base);
    return len == 5 && memcmp(base, "faucc", 5) == 0;
}

/* is_pcc -- does $CC name pcc (Portable C Compiler), with or without a path?
 * pcc advertises __GNUC__ but does not understand every glibc attribute: it
 * warns "unsupported attribute `__cold__'" on stdlib.h/stdio.h on every
 * translation unit. Scoped here (not in append_common_flags for every
 * compiler) because gcc/clang builds want real attribute diagnostics. */
static bool is_pcc(void) {
    const char *prog = cc_prog();
    const char *base = prog;
    const char *p;
    size_t len;
    for (p = prog; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    len = strlen(base);
    return len == 3 && memcmp(base, "pcc", 3) == 0;
}

/* is_lcc -- does $CC name lcc (Fraser & Hanson's retargetable C compiler),
 * with or without a path? lcc's driver does not speak gcc's flag dialect at
 * all: it reads -Wall as -Wa<ll> (an assembler flag "ll" that GNU as then
 * fails to open), and any option it does not recognize before -c is silently
 * treated as a linker input file. So lcc gets its own branch of
 * append_common_flags below. Deliberately absent from cc_ladder (it is a
 * 32-bit-only i386 C89 compiler a user picks explicitly, never a default).
 */
static bool is_lcc(void) {
    const char *prog = cc_prog();
    const char *base = prog;
    const char *p;
    size_t len;
    for (p = prog; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    len = strlen(base);
    return len == 3 && memcmp(base, "lcc", 3) == 0;
}

/* check_cc_detection -- runs on every invocation; the dialect pick is the
 * one thing here that silently produces a garbage command line if wrong. */
static void check_cc_detection(void) {
    if (is_chibicc()) {
        nob_log(NOB_WARNING,
                "CC=%s: chibicc cannot compile this tree against glibc/GCC headers "
                "(no include path to stddef.h; cannot parse GCC's stdarg.h). "
                "Use tcc, gcc or clang.", cc());
    }
    if (is_faucc()) {
        nob_log(NOB_WARNING,
                "CC=%s: faucc is 32-bit-only (-b i386) and its cc1 cannot consume "
                "this host's glibc/GCC headers (no __builtin_bswap*/__builtin_expect, "
                "no casts in constant expressions). The flags below are faucc-correct, "
                "but any TU that includes a system header still fails at cc1. "
                "Use tcc, gcc or clang for a full build.", cc());
    }
    NOB_ASSERT(is_msvc_name("cl"));
    NOB_ASSERT(is_msvc_name("cl.exe"));
    NOB_ASSERT(is_msvc_name("C:\\VC\\bin\\cl.exe"));
    NOB_ASSERT(is_msvc_name("clang-cl"));
    NOB_ASSERT(!is_msvc_name("clang"));
    NOB_ASSERT(!is_msvc_name("gcc"));
    NOB_ASSERT(!is_msvc_name("/usr/bin/x86_64-w64-mingw32-gcc"));
    {
        const char *w[CC_MAX_WORDS];
        NOB_ASSERT(split_words("gcc", w, CC_MAX_WORDS) == 1);
        NOB_ASSERT(split_words("  zig   cc ", w, CC_MAX_WORDS) == 2 && strcmp(w[1], "cc") == 0);
        NOB_ASSERT(split_words("", w, CC_MAX_WORDS) == 0);
    }
}

/* unoptimized_src -- sources built at -O0 on purpose.
 *
 * Only one so far: lib/yaml.c is the vendored parser's 13k-line
 * implementation, and it is the most expensive unit in the tree to
 * optimize -- gcc -O2 spends about 3.4s on it, more than three times the
 * next-slowest file, where -O0 costs 0.7s. What that buys is throughput
 * inside a YAML parser reading configuration files of a few kilobytes,
 * which is not a cost this tool can measure. Move it back if a profile ever
 * disagrees.
 *
 * The dialects that ignore -O flags anyway (lcc invokes its compiler bare,
 * faucc documents -O as accepted-and-ignored) are unaffected either way.
 */
static bool unoptimized_src(const char *src) {
    return strcmp(src, "lib/yaml.c") == 0;
}

/* append_common_flags -- the same std/warning/XP-floor flags every binary
 * this script produces is built with, in whichever dialect $CC speaks. XP
 * floor: see PLAN_UNIVERSAL.md's toolchain matrix -- checked today against
 * an ordinary current mingw-w64; the pinned XP toolchain itself is still
 * long-away-planned (Task 0.1).
 */
static void append_common_flags_for(Nob_Cmd *cmd, const char *src) {
    /* -O0 rather than -O2 for one file, and emitted in place of it rather
     * than after it: "the last -O wins" is how gcc, clang and tcc behave,
     * but it is not a rule every compiler follows, and a line carrying both
     * levels is ambiguous to read besides. See unoptimized_src. */
    bool o0 = src && unoptimized_src(src);
    append_cc(cmd);
    if (is_msvc()) {
        /* No /std: equivalent to -std=c89 -- MSVC's C mode is already C89
         * plus extensions and /Za (the closest thing) is long discouraged.
         * /wd4505 is -Wno-unused-function; the CRT one silences the
         * fopen/getenv "deprecation" that C89 code cannot avoid. cl only
         * ever targets Windows, so the XP defines are unconditional here. */
        cmd_append_args(cmd, "/nologo", "/W4", o0 ? "/Od" : "/O2", NULL);
        cmd_append_args(cmd, "/wd4505", "/D_CRT_SECURE_NO_WARNINGS", NULL);
        cmd_append_args(cmd, "/DWINVER=0x0501", "/D_WIN32_WINNT=0x0501", NULL);
        return;
    }
    if (is_faucc()) {
        /* faucc rejects every gcc dialect/warning flag we use elsewhere
         * (-std=c89, -Wall, -Wextra, -pedantic, -Wno-unused-function) and
         * has no 64-bit backend at all, so the 32-bit one is the only
         * choice for this tree. -b i386 is the architecture selector
         * (i286 would be 16-bit); -O level is accepted but ignored by
         * faucc's own man page. Its cc1 then still fails on any TU that
         * pulls in a system header -- see check_cc_detection. */
        cmd_append_args(cmd, "-b", "i386", "-O", "2", NULL);
        return;
    }
    if (is_lcc()) {
        /* lcc's driver (see is_lcc) does not understand -std/-Wall/-Wextra/
         * -pedantic/-O2: -Wall becomes -Wa<ll> and breaks the assembler, and
         * unrecognized options before -c are treated as linker inputs. The
         * patched driver (modules/src/lcc-linux.c) already forces -std=c89 on
         * its own cpp line and lcc optimizes by default, so the compiler is
         * invoked bare -- append_cc(cmd) above already put it on the line. */
        return;
    }
    cmd_append_args(cmd, "-std=c89", "-Wall", "-Wextra", "-pedantic",
                    o0 ? "-O0" : "-O2", NULL);
    /* helpers used only by one platform branch of a file are dead on the
     * other -- that is expected, not a defect. */
    nob_cmd_append(cmd, "-Wno-unused-function");
    /* pcc turns on -Wshadow under -Wall and has no diagnostic pragma to
     * scope it, so the vendored thirdparty/yaml.h (included tree-wide)
     * warns from every TU. gcc/clang do not enable -Wshadow at all under
     * -Wall -Wextra, so nothing is lost by silencing it for pcc only. */
    if (is_pcc()) cmd_append_args(cmd, "-Wno-attributes", "-Wno-shadow", NULL);
#ifdef _WIN32
    cmd_append_args(cmd, "-DWINVER=0x0501", "-D_WIN32_WINNT=0x0501", NULL);
#endif
}


static void append_common_flags(Nob_Cmd *cmd) {
    append_common_flags_for(cmd, NULL);
}

/* BIN -- a program's path in the build directory, with the host's suffix:
 * BIN("install") is "build/install.exe" on Windows, "build/install"
 * elsewhere. A macro, not a function, so it stays a plain literal usable
 * anywhere a string is. */
#define BIN(name) BUILD_DIR "/" name EXE

/* obj_of -- "lib/net.c" -> "build/obj/lib_net.o". All objects live in one
 * flat directory; the path separators become '_' to keep names unique
 * without having to mirror the directory layout under build/obj.
 * Objects, not one big gcc line per binary, so the shared lib/modules
 * translation units are compiled once in parallel and then linked into all
 * of install/wallpaper/the test binaries.
 */
static const char *build_path_of(const char *src, const char *ext) {
    size_t len = strlen(src);
    char *out = nob_temp_sprintf("%s/%.*s%s", OBJ_DIR, (int)(len - 2), src, ext); /* strip ".c" */
    char *p;
    for (p = out + sizeof(OBJ_DIR); *p; p++) {
        if (*p == '/' || *p == '\\') *p = '_';
    }
    return out;
}

static const char *obj_of(const char *src) {
    return build_path_of(src, ".o");
}

/* dep_of -- "lib/net.c" -> "build/obj/lib_net.d", the make-syntax list of
 * headers the compiler saw the last time it built that object. Next to the
 * object on purpose: the two are made together and thrown away together. */
static const char *dep_of(const char *src) {
    return build_path_of(src, ".d");
}

/* --- incremental builds ---------------------------------------------
 *
 * The rule is the usual make one, done with file timestamps: an object is
 * recompiled only when it is older than something it is made of, a binary
 * relinked only when it is older than its objects. Two runs in a row with
 * nothing edited in between means the second one runs no compiler at all.
 *
 * Every object depends on nob.c as well as its own .c file: the compiler
 * flags live in nob.c, so editing them has to invalidate every object.
 * Headers are tracked per object out of the dependency files the compiler
 * writes (see dep_flag below). deps -- every .h in the tree -- is the
 * fallback for compilers that cannot write those, and for the first build
 * of an object, when no dependency file exists yet: touching any header
 * then rebuilds everything. That over-builds, but it cannot under-build,
 * and under-building is the failure mode that silently links a stale
 * object and costs an afternoon.
 */
static Nob_File_Paths deps = {0};
static bool deps_collected = false;
static bool deps_usable = false;

/* actions -- compiler/linker commands actually issued this run; 0 means
 * everything was already up to date and main() says so. */
static size_t actions = 0;

static bool collect_dep(Nob_Walk_Entry entry) {
    size_t len;
    if (entry.type == NOB_FILE_DIRECTORY) {
        const char *base = nob_path_name(entry.path);
        /* build/ is our own output (and holds nob.exe.old after a
         * self-rebuild); .git holds nothing that is ever compiled. */
        if (strcmp(base, BUILD_DIR) == 0 || strcmp(base, ".git") == 0) {
            *entry.action = NOB_WALK_SKIP;
        }
        return true;
    }
    len = strlen(entry.path);
    if (len > 2 && strcmp(entry.path + len - 2, ".h") == 0) {
        nob_da_append(&deps, nob_temp_strdup(entry.path));
    }
    return true;
}

/* unity_srcs -- .c files that are #included by another translation unit
 * rather than compiled on their own.
 *
 * They have to be listed because collect_dep only registers .h files, and for
 * dependency purposes an #included .c IS a header: nothing else records that
 * test/unit_c/uv_journal_test.c is rebuilt when lib/uv/journal.c changes. The
 * failure this prevents is the bad direction -- editing the unit under test
 * and running a test binary compiled from the version before the edit, which
 * reports green about code that no longer exists.
 *
 * Explicit rather than derived by scanning every source for its includes: the
 * set changes about once a year, and a list beside the lists of what IS
 * compiled is the obvious place to look when it does.
 */
static const char *unity_srcs[] = {
    "test/harness.c",       /* every test that #includes the harness */
    "lib/common.c",         /* uv_journal_test, bench_test */
    "lib/uv/journal.c", "lib/uv/backend.c", "lib/uv/generic_opp.c",
    "lib/bench/cpu.c", "lib/bench/power.c", "lib/bench/util.c",
};
#define UNITY_SRCS_COUNT (sizeof(unity_srcs) / sizeof(unity_srcs[0]))

/* collect_deps -- walk the tree once per run for headers. If the walk
 * fails (an unreadable directory, say) deps_usable stays false and every
 * timestamp check below answers "rebuild": without the full header list
 * we cannot prove anything is up to date, and guessing wrong ships a
 * stale binary. */
static void collect_deps(void) {
    size_t i;
    if (deps_collected) return;
    deps_collected = true;
    deps_usable = nob_walk_dir(".", collect_dep);
    nob_da_append(&deps, "nob.c");
    for (i = 0; i < UNITY_SRCS_COUNT; i++) nob_da_append(&deps, unity_srcs[i]);
}

/* --- exact header dependencies ---------------------------------------
 *
 * The whole-tree rule above is the fallback, not the plan. When the compiler
 * can write dependency files -- `-MMD -MF x.d`, which gcc, clang, tcc and
 * zig cc all speak -- it is asked to, and then an object depends on the
 * headers it actually included rather than on every header in the tree.
 * Editing thirdparty/yaml.h rebuilds lib/yaml.o; editing lib/ui.h rebuilds
 * the dozen units that include it, not all 190.
 *
 * The flag is probed rather than assumed, once per `clean` and cached in
 * build/cc.deps, because $CC can be anything: MSVC spells this /showIncludes
 * and needs its output parsed, lcc and faucc have no equivalent at all, and
 * a compiler handed a flag it does not know fails the build rather than the
 * probe. Anything that does not answer the probe keeps the old rule, which
 * over-builds but cannot under-build.
 *
 * A missing or unreadable .d also falls back: on the first build of an
 * object there is nothing to read yet, and "no record" must never be read as
 * "no dependencies".
 */
#define CC_DEPS BUILD_DIR "/cc.deps"
#define CC_PROBE_DEP BUILD_DIR "/cc_probe.d"

/* dep_flag_probe -- does $CC write a dep file when handed `flag -MF path`?
 * Compile-only: the linker has nothing to do with it, and cc_probe() already
 * proved the link works. */
static bool dep_flag_probe(const char *flag) {
    Nob_Log_Level saved = nob_minimal_log_level;
    Nob_Cmd cmd = {0};
    bool ok;
    if (!nob_write_entire_file(CC_PROBE_SRC, "int main(void) { return 0; }\n", 29)) return false;
    if (nob_file_exists(CC_PROBE_DEP) > 0) nob_delete_file(CC_PROBE_DEP);
    append_cc(&cmd);
    cmd_append_args(&cmd, flag, "-MF", CC_PROBE_DEP, "-c", CC_PROBE_SRC,
                    "-o", CC_PROBE_OBJ, NULL);
    nob_minimal_log_level = NOB_WARNING;
    {
        Nob_Cmd_Opt opt = {0};
        opt.stdout_path = DEV_NULL;
        opt.stderr_path = DEV_NULL;
        ok = nob_cmd_run_opt(&cmd, opt);
    }
    ok = ok && nob_file_exists(CC_PROBE_DEP) > 0;
    if (nob_file_exists(CC_PROBE_DEP) > 0) nob_delete_file(CC_PROBE_DEP);
    cc_probe_cleanup();
    nob_minimal_log_level = saved;
    return ok;
}

/* dep_flag -- the flag this compiler wants, or NULL if it has none.
 *
 * gcc, clang and zig cc take -MMD (deps without the system headers); tcc
 * only knows -MD, which for it means the same thing. The answer is cached
 * in build/cc.deps, next to the detected compiler, so the probe runs once
 * per toolchain rather than once per build. Compilers whose dep output is
 * spelled differently or not at all (MSVC's /showIncludes, lcc, faucc) get
 * NULL and fall back to the whole-tree rule; handing a compiler a flag it
 * does not know fails the build rather than the probe.
 */
static const char *dep_flag(void) {
    static char cached[8] = "";
    static bool probed = false;
    Nob_String_Builder sb = {0};
    if (probed) return cached[0] ? cached : NULL;
    probed = true;
    if (is_msvc() || is_lcc() || is_faucc()) return NULL;
    if (nob_file_exists(CC_DEPS) > 0 && nob_read_entire_file(CC_DEPS, &sb) && sb.count > 0) {
        size_t n = sb.count < sizeof(cached) - 1 ? sb.count : sizeof(cached) - 1;
        memcpy(cached, sb.items, n);
        cached[n] = '\0';
        nob_sb_free(sb);
        return cached[0] == '-' ? cached : NULL;
    }
    nob_sb_free(sb);
    if (!mkdir_if_needed(BUILD_DIR)) return NULL;
    if (dep_flag_probe("-MMD")) strcpy(cached, "-MMD");
    else if (dep_flag_probe("-MD")) strcpy(cached, "-MD");
    else strcpy(cached, "no");
    nob_write_entire_file(CC_DEPS, cached, strlen(cached));
    return cached[0] == '-' ? cached : NULL;
}

/* read_dep_file -- the prerequisites one .d file records.
 *
 * The file is a make rule: the object, a colon, then its sources and headers,
 * with "\" before each newline of a wrapped list. Only the right-hand side is
 * wanted, so the scan skips to the first colon that ends a word (a Windows
 * "C:\..." target has one that does not) and then splits on whitespace,
 * treating a lone backslash as more whitespace. A path containing a space is
 * escaped as "\ " by gcc and would split wrongly here; no file in this tree
 * has one, and the failure mode is an extra rebuild rather than a stale one.
 *
 * The tokens point into a buffer reused across calls, so a caller must be
 * done with them before the next call -- needs_compile() is, immediately.
 */
static bool read_dep_file(const char *path, Nob_File_Paths *out) {
    static Nob_String_Builder sb = {0};
    char *p;
    if (nob_file_exists(path) <= 0) return false;
    sb.count = 0;
    if (!nob_read_entire_file(path, &sb)) return false;
    nob_sb_append_null(&sb);
    p = sb.items;
    while (*p != '\0' && !(*p == ':' && (p[1] == ' ' || p[1] == '\t' || p[1] == '\n' || p[1] == '\0'))) p++;
    if (*p == '\0') return false;
    p++;
    out->count = 0;
    while (*p != '\0') {
        char *tok;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\\') p++;
        if (*p == '\0') break;
        tok = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
        if (*p != '\0') *p++ = '\0';
        nob_da_append(out, tok);
    }
    return out->count > 0;
}

/* needs_compile -- is src's object missing, older than src, or older than a
 * header it was built from? A timestamp we could not read (-1) counts as
 * "yes" for the same reason as above.
 *
 * nob.c is checked by hand because no .d will ever list it: the flags live
 * in this file, so editing it has to invalidate every object. */
static Nob_File_Paths dep_items = {0};

static bool needs_compile(const char *src) {
    const char *obj = obj_of(src);
    if (nob_needs_rebuild1(obj, src) != 0) return true;
    if (nob_needs_rebuild1(obj, "nob.c") != 0) return true;
    if (dep_flag() && read_dep_file(dep_of(src), &dep_items)) {
        return nob_needs_rebuild(obj, dep_items.items, dep_items.count) != 0;
    }
    collect_deps();
    if (!deps_usable) return true;
    return nob_needs_rebuild(obj, deps.items, deps.count) != 0;
}

/* host_objs -- every object that goes into the one binary: main's, the core's,
 * and this system's own list. Returns how many were written. */
static size_t host_objs(const char **objs, const char *main_src) {
    size_t count = 0;
    size_t i;
    objs[count++] = obj_of(main_src);
    for (i = 0; i < CORE_SRCS_COUNT; i++) objs[count++] = obj_of(core_srcs[i]);
    for (i = 0; i < HOST_SRCS_COUNT; i++) objs[count++] = obj_of(HOST_SRCS[i]);
    return count;
}

/* needs_link -- is bin missing or older than any object linked into it?
 * Called only after compile_objs() has flushed, so the objects' mtimes
 * are final by the time we look at them. */
static bool needs_link(const char *bin, const char *main_src) {
    const char *objs[CORE_SRCS_COUNT + POSIX_SRCS_COUNT + WIN_SRCS_COUNT + 1];
    size_t count = host_objs(objs, main_src);
    return nob_needs_rebuild(bin, objs, count) != 0;
}

/* compile_objs -- one gcc -c per source, all started at once (nob caps the
 * batch at nob_nprocs()), waited on together. */
static bool compile_objs(const char **srcs, size_t count) {
    Nob_Procs procs = {0};
    Nob_Cmd cmd = {0};
    size_t i;
    if (!mkdir_if_needed(BUILD_DIR)) return false;
    if (!mkdir_if_needed(OBJ_DIR)) return false;
    for (i = 0; i < count; i++) {
        if (!needs_compile(srcs[i])) continue;
        actions++;
        append_common_flags_for(&cmd, srcs[i]);
        if (dep_flag()) cmd_append_args(&cmd, dep_flag(), "-MF", dep_of(srcs[i]), NULL);
        if (is_msvc()) {
            /* /Fo takes its path glued on, no separate argument. The .o
             * name (rather than MSVC's usual .obj) is fine and keeps
             * obj_of()/clean() single-dialect. */
            cmd_append_args(&cmd, "/c", srcs[i], nob_temp_sprintf("/Fo%s", obj_of(srcs[i])), NULL);
        } else {
            cmd_append_args(&cmd, "-c", srcs[i], "-o", obj_of(srcs[i]), NULL);
        }
        if (timing) {
            if (!run_timed(&cmd, obj_of(srcs[i]))) return false;
        } else {
            Nob_Cmd_Opt opt = {0};
            opt.async = &procs;   /* nob89 runs this synchronously */
            if (!nob_cmd_run_opt(&cmd, opt)) return false;
        }
    }
    return nob_procs_flush(&procs);
}

static void append_lib_objs(Nob_Cmd *cmd) {
    size_t i;
    for (i = 0; i < CORE_SRCS_COUNT; i++) nob_cmd_append(cmd, obj_of(core_srcs[i]));
    for (i = 0; i < HOST_SRCS_COUNT; i++) nob_cmd_append(cmd, obj_of(HOST_SRCS[i]));
}

/* Windows-only link libs -- the sources' POSIX branches (see lib/net.c's
 * #else) need nothing beyond libc, so on a Linux host we link plain.
 *
 * -lwininet: lib/net.c's WinInet calls.
 * -ladvapi32: lib/fonts.c's RegOpenKeyExA/RegEnumValueA (registry font check).
 * -lshell32: SystemParametersInfoA (lib/wallpaper.c) links via user32 in
 * most mingw setups, but shell32 covers the COM-ish helpers if that ever
 * grows; included now so a future addition doesn't need a second flag
 * change hunted down by a link error.
 */
static void append_common_libs(Nob_Cmd *cmd) {
    if (is_msvc()) {
        /* cl hands plain .lib arguments straight to the linker. */
        cmd_append_args(cmd, "wininet.lib", "advapi32.lib", "user32.lib", "shell32.lib", NULL);
        return;
    }
#ifdef _WIN32
    cmd_append_args(cmd, "-lwininet", "-ladvapi32", "-luser32", "-lshell32", NULL);
#else
    NOB_UNUSED(cmd);
#endif
}

/* link_runtime -- the runtime-module host, built in one compiler invocation.
 * Its object set differs from the static host only by a preprocessor
 * definition, so compiling source-to-binary avoids mixing incompatible objects
 * in build/obj while leaving the ordinary static build unchanged. POSIX only:
 * it dlopens what it compiles. */
static bool link_runtime(const char *bin) {
    Nob_Cmd cmd = {0};
    const char *inputs[CORE_NO_MODULES_COUNT + POSIX_NO_MODULES_COUNT + 2];
    size_t count = 0;
    size_t i;

    inputs[count++] = "osr.c";
    for (i = 0; i < CORE_NO_MODULES_COUNT; i++) inputs[count++] = core_srcs[i];
    for (i = 0; i < POSIX_NO_MODULES_COUNT; i++) inputs[count++] = posix_srcs[i];
    inputs[count++] = POSIX_RUNTIME_SRC;
    collect_deps();
    if (nob_needs_rebuild(bin, inputs, count) == 0 &&
        nob_needs_rebuild(bin, deps.items, deps.count) == 0) return true;

    actions++;
    append_common_flags(&cmd);
    /* -rdynamic exports the host's symbols for the dlopen'd modules to
     * resolve. lcc's driver does not know it and would hand it to ld as an
     * input file; -Wl-E is the same linker flag (-E) through lcc's loader
     * options (-Wl takes the flags with no comma: -Wl,arg is a lone token). */
    cmd_append_args(&cmd, "-DOSR_RUNTIME_MODULES=1",
                    is_lcc() ? "-Wl-E" : "-rdynamic", "-o", bin, "osr.c", NULL);
    for (i = 0; i < CORE_NO_MODULES_COUNT; i++) nob_cmd_append(&cmd, core_srcs[i]);
    for (i = 0; i < POSIX_NO_MODULES_COUNT; i++) nob_cmd_append(&cmd, posix_srcs[i]);
    cmd_append_args(&cmd, POSIX_RUNTIME_SRC, "-ldl", NULL);
    if (timing) return run_timed(&cmd, bin);
    return nob_cmd_run(&cmd);
}

/* link_exe -- main_src's own object + every shared object. Async when procs
 * is given, so the binaries of one batch link in parallel too. */
static bool link_exe(const char *bin, const char *main_src, Nob_Procs *procs) {
    Nob_Cmd cmd = {0};
    if (!needs_link(bin, main_src)) return true;
    actions++;
    append_common_flags(&cmd);
    if (is_msvc()) {
        nob_cmd_append(&cmd, nob_temp_sprintf("/Fe%s", bin));
    } else {
        cmd_append_args(&cmd, "-o", bin, NULL);
    }
    nob_cmd_append(&cmd, obj_of(main_src));
    append_lib_objs(&cmd);
    append_common_libs(&cmd);
    if (timing) return run_timed(&cmd, bin);
    {
        Nob_Cmd_Opt opt = {0};
        opt.async = procs;   /* nob89 runs this synchronously */
        return nob_cmd_run_opt(&cmd, opt);
    }
}

/* link_standalone -- one object and no lib objects: the unity-built tests,
 * which include their subject rather than linking it. The system link
 * libraries still go on, because a unity that includes a unit calling
 * RegCreateKeyEx needs them as much as the real binary does. */
static bool link_standalone(const char *bin, const char *main_src, Nob_Procs *procs) {
    Nob_Cmd cmd = {0};
    const char *obj = obj_of(main_src);
    if (nob_needs_rebuild(bin, &obj, 1) == 0) return true;
    actions++;
    append_common_flags(&cmd);
    if (is_msvc()) {
        nob_cmd_append(&cmd, nob_temp_sprintf("/Fe%s", bin));
    } else {
        cmd_append_args(&cmd, "-o", bin, NULL);
    }
    nob_cmd_append(&cmd, obj);
    append_common_libs(&cmd);
    if (timing) return run_timed(&cmd, bin);
    {
        Nob_Cmd_Opt opt = {0};
        opt.async = procs;   /* nob89 runs this synchronously */
        return nob_cmd_run_opt(&cmd, opt);
    }
}

/* run_test -- tests read fixtures via a path relative to test/unit_c/, so
 * that is still the working directory they run in; only the binary itself
 * moved out to build/test/, hence the climb back up in its path.
 */
static bool run_test(const char *name) {
    const char *bin_name = nob_temp_sprintf("../../" TEST_BIN_DIR "/%s" EXE, name);
    Nob_Cmd cmd = {0};
    bool ok;
    nob_log(NOB_INFO, "--- %s ---", name);
    nob_cmd_append(&cmd, bin_name);
    if (!nob_set_current_dir("test/unit_c")) return false;
    ok = nob_cmd_run(&cmd);
    nob_set_current_dir("../..");
    return ok;
}

static bool build_tests(void) {
    const char *srcs[TEST_COUNT];
    const char *usrcs[UNITY_TEST_COUNT];
    const char *psrcs[POSIX_TEST_COUNT];
    Nob_Procs procs = {0};
    size_t i;
    for (i = 0; i < TEST_COUNT; i++) srcs[i] = nob_temp_sprintf("test/unit_c/%s.c", test_names[i]);
    if (!compile_objs(srcs, TEST_COUNT)) return false;
    if (!mkdir_if_needed(TEST_BIN_DIR)) return false;
    for (i = 0; i < TEST_COUNT; i++) {
        const char *bin = nob_temp_sprintf(TEST_BIN_DIR "/%s" EXE, test_names[i]);
        if (!link_exe(bin, srcs[i], &procs)) return false;
    }
    if (!nob_procs_flush(&procs)) return false;

    for (i = 0; i < UNITY_TEST_COUNT; i++) {
        usrcs[i] = nob_temp_sprintf("test/unit_c/%s.c", unity_test_names[i]);
    }
    if (!compile_objs(usrcs, UNITY_TEST_COUNT)) return false;
    for (i = 0; i < UNITY_TEST_COUNT; i++) {
        const char *bin = nob_temp_sprintf(TEST_BIN_DIR "/%s" EXE, unity_test_names[i]);
        if (!link_standalone(bin, usrcs[i], &procs)) return false;
    }
    if (!nob_procs_flush(&procs)) return false;

#ifndef _WIN32
    for (i = 0; i < POSIX_TEST_COUNT; i++) {
        psrcs[i] = nob_temp_sprintf("test/unit_c/%s.c", posix_test_names[i]);
    }
    if (!compile_objs(psrcs, POSIX_TEST_COUNT)) return false;
    for (i = 0; i < POSIX_TEST_COUNT; i++) {
        const char *bin = nob_temp_sprintf(TEST_BIN_DIR "/%s" EXE, posix_test_names[i]);
        if (!link_standalone(bin, psrcs[i], &procs)) return false;
    }
#else
    NOB_UNUSED(psrcs);
#endif
    return nob_procs_flush(&procs);
}

#ifndef _WIN32
static bool run_runtime_module_tests(void) {
    Nob_Cmd cmd = {0};
    nob_log(NOB_INFO, "--- runtime_modules ---");
    cmd_append_args(&cmd, "sh", "test/runtime_modules.sh", NULL);
    return nob_cmd_run(&cmd);
}
#endif

static bool run_all_tests(void) {
    bool ok = true;
    size_t i;
    if (!build_tests()) return false;
    for (i = 0; i < TEST_COUNT; i++) {
        if (!run_test(test_names[i])) ok = false;
    }
    for (i = 0; i < UNITY_TEST_COUNT; i++) {
        if (!run_test(unity_test_names[i])) ok = false;
    }
#ifndef _WIN32
    for (i = 0; i < POSIX_TEST_COUNT; i++) {
        if (!run_test(posix_test_names[i])) ok = false;
    }
    if (!run_runtime_module_tests()) ok = false;
#endif
    return ok;
}

static void delete_if_exists(const char *path) {
    if (nob_file_exists(path) > 0) nob_delete_file(path);
}

/* delete_built -- everything compile_objs left behind for one source: the
 * object and, when the compiler wrote one, its dependency file. */
static void delete_built(const char *src) {
    delete_if_exists(obj_of(src));
    delete_if_exists(dep_of(src));
}

static bool clean(void) {
    size_t i;
    delete_if_exists(BIN("osr"));
    delete_if_exists(BIN("osr-runtime"));
    delete_built("osr.c");
    for (i = 0; i < CORE_SRCS_COUNT; i++) delete_built(core_srcs[i]);
    for (i = 0; i < POSIX_SRCS_COUNT; i++) delete_built(posix_srcs[i]);
    for (i = 0; i < WIN_SRCS_COUNT; i++) delete_built(win_srcs[i]);
    for (i = 0; i < TEST_COUNT; i++) {
        delete_if_exists(nob_temp_sprintf(TEST_BIN_DIR "/%s" EXE, test_names[i]));
        delete_built(nob_temp_sprintf("test/unit_c/%s.c", test_names[i]));
    }
    for (i = 0; i < UNITY_TEST_COUNT; i++) {
        delete_if_exists(nob_temp_sprintf(TEST_BIN_DIR "/%s" EXE, unity_test_names[i]));
        delete_built(nob_temp_sprintf("test/unit_c/%s.c", unity_test_names[i]));
    }
    for (i = 0; i < POSIX_TEST_COUNT; i++) {
        delete_if_exists(nob_temp_sprintf(TEST_BIN_DIR "/%s" EXE, posix_test_names[i]));
        delete_built(nob_temp_sprintf("test/unit_c/%s.c", posix_test_names[i]));
    }
    /* the compiler bookkeeping goes too: with no objects left there is no
     * toolchain to record, and dropping the detection cache is what makes
     * `clean` the way to have a newly installed compiler noticed. */
    delete_if_exists(CC_DETECTED);
    delete_if_exists(CC_STAMP);
    delete_if_exists(CC_DEPS);
    delete_if_exists(CC_PROBE_DEP);
    delete_if_exists(CC_PROBE_SRC);
    delete_if_exists(CC_PROBE_OBJ);
    delete_if_exists(CC_PROBE_BIN);
    return true;
}

/* cc_toolchain_check -- objects belong to the compiler that produced them.
 * Nothing in the timestamp rules notices a change of compiler, so without
 * this a build that switches (CC=gcc after a default tcc build, or a
 * detection that now lands somewhere else) finds every object newer than
 * its source, skips straight to the link, and hands one toolchain's objects
 * to another's linker -- which fails, if you are lucky, with something as
 * opaque as "undefined reference to `__va_arg`".
 *
 * So the compiler that built the current objects is recorded next to them,
 * and a mismatch means a full rebuild. Failing to write the stamp is not
 * fatal: the cost is re-cleaning next run, not a wrong build.
 */
static bool cc_toolchain_check(void) {
    const char *current = cc();
    Nob_String_Builder sb = {0};
    bool same = false;

    if (nob_file_exists(CC_STAMP) > 0 && nob_read_entire_file(CC_STAMP, &sb)) {
        nob_sb_append_null(&sb);
        same = strcmp(sb.items, current) == 0;
    }
    nob_sb_free(sb);
    if (same) return true;

    if (nob_file_exists(OBJ_DIR) > 0) {
        nob_log(NOB_INFO, "compiler is now %s -- rebuilding everything", current);
        if (!clean()) return false;
    }
    if (!mkdir_if_needed(BUILD_DIR)) return false;
    if (cc_autodetected) nob_write_entire_file(CC_DETECTED, current, strlen(current));
    nob_write_entire_file(CC_STAMP, current, strlen(current));
    return true;
}

/* build_all -- every shared object plus the two program objects compiled in
 * one parallel batch, then both binaries linked from them. */
/* build_all -- one binary, build/osr. Every object it needs compiled in one
 * parallel batch, then linked. */
static bool build_all(void) {
    const char *srcs[CORE_SRCS_COUNT + HOST_SRCS_COUNT + 1];
    size_t count = 0;
    Nob_Procs procs = {0};
    size_t i;

    if (!cc_toolchain_check()) return false;

    srcs[count++] = "osr.c";
    for (i = 0; i < CORE_SRCS_COUNT; i++) srcs[count++] = core_srcs[i];
    for (i = 0; i < HOST_SRCS_COUNT; i++) srcs[count++] = HOST_SRCS[i];

    if (!compile_objs(srcs, count)) return false;
    if (!link_exe(BIN("osr"), "osr.c", &procs)) return false;
    return nob_procs_flush(&procs);
}

static bool build_runtime(void) {
#ifdef _WIN32
    nob_log(NOB_ERROR, "runtime C modules are supported only by the POSIX build");
    return false;
#else
    if (!cc_toolchain_check()) return false;
    if (!mkdir_if_needed(BUILD_DIR)) return false;
    return link_runtime(BIN("osr-runtime"));
#endif
}

/* --- autoconf-style command echo -------------------------------------
 *
 * nob.h echoes every command it starts in full, and offers no knob for it
 * short of NOB_NO_ECHO, which silences its whole log. At this tree's size
 * the full lines are unreadable: forty compiler invocations differing in
 * one filename each, then a link line naming twenty-three objects.
 *
 * The vendored header stays untouched -- its log handler hook is the
 * override point. Every command echo is rewritten the way an autoconf
 * build with silent rules prints: the tool that ran, then the file it
 * produced, one line per output.
 *
 *   TCC      build/obj/lib_net.o
 *   TCC      build/obj/install.o
 *   LD       build/install
 *   RUN      ../../build/test/test_ini
 *
 * The tool tag is the compiler's own name rather than a fixed "CC", so a
 * run says which compiler the detection settled on without anyone having
 * to read back to the "compiler: ..." line: GCC, TCC, ZIG CC, CLANG, CL.
 * Linking says LD whatever drives it, since that is the step's name, and
 * anything that is neither a compile nor a link -- a test binary being
 * started -- says RUN.
 *
 * Little is actually dropped: the flags are identical for every command in
 * a run (append_common_flags is the only source of them), and an object's
 * name says which source produced it (obj_of). `nob -v` / `nob --verbose`
 * -- or NOB_VERBOSE=1 in the environment, for the `make` wrapper, which
 * forwards no arguments -- leaves nob.h's own full lines alone. Every log
 * record that is not a command echo is passed through either way.
 */

/* the exact format string nob.h logs a started command with (see
 * nob__cmd_start_process); matching on it is what tells a command echo
 * apart from every other record the handler is handed. */
#define CMD_ECHO_FMT "CMD: %s"

static bool is_verbose_flag(const char *arg) {
    return strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0;
}

static bool is_time_flag(const char *arg) {
    return strcmp(arg, "-t") == 0 || strcmp(arg, "--time") == 0;
}

/* Cmd_Brief -- what one rendered command line boils down to: the tool that
 * ran it and the file it acted on. `name` points into the line the handler
 * was given, which outlives the printing of it. */
#define TAG_CAP 24
typedef struct {
    char tag[TAG_CAP];
    const char *name;
    size_t name_len;
} Cmd_Brief;

static bool tok_eq(const char *tok, size_t n, const char *s) {
    return n == strlen(s) && memcmp(tok, s, n) == 0;
}

static bool tok_ends_with(const char *tok, size_t n, const char *suffix) {
    size_t m = strlen(suffix);
    return n >= m && memcmp(tok + n - m, suffix, m) == 0;
}

static bool tok_starts_with(const char *tok, size_t n, const char *prefix) {
    size_t m = strlen(prefix);
    return n >= m && memcmp(tok, prefix, m) == 0;
}

/* tag_append -- one argument's basename, upper-cased, onto the tag being
 * built: "/usr/bin/gcc" -> "GCC", "cl.exe" -> "CL". Truncates rather than
 * overflowing; a cross-compiler with a triple in its name is long enough
 * to make that reachable, and a clipped tag still reads fine. */
static void tag_append(char *dst, size_t cap, const char *tok, size_t n) {
    const char *base = tok;
    size_t len = strlen(dst);
    size_t i;
    for (i = 0; i < n; i++) {
        if (tok[i] == '/' || tok[i] == '\\') base = tok + i + 1;
    }
    n -= (size_t)(base - tok);
    if (n > 4 && memcmp(base + n - 4, ".exe", 4) == 0) n -= 4;
    for (i = 0; i < n && len + 1 < cap; i++) {
        dst[len++] = (char)toupper((unsigned char)base[i]);
    }
    dst[len] = '\0';
}

/* tool_tag -- the compiler's name as it goes on the line. "zig cc" is two
 * arguments and one compiler, so a second word that is not a flag joins
 * the tag: ZIG CC. */
static void tool_tag(char *dst, size_t cap, const char *prog, size_t prog_len,
                     const char *sub, size_t sub_len) {
    dst[0] = '\0';
    tag_append(dst, cap, prog, prog_len);
    if (sub != NULL && sub_len > 0 && sub[0] != '-' && sub[0] != '/') {
        size_t len = strlen(dst);
        if (len + 1 < cap) {
            dst[len++] = ' ';
            dst[len] = '\0';
            tag_append(dst, cap, sub, sub_len);
        }
    }
}

/* brief_cmd -- reduce one rendered command line to a tag and a filename.
 *
 * The input is what nob_cmd_render() produced, so an argument containing a
 * space arrives wrapped in single quotes -- hence the quote handling in the
 * scanner; anything else splits on spaces.
 *
 * What the scan is after: the program (argv[0], whatever it looks like --
 * an absolute /usr/bin/gcc must not be mistaken for a flag), whether -c//c
 * makes this a compile, and where the output goes. cl glues that path onto
 * its flag (/Fobuild/obj/x.o), everything else takes it as the argument
 * after a bare -o. */
static void brief_cmd(const char *line, Cmd_Brief *out) {
    const char *prog = NULL;
    size_t prog_len = 0;
    const char *sub = NULL;
    size_t sub_len = 0;
    const char *path = NULL;
    size_t path_len = 0;
    const char *first_in = NULL;
    size_t first_in_len = 0;
    bool compiling = false;
    bool has_src = false;
    bool has_obj = false;
    bool want_path = false;
    size_t i = 0;
    size_t argi = 0;

    while (line[i] != '\0') {
        const char *tok;
        size_t n;
        char quote = '\0';

        while (line[i] == ' ') i++;
        if (line[i] == '\0') break;
        if (line[i] == '\'') { quote = '\''; i++; }
        tok = line + i;
        while (line[i] != '\0' && line[i] != (quote != '\0' ? quote : ' ')) i++;
        n = (size_t)(line + i - tok);
        if (quote != '\0' && line[i] == quote) i++;

        if (argi++ == 0) { prog = tok; prog_len = n; continue; }
        if (sub == NULL) { sub = tok; sub_len = n; }

        if (want_path) { path = tok; path_len = n; want_path = false; continue; }
        if (tok_eq(tok, n, "-c") || tok_eq(tok, n, "/c")) { compiling = true; continue; }
        if (tok_eq(tok, n, "-o")) { want_path = true; continue; }
        if (tok_starts_with(tok, n, "/Fo") || tok_starts_with(tok, n, "/Fe")) {
            path = tok + 3;
            path_len = n - 3;
            continue;
        }
        if (tok_ends_with(tok, n, ".c")) has_src = true;
        else if (tok_ends_with(tok, n, ".o") || tok_ends_with(tok, n, ".obj")) has_obj = true;
        else continue;
        if (first_in == NULL) { first_in = tok; first_in_len = n; }
    }

    if (path == NULL && !has_src && !has_obj) {
        /* neither compile nor link: a test binary being started. */
        strcpy(out->tag, "RUN");
        out->name = prog;
        out->name_len = prog_len;
        return;
    }
    if (compiling || (has_src && !has_obj)) {
        /* a plain compile, or the compile-and-link of the self-rebuild --
         * either way the compiler is what the line is about. */
        tool_tag(out->tag, TAG_CAP, prog, prog_len, sub, sub_len);
    } else {
        strcpy(out->tag, "LD");
    }
    out->name = path != NULL ? path : first_in;
    out->name_len = path != NULL ? path_len : first_in_len;
}

static void brief_log_handler(Nob_Log_Level level, const char *fmt, va_list args) {
    if (level == NOB_INFO && strcmp(fmt, CMD_ECHO_FMT) == 0) {
        Cmd_Brief brief;
        if (level < nob_minimal_log_level) return;
        brief_cmd(va_arg(args, const char *), &brief);
        /* no "[INFO]" here: these lines are the build's output, not
         * commentary on it, and the column they line up in is the point. */
        if (timing) {
            /* park it: run_timed() prints this line once the command it
             * describes has finished and its duration is known. */
            size_t n = brief.name_len;
            if (n > sizeof(pending_echo) - 16) n = sizeof(pending_echo) - 16;
            sprintf(pending_echo, "  %-8s %.*s", brief.tag, (int)n, brief.name);
            return;
        }
        fprintf(stderr, "  %-8s %.*s\n", brief.tag, (int)brief.name_len, brief.name);
        return;
    }
    nob_default_log_handler(level, fmt, args);
}

/* want_verbose -- read-only on argv: it still has to reach
 * NOB_GO_REBUILD_URSELF intact, so that a nob which re-execs itself after
 * rebuilding keeps the flag it was given. */
static bool want_verbose(int argc, char **argv) {
    const char *env = getenv("NOB_VERBOSE");
    int i;
    if (env != NULL && *env != '\0' && strcmp(env, "0") != 0) return true;
    for (i = 1; i < argc; i++) {
        if (is_verbose_flag(argv[i])) return true;
    }
    return false;
}

static bool want_timing(int argc, char **argv) {
    const char *env = getenv("NOB_TIME");
    int i;
    if (env != NULL && *env != '\0' && strcmp(env, "0") != 0) return true;
    for (i = 1; i < argc; i++) {
        if (is_time_flag(argv[i])) return true;
    }
    return false;
}

/* drop_verbose_flags -- compact argv so the subcommand parse in main() sees
 * subcommands only, and `nob -v test` (or `nob -t test`) works in either
 * order. */
static int drop_verbose_flags(int argc, char **argv) {
    int i;
    int n = 0;
    for (i = 0; i < argc; i++) {
        if (i > 0 && (is_verbose_flag(argv[i]) || is_time_flag(argv[i]))) continue;
        argv[n++] = argv[i];
    }
    return n;
}

int main(int argc, char **argv) {
    const char *program;
    const char *subcommand;

    timing = want_timing(argc, argv);
    if (timing) atexit(&report_timed_total);
    if (!want_verbose(argc, argv)) nob_set_log_handler(&brief_log_handler);
    NOB_GO_REBUILD_URSELF(argc, argv);
    argc = drop_verbose_flags(argc, argv);
    check_cc_detection();

    program = nob_shift(argv, argc);
    NOB_UNUSED(program);
    subcommand = argc > 0 ? nob_shift(argv, argc) : NULL;

    if (subcommand == NULL || strcmp(subcommand, "all") == 0 || strcmp(subcommand, "static") == 0) {
        if (!build_all()) return 1;
        if (actions == 0) nob_log(NOB_INFO, "everything up to date");
        return 0;
    }
    if (strcmp(subcommand, "runtime") == 0) {
        if (!build_runtime()) return 1;
        if (actions == 0) nob_log(NOB_INFO, "everything up to date");
        return 0;
    }
    if (strcmp(subcommand, "both") == 0) {
        if (!build_all() || !build_runtime()) return 1;
        if (actions == 0) nob_log(NOB_INFO, "everything up to date");
        return 0;
    }
    if (strcmp(subcommand, "test") == 0) {
        if (!build_all()) return 1;
#ifndef _WIN32
        /* The runtime host is a POSIX output (it dlopens what it compiles), so
         * it is built here only where it exists -- and it is built at all
         * because test/runtime_modules.sh drives it. */
        if (!build_runtime()) return 1;
#endif
        return run_all_tests() ? 0 : 1;
    }
    if (strcmp(subcommand, "clean") == 0) {
        return clean() ? 0 : 1;
    }

    nob_log(NOB_ERROR, "unknown subcommand '%s' (try: static, runtime, both, test, clean; -v for full command lines)", subcommand);
    return 1;
}
