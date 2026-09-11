/* test/unit_c/update_test.c -- what `osr update` must do.
 *
 * The command exists to replace the binary that is running, which is the one
 * operation where a plausible-looking success is worse than a failure: an
 * update that "worked" and changed nothing sends someone chasing a bug that
 * was fixed weeks ago. So both routes are pinned here by what they RUN.
 *
 * Hermetic: $PATH is stubs. git logs and does nothing, cc materialises the
 * nob it was asked to build so the build step has something to execute, and
 * curl never has to be reached at all -- every scenario here stops before the
 * network, on purpose. The download itself is lib/fetch.c's, tested there.
 */
#include "../harness.c"

static OsrSandbox sb;

/* checkout -- a tree that carries the build system, which is what makes it a
 * checkout rather than the data-only clone a released binary makes. */
static void checkout(const char *rel) {
    HStr p;
    hs_init(&p);
    hs_add(&p, rel);
    hs_add(&p, "/nob.c");
    osr_sb_write(&sb, hs_text(&p), "int main(void){return 0;}\n", 0644);
    hs_free(&p);
}

static int update(const char *root_rel, const char *flag) {
    HStr dir;
    int rc;
    hs_init(&dir);
    hs_path(&dir, hs_text(&sb.root), root_rel);
    osr_sb_env(&sb, "OSR_ROOT", hs_text(&dir));
    osr_sb_reset(&sb);
    rc = osr_sb_run_core(&sb, "update", flag, (const char *)NULL);
    hs_free(&dir);
    return rc;
}

int main(void) {
    osr_sb_init(&sb);
    osr_sb_stub_body(&sb, "git", "printf 'git %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    osr_sb_stub_body(&sb, "install", "printf 'install %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    /* The compiler is only ever asked for one thing here -- bootstrap nob --
     * and the build step runs what it produced, so the stub has to leave a
     * runnable file behind or the next step fails for the wrong reason. */
    osr_sb_stub_body(&sb, "cc",
        "printf 'cc %s\\n' \"$*\" >>\"$LOG\"\n"
        "[ \"$1\" = \"-o\" ] || exit 0\n"
        "printf '#!/bin/sh\\nprintf \"nob %%s\\\\n\" \"$*\" >>\"$LOG\"\\n' >\"$2\"\n"
        "chmod 0755 \"$2\"\n");

    /* ================================================================
     * 1. A checkout refuses the release route
     *
     * The launcher rebuilds build/osr out of the checkout on every single
     * invocation. A release binary dropped in there survives until the next
     * command and not one step further, so the only honest answer is to say
     * so and name the route that works.
     * ================================================================ */
    checkout("tree");
    update("tree", (const char *)NULL);
    osr_assert_log_empty(&sb,
        "a checkout does not reach the network for a binary that its own next "
        "command would overwrite");
    osr_assert_err(&sb, "--src",
        "and it says which route does work, rather than only that this one "
        "does not");

    /* ================================================================
     * 2. --src, in a checkout: pull, bootstrap, build
     *
     * --ff-only and NOT the reset lib/git.c does to its own clones: this tree
     * may be someone's working copy, and refusing to update is recoverable
     * where discarding their work is not.
     * ================================================================ */
    update("tree", "--src");
    osr_assert_log(&sb, "git -C ROOT pull --ff-only",
        "the checkout is fast-forwarded, never reset -- os-rice does not own "
        "the tree it was built from");
    osr_assert_log(&sb, "cc -o ROOT/tree/build/nob ROOT/tree/nob.c",
        "nob.c is the build system, so it is bootstrapped before it is used");
    osr_assert_log(&sb, "nob static",
        "and the binary is rebuilt by nob, the same way every other build in "
        "this tree happens");

    osr_sb_free(&sb);
    return osr_finish();
}
