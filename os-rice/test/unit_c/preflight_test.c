/* test/unit_c/preflight_test.c -- what lib/preflight.c must answer for every
 * require: predicate.
 *
 * A predicate is a yes/no question a rice asks about the box BEFORE anything
 * is mutated: `require: distro:void|debian`, `require: gpu:present`. Getting
 * one wrong either blocks an install that would have worked or lets one start
 * that cannot finish half-way through, so what is asserted is the answer and,
 * where a predicate is allowed to complain, what it said.
 *
 * Hermetic: $PATH is a directory of stubs, so `cmd:` is a property of the
 * scenario; $OSR_DRI points at a directory this test creates, so is a GPU.
 *
 * The box is Void on x86_64 with runit -- a distro whose codename and version
 * id differ, which is what makes the two-variable release check worth asking
 * about.
 *
 * ON EXIT STATUS
 *
 * `met` / `unmet`, never the raw number. The shell predecessor's `cmd:` branch
 * was `command -v "$v"`, and dash answers a missing command with 127 where
 * lib/preflight.c answers 1 -- 127 is dash's quirk, not a decision the
 * preflight made, and nothing reads the number: every caller asks a predicate
 * as a boolean. This is also how the sh test that came before was found to be
 * broken -- it ran the shell under `set -e` and appended `rc=$?` afterwards,
 * so on an unmet predicate the subshell died before the rc line was reached,
 * on both sides, and it compared two empty strings for years.
 *
 * Was test/unit/preflight_c_parity.sh. See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

/* met -- ask one predicate; 1 when it holds. */
static int met(const char *pred) {
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "preflight", "check", pred, (const char *)NULL) == 0;
}

/* holds / fails -- one predicate, one named promise. Two functions rather
 * than a boolean argument so the call site reads as the claim it is making. */
static void holds(const char *pred, const char *label) {
    osr_assert_true(met(pred), label);
}
static void fails(const char *pred, const char *label) {
    osr_assert_true(!met(pred), label);
}

int main(void) {
    HStr p;

    osr_sb_init(&sb);
    hs_init(&p);

    /* Two DRI directories: an empty one, so gpu:present is false by default,
     * and one holding a render node for the scenario that wants a GPU. */
    osr_sb_mkdir(&sb, "dri-empty");
    osr_sb_write(&sb, "dri-gpu/renderD128", "", 0644);

    osr_sb_env(&sb, "OSR_ARCH", "x86_64");
    osr_sb_env(&sb, "OSR_ARCH_DEB", "amd64");
    osr_sb_env(&sb, "OSR_INIT", "runit");
    osr_sb_env(&sb, "OSR_DISTRO", "void");
    osr_sb_env(&sb, "OSR_CODENAME", "rolling");
    osr_sb_env(&sb, "OSR_VERSION_ID", "20240314");
    hs_path(&p, hs_text(&sb.root), "dri-empty");
    osr_sb_env(&sb, "OSR_DRI", hs_text(&p));
    osr_sb_env(&sb, "HOME", hs_text(&sb.root));

    /* --- 1. the single-value predicates ----------------------------------
     * arch: is two variables, not one -- a Debian manifest says amd64 where
     * uname says x86_64, and both have to name the same machine. */
    holds("arch:x86_64", "arch: matches the detected arch");
    holds("arch:amd64", "arch: also matches the Debian spelling of it");
    fails("arch:aarch64", "arch: does not match some other arch");

    holds("init:runit", "init: matches the detected init");
    fails("init:systemd", "init: does not match another init");
    holds("distro:void", "distro: matches the detected distro");
    fails("distro:debian", "distro: does not match another distro");

    /* release: is two variables for the same reason arch: is: a rolling
     * distro has a codename and no meaningful version, a pinned one has
     * both, and a manifest may reasonably name either. */
    holds("release:rolling", "release: matches the codename");
    holds("release:20240314", "release: matches the version id");
    fails("release:bookworm", "release: does not match another release");

    holds("cmd:sh", "cmd: holds for a command on PATH");
    fails("cmd:definitely-not-here", "cmd: fails for one that is not");

    /* --- 2. alternation ---------------------------------------------------
     * `|` is the only combinator, because two require: lines already mean
     * AND. "this manifest resolves on these package managers" is a set. */
    holds("distro:void|debian|ubuntu", "alternation: the host is the first branch");
    holds("distro:debian|void", "alternation: the host is the last branch");
    fails("distro:debian|ubuntu", "alternation: no branch matches");
    holds("arch:aarch64|amd64", "alternation: a branch matches the second variable");
    holds("init:systemd|openrc|runit", "alternation: over inits");
    holds("cmd:definitely-not-here|sh", "alternation: over commands");

    /* An empty branch must be SKIPPED, not treated as a wildcard that
     * matches everything -- `distro:|void` comes from a hand-edited manifest
     * and must still mean "void". */
    holds("distro:|void", "alternation: a leading empty branch is skipped");
    holds("distro:void|", "alternation: a trailing empty branch is skipped");

    /* --- 3. the GPU probe -------------------------------------------------
     * Two independent sources, because detection may already have counted
     * and the render node is the fallback when it has not. */
    fails("gpu:present", "gpu: no render node and no count means no GPU");

    osr_sb_env(&sb, "OSR_GPU_COUNT", "2");
    holds("gpu:present", "gpu: a count from detection is enough");

    osr_sb_env(&sb, "OSR_GPU_COUNT", "0");
    hs_path(&p, hs_text(&sb.root), "dri-gpu");
    osr_sb_env(&sb, "OSR_DRI", hs_text(&p));
    holds("gpu:present", "gpu: a render node is enough on its own");

    osr_sb_env(&sb, "OSR_GPU_COUNT", "");
    hs_path(&p, hs_text(&sb.root), "dri-empty");
    osr_sb_env(&sb, "OSR_DRI", hs_text(&p));

    /* --- 4. a predicate this build does not know --------------------------
     * Warns and PASSES. Failing closed would mean a manifest written against
     * a newer harness cannot be installed by an older one at all, where the
     * honest outcome is "I do not know how to check this" plus the install. */
    holds("nonsense:whatever", "an unknown predicate passes rather than failing closed");
    osr_assert_err(&sb, "unknown require predicate",
                   "an unknown predicate says so");
    holds("bareword", "a predicate with no colon at all also passes");

    /* --- 5. the run-level verb --------------------------------------------
     * `osr preflight <p>...` is what runs before an install. The FIRST unmet
     * predicate ends it -- nothing after it is even asked, because the point
     * is to fail before mutating anything. */
    osr_sb_reset(&sb);
    osr_assert_rc(osr_sb_run_core(&sb, "preflight", "distro:void", "init:runit",
                                  (const char *)NULL), 0,
                  "a run with every predicate met succeeds");

    osr_sb_reset(&sb);
    osr_assert_true(osr_sb_run_core(&sb, "preflight", "distro:void",
                                    "distro:debian", "init:runit",
                                    (const char *)NULL) != 0,
                    "a run with an unmet predicate fails");
    osr_assert_err(&sb, "distro:debian",
                   "and it names the predicate that was not met");

    osr_sb_reset(&sb);
    osr_assert_rc(osr_sb_run_core(&sb, "preflight", "", "distro:void",
                                  (const char *)NULL), 0,
                  "an empty predicate is skipped, not failed");

    hs_free(&p);
    osr_sb_free(&sb);
    return osr_finish();
}
