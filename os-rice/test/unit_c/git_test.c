/* test/unit_c/git_test.c -- what lib/git.c must do with a repository.
 *
 * Three verbs: keep a clone in sync with a remote, put an oh-my-zsh plugin
 * where oh-my-zsh looks for it, and install oh-my-zsh itself. All three are
 * rerun on every `osr install`, so SS2 is the whole design: a second run must
 * pull rather than re-clone, and a third must do nothing visible at all.
 *
 * Hermetic: $PATH is a directory of stubs. git answers off marker files under
 * .git/, so a scenario is a directory LAYOUT rather than a real clone --
 * REMOTE says what `remote get-url` reports, DIRTY and STAGED say what
 * `diff --quiet` reports, PULLFAIL makes the pull fail. curl serves a fake
 * oh-my-zsh installer, and the sh stub logs what was fed to it on stdin,
 * which is the only way to see the patch applied to that installer.
 *
 * ON THE DOUBLED LOG LINES
 *
 * The sudo stub logs the escalation and then execs the real command -- itself
 * a stub, which logs again. So `sudo -u tester git ...` followed by `git ...`
 * is one invocation seen twice: that it escalated, and what it escalated to.
 * Both halves matter, because running git as root in a user's home is how a
 * checkout ends up with files the user cannot write (SS8).
 *
 * Replaces test/unit/git_c_parity.sh and omz_install.sh, which drove
 * lib/git.sh. See test/harness.h for why the expectations are stated here.
 */
#include "../harness.c"

#define URL "https://github.com/zsh-users/zsh-autosuggestions"
#define OMZ_URL "https://github.com/ohmyzsh/ohmyzsh.git"

static OsrSandbox sb;

/* fresh -- an empty home. Every scenario describes its own starting layout,
 * so nothing a previous one left can be read as something this one did. */
static void fresh(void) {
    osr_sb_rm(&sb, "home");
    osr_sb_mkdir(&sb, "home");
    osr_sb_reset(&sb);
}

/* seeded_repo -- a checkout at `rel` whose origin is `remote`. */
static void seeded_repo(const char *rel, const char *remote) {
    HStr p;
    hs_init(&p);
    hs_add(&p, rel);
    hs_add(&p, "/.git/REMOTE");
    osr_sb_write(&sb, hs_text(&p), remote, 0644);
    hs_free(&p);
}

/* marker -- one of git's answers, set by touching a file under .git/. */
static void marker(const char *rel, const char *name) {
    HStr p;
    hs_init(&p);
    hs_add(&p, rel);
    hs_add(&p, "/.git/");
    hs_add(&p, name);
    osr_sb_write(&sb, hs_text(&p), "", 0644);
    hs_free(&p);
}

/* repo -- `osr git repo <name> <url> <dir> [flag]`, against ROOT/home/demo. */
static int repo(const char *url, const char *flag) {
    HStr dir;
    int rc;
    hs_init(&dir);
    hs_path(&dir, hs_text(&sb.root), "home/demo");
    osr_sb_reset(&sb);
    rc = osr_sb_run_core(&sb, "git", "repo", "demo", url, hs_text(&dir), flag,
                         (const char *)NULL);
    hs_free(&dir);
    return rc;
}

int main(void) {
    osr_sb_init(&sb);

    /* git answers from marker files, so a scenario states the repository's
     * condition by writing a file rather than by rebuilding a stub. `clone`
     * materialises a checkout, so what a run LEFT BEHIND can be asserted. */
    osr_sb_stub_body(&sb, "git",
        "printf 'git %s\\n' \"$*\" >>\"$LOG\"\n"
        "_dir=''\n"
        "if [ \"$1\" = \"-C\" ]; then _dir=$2; shift 2; fi\n"
        "case \"$1 ${2:-}\" in\n"
        "  \"remote get-url\") [ -f \"$_dir/.git/REMOTE\" ] || exit 1\n"
        "                     cat \"$_dir/.git/REMOTE\" ;;\n"
        "  \"diff --quiet\")   [ -f \"$_dir/.git/DIRTY\" ] && exit 1 ; exit 0 ;;\n"
        "  \"diff --cached\")  [ -f \"$_dir/.git/STAGED\" ] && exit 1 ; exit 0 ;;\n"
        "  \"pull --ff-only\") [ -f \"$_dir/.git/PULLFAIL\" ] && exit 3\n"
        "                     echo 'Already up to date.' ;;\n"
        "  \"reset --hard\")   rm -f \"$_dir/.git/DIRTY\" \"$_dir/.git/STAGED\" ;;\n"
        "  \"clean -fd\")      rm -f \"$_dir/dirt\" ;;\n"
        "  clone*) shift; while [ $# -gt 2 ]; do shift; done\n"
        "          mkdir -p \"$2/.git\"; printf '%s\\n' \"$1\" >\"$2/.git/REMOTE\"\n"
        "          printf 'core\\n' >\"$2/oh-my-zsh.sh\" ;;\n"
        "esac\n"
        "exit 0\n");
    osr_sb_stub_body(&sb, "curl",
        "printf 'curl %s\\n' \"$*\" >>\"$LOG\"\ncat \"$PAYLOAD\"\n");
    /* `sh -s` is the oh-my-zsh installer being fed on stdin, and logging that
     * stdin is the only way to see the patch applied to it. Every other use of
     * sh -- the stubs themselves -- has to keep working, hence the exec. */
    osr_sb_stub_body(&sb, "sh",
        "if [ \"$1\" = \"-s\" ]; then shift\n"
        "  { printf 'sh -s args=[%s]\\n' \"$*\"; sed 's/^/  | /'; } >>\"$LOG\"\n"
        "  exit 0\n"
        "fi\n"
        "exec /bin/sh \"$@\"\n");

    osr_sb_write(&sb, "omz-install.sh",
        "#!/bin/sh\n"
        "setup_shell() {\n"
        "  chsh -s /bin/zsh \"$USER\" || exit 1\n"
        "}\n"
        "main() {\n"
        "  setup_shell\n"
        "  exec env zsh -l\n"
        "}\n"
        "main \"$@\"\n", 0644);
    {
        HStr p;
        hs_init(&p);
        hs_path(&p, hs_text(&sb.root), "omz-install.sh");
        osr_sb_env(&sb, "PAYLOAD", hs_text(&p));
        hs_free(&p);
    }

    /* ================================================================
     * 1. A repository that is not there yet
     * ================================================================ */
    fresh();
    repo(URL, "--depth 1");
    osr_assert_log_is(&sb,
        "sudo -u tester git clone --depth 1 " URL " ROOT/home/demo\n"
        "git clone --depth 1 " URL " ROOT/home/demo\n",
        "an absent repo is cloned once, as the user, with the caller's flags");
    osr_assert_tree_is(&sb, "home/demo",
        "home/demo\n"
        "home/demo/.git\n"
        "home/demo/.git/REMOTE\n"
        "home/demo/oh-my-zsh.sh\n",
        "the clone lands where it was asked to");

    /* ================================================================
     * 2. A repository already there, pointing at the same remote
     *
     * This is the rerun case, and it is the one that runs on every install
     * after the first: probe the remote, check the tree is clean, pull.
     * ================================================================ */
    fresh();
    seeded_repo("home/demo", URL "\n");
    repo(URL, "--depth 1");
    osr_assert_log_is(&sb,
        "sudo chown -R tester:tester ROOT/home/demo\n"
        "sudo -u tester git -C ROOT/home/demo remote get-url origin\n"
        "git -C ROOT/home/demo remote get-url origin\n"
        "sudo -u tester git -C ROOT/home/demo diff --quiet\n"
        "git -C ROOT/home/demo diff --quiet\n"
        "sudo -u tester git -C ROOT/home/demo diff --cached --quiet\n"
        "git -C ROOT/home/demo diff --cached --quiet\n"
        "sudo -u tester git -C ROOT/home/demo pull --ff-only\n"
        "git -C ROOT/home/demo pull --ff-only\n",
        "an existing clone of the same remote is pulled, never re-cloned (SS2)");
    osr_refute_log(&sb, "git clone",
        "a rerun does not throw away a working checkout to fetch it again");

    /* The chown first: a checkout an earlier run made as root is unwritable
     * by the user, and every git command after it would fail on ownership
     * rather than on anything to do with the repository. */
    osr_assert_log(&sb, "sudo chown -R tester:tester ROOT/home/demo",
        "the checkout is handed to the user before anything is asked of it (SS8)");

    /* --ff-only, never a merge: a rebase or a merge commit in a directory the
     * user did not ask to be a working copy is a mess os-rice cannot resolve. */
    osr_assert_log(&sb, "git -C ROOT/home/demo pull --ff-only",
        "the pull is fast-forward only -- os-rice never creates a merge here");

    /* The three spellings of one remote all count as a match, because a
     * mismatch means recloning and losing whatever was there. */
    fresh();
    seeded_repo("home/demo", URL ".git\n");
    repo(URL, NULL);
    osr_refute_log(&sb, "git clone",
        "a remote recorded with .git matches a URL written without it");

    fresh();
    seeded_repo("home/demo", URL "\n");
    repo(URL ".git", NULL);
    osr_refute_log(&sb, "git clone",
        "a URL written with .git matches a remote recorded without it");

    /* ================================================================
     * 3. A dirty tree is reset before the pull
     *
     * --ff-only fails on a dirty tree, so the choice is between resetting and
     * failing every rerun. These are os-rice-owned checkouts (SS5), not the
     * user's work, so resetting is the right answer -- but it says so.
     * ================================================================ */
    fresh();
    seeded_repo("home/demo", URL "\n");
    marker("home/demo", "DIRTY");
    osr_sb_write(&sb, "home/demo/dirt", "junk\n", 0644);
    repo(URL, NULL);
    osr_assert_log_is(&sb,
        "sudo chown -R tester:tester ROOT/home/demo\n"
        "sudo -u tester git -C ROOT/home/demo remote get-url origin\n"
        "git -C ROOT/home/demo remote get-url origin\n"
        "sudo -u tester git -C ROOT/home/demo diff --quiet\n"
        "git -C ROOT/home/demo diff --quiet\n"
        "sudo -u tester git -C ROOT/home/demo reset --hard HEAD\n"
        "git -C ROOT/home/demo reset --hard HEAD\n"
        "sudo -u tester git -C ROOT/home/demo clean -fd\n"
        "git -C ROOT/home/demo clean -fd\n"
        "sudo -u tester git -C ROOT/home/demo pull --ff-only\n"
        "git -C ROOT/home/demo pull --ff-only\n",
        "a dirty tree is reset AND cleaned before the pull -- reset alone "
        "leaves untracked files that can still block a checkout");
    osr_assert_out(&sb, "demo has local changes - resetting to clean state",
        "the reset is announced rather than done silently");

    /* A staged-only change is invisible to `diff --quiet`, which is why the
     * index is probed separately. Without it, a rerun after a half-finished
     * `git add` fails on the pull with nothing explaining why. */
    fresh();
    seeded_repo("home/demo", URL "\n");
    marker("home/demo", "STAGED");
    repo(URL, NULL);
    osr_assert_log(&sb, "git -C ROOT/home/demo reset --hard HEAD",
        "a staged-only change is caught too: the index is probed separately");

    /* ================================================================
     * 4. A different remote is thrown away and recloned
     * ================================================================ */
    fresh();
    seeded_repo("home/demo", "https://example.invalid/other\n");
    osr_sb_write(&sb, "home/demo/stale-file", "stale\n", 0644);
    repo(URL, "--depth 1");
    osr_assert_log_is(&sb,
        "sudo chown -R tester:tester ROOT/home/demo\n"
        "sudo -u tester git -C ROOT/home/demo remote get-url origin\n"
        "git -C ROOT/home/demo remote get-url origin\n"
        "sudo -u tester git clone --depth 1 " URL " ROOT/home/demo\n"
        "git clone --depth 1 " URL " ROOT/home/demo\n",
        "a checkout of a DIFFERENT remote is recloned rather than pulled into");
    osr_assert_out(&sb, "demo points at a different remote - recloning",
        "the reclone says why it is discarding what was there");
    osr_assert_absent(&sb, "home/demo/stale-file",
        "the old checkout is removed first -- git clone will not write into a "
        "directory that already has content");

    /* A directory with no .git at all is a clone target, not a repository. */
    fresh();
    osr_sb_write(&sb, "home/demo/loose", "x\n", 0644);
    repo(URL, NULL);
    osr_assert_log(&sb, "git clone " URL " ROOT/home/demo",
        "a directory without .git is cloned into rather than pulled");

    /* ================================================================
     * 5. A failing pull is fatal
     *
     * Not a warning: everything downstream of a repo assumes its contents,
     * and a rice that carries on around a checkout that is not what it says
     * it is fails later, somewhere unrelated.
     * ================================================================ */
    fresh();
    seeded_repo("home/demo", URL "\n");
    marker("home/demo", "PULLFAIL");
    osr_assert_rc(repo(URL, NULL), 1, "a failed pull is fatal");
    osr_assert_err(&sb, "failed to update demo (exit 3)",
        "the failure names the repo and the status git actually returned");

    /* ================================================================
     * 6. An oh-my-zsh plugin
     *
     * Same machinery, but the destination is oh-my-zsh's own layout -- a
     * plugin anywhere else is a plugin oh-my-zsh will not load.
     * ================================================================ */
    fresh();
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "git", "plugin", "zsh-autosuggestions", URL,
                    (const char *)NULL);
    osr_assert_log_is(&sb,
        "sudo -u tester git clone --depth 1 " URL
        " ROOT/home/.oh-my-zsh/custom/plugins/zsh-autosuggestions\n"
        "git clone --depth 1 " URL
        " ROOT/home/.oh-my-zsh/custom/plugins/zsh-autosuggestions\n",
        "a plugin is cloned into custom/plugins/<name>, where oh-my-zsh looks");

    fresh();
    seeded_repo("home/.oh-my-zsh/custom/plugins/zsh-autosuggestions", URL "\n");
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "git", "plugin", "zsh-autosuggestions", URL,
                    (const char *)NULL);
    osr_refute_log(&sb, "git clone",
        "an existing plugin is updated in place, not re-cloned (SS2)");

    /* ================================================================
     * 7. oh-my-zsh itself
     *
     * The probe is the FILE, not the directory, and that distinction is the
     * whole reason this has a test. On any box where the distro packages
     * oh-my-zsh system-wide (Armbian ships /etc/oh-my-zsh and exports $ZSH at
     * it), ~/.oh-my-zsh exists as an empty shell -- install_zsh_plugin above
     * creates custom/plugins/ inside it. A directory probe would call that
     * "installed", leaving `source $ZSH/oh-my-zsh.sh` pointing at nothing and
     * every plugin silently unloaded, up-arrow history included.
     * ================================================================ */
    fresh();
    osr_sb_write(&sb, "home/.oh-my-zsh/oh-my-zsh.sh", "core\n", 0644);
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "git", "omz", (const char *)NULL);
    osr_assert_log_empty(&sb,
        "a real oh-my-zsh is left alone entirely (SS2)");
    osr_assert_out(&sb, "oh-my-zsh already installed - skipping",
        "the skip says what it recognised");

    /* The stub case: a directory with plugins in it but no core. The clone
     * goes to a staging path and the existing custom/ is carried across, so
     * the plugins installed before this ran are not lost. */
    fresh();
    osr_sb_write(&sb,
        "home/.oh-my-zsh/custom/plugins/zsh-autosuggestions/file.zsh",
        "plugin\n", 0644);
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "git", "omz", (const char *)NULL);
    osr_assert_log_is(&sb,
        "sudo -u tester rm -rf ROOT/home/.oh-my-zsh.osr-new\n"
        "sudo -u tester git clone --depth 1 " OMZ_URL
        " ROOT/home/.oh-my-zsh.osr-new\n"
        "git clone --depth 1 " OMZ_URL " ROOT/home/.oh-my-zsh.osr-new\n"
        "sudo -u tester rm -rf ROOT/home/.oh-my-zsh.osr-new/custom\n"
        "sudo -u tester mv ROOT/home/.oh-my-zsh/custom "
        "ROOT/home/.oh-my-zsh.osr-new/custom\n"
        "sudo -u tester rm -rf ROOT/home/.oh-my-zsh\n"
        "sudo -u tester mv ROOT/home/.oh-my-zsh.osr-new ROOT/home/.oh-my-zsh\n",
        "a stub directory is repaired by cloning beside it and swapping, so a "
        "failure part-way leaves the old directory intact");
    osr_assert_tree_is(&sb, "home/.oh-my-zsh",
        "home/.oh-my-zsh\n"
        "home/.oh-my-zsh/.git\n"
        "home/.oh-my-zsh/.git/REMOTE\n"
        "home/.oh-my-zsh/custom\n"
        "home/.oh-my-zsh/custom/plugins\n"
        "home/.oh-my-zsh/custom/plugins/zsh-autosuggestions\n"
        "home/.oh-my-zsh/custom/plugins/zsh-autosuggestions/file.zsh\n"
        "home/.oh-my-zsh/oh-my-zsh.sh\n",
        "the core is in place AND the plugins that were already there survived");
    osr_assert_absent(&sb, "home/.oh-my-zsh.osr-new",
        "the staging directory is not left behind");

    /* And the repair is idempotent: the run above left a real oh-my-zsh, so
     * a second one recognises it and stops. */
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "git", "omz", (const char *)NULL);
    osr_assert_log_empty(&sb, "the repair is a no-op the second time (SS2)");

    /* Nothing there at all: upstream's own installer, PATCHED.
     *
     * Two edits, and both are about not letting the installer take over the
     * session it is running inside: setup_shell would run chsh (os-rice owns
     * the login shell, in lib/user.c, where the result is verified), and the
     * trailing `exec zsh -l` would replace the installing process with an
     * interactive shell and the rest of the rice would never run.
     */
    fresh();
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "git", "omz", (const char *)NULL);
    osr_assert_log(&sb,
        "curl -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/"
        "tools/install.sh",
        "with nothing there, upstream's installer is fetched");
    osr_assert_log(&sb, "sh -s args=[--  --unattended --skip-chsh]",
        "the installer runs unattended and is told not to touch the login shell");
    osr_assert_log(&sb, "  |   true",
        "setup_shell is neutered: os-rice owns the login shell and verifies it");
    osr_assert_log(&sb, "  |   exec ",
        "the trailing `exec zsh -l` is stripped -- it would replace the "
        "installing process and the rest of the rice would never run");
    osr_refute_log(&sb, "chsh -s",
        "no `chsh -s` survives in the patched installer (--skip-chsh on the "
        "command line is the flag, not a call)");

    osr_sb_free(&sb);
    return osr_finish();
}
