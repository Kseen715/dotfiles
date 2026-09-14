/* test/c_test.h -- tiny assertion helpers for the C unit tests.
 *
 * C89, no dependencies beyond <stdio.h>/<string.h>, mirrors the pass/fail/
 * finish shape of test/lib.sh (POSIX sh) so the two suites read the same way
 * even though nothing here is generated from the other.
 *
 * Usage (one .c file per unit, same convention as the sh unit tests):
 *
 *   #include "../c_test.h"
 *   int main(void) {
 *       OSR_T_INIT();
 *       osr_t_eq_str("literal", my_fn(), "expected");
 *       osr_t_eq_int(1, my_other_fn());
 *       return osr_t_finish();
 *   }
 */
/* Feature macros before the first libc header, because this header IS the
 * first include of every unit test and those tests are unity builds: a test
 * that pulls in lib/common.c gets common.c's own _POSIX_C_SOURCE block far
 * too late to matter, and setenv/lstat/realpath then compile as implicit
 * declarations -- which clang reads as an error the moment one of them
 * returns a pointer. Same values common.c asks for, so the later definition
 * is an identical redefinition. */
#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#define _DARWIN_C_SOURCE 1
#endif

#ifndef OSR_C_TEST_H
#define OSR_C_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int osr_t_pass = 0;
static int osr_t_fail = 0;

#define OSR_T_INIT() do { osr_t_pass = 0; osr_t_fail = 0; } while (0)

/* The same two knobs the sandbox harness reads (test/harness.c): the runner
 * hands the palette down in OSR_TEST_COLOR, and asks for the compact
 * one-character-per-assertion readout with OSR_TEST_DOTS. A unity test that
 * printed ok-lines while every other test printed dots would break the one
 * readout the runner draws, so both flavours answer to the same variables. */
static const char *osr_t_color(const char *code) {
    const char *c = getenv("OSR_TEST_COLOR");
    return (c != NULL && *c != '\0') ? code : "";
}
static int osr_t_dots(void) {
    const char *c = getenv("OSR_TEST_DOTS");
    return c != NULL && *c != '\0';
}

static void osr_t_ok_impl(const char *label) {
    osr_t_pass++;
    if (osr_t_dots()) {
        printf("%s.%s", osr_t_color("\033[0;32m"), osr_t_color("\033[0m"));
        fflush(stdout);
        return;
    }
    printf("  %sok%s   %s\n", osr_t_color("\033[0;32m"), osr_t_color("\033[0m"), label);
}

/* Detail on stderr, like the sandbox harness: the runner captures the two
 * streams separately and prints stderr under FAILURES. */
static void osr_t_fail_impl(const char *label, const char *detail) {
    osr_t_fail++;
    if (osr_t_dots()) printf("%sF%s", osr_t_color("\033[0;31m"), osr_t_color("\033[0m"));
    fflush(stdout);
    fprintf(stderr, "  %sFAIL%s %s (%s)\n",
            osr_t_color("\033[0;31m"), osr_t_color("\033[0m"), label, detail);
    fflush(stderr);
}

#define osr_t_ok(label) osr_t_ok_impl((label))
#define osr_t_fail_msg(label, detail) osr_t_fail_impl((label), (detail))

/* osr_t_eq_str(label, actual, expected) -- NULL-safe string compare. */
static void osr_t_eq_str_impl(const char *label, const char *actual, const char *expected) {
    if (actual != NULL && expected != NULL && strcmp(actual, expected) == 0) {
        osr_t_ok(label);
    } else {
        char detail[512];
        sprintf(detail, "expected '%s', got '%s'",
                expected ? expected : "(null)", actual ? actual : "(null)");
        osr_t_fail_msg(label, detail);
    }
}
#define osr_t_eq_str(label, actual, expected) osr_t_eq_str_impl((label), (actual), (expected))

/* osr_t_eq_int(label, actual, expected) -- long compare, works for int/long/size_t. */
static void osr_t_eq_int_impl(const char *label, long actual, long expected) {
    if (actual == expected) {
        osr_t_ok(label);
    } else {
        char detail[128];
        sprintf(detail, "expected %ld, got %ld", expected, actual);
        osr_t_fail_msg(label, detail);
    }
}
#define osr_t_eq_int(label, actual, expected) osr_t_eq_int_impl((label), (long)(actual), (long)(expected))

#define osr_t_true(label, cond) \
    do { if (cond) { osr_t_ok(label); } else { osr_t_fail_msg((label), "expected true"); } } while (0)

/* osr_t_finish() -- print the summary line, return a process exit code. In
 * dots mode that line is the machine-readable count the runner totals up. */
static int osr_t_finish(void) {
    if (osr_t_dots()) {
        printf("\n@@ %d %d\n", osr_t_pass, osr_t_fail);
        fflush(stdout);
        return osr_t_fail == 0 ? 0 : 1;
    }
    printf("  --- %d passed, %d failed ---\n", osr_t_pass, osr_t_fail);
    return osr_t_fail == 0 ? 0 : 1;
}

#endif /* OSR_C_TEST_H */
