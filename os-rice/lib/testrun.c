/* lib/testrun.c -- the C behind what used to be test/run.sh: the fast, no-container test suite.
 *
 *   <lint>            test/lint.sh, run first, failure is not fatal
 *   C unit tests:     ./build/nob test -- every test under test/unit_c/,
 *                     built and run by the build system that owns them
 *   Unit tests:       every unit test under test/unit/, each named as it runs
 *   ALL GREEN         or SOME FAILED, and the exit status to match
 *
 * Takes the test directory as its one argument, because the shim that used
 * to call it already resolved that from $0 -- and because it makes the whole
 * runner testable against a tree of fixture tests, which is what
 * test/unit_c/testrun_test.c does.
 *
 * The palette comes from the environment (osr's startup_env decides it once,
 * against the real terminal), and OSR_TEST_COLOR is exported from here for the
 * children: a test blanks OSR_* on purpose, so that the code under test prints
 * plain text and its output can be compared -- which means the terminal
 * decision has to reach the test harness by a name of its own. test/harness.c
 * reads it, and colours its own ok/FAIL lines with it.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "cmds.h"

#include <glob.h>
#include <sys/wait.h>
#include <unistd.h>

/* emit -- print one composed line now. The runner interleaves its own
 * output with its children's, so nothing may sit in a buffer across a fork.
 */
static void emit(Str *s) {
    out_flush(s);
    str_free(s);
    str_init(s);
}

/* colored -- printf '%b%s%b\n' "$color" text "$OSR_NC", ui.sh's shape for
 * every line this runner prints itself. */
static void colored(const char *color_env, const char *text) {
    Str out;
    str_init(&out);
    if (!expand_b(&out, color(color_env))) {
        str_addz(&out, text);
        if (!expand_b(&out, color("OSR_NC"))) str_addc(&out, '\n');
    }
    emit(&out);
}

/* run_prog -- fork/exec a program with an argument, returning its exit
 * status. Same shape as run_sh and for the same reason: no shell in the
 * middle whose own diagnostics could reach the output. */
static int run_prog(const char *prog, const char *arg, const char *dir) {
    pid_t pid;
    int status;

    fflush(stdout);
    fflush(stderr);
    pid = fork();
    if (pid < 0) return 1;
    if (pid == 0) {
        if (dir != NULL && chdir(dir) != 0) _exit(127);
        execl(prog, prog, arg, (char *)NULL);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) return 1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}

/* run_sh -- `sh <path>`, returning its exit status. Not system(): that would
 * put an extra shell in the middle whose own diagnostics could reach the
 * output, and this program's job is to be transparent about what the test
 * printed. A test that cannot be started counts as a failure, which is what
 * `sh "$t"` did too.
 */
static int run_sh(const char *path) {
    pid_t pid;
    int status;

    fflush(stdout);
    fflush(stderr);
    pid = fork();
    if (pid < 0) return 1;
    if (pid == 0) {
        execlp("sh", "sh", path, (char *)NULL);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) return 1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}

/* base_name -- what `basename "$t"` printed: everything after the last '/'.
 * The paths come from our own glob, so no trailing-slash case to handle. */
static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

int osr_testrun_main(int argc, char **argv) {
    const char *here;
    Str path;
    Str line;
    glob_t g;
    int rc = 0;
    size_t i;

    if (argc != 2) {
        fputs("usage: osr test-run <test-dir>\n", stderr);
        return 2;
    }
    here = argv[1];

    /* [ -n "$OSR_GREEN" ] && OSR_TEST_COLOR=1 || OSR_TEST_COLOR='' */
    if (setenv("OSR_TEST_COLOR", env_is_set("OSR_GREEN") ? "1" : "", 1) != 0) return 1;

    str_init(&path);
    str_addz(&path, here);
    str_addz(&path, "/lint.sh");
    if (run_sh(str_text(&path)) != 0) rc = 1;
    str_free(&path);

    /* The C unit tests, built and run by nob.c -- it owns the compiler
     * choice and the dependency tracking, and duplicating either here to
     * save one fork would put two answers to "is this test up to date" in
     * the tree. `here` is <root>/test, so the root is its parent.
     *
     * Silent when there is no build system to ask, header included. The
     * runner is pointed at a tree of fixture tests by its own parity test,
     * and a tree with no nob.c has no C tests to run -- announcing an empty
     * section there would be noise, and treating it as a failure would make
     * "can this runner run a directory of tests" depend on the directory
     * being a whole project. */
    str_init(&path);
    str_addz(&path, here);
    str_addz(&path, "/..");
    {
        Str nob;
        Str nobsrc;
        str_init(&nob);
        str_addz(&nob, str_text(&path));
        str_addz(&nob, "/build/nob");
        str_init(&nobsrc);
        str_addz(&nobsrc, str_text(&path));
        str_addz(&nobsrc, "/nob.c");
        /* Both, and nob.c is the one that matters: the fixture tree symlinks
         * build/ to the real one, so the binary is there while the sources
         * it builds from are not. nob.c present is what "this is a project
         * with C tests" actually means. */
        if (access(str_text(&nobsrc), R_OK) == 0 && access(str_text(&nob), X_OK) == 0) {
            fputc('\n', stdout);
            colored("OSR_CYAN", "C unit tests:");
            if (run_prog(str_text(&nob), "test", str_text(&path)) != 0) rc = 1;
        }
        str_free(&nob);
        str_free(&nobsrc);
    }
    str_free(&path);

    str_init(&path);
    str_addz(&path, here);
    str_addz(&path, "/unit/*.sh");
    /* The shell tier is OPTIONAL now, and empty: every test is a C behaviour
     * test under test/unit_c/, run by the step above. So no matches is not a
     * failure -- but it is not silent either, because "a green run of nothing"
     * is the one way a test suite lies. Without GLOB_NOCHECK an unmatched
     * pattern simply yields no paths, and the section prints nothing at all;
     * a `.sh` test dropped back in is picked up again with no other change. */
    if (glob(str_text(&path), 0, NULL, &g) == 0) {
        if (g.gl_pathc > 0) {
            fputc('\n', stdout);
            colored("OSR_CYAN", "Unit tests (shell):");
        }
        for (i = 0; i < g.gl_pathc; i++) {
            str_init(&line);
            str_addz(&line, "- ");
            str_addz(&line, base_name(g.gl_pathv[i]));
            colored("OSR_DIM", str_text(&line));
            str_free(&line);
            if (run_sh(g.gl_pathv[i]) != 0) rc = 1;
        }
        globfree(&g);
    }
    str_free(&path);

    fputc('\n', stdout);
    colored(rc == 0 ? "OSR_GREEN" : "OSR_RED", rc == 0 ? "ALL GREEN" : "SOME FAILED");
    return rc;
}
