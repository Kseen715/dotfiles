/* test/unit_c/testrun_test.c -- `osr test-run`, the suite runner itself.
 *
 * A test runner has one property that matters more than everything else it
 * does: WHEN SOMETHING FAILS, IT MUST FAIL. A runner that reports green over a
 * broken tree is worse than no runner, because it converts a build everyone
 * would have noticed into one nobody will. Most of what is asserted here is
 * that one property, from every direction it can be lost -- a failing test, a
 * failing lint, a tree with nothing in it.
 *
 * It is driven against a FIXTURE tree -- a lib/ symlink, a stub lint.sh and a
 * few stub unit tests -- rather than against the real suite, so the scenarios
 * are deterministic and nothing recurses into the suite this test is part of.
 *
 * Replaces test/unit/testrun_c_parity.sh. See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

/* make_tree -- a miniature os-rice tree the runner can be pointed at.
 *
 * `lint_rc` is what the stub lint exits with; `kind` chooses which stub unit
 * tests are laid down. lib/ is a symlink to the real one because the runner
 * resolves the harness from its own location, which is part of what the
 * fixture is exercising. */
static void make_tree(const char *kind, int lint_rc) {
    HStr lint;

    osr_sb_rm(&sb, "tree");
    osr_sb_mkdir(&sb, "tree/test/unit");
    {
        /* An absolute symlink to the real lib/, which osr_sb_symlink cannot
         * express (it takes sandbox-relative paths), so it is made by hand. */
        HStr link;
        hs_init(&link);
        hs_path(&link, hs_text(&sb.root), "tree/lib");
        unlink(hs_text(&link));
        if (symlink(hs_text(&sb.osr_lib), hs_text(&link)) != 0) {
            osr_fail("fixture: lib symlink", hs_text(&link));
        }
        hs_free(&link);
    }

    hs_init(&lint);
    hs_add(&lint,
        "printf 'stub lint: checking\\n'\n"
        "printf 'stub lint: a warning\\n' >&2\n"
        "exit ");
    hs_addn(&lint, (long)lint_rc);
    hs_addc(&lint, '\n');
    osr_sb_write(&sb, "tree/test/lint.sh", hs_text(&lint), 0644);
    hs_free(&lint);

    if (strcmp(kind, "pass") == 0 || strcmp(kind, "lintfail") == 0) {
        osr_sb_write(&sb, "tree/test/unit/aaa_first.sh",
                     "printf \"  ok   stub one\\n\"\n", 0644);
        /* One stub echoes $OSR_TEST_COLOR: it is the single variable the
         * runner has to export for test/lib.sh, because a test that decided
         * colour for itself would be colourless inside a coloured run. */
        osr_sb_write(&sb, "tree/test/unit/bbb_color.sh",
                     "printf \"  color=[%s]\\n\" \"$OSR_TEST_COLOR\"\n", 0644);
        osr_sb_write(&sb, "tree/test/unit/ccc_last.sh",
                     "printf \"  ok   stub three\\n\"\n", 0644);
    } else if (strcmp(kind, "withfail") == 0) {
        osr_sb_write(&sb, "tree/test/unit/aaa_first.sh",
                     "printf \"  ok   stub one\\n\"\n", 0644);
        osr_sb_write(&sb, "tree/test/unit/bbb_broken.sh",
                     "printf \"  FAIL stub two\\n\" >&2\nexit 1\n", 0644);
        osr_sb_write(&sb, "tree/test/unit/ccc_last.sh",
                     "printf \"  ok   stub three\\n\"\n", 0644);
    }
    osr_sb_reset(&sb);
}

/* run -- the runner, against the fixture tree. */
static int run(void) {
    HStr dir;
    int rc;
    hs_init(&dir);
    hs_path(&dir, hs_text(&sb.root), "tree/test");
    osr_sb_reset(&sb);
    rc = osr_sb_run_core(&sb, "test-run", hs_text(&dir), (const char *)NULL);
    hs_free(&dir);
    return rc;
}

static void printed(const char *needle, const char *label) {
    osr_assert_true(strstr(osr_sb_capture_both(&sb), needle) != NULL, label);
}

int main(void) {
    osr_sb_init(&sb);

    /* The runner resolves lib/ from its own location; $OSR_LIB must not leak
     * in, or the fixture tree would not be exercising that. */
    osr_sb_env(&sb, "OSR_LIB", "");
    osr_sb_env(&sb, "OSR_ROOT", "");
    osr_sb_env(&sb, "COLUMNS", "");

    /* ================================================================
     * 1. A tree where everything passes
     * ================================================================ */
    make_tree("pass", 0);
    osr_assert_rc(run(), 0, "a tree where everything passes exits 0");
    printed("stub lint: checking", "lint runs, and its output reaches the user");
    printed("aaa_first", "each unit test is announced by name");
    printed("bbb_color", "including the second");
    printed("ccc_last", "and the third");
    printed("ok   stub one", "and each test's own output is passed through");
    printed("ALL GREEN",
        "a fully passing run ends with a single unambiguous line -- the thing "
        "a person actually reads");

    /* Order is alphabetical and therefore reproducible. A runner whose order
     * came from readdir gives a different sequence on every filesystem, and
     * then a test that only fails after another has run is unreproducible. */
    {
        const char *out = osr_sb_capture_both(&sb);
        const char *a = strstr(out, "aaa_first");
        const char *b = strstr(out, "bbb_color");
        const char *c = strstr(out, "ccc_last");
        osr_assert_true(a != NULL && b != NULL && c != NULL && a < b && b < c,
            "the tests run in sorted order, so a run is reproducible rather "
            "than at the mercy of directory order");
    }

    /* $OSR_TEST_COLOR is exported by the runner and read by test/lib.sh. The
     * decision is made once, here, against the real terminal -- a test process
     * whose stdout is a pipe would otherwise decide "no colour" while the
     * runner around it was colouring. */
    printed("color=[]",
        "the colour decision is exported to every test: piped output makes it "
        "empty, and every test agrees rather than deciding for itself");

    /* ================================================================
     * 2. A tree with a failing test
     *
     * The one that matters.
     * ================================================================ */
    make_tree("withfail", 0);
    osr_assert_true(run() != 0,
        "a tree with a failing test exits non-zero -- the whole point");
    printed("FAIL stub two", "the failing test's own output is shown");
    printed("bbb_broken", "and the file it came from is named");

    /* The run does not stop at the first failure: a suite that aborts on the
     * first red gives you one failure per run, and finding the other four
     * takes four more runs. */
    printed("ccc_last",
        "the tests after the failing one still run -- a suite that stopped at "
        "the first failure would report them one run at a time");
    osr_assert_true(strstr(osr_sb_capture_both(&sb), "ALL GREEN") == NULL,
        "and the run does NOT end with ALL GREEN");

    /* ================================================================
     * 3. A tree whose lint fails
     *
     * Lint failing is a failure like any other. It runs FIRST, because a tree
     * that will not lint is one whose test results are not worth reading.
     * ================================================================ */
    make_tree("lintfail", 1);
    osr_assert_true(run() != 0, "a failing lint fails the run");
    {
        const char *out = osr_sb_capture_both(&sb);
        const char *lint = strstr(out, "stub lint: checking");
        const char *first = strstr(out, "aaa_first");
        osr_assert_true(lint != NULL && (first == NULL || lint < first),
            "lint runs before the unit tests, not after them");
    }

    /* ================================================================
     * 4. An empty tree
     *
     * Nothing to run is not success. A green light over zero tests is exactly
     * how a broken test discovery goes unnoticed for a month.
     * ================================================================ */
    make_tree("empty", 0);
    run();
    osr_assert_true(strstr(osr_sb_capture_both(&sb), "aaa_first") == NULL,
        "an empty tree runs no tests");
    printed("stub lint: checking",
        "but it still lints -- there is nothing to discover for that");

    osr_sb_free(&sb);
    return osr_finish();
}
