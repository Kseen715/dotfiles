/* nob.c -- build script for the os-rice Windows C core. Replaces the old
 * Makefile: this only needs a C compiler, never a separate `make` binary
 * (the actual complaint that started this file: `make` on Windows meant
 * one more thing to install/PATH-manage besides gcc -- see
 * PLAN_UNIVERSAL.md decision 6). Bootstrap once, then just run it:
 *
 *   mkdir build
 *   cc -o build/nob nob.c   (build\nob.exe on Windows)
 *   ./build/nob             (builds build/install)
 *   ./build/nob test        (builds + runs the C unit tests)
 *   ./build/nob clean
 *   ./build/nob -v          (any of the above, with full command lines)
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
#define NOB_IMPLEMENTATION
#include "nob.h"

#include <ctype.h>
#include <string.h>

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

static const char *lib_srcs[] = {
    "lib/net.c",
    "lib/winpkg.c",
    "lib/winbin.c",
    "lib/elevate.c",
    "lib/manifest.c",
    "lib/winui.c",
    "lib/winstate.c",
    "lib/theme_list.c",
    "lib/theme_render.c",
    "lib/config_copy.c",
    "lib/fonts.c",
    "lib/wallpaper.c",
    "lib/wintweak.c",
    "provide_module.c",
    "modules.c",
    "modules/src/common.c",
    "modules/wezterm.c",
    "modules/pwsh.c",
    "modules/oh-my-posh.c",
    "modules/win-tweaks.c",
    "modules/win-update.c",
    "modules/win-debloat.c",
#ifdef _WIN32
    /* Modules that exist on BOTH systems are one file holding both branches
     * (modules/<name>.c, never modules/<os>/<name>.c) -- and the two branches
     * export the same osrm_<name> with their own tier's signature, so exactly
     * one core may have the object: the Windows core here, the POSIX harness
     * in posix_srcs below. Everything above this line is Windows-only code
     * whose POSIX branch is a stub, which is why those rows are unconditional
     * (test/unit_c/wintweak_test.c reads win-tweaks.c's tables on a Linux CI
     * host, and only gets to because that object is built there).
     *
     * Split on the HOST, like the -DWINVER flags and the -lwininet line
     * further down: this script has always assumed the host it runs on is the
     * system it builds for. */
    "modules/fastfetch.c",
#endif
};
#define LIB_SRCS_COUNT (sizeof(lib_srcs) / sizeof(lib_srcs[0]))

/* posix_srcs -- the POSIX harness core. One binary, build/osr, linked from
 * osr.c (the dispatcher) plus one translation unit per lib/<x>.sh that has
 * been rewritten -- the same arrangement as the Windows core's install.c plus
 * its lib units, and the reason the remaining sh files need no build step of
 * their own: they just exec build/osr.
 *
 * POSIX-only by design (unistd, termios, glob, sudo), so the whole binary is
 * skipped on a Windows host, where the sh side does not run at all.
 */
static const char *posix_srcs[] = {
    "lib/common.c",
    "lib/ui.c",
    "lib/log.c",
    "lib/state.c",
    "lib/user.c",
    "lib/detect.c",
    "lib/theme.c",
    "lib/install.c",
    "lib/testrun.c",
    "lib/benchmark.c",
    /* lib/bench/ -- CPU measurement. Unconditional: every architecture has a
     * throughput number worth taking, and the power layer degrades to "no
     * sensor" rather than to a wrong reading. */
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
    "lib/render.c",
    "lib/module.c",
    "lib/pkg.c",
    "lib/modules.c",
    "modules/flameshot.c",
    "modules/helpers.c",
    "modules/docker.c",
    "modules/tcc.c",
#ifndef _WIN32
    /* the other half of the split described in lib_srcs: this file's POSIX
     * branch is the Linux fastfetch module, its Windows branch is the Windows
     * one, and only one of them is ever in a binary. */
    "modules/fastfetch.c",
#endif
};
#define POSIX_SRCS_COUNT (sizeof(posix_srcs) / sizeof(posix_srcs[0]))

static const char *test_names[] = {
    "net_parse_test", "winpkg_test", "winbin_test", "manifest_test", "theme_render_test",
    "config_copy_test", "wintweak_test",
};
#define TEST_COUNT (sizeof(test_names) / sizeof(test_names[0]))

/* posix_test_names -- tests of the POSIX-only units, which cannot be linked
 * the way the ones above are.
 *
 * The tests in test_names link against every lib_srcs object, and lib/winui.c
 * is one of them -- it defines osr_info/osr_warn, and so does lib/common.c on
 * the POSIX side. The two are alternative implementations of the same five log
 * lines for the two cores, so they can never appear in one binary. A test of
 * lib/uv/* therefore includes the .c files it needs directly (a unity build)
 * and links nothing else at all, which is also why these tests can reach
 * static helpers the header does not export.
 */
static const char *posix_test_names[] = {
    "uv_journal_test", "bench_test"
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
static const char *cc_ladder[] = {"tcc", "zig cc", "gcc", "cc", DEFAULT_CC};
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
        nob_cmd_append(&cmd, "/nologo", CC_PROBE_SRC, "/Fo" CC_PROBE_OBJ, "/Fe" CC_PROBE_BIN);
    } else {
        nob_cmd_append(&cmd, CC_PROBE_SRC, "-o", CC_PROBE_BIN);
    }
    /* the "CMD: ..." line for a candidate we are only trying out is noise */
    nob_minimal_log_level = NOB_WARNING;
    ok = nob_cmd_run(&cmd, .stdout_path = DEV_NULL, .stderr_path = DEV_NULL);
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

/* check_cc_detection -- runs on every invocation; the dialect pick is the
 * one thing here that silently produces a garbage command line if wrong. */
static void check_cc_detection(void) {
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

/* append_common_flags -- the same std/warning/XP-floor flags every binary
 * this script produces is built with, in whichever dialect $CC speaks. XP
 * floor: see PLAN_UNIVERSAL.md's toolchain matrix -- checked today against
 * an ordinary current mingw-w64; the pinned XP toolchain itself is still
 * long-away-planned (Task 0.1).
 */
static void append_common_flags(Nob_Cmd *cmd) {
    append_cc(cmd);
    if (is_msvc()) {
        /* No /std: equivalent to -std=c89 -- MSVC's C mode is already C89
         * plus extensions and /Za (the closest thing) is long discouraged.
         * /wd4505 is -Wno-unused-function; the CRT one silences the
         * fopen/getenv "deprecation" that C89 code cannot avoid. cl only
         * ever targets Windows, so the XP defines are unconditional here. */
        nob_cmd_append(cmd, "/nologo", "/W4", "/O2");
        nob_cmd_append(cmd, "/wd4505", "/D_CRT_SECURE_NO_WARNINGS");
        nob_cmd_append(cmd, "/DWINVER=0x0501", "/D_WIN32_WINNT=0x0501");
        return;
    }
    nob_cmd_append(cmd, "-std=c89", "-Wall", "-Wextra", "-pedantic", "-O2");
    /* helpers used only by one platform branch of a file are dead on the
     * other -- that is expected, not a defect. */
    nob_cmd_append(cmd, "-Wno-unused-function");
#ifdef _WIN32
    nob_cmd_append(cmd, "-DWINVER=0x0501", "-D_WIN32_WINNT=0x0501");
#endif
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
static const char *obj_of(const char *src) {
    size_t len = strlen(src);
    char *obj = nob_temp_sprintf("%s/%.*s.o", OBJ_DIR, (int)(len - 2), src); /* strip ".c" */
    char *p;
    for (p = obj + sizeof(OBJ_DIR); *p; p++) {
        if (*p == '/' || *p == '\\') *p = '_';
    }
    return obj;
}

/* --- incremental builds ---------------------------------------------
 *
 * The rule is the usual make one, done with file timestamps: an object is
 * recompiled only when it is older than something it is made of, a binary
 * relinked only when it is older than its objects. Two runs in a row with
 * nothing edited in between means the second one runs no compiler at all.
 *
 * deps -- what every object depends on besides its own .c file: nob.c
 * (the compiler flags live in it, so editing them has to invalidate every
 * object) and every .h in the tree. Which .c includes which .h is
 * deliberately not tracked -- no -MMD dep files to parse, nothing that
 * works in only one compiler's dialect -- so touching any header rebuilds
 * everything. That over-builds, but it cannot under-build, and
 * under-building is the failure mode that silently links a stale object
 * and costs an afternoon. A full build of this tree is a couple seconds.
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

/* collect_deps -- walk the tree once per run for headers. If the walk
 * fails (an unreadable directory, say) deps_usable stays false and every
 * timestamp check below answers "rebuild": without the full header list
 * we cannot prove anything is up to date, and guessing wrong ships a
 * stale binary. */
static void collect_deps(void) {
    if (deps_collected) return;
    deps_collected = true;
    deps_usable = nob_walk_dir(".", collect_dep);
    nob_da_append(&deps, "nob.c");
}

/* needs_compile -- is src's object missing, older than src, or older than
 * any of deps? A timestamp we could not read (-1) counts as "yes" for the
 * same reason as above. */
static bool needs_compile(const char *src) {
    const char *obj = obj_of(src);
    collect_deps();
    if (!deps_usable) return true;
    if (nob_needs_rebuild1(obj, src) != 0) return true;
    return nob_needs_rebuild(obj, deps.items, deps.count) != 0;
}

/* needs_link -- is bin missing or older than any object linked into it?
 * Called only after compile_objs() has flushed, so the objects' mtimes
 * are final by the time we look at them. */
static bool needs_link(const char *bin, const char *main_src) {
    const char *objs[LIB_SRCS_COUNT + 1];
    size_t i;
    objs[0] = obj_of(main_src);
    for (i = 0; i < LIB_SRCS_COUNT; i++) objs[i + 1] = obj_of(lib_srcs[i]);
    return nob_needs_rebuild(bin, objs, LIB_SRCS_COUNT + 1) != 0;
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
        append_common_flags(&cmd);
        if (is_msvc()) {
            /* /Fo takes its path glued on, no separate argument. The .o
             * name (rather than MSVC's usual .obj) is fine and keeps
             * obj_of()/clean() single-dialect. */
            nob_cmd_append(&cmd, "/c", srcs[i], nob_temp_sprintf("/Fo%s", obj_of(srcs[i])));
        } else {
            nob_cmd_append(&cmd, "-c", srcs[i], "-o", obj_of(srcs[i]));
        }
        if (!nob_cmd_run(&cmd, .async = &procs)) return false;
    }
    return nob_procs_flush(&procs);
}

static void append_lib_objs(Nob_Cmd *cmd) {
    size_t i;
    for (i = 0; i < LIB_SRCS_COUNT; i++) nob_cmd_append(cmd, obj_of(lib_srcs[i]));
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
        nob_cmd_append(cmd, "wininet.lib", "advapi32.lib", "user32.lib", "shell32.lib");
        return;
    }
#ifdef _WIN32
    nob_cmd_append(cmd, "-lwininet", "-ladvapi32", "-luser32", "-lshell32");
#else
    NOB_UNUSED(cmd);
#endif
}

/* link_posix -- link the POSIX harness core: osr.c's object plus every
 * lib/osr_*.c object, and none of the Windows core's objects or link
 * libraries. Nothing here includes a Windows header, and nothing there is
 * reachable from ./osr. */
static bool link_posix(const char *bin) {
    Nob_Cmd cmd = {0};
    const char *objs[POSIX_SRCS_COUNT + 1];
    size_t i;
    objs[0] = obj_of("osr.c");
    for (i = 0; i < POSIX_SRCS_COUNT; i++) objs[i + 1] = obj_of(posix_srcs[i]);
    if (nob_needs_rebuild(bin, objs, POSIX_SRCS_COUNT + 1) == 0) return true;
    actions++;
    append_common_flags(&cmd);
    if (is_msvc()) {
        nob_cmd_append(&cmd, nob_temp_sprintf("/Fe%s", bin));
    } else {
        nob_cmd_append(&cmd, "-o", bin);
    }
    for (i = 0; i < POSIX_SRCS_COUNT + 1; i++) nob_cmd_append(&cmd, objs[i]);
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
        nob_cmd_append(&cmd, "-o", bin);
    }
    nob_cmd_append(&cmd, obj_of(main_src));
    append_lib_objs(&cmd);
    append_common_libs(&cmd);
    return nob_cmd_run(&cmd, .async = procs);
}

/* link_standalone -- one object, no lib objects, no Windows link libraries:
 * the unity-built POSIX tests described at posix_test_names. */
static bool link_standalone(const char *bin, const char *main_src, Nob_Procs *procs) {
    Nob_Cmd cmd = {0};
    const char *obj = obj_of(main_src);
    if (nob_needs_rebuild(bin, &obj, 1) == 0) return true;
    actions++;
    append_common_flags(&cmd);
    if (is_msvc()) {
        nob_cmd_append(&cmd, nob_temp_sprintf("/Fe%s", bin));
    } else {
        nob_cmd_append(&cmd, "-o", bin);
    }
    nob_cmd_append(&cmd, obj);
    return nob_cmd_run(&cmd, .async = procs);
}

/* run_test -- tests read fixtures via a path relative to test/unit_c/, so
 * that is still the working directory they run in; only the binary itself
 * moved out to build/test/, hence the climb back up in its path.
 */
static bool run_test(const char *name) {
    const char *bin_name = nob_temp_sprintf("../../" TEST_BIN_DIR "/%s" EXE, name);
    Nob_Cmd cmd = {0};
    nob_log(NOB_INFO, "--- %s ---", name);
    nob_cmd_append(&cmd, bin_name);
    if (!nob_set_current_dir("test/unit_c")) return false;
    bool ok = nob_cmd_run(&cmd);
    nob_set_current_dir("../..");
    return ok;
}

static bool build_tests(void) {
    const char *srcs[TEST_COUNT];
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

static bool run_all_tests(void) {
    bool ok = true;
    size_t i;
    if (!build_tests()) return false;
    for (i = 0; i < TEST_COUNT; i++) {
        if (!run_test(test_names[i])) ok = false;
    }
#ifndef _WIN32
    for (i = 0; i < POSIX_TEST_COUNT; i++) {
        if (!run_test(posix_test_names[i])) ok = false;
    }
#endif
    return ok;
}

static void delete_if_exists(const char *path) {
    if (nob_file_exists(path) > 0) nob_delete_file(path);
}

static bool clean(void) {
    size_t i;
    delete_if_exists(BIN("install"));
    delete_if_exists(BIN("wallpaper"));
    delete_if_exists(BIN("osr"));
    delete_if_exists(obj_of("install.c"));
    delete_if_exists(obj_of("wallpaper.c"));
    delete_if_exists(obj_of("osr.c"));
    for (i = 0; i < POSIX_SRCS_COUNT; i++) delete_if_exists(obj_of(posix_srcs[i]));
    for (i = 0; i < LIB_SRCS_COUNT; i++) delete_if_exists(obj_of(lib_srcs[i]));
    for (i = 0; i < TEST_COUNT; i++) {
        delete_if_exists(nob_temp_sprintf(TEST_BIN_DIR "/%s" EXE, test_names[i]));
        delete_if_exists(obj_of(nob_temp_sprintf("test/unit_c/%s.c", test_names[i])));
    }
    for (i = 0; i < POSIX_TEST_COUNT; i++) {
        delete_if_exists(nob_temp_sprintf(TEST_BIN_DIR "/%s" EXE, posix_test_names[i]));
        delete_if_exists(obj_of(nob_temp_sprintf("test/unit_c/%s.c", posix_test_names[i])));
    }
    /* the compiler bookkeeping goes too: with no objects left there is no
     * toolchain to record, and dropping the detection cache is what makes
     * `clean` the way to have a newly installed compiler noticed. */
    delete_if_exists(CC_DETECTED);
    delete_if_exists(CC_STAMP);
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
static bool build_all(void) {
    const char *srcs[LIB_SRCS_COUNT + 2 + POSIX_SRCS_COUNT + 1];
    size_t count = 0;
    Nob_Procs procs = {0};
    size_t i;

    if (!cc_toolchain_check()) return false;

    for (i = 0; i < LIB_SRCS_COUNT; i++) srcs[count++] = lib_srcs[i];
    srcs[count++] = "install.c";
    srcs[count++] = "wallpaper.c";
#ifndef _WIN32
    srcs[count++] = "osr.c";
    for (i = 0; i < POSIX_SRCS_COUNT; i++) srcs[count++] = posix_srcs[i];
#endif

    if (!compile_objs(srcs, count)) return false;
    if (!link_exe(BIN("install"), "install.c", &procs)) return false;
    if (!link_exe(BIN("wallpaper"), "wallpaper.c", &procs)) return false;
    if (!nob_procs_flush(&procs)) return false;
#ifndef _WIN32
    if (!link_posix(BIN("osr"))) return false;
#endif
    return true;
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

/* drop_verbose_flags -- compact argv so the subcommand parse in main() sees
 * subcommands only, and `nob -v test` works in either order. */
static int drop_verbose_flags(int argc, char **argv) {
    int i;
    int n = 0;
    for (i = 0; i < argc; i++) {
        if (i > 0 && is_verbose_flag(argv[i])) continue;
        argv[n++] = argv[i];
    }
    return n;
}

int main(int argc, char **argv) {
    if (!want_verbose(argc, argv)) nob_set_log_handler(&brief_log_handler);
    NOB_GO_REBUILD_URSELF(argc, argv);
    argc = drop_verbose_flags(argc, argv);
    check_cc_detection();

    const char *program = nob_shift(argv, argc);
    NOB_UNUSED(program);
    const char *subcommand = argc > 0 ? nob_shift(argv, argc) : NULL;

    if (subcommand == NULL || strcmp(subcommand, "all") == 0) {
        if (!build_all()) return 1;
        if (actions == 0) nob_log(NOB_INFO, "everything up to date");
        return 0;
    }
    if (strcmp(subcommand, "test") == 0) {
        if (!build_all()) return 1;
        return run_all_tests() ? 0 : 1;
    }
    if (strcmp(subcommand, "clean") == 0) {
        return clean() ? 0 : 1;
    }

    nob_log(NOB_ERROR, "unknown subcommand '%s' (try: (none)/all, test, clean; -v for full command lines)", subcommand);
    return 1;
}
