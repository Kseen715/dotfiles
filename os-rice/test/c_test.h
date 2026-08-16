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
#ifndef OSR_C_TEST_H
#define OSR_C_TEST_H

#include <stdio.h>
#include <string.h>

static int osr_t_pass = 0;
static int osr_t_fail = 0;

#define OSR_T_INIT() do { osr_t_pass = 0; osr_t_fail = 0; } while (0)

#define osr_t_ok(label) \
    do { osr_t_pass++; printf("  ok   %s\n", (label)); } while (0)

#define osr_t_fail_msg(label, detail) \
    do { osr_t_fail++; printf("  FAIL %s (%s)\n", (label), (detail)); } while (0)

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

/* osr_t_finish() -- print the summary line, return a process exit code. */
static int osr_t_finish(void) {
    printf("  --- %d passed, %d failed ---\n", osr_t_pass, osr_t_fail);
    return osr_t_fail == 0 ? 0 : 1;
}

#endif /* OSR_C_TEST_H */
