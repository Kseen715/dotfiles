/* test/harness.h -- the sandbox every C behaviour test is written against.
 *
 * WHAT A TEST HERE CHECKS
 *
 * What a package layer, a builder, a service verb or a whole module does to a
 * box is exactly one thing: the list of commands it decided to run, with which
 * arguments, in which order. So a test reduces $PATH to a directory of stubs
 * that log their own argv and then runs the code under test. That argv log IS
 * the assertion -- nothing about this machine can leak into it, because
 * "is dpkg installed", "is this package held" and "does groupadd exist" are all
 * answered by a stub the scenario wrote.
 *
 * THE C TIER IS THE GROUND TRUTH
 *
 * These tests state what the harness core is SUPPOSED to do, in the test, by
 * name. They do not diff it against a recording of the shell tier it replaced.
 *
 * That is a deliberate reversal. The sh suite compared two live
 * implementations -- it ran lib/pkg.sh to produce an expected log and lib/pkg.c
 * to produce the actual one -- which answers "did the rewrite change
 * behaviour?" That is a migration question, and it expired when the migration
 * did. It never answered "is this behaviour correct": the shell tier had no
 * tests of its own, so comparing against it froze whatever it happened to do,
 * defects included. Two of those surfaced during the port -- dash's
 * `command -v` answering 127 where the C answers 1, and a test whose `set -e`
 * killed the subshell before it could compare anything, so it compared two
 * empty strings for years and called it parity.
 *
 * So an expectation lives in the test file, spelled out, with a name:
 *
 *     osr_assert_log_is(&sb,
 *         "sudo rc-update add cups default\n"
 *         "rc-update add cups default\n"
 *         "sudo rc-service cups start\n"
 *         "rc-service cups start\n",
 *         "openrc adds the service, then starts it");
 *
 * When that fails you are told which promise broke. When you change the
 * behaviour on purpose you edit a line that says what the behaviour is, rather
 * than re-recording a blob and hoping the diff was the one you meant.
 *
 * LAYOUT
 *
 * A test runs with test/unit_c as its working directory (nob.c's run_test), so
 * the tree root is "../..". Each test gets its own sandbox:
 *
 *   <tmp>/bin      the whole of $PATH: stubs plus symlinks to the real
 *                  coreutils the code under test cannot run without
 *   <tmp>/home     $HOME and $OSR_HOME
 *   <tmp>/log      the argv log; every stub appends one line to it
 *   <tmp>/out      the last run's stdout
 *   <tmp>/err      the last run's stderr, kept apart from stdout: which stream
 *                  a line goes to is half of what a logger decides
 *
 * The environment is built from nothing (execve with our own envp, which is
 * what `env -i` did), so a variable the code under test reads is a property of
 * the scenario and never of the developer's shell.
 *
 * C89 + POSIX. Unity-built: a test #includes harness.c directly, links no lib
 * objects, and drives build/osr as a subprocess -- these tests are black-box by
 * construction, which is why they survive the units being renamed or split.
 */
#ifndef OSR_TEST_HARNESS_H
#define OSR_TEST_HARNESS_H

#include <stddef.h>

/* --- a grow-on-append byte buffer -------------------------------------
 *
 * The harness has its own rather than lib/common.c's Str: a test links
 * nothing, on purpose, so that deleting or renaming a lib unit can never break
 * the test that says what that unit must do.
 */
typedef struct {
    char *p;
    size_t len;
    size_t cap;
} HStr;

void hs_init(HStr *s);
void hs_reset(HStr *s);
void hs_free(HStr *s);
void hs_add(HStr *s, const char *z);
void hs_addc(HStr *s, char c);
void hs_addn(HStr *s, long n);
const char *hs_text(const HStr *s);
/* hs_path(s, a, b) -- "a/b", the one composition this file does constantly. */
void hs_path(HStr *s, const char *dir, const char *leaf);

/* --- the sandbox ------------------------------------------------------ */

#define OSR_SB_MAX_ENV 64
#define OSR_SB_MAX_DROP 8
#define OSR_SB_MAX_MASK 8

typedef struct {
    HStr root;     /* the temporary directory, removed by osr_sb_free */
    HStr bin;      /* root/bin  -- the whole $PATH */
    HStr home;     /* root/home -- $HOME and $OSR_HOME */
    HStr log;      /* root/log  -- the argv log */
    HStr out;      /* root/out  -- the last run's stdout */
    HStr err;      /* root/err  -- the last run's stderr */
    HStr osr_root; /* the os-rice tree, absolute */
    HStr osr_lib;  /* <tree>/lib */
    HStr osr_exe;  /* <tree>/build/osr */

    char *env[OSR_SB_MAX_ENV]; /* NAME=VALUE; the complete environment */
    int env_n;

    /* drop -- log lines removed before any comparison; see osr_sb_drop. */
    char *drop[OSR_SB_MAX_DROP];
    int drop_n;

    /* mask -- prefixes whose trailing run of non-whitespace is collapsed
     * before comparing; see osr_sb_mask. */
    char *mask[OSR_SB_MAX_MASK];
    int mask_n;
} OsrSandbox;

/* osr_sb_init -- make the sandbox and the baseline environment. Fatal on
 * failure: a test that cannot build its own world has nothing to report. */
void osr_sb_init(OsrSandbox *sb);
void osr_sb_free(OsrSandbox *sb);

/* osr_sb_env -- set or replace one variable. The value is copied. */
void osr_sb_env(OsrSandbox *sb, const char *name, const char *value);

/* osr_sb_stub -- a fake tool that logs how it was called and exits with
 * `code`. Replaces any stub of that name already in place, so a scenario can
 * flip "dpkg says installed" to "dpkg says absent" between runs. */
void osr_sb_stub(OsrSandbox *sb, const char *name, int code);

/* osr_sb_stub_body -- a stub whose behaviour is more than an exit code: the
 * text is the body of a /bin/sh script, and it is on its own to log. Use it
 * when the answer depends on the argv (`pacman -Q` absent but `pacman -S`
 * fine) or when the stub has to print something. */
void osr_sb_stub_body(OsrSandbox *sb, const char *name, const char *body);

/* osr_sb_real -- symlink the host's real <name> into the sandbox PATH.
 * osr_sb_init already links the tools nothing runs without; this is for a
 * scenario that needs one more. What these answer is not part of what is
 * asserted -- they are there so the code under test can run at all. */
void osr_sb_real(OsrSandbox *sb, const char *name);

/* osr_sb_drop -- remove every log line exactly equal to `line` before any
 * comparison. For a command whose presence says nothing about what the code
 * decided: `id -u` is how a shell asks whether it is already root, where the
 * C tier calls getuid(). Same decision, one fewer process. */
void osr_sb_drop(OsrSandbox *sb, const char *line);

/* osr_sb_mask -- collapse whatever follows `prefix` up to the next
 * whitespace, replacing it with "X". For a staging file's random suffix and
 * the pid inside a generated name -- neither names a decision the code made
 * about the box. A mask that hid an ARGUMENT would hide a real defect, so
 * keep the prefix long enough to be unambiguous. */
void osr_sb_mask(OsrSandbox *sb, const char *prefix);

/* osr_sb_write -- create a file under the sandbox with the given contents,
 * making parent directories as needed. `rel` is relative to the sandbox root.
 * `mode` is passed to chmod (0755 for something meant to be run). */
void osr_sb_write(OsrSandbox *sb, const char *rel, const char *contents, int mode);
void osr_sb_mkdir(OsrSandbox *sb, const char *rel);
/* osr_sb_symlink -- a link at `rel` pointing at `target_rel`, both relative
 * to the sandbox root. For seeding a state a verb is supposed to find. */
void osr_sb_symlink(OsrSandbox *sb, const char *target_rel, const char *rel);
/* osr_sb_rm -- remove a path under the sandbox, recursively. */
void osr_sb_rm(OsrSandbox *sb, const char *rel);

/* osr_sb_reset -- truncate the argv log. Call it before the run you are
 * about to assert on. */
void osr_sb_reset(OsrSandbox *sb);

/* osr_sb_run_core -- run build/osr with the sandbox environment. The arguments
 * are the argv after the program name, NULL-terminated. Returns the exit
 * status; stdout lands in sb->out and stderr in sb->err. */
int osr_sb_run_core(OsrSandbox *sb, ...);

/* osr_sb_capture -- the last run's stdout. Valid until the next capture. */
const char *osr_sb_capture(OsrSandbox *sb);
/* osr_sb_capture_err -- the last run's stderr, separately. */
const char *osr_sb_capture_err(OsrSandbox *sb);
/* osr_sb_capture_both -- stdout then stderr, for a scenario that only cares
 * that the bytes are right and not which stream carried them. */
const char *osr_sb_capture_both(OsrSandbox *sb);
/* osr_sb_log -- the last run's argv log, with drops and masks applied. */
const char *osr_sb_log(OsrSandbox *sb);

/* osr_sb_tree -- a sorted listing of one directory under the sandbox, one
 * entry per line, with `name -> target` for a symlink. How a scenario says
 * what a run LEFT BEHIND rather than only what it ran -- the runit branch of
 * a service verb makes one link and runs almost nothing. */
const char *osr_sb_tree(OsrSandbox *sb, const char *rel);

/* osr_sb_scrub -- machine-specific bytes out, stable placeholders in: the
 * sandbox root becomes ROOT and the os-rice tree becomes TREE. Applied by
 * every assertion below, so an expectation written in a test can say
 * "ROOT/sv/bluetoothd" and mean it. */
const char *osr_sb_scrub(OsrSandbox *sb, const char *text);

/* --- assertions -------------------------------------------------------
 *
 * Every one prints a single ok/FAIL line and, on failure, the first line that
 * differed. `label` is the promise being made -- write it as a sentence about
 * behaviour ("openrc adds the service, then starts it"), not as a restatement
 * of the mechanism ("log matches").
 */
void osr_ok(const char *label);
void osr_fail(const char *label, const char *detail);

/* osr_assert_log_is -- the argv log is EXACTLY this, in this order.
 *
 * The strongest form and the default one to reach for: the complete list of
 * commands a run issued is the whole of what it did to the box, so anything
 * extra is as much a defect as anything missing. `expected` is scrubbed text,
 * newline-terminated per line. */
void osr_assert_log_is(OsrSandbox *sb, const char *expected, const char *label);
/* osr_assert_log_empty -- the run issued no command at all. Named separately
 * because "" as an argument reads like an oversight. */
void osr_assert_log_empty(OsrSandbox *sb, const char *label);
/* osr_assert_log / osr_refute_log -- one line is somewhere in the log, or is
 * not. For a scenario where the surrounding commands are not the point. */
void osr_assert_log(OsrSandbox *sb, const char *needle, const char *label);
void osr_refute_log(OsrSandbox *sb, const char *needle, const char *label);

/* osr_assert_out / osr_assert_err -- a substring of what the user saw, on the
 * stream it must have gone to. */
void osr_assert_out(OsrSandbox *sb, const char *needle, const char *label);
void osr_assert_err(OsrSandbox *sb, const char *needle, const char *label);
void osr_assert_out_is(OsrSandbox *sb, const char *expected, const char *label);
/* osr_assert_silent -- the run printed nothing on either stream. */
void osr_assert_silent(OsrSandbox *sb, const char *label);

/* osr_assert_tree_is -- the listing of a directory under the sandbox is
 * exactly this. What the run left behind, stated. */
void osr_assert_tree_is(OsrSandbox *sb, const char *rel, const char *expected,
                        const char *label);
/* osr_assert_link -- `rel` is a symlink whose target is `target` (scrubbed,
 * so "ROOT/sv/bluetoothd" is what you write). */
void osr_assert_link(OsrSandbox *sb, const char *rel, const char *target,
                     const char *label);
/* osr_assert_absent -- nothing exists at `rel`. */
void osr_assert_absent(OsrSandbox *sb, const char *rel, const char *label);

void osr_assert_eq(const char *expected, const char *actual, const char *label);
void osr_assert_true(int cond, const char *label);
/* osr_assert_rc -- the exit status of the last run. */
void osr_assert_rc(int actual, int expected, const char *label);

/* osr_finish -- the "--- N passed, N failed ---" line and the exit status. */
int osr_finish(void);

#endif /* OSR_TEST_HARNESS_H */
