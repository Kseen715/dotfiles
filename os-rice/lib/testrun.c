/* lib/testrun.c -- the C behind what used to be test/run.sh: the fast, no-container test suite.
 *
 *   <lint>            test/lint.sh, run first, failure is not fatal
 *   Unit tests:       every unit test under test/unit/, each named as it runs
 *   ALL GREEN         or SOME FAILED, and the exit status to match
 *
 * Takes the test directory as its one argument, because test/run.sh (the
 * shim) already resolved it from $0 -- and because that makes the whole
 * runner testable against a tree of fixture tests, which is what
 * test/unit/testrun_c_parity.sh does.
 *
 * The palette comes from the environment (test/run.sh sources lib/ui.sh
 * first, which exports it), and OSR_TEST_COLOR is exported from here for the
 * children: the unit tests blank OSR_* on purpose -- they source ui.sh with
 * NO_COLOR=1 so the code under test prints plain text -- so the terminal
 * decision has to be handed to test/lib.sh separately.
 *
 * The line shapes are lib/ui.sh's own printf calls, `%b` and all; see
 * common.h's expand_b for why that matters.
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

    fputc('\n', stdout);
    colored("OSR_CYAN", "Unit tests:");

    str_init(&path);
    str_addz(&path, here);
    str_addz(&path, "/unit/*.sh");
    /* GLOB_NOCHECK: with no matches sh leaves the pattern itself in the loop
     * variable (no nullglob in POSIX sh), and the run fails on it -- same
     * here, rather than silently reporting a green run of nothing. */
    if (glob(str_text(&path), GLOB_NOCHECK, NULL, &g) != 0) {
        str_free(&path);
        return 1;
    }
    str_free(&path);

    for (i = 0; i < g.gl_pathc; i++) {
        str_init(&line);
        str_addz(&line, "- ");
        str_addz(&line, base_name(g.gl_pathv[i]));
        colored("OSR_DIM", str_text(&line));
        str_free(&line);
        if (run_sh(g.gl_pathv[i]) != 0) rc = 1;
    }
    globfree(&g);

    fputc('\n', stdout);
    colored(rc == 0 ? "OSR_GREEN" : "OSR_RED", rc == 0 ? "ALL GREEN" : "SOME FAILED");
    return rc;
}
