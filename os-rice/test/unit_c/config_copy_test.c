/* test/unit_c/config_copy_test.c -- lib/config_copy.c. */
#include "../c_test.h"
#include "../../lib/config_copy.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void test_expand_home(void) {
    char out[512];
    const char *userprofile = getenv("USERPROFILE");

    osr_expand_home("~/.config/osr/state", out, sizeof(out));
    if (userprofile != NULL) {
        char expected[512];
        sprintf(expected, "%s/.config/osr/state", userprofile);
        osr_t_eq_str("expand_home: ~/ expands to USERPROFILE", out, expected);
    } else {
        osr_t_true("expand_home: skipped, no USERPROFILE in this environment", 1);
    }

    osr_expand_home("C:\\already\\absolute\\path", out, sizeof(out));
    osr_t_eq_str("expand_home: a path with no leading ~ passes through unchanged",
        out, "C:\\already\\absolute\\path");

    osr_expand_home("~", out, sizeof(out));
    if (userprofile != NULL) {
        osr_t_eq_str("expand_home: bare ~ expands to just the home dir", out, userprofile);
    }
}

static void test_copy_file(void) {
    FILE *fp;
    int ok;
    char buf[64];

    fp = fopen("fixtures/copy_src.txt", "wb");
    fputs("hello from config_copy_test\n", fp);
    fclose(fp);

    remove("fixtures/copy_out/nested/copy_dst.txt");

    ok = osr_copy_file("fixtures/copy_src.txt", "fixtures/copy_out/nested/copy_dst.txt");
    osr_t_true("copy_file: succeeds, creating parent directories", ok);

    fp = fopen("fixtures/copy_out/nested/copy_dst.txt", "rb");
    osr_t_true("copy_file: destination file exists and is readable", fp != NULL);
    if (fp != NULL) {
        size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
        buf[n] = '\0';
        fclose(fp);
        osr_t_eq_str("copy_file: content matches the source", buf, "hello from config_copy_test\n");
    }

    remove("fixtures/copy_src.txt");
    remove("fixtures/copy_out/nested/copy_dst.txt");
}

static void test_copy_file_missing_source(void) {
    int ok = osr_copy_file("fixtures/no-such-source.txt", "fixtures/copy_out/should-not-exist.txt");
    osr_t_true("copy_file: a missing source fails cleanly", !ok);
}

int main(void) {
    OSR_T_INIT();
    test_expand_home();
    test_copy_file();
    test_copy_file_missing_source();
    return osr_t_finish();
}
