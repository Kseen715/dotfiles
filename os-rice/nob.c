/* nob.c -- build script for the os-rice Windows C core. Replaces the old
 * Makefile: this only needs a C compiler, never a separate `make` binary
 * (the actual complaint that started this file: `make` on Windows meant
 * one more thing to install/PATH-manage besides gcc -- see
 * PLAN_UNIVERSAL.md decision 6). Bootstrap once, then just run it:
 *
 *   gcc -o nob.exe nob.c
 *   nob.exe              (builds install.exe)
 *   nob.exe test         (builds + runs the C unit tests)
 *   nob.exe clean
 *
 * After the first bootstrap you never type that gcc line again -- nob.h's
 * "Go Rebuild Urself" technology (NOB_GO_REBUILD_URSELF below) recompiles
 * nob.exe on the spot whenever nob.c itself changes, before doing anything
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

static const char *lib_srcs[] = {
    "lib/net.c",
    "lib/winpkg.c",
    "lib/manifest.c",
    "lib/ui.c",
    "lib/state.c",
    "lib/theme_list.c",
    "lib/theme_render.c",
    "lib/config_copy.c",
    "lib/fonts.c",
    "lib/wallpaper.c",
    "modules.c",
};
#define LIB_SRCS_COUNT (sizeof(lib_srcs) / sizeof(lib_srcs[0]))

static const char *test_names[] = {
    "net_parse_test", "winpkg_test", "manifest_test", "theme_render_test", "config_copy_test",
};
#define TEST_COUNT (sizeof(test_names) / sizeof(test_names[0]))

/* append_common_flags -- the same std/warning/XP-floor flags every binary
 * this script produces is built with. XP floor: see PLAN_UNIVERSAL.md's
 * toolchain matrix -- checked today against an ordinary current mingw-w64;
 * the pinned XP toolchain itself is still long-away-planned (Task 0.1).
 */
static void append_common_flags(Nob_Cmd *cmd) {
    nob_cmd_append(cmd, "gcc");
    nob_cmd_append(cmd, "-std=c89", "-Wall", "-Wextra", "-pedantic", "-O2");
    nob_cmd_append(cmd, "-DWINVER=0x0501", "-D_WIN32_WINNT=0x0501");
}

static void append_lib_srcs(Nob_Cmd *cmd) {
    size_t i;
    for (i = 0; i < LIB_SRCS_COUNT; i++) nob_cmd_append(cmd, lib_srcs[i]);
}

/* -lwininet: lib/net.c's WinInet calls.
 * -ladvapi32: lib/fonts.c's RegOpenKeyExA/RegEnumValueA (registry font check).
 * -lshell32: SystemParametersInfoA (lib/wallpaper.c) links via user32 in
 * most mingw setups, but shell32 covers the COM-ish helpers if that ever
 * grows; included now so a future addition doesn't need a second flag
 * change hunted down by a link error.
 */
static void append_common_libs(Nob_Cmd *cmd) {
    nob_cmd_append(cmd, "-lwininet", "-ladvapi32", "-luser32", "-lshell32");
}

static bool build_install_exe(void) {
    Nob_Cmd cmd = {0};
    append_common_flags(&cmd);
    nob_cmd_append(&cmd, "-o", "install.exe", "install.c");
    append_lib_srcs(&cmd);
    append_common_libs(&cmd);
    return nob_cmd_run(&cmd);
}

static bool build_wallpaper_exe(void) {
    Nob_Cmd cmd = {0};
    append_common_flags(&cmd);
    nob_cmd_append(&cmd, "-o", "wallpaper.exe", "wallpaper.c");
    append_lib_srcs(&cmd);
    append_common_libs(&cmd);
    return nob_cmd_run(&cmd);
}

static bool build_one_test(const char *name) {
    const char *src = nob_temp_sprintf("test/unit_c/%s.c", name);
    const char *bin = nob_temp_sprintf("test/unit_c/%s.exe", name);
    Nob_Cmd cmd = {0};
    append_common_flags(&cmd);
    nob_cmd_append(&cmd, "-o", bin, src);
    append_lib_srcs(&cmd);
    append_common_libs(&cmd);
    return nob_cmd_run(&cmd);
}

/* run_test -- tests read fixtures via a path relative to test/unit_c/, so
 * the binary runs from there, same as it did under the old Makefile.
 */
static bool run_test(const char *name) {
    const char *bin_name = nob_temp_sprintf("%s.exe", name);
    Nob_Cmd cmd = {0};
    nob_log(NOB_INFO, "--- %s ---", name);
    nob_cmd_append(&cmd, bin_name);
    if (!nob_set_current_dir("test/unit_c")) return false;
    bool ok = nob_cmd_run(&cmd);
    nob_set_current_dir("../..");
    return ok;
}

static bool run_all_tests(void) {
    bool ok = true;
    size_t i;
    for (i = 0; i < TEST_COUNT; i++) {
        if (!build_one_test(test_names[i])) ok = false;
    }
    if (!ok) return false;
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
    delete_if_exists("install.exe");
    delete_if_exists("wallpaper.exe");
    for (i = 0; i < TEST_COUNT; i++) {
        delete_if_exists(nob_temp_sprintf("test/unit_c/%s.exe", test_names[i]));
    }
    return true;
}

static bool build_all(void) {
    bool ok = true;
    if (!build_install_exe()) ok = false;
    if (!build_wallpaper_exe()) ok = false;
    return ok;
}

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

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
