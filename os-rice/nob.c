/* nob.c -- build script for the os-rice Windows C core. Replaces the old
 * Makefile: this only needs a C compiler, never a separate `make` binary
 * (the actual complaint that started this file: `make` on Windows meant
 * one more thing to install/PATH-manage besides gcc -- see
 * PLAN_UNIVERSAL.md decision 6). Bootstrap once, then just run it:
 *
 *   cc -o nob nob.c      (nob.exe on Windows)
 *   ./nob                (builds install)
 *   ./nob test           (builds + runs the C unit tests)
 *   ./nob clean
 *
 * The compiler used for the actual build is the host's default; set $CC
 * to override it (CC=clang ./nob). Both flag dialects are handled: a $CC
 * named cl/clang-cl gets MSVC's spelling (/W4, /c, /Fo, *.lib), everything
 * else gets gcc/clang's.
 *
 * After the first bootstrap you never type that gcc line again -- nob.h's
 * "Go Rebuild Urself" technology (NOB_GO_REBUILD_URSELF below) recompiles
 * nob on the spot whenever nob.c itself changes, before doing anything
 * else. osr.ps1/osr.bat lean on exactly this: they just run `nob.exe`,
 * even the very first time nob.exe doesn't exist yet (see osr.ps1).
 *
 * nob.c/nob.h are build-time tooling, run only on the developer/CI host --
 * unlike install.c/lib/*.c they are never cross-compiled for the XP
 * target, so unlike those files this one is free to use C99 (nob.h itself
 * requires it).
 */
#define NOB_IMPLEMENTATION
#include "nob.h"

#include <string.h>

/* EXE -- the host's executable suffix. Windows needs ".exe"; on a Linux/CI
 * host the produced binaries (and the tests we actually run there) carry no
 * suffix. */
#ifdef _WIN32
#define EXE ".exe"
#else
#define EXE ""
#endif

static const char *lib_srcs[] = {
    "lib/net.c",
    "lib/winpkg.c",
    "lib/winbin.c",
    "lib/elevate.c",
    "lib/manifest.c",
    "lib/ui.c",
    "lib/state.c",
    "lib/theme_list.c",
    "lib/theme_render.c",
    "lib/config_copy.c",
    "lib/fonts.c",
    "lib/wallpaper.c",
    "modules.c",
    "modules/src/common.c",
    "modules/fastfetch.c",
    "modules/wezterm.c",
    "modules/pwsh.c",
    "modules/oh-my-posh.c",
};
#define LIB_SRCS_COUNT (sizeof(lib_srcs) / sizeof(lib_srcs[0]))

static const char *test_names[] = {
    "net_parse_test", "winpkg_test", "winbin_test", "manifest_test", "theme_render_test",
    "config_copy_test",
};
#define TEST_COUNT (sizeof(test_names) / sizeof(test_names[0]))

/* DEFAULT_CC -- what to build with when $CC is unset: the same family of
 * compiler that built nob itself, which is the one known to exist on this
 * host. */
#if defined(_MSC_VER)
#define DEFAULT_CC "cl.exe"
#elif defined(__clang__)
#define DEFAULT_CC "clang"
#elif defined(__GNUC__)
#define DEFAULT_CC "gcc"
#else
#define DEFAULT_CC "cc"
#endif

static const char *cc(void) {
    const char *env = getenv("CC");
    return (env && *env) ? env : DEFAULT_CC;
}

/* $CC is a command line, not just a program name: "zig cc", "ccache gcc" and
 * "gcc -m32" are all ordinary values. exec takes one program plus separate
 * arguments, so split it on spaces once and keep the words. */
#define CC_MAX_WORDS 16
static const char *cc_words[CC_MAX_WORDS];
static size_t cc_word_count = 0;

static void cc_split(void) {
    char *buf;
    char *p;
    if (cc_word_count > 0) return;
    buf = (char *)malloc(strlen(cc()) + 1); /* leaked on purpose: lives for the run */
    NOB_ASSERT(buf != NULL);
    strcpy(buf, cc());
    for (p = buf; *p;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        NOB_ASSERT(cc_word_count < CC_MAX_WORDS);
        cc_words[cc_word_count++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
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

/* is_msvc -- does $CC speak MSVC's flag dialect (/W4, /c, /Fo) rather than
 * gcc/clang's? True for "cl", "clang-cl" and cross spellings ending in
 * "-cl"; deliberately false for plain "clang", which takes gcc flags.
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

#define OBJ_DIR "build/obj"

/* obj_of -- "lib/net.c" -> "build/obj/lib_net.o". All objects live in one
 * flat directory so the source tree stays clean and `clean` has a single
 * place to look; the path separators become '_' to keep names unique
 * without having to mirror the directory layout under build/obj.
 * Objects, not one big gcc line per binary, so the shared lib/modules
 * translation units are compiled once in parallel and then linked into all
 * of install.exe/wallpaper.exe/the test binaries.
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

/* compile_objs -- one gcc -c per source, all started at once (nob caps the
 * batch at nob_nprocs()), waited on together. */
static bool compile_objs(const char **srcs, size_t count) {
    Nob_Procs procs = {0};
    Nob_Cmd cmd = {0};
    size_t i;
    if (!nob_mkdir_if_not_exists("build")) return false;
    if (!nob_mkdir_if_not_exists(OBJ_DIR)) return false;
    for (i = 0; i < count; i++) {
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

/* link_exe -- main_src's own object + every shared object. Async when procs
 * is given, so the binaries of one batch link in parallel too. */
static bool link_exe(const char *bin, const char *main_src, Nob_Procs *procs) {
    Nob_Cmd cmd = {0};
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

/* run_test -- tests read fixtures via a path relative to test/unit_c/, so
 * the binary runs from there, same as it did under the old Makefile.
 */
static bool run_test(const char *name) {
    const char *bin_name = nob_temp_sprintf("./%s" EXE, name);
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
    Nob_Procs procs = {0};
    size_t i;
    for (i = 0; i < TEST_COUNT; i++) srcs[i] = nob_temp_sprintf("test/unit_c/%s.c", test_names[i]);
    if (!compile_objs(srcs, TEST_COUNT)) return false;
    for (i = 0; i < TEST_COUNT; i++) {
        const char *bin = nob_temp_sprintf("test/unit_c/%s" EXE, test_names[i]);
        if (!link_exe(bin, srcs[i], &procs)) return false;
    }
    return nob_procs_flush(&procs);
}

static bool run_all_tests(void) {
    bool ok = true;
    size_t i;
    if (!build_tests()) return false;
    for (i = 0; i < TEST_COUNT; i++) {
        if (!run_test(test_names[i])) ok = false;
    }
    return ok;
}

static void delete_if_exists(const char *path) {
    if (nob_file_exists(path) > 0) nob_delete_file(path);
}

static bool clean(void) {
    size_t i;
    delete_if_exists("install" EXE);
    delete_if_exists("wallpaper" EXE);
    delete_if_exists(obj_of("install.c"));
    delete_if_exists(obj_of("wallpaper.c"));
    for (i = 0; i < LIB_SRCS_COUNT; i++) delete_if_exists(obj_of(lib_srcs[i]));
    for (i = 0; i < TEST_COUNT; i++) {
        delete_if_exists(nob_temp_sprintf("test/unit_c/%s" EXE, test_names[i]));
        delete_if_exists(obj_of(nob_temp_sprintf("test/unit_c/%s.c", test_names[i])));
    }
    return true;
}

/* build_all -- every shared object plus the two program objects compiled in
 * one parallel batch, then both binaries linked from them. */
static bool build_all(void) {
    const char *srcs[LIB_SRCS_COUNT + 2];
    Nob_Procs procs = {0};
    size_t i;

    for (i = 0; i < LIB_SRCS_COUNT; i++) srcs[i] = lib_srcs[i];
    srcs[LIB_SRCS_COUNT] = "install.c";
    srcs[LIB_SRCS_COUNT + 1] = "wallpaper.c";

    if (!compile_objs(srcs, LIB_SRCS_COUNT + 2)) return false;
    if (!link_exe("install" EXE, "install.c", &procs)) return false;
    if (!link_exe("wallpaper" EXE, "wallpaper.c", &procs)) return false;
    return nob_procs_flush(&procs);
}

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);
    check_cc_detection();

    const char *program = nob_shift(argv, argc);
    NOB_UNUSED(program);
    const char *subcommand = argc > 0 ? nob_shift(argv, argc) : NULL;

    if (subcommand == NULL || strcmp(subcommand, "all") == 0) {
        return build_all() ? 0 : 1;
    }
    if (strcmp(subcommand, "test") == 0) {
        if (!build_all()) return 1;
        return run_all_tests() ? 0 : 1;
    }
    if (strcmp(subcommand, "clean") == 0) {
        return clean() ? 0 : 1;
    }

    nob_log(NOB_ERROR, "unknown subcommand '%s' (try: (none)/all, test, clean)", subcommand);
    return 1;
}
