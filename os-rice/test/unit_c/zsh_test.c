/* test/unit_c/zsh_test.c -- what modules/zsh.c must do to a box that was riced
 * before the current rc.d layers existed.
 *
 * SS5 keeps 00-env.zsh and 99-local.zsh SEEDED: written once, then owned by the
 * user. install_layer can never reach them again, so every fix shipped in those
 * files would otherwise apply to new machines only, and an old box would keep a
 * leaking ssh-agent and an eager nvm source forever.
 *
 * The safety property is the point here, not the patching. Two rules, and the
 * scenarios below exist to hold them apart:
 *
 *   AN UNTOUCHED REGION IS REWRITTEN. It is byte-for-byte what os-rice itself
 *   shipped, so replacing it takes nothing from the user.
 *
 *   AN EDITED REGION IS REPORTED AND LEFT ALONE. One changed character and the
 *   file is the user's work. The migration says so and stops -- and the
 *   ADDITIVE migrations still apply, because appending a line takes nothing
 *   away either.
 *
 * Hermetic: $OSR_HOME is a sandbox and $PATH is a directory of stubs, so
 * nothing reaches the network, the package manager, or the login shell. The
 * mechanics of patching are lib/migrate.c's and are asserted in
 * migrate_test.c; what is asserted here is which migrations this module
 * applies and to what.
 *
 * Replaces test/unit/zsh_migrate.sh. See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

#define RCDIR "home/.config/osr/zsh/rc.d"

/* The legacy 99-local.zsh that shells used to carry, verbatim -- an ssh-agent
 * that respawns on every shell because start_agent never writes $SSH_ENV, and
 * an nvm source that runs eagerly on every startup. */
static const char *LEGACY_LOCAL =
    "# --- ssh-agent: reuse an existing agent, or start one -------------------------\n"
    "# NOTE: start_agent never writes $SSH_ENV, so the -f test below is never true and\n"
    "# a fresh agent gets spawned for every shell. Moved verbatim; not fixed here.\n"
    "SSH_ENV=\"$HOME/.ssh/agent-environment\"\n"
    "\n"
    "start_agent() {\n"
    "    eval \"$(ssh-agent -s)\" >/dev/null\n"
    "    # Only add private keys (ignore .pub, config, known_hosts, etc.)\n"
    "    ssh-add ~/.ssh/* 2>/dev/null\n"
    "}\n"
    "\n"
    "if [ -f \"$SSH_ENV\" ]; then\n"
    "    . \"$SSH_ENV\" >/dev/null\n"
    "    kill -0 \"$SSH_AGENT_PID\" 2>/dev/null || start_agent\n"
    "else\n"
    "    start_agent\n"
    "fi\n"
    "\n"
    "# --- nvm ---------------------------------------------------------------------\n"
    "# Sourced last on purpose: nvm prepends its active node dir to PATH and should\n"
    "# win over the PATH edits in 00-env.zsh.\n"
    "export NVM_DIR=\"$HOME/.nvm\"\n"
    "[ -s \"$NVM_DIR/nvm.sh\" ] && \\. \"$NVM_DIR/nvm.sh\"\n"
    "[ -s \"$NVM_DIR/bash_completion\" ] && \\. \"$NVM_DIR/bash_completion\"\n";

/* The first shipped 00-env.zsh: a brew probe that needed brew ALREADY on
 * $PATH, which on a fresh login it is not. */
static const char *LEGACY_ENV_V1 =
    "# 00-env.zsh - user/machine environment.\n"
    "export EDITOR=micro\n"
    "export MY_OWN_SETTING=keepme\n"
    "\n"
    "# Homebrew shell environment (machine-specific), only if installed.\n"
    "if command -v brew >/dev/null 2>&1; then\n"
    "    eval \"$(brew shellenv)\"\n"
    "fi\n";

/* The second generation of the same block. Two shipped forms means two exact
 * needles, which is why the migration cannot be a single pattern. */
static const char *LEGACY_ENV_V2 =
    "# 00-env.zsh - user/machine environment.\n"
    "export MY_OWN_SETTING=keepme\n"
    "\n"
    "if [ -z \"${HOMEBREW_PREFIX:-}\" ]; then\n"
    "    if [ -x /home/linuxbrew/.linuxbrew/bin/brew ]; then\n"
    "        eval \"$(/home/linuxbrew/.linuxbrew/bin/brew shellenv)\"\n"
    "    elif command -v brew >/dev/null 2>&1; then\n"
    "        eval \"$(brew shellenv)\"\n"
    "    fi\n"
    "fi\n";

/* read_rel -- a sandbox file's contents. The caller frees. */
static char *read_rel(const char *rel) {
    HStr path;
    char *got;
    hs_init(&path);
    hs_path(&path, hs_text(&sb.root), rel);
    got = h_slurp(hs_text(&path));
    hs_free(&path);
    return got;
}

/* holds / lacks -- a substring of one file under the sandbox. */
static void holds(const char *rel, const char *needle, const char *label) {
    char *got = read_rel(rel);
    osr_assert_true(strstr(got, needle) != NULL, label);
    free(got);
}
static void lacks(const char *rel, const char *needle, const char *label) {
    char *got = read_rel(rel);
    osr_assert_true(strstr(got, needle) == NULL, label);
    free(got);
}

/* old_install -- a sandbox holding a pre-migration rc.d. */
static void old_install(const char *env_layer, const char *local_layer) {
    osr_sb_rm(&sb, "home");
    osr_sb_mkdir(&sb, "home");
    osr_sb_write(&sb, RCDIR "/00-env.zsh", env_layer, 0644);
    osr_sb_write(&sb, RCDIR "/99-local.zsh", local_layer, 0644);
    /* The account already uses zsh, so the login-shell path is a no-op (SS2)
     * and this test never goes near a real chsh. */
    {
        HStr line, shell;
        hs_init(&line);
        hs_init(&shell);
        hs_path(&shell, hs_text(&sb.bin), "zsh");
        hs_add(&line, "tester:x:1000:1000::");
        hs_add(&line, hs_text(&sb.home));
        hs_addc(&line, ':');
        hs_add(&line, hs_text(&shell));
        hs_addc(&line, '\n');
        osr_sb_write(&sb, "home/.passwd", hs_text(&line), 0644);
        hs_reset(&line);
        hs_add(&line, hs_text(&shell));
        hs_addc(&line, '\n');
        osr_sb_write(&sb, "home/.shells", hs_text(&line), 0644);
        hs_free(&line);
        hs_free(&shell);
    }
    osr_sb_reset(&sb);
}

/* run -- `osr module run zsh`. */
static int run(void) {
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "module", "run", "zsh", (const char *)NULL);
}

int main(void) {
    HStr p;

    osr_sb_init(&sb);
    hs_init(&p);

    osr_sb_env(&sb, "OSR_PKG", "apt");
    osr_sb_env(&sb, "OSR_INIT", "systemd");
    hs_path(&p, hs_text(&sb.osr_root), "..");
    osr_sb_env(&sb, "OSR_DOTFILES", hs_text(&p));
    hs_path(&p, hs_text(&sb.home), ".shells");
    osr_sb_env(&sb, "OSR_SHELLS_FILE", hs_text(&p));
    hs_path(&p, hs_text(&sb.home), ".passwd");
    osr_sb_env(&sb, "OSR_PASSWD_FILE", hs_text(&p));

    /* The sandbox's own sudo stub stands: it logs the escalation and then
     * execs the real command, so `as_user cp` genuinely copies. Replacing it
     * with a no-op would make every user-space write silently vanish while
     * the migration still reported success -- which is exactly the shape of
     * failure this test exists to catch.
     *
     * Nothing here needs root: everything under $OSR_HOME is the user's own,
     * and the escalations in the log are all `sudo -u tester`. */
    /* apt says everything is installed, so the package step is a no-op (SS2). */
    osr_sb_stub_body(&sb, "dpkg", "exit 0\n");
    osr_sb_stub_body(&sb, "apt-mark", "exit 0\n");
    osr_sb_stub_body(&sb, "zsh", "exit 0\n");
    /* A new-enough fzf keeps the provider path out of the way: modules/zsh.c
     * gates on the version because an old distro fzf breaks the up-arrow
     * history widget, and a CI host with an old one would start a download. */
    osr_sb_stub_body(&sb, "fzf", "printf '0.74.3 (15f64c49)\\n'\n");
    /* And an lsd that STARTS, for the same reason with a different failure at
     * the end of it: modules/zsh.c gates on the binary running at all, so a
     * host whose lsd cannot load its libgit2/libssh2 chain -- or one with no
     * lsd -- would otherwise start a release download here. */
    osr_sb_stub_body(&sb, "lsd", "printf 'lsd 1.2.0\\n'\n");
    /* A no-op git leaves the module's own idempotency probes to decide, and
     * nothing is fetched. */
    osr_sb_stub_body(&sb, "git", "exit 0\n");

    /* ================================================================
     * 1. A stock legacy box is patched
     * ================================================================ */
    old_install(LEGACY_ENV_V1, LEGACY_LOCAL);
    osr_assert_rc(run(), 0, "the module runs to completion on a legacy box");

    holds(RCDIR "/00-env.zsh", "_osr_brew",
        "00-env: the brew probe is rewritten to try absolute paths, so it "
        "works on a login shell where brew is not yet on $PATH");
    lacks(RCDIR "/00-env.zsh", "if command -v brew",
        "00-env: the PATH-dependent probe is gone, not merely shadowed");
    holds(RCDIR "/00-env.zsh", "typeset -U path",
        "00-env: the additive migration is appended");
    holds(RCDIR "/00-env.zsh", "MY_OWN_SETTING",
        "00-env: content the user added around the region is preserved");

    lacks(RCDIR "/99-local.zsh", "nvm.sh",
        "99-local: the eager nvm source is removed -- it ran on every shell "
        "startup and cost more than nvm itself");
    lacks(RCDIR "/99-local.zsh", "start_agent",
        "99-local: the leaking ssh-agent is removed -- it spawned a fresh "
        "agent per shell because it never wrote $SSH_ENV");
    holds(RCDIR "/30-tools.zsh", "unfunction nvm",
        "30-tools.zsh is installed to replace both of them, lazily");

    {
        char *backup = read_rel(RCDIR "/00-env.zsh.pre-migrate");
        osr_assert_true(strstr(backup, "MY_OWN_SETTING") != NULL,
            "a pre-migrate backup of the user's file is kept before anything "
            "is rewritten");
        free(backup);
    }

    /* ================================================================
     * 2. Re-running is silent
     *
     * The second run is the one that matters: a migration that reports itself
     * every time trains the user to ignore the report, and the one that
     * matters is the 'still has' warning below.
     * ================================================================ */
    run();
    osr_assert_true(strstr(osr_sb_capture_both(&sb), "migrated") == NULL,
        "a second run migrates nothing (SS2)");
    osr_assert_true(strstr(osr_sb_capture_both(&sb), "still has") == NULL,
        "a second run does not warn about the fix the first run applied");

    /* ================================================================
     * 3. The newer shipped brew block is recognised too
     *
     * Two generations of the same block shipped, so both are needles. Missing
     * one would leave a box on the middle version unpatched forever, with
     * nothing saying so.
     * ================================================================ */
    old_install(LEGACY_ENV_V2, LEGACY_LOCAL);
    run();
    holds(RCDIR "/00-env.zsh", "_osr_brew",
        "the second brew generation is migrated as well");
    lacks(RCDIR "/00-env.zsh", "elif command -v brew",
        "its PATH-dependent fallback branch is gone too");

    /* ================================================================
     * 4. A hand-edited region is reported, never rewritten
     *
     * This is the rule the whole seeded-layer design rests on. Both files are
     * edited here: 00-env's brew block gets a trailing comment, and 99-local's
     * nvm path is changed to a directory the user chose.
     * ================================================================ */
    {
        HStr env_edit, local_edit;
        char *before;

        hs_init(&env_edit);
        hs_add(&env_edit,
            "# 00-env.zsh - user/machine environment.\n"
            "export EDITOR=micro\n"
            "export MY_OWN_SETTING=keepme\n"
            "\n"
            "# Homebrew shell environment (machine-specific), only if installed.\n"
            "if command -v brew >/dev/null 2>&1; then\n"
            "    eval \"$(brew shellenv)\"  # mine\n"
            "fi\n");
        hs_init(&local_edit);
        hs_add(&local_edit,
            "export NVM_DIR=\"$HOME/custom-nvm\"\n"
            "[ -s \"$NVM_DIR/nvm.sh\" ] && \\. \"$NVM_DIR/nvm.sh\"\n");

        old_install(hs_text(&env_edit), hs_text(&local_edit));
        before = read_rel(RCDIR "/99-local.zsh");
        run();

        osr_assert_err(&sb, "still has",
            "an edited region is REPORTED, so the user is told there is a fix "
            "they have to apply by hand");
        holds(RCDIR "/00-env.zsh", "# mine",
            "the user's edit survives untouched");
        {
            char *after = read_rel(RCDIR "/99-local.zsh");
            osr_assert_eq(before, after,
                "an edited 99-local is byte-identical after the run");
            free(after);
        }
        /* Additive migrations are not destructive, so they apply either way --
         * refusing them because some OTHER region was edited would leave the
         * box missing a fix for no reason. */
        holds(RCDIR "/00-env.zsh", "typeset -U path",
            "an additive migration still applies to an edited file");

        free(before);
        hs_free(&env_edit);
        hs_free(&local_edit);
    }

    /* ================================================================
     * 5. The login shell
     *
     * No package manager sets a login shell, and chsh is not everywhere, so
     * osr_set_login_shell walks three mechanisms -- chsh, then usermod, then
     * rewriting /etc/passwd -- and VERIFIES the result after each rather than
     * trusting an exit status. A chsh that exits 0 and changes nothing is a
     * real thing on a box with a restrictive PAM config.
     *
     * The observation is the sandbox /etc/passwd, because that is the only
     * thing that decides what shell the user gets at their next login.
     * ================================================================ */
    {
        /* A fake chsh/usermod that rewrites the sandbox passwd, which is what
         * the real ones do to the real file. */
        static const char *const rewriter =
            "printf '%s %s\\n' \"$(basename $0)\" \"$*\" >>\"$LOG\"\n"
            "[ \"$1\" = \"-s\" ] || exit 1\n"
            "awk -F: -v OFS=: -v u=\"$3\" -v s=\"$2\" "
            "'$1==u{$7=s}{print}' \"$OSR_PASSWD_FILE\" >\"$OSR_PASSWD_FILE.n\"\n"
            "cp -f \"$OSR_PASSWD_FILE.n\" \"$OSR_PASSWD_FILE\"\n";
        HStr zsh_path;

        hs_init(&zsh_path);
        hs_path(&zsh_path, hs_text(&sb.bin), "zsh");

        /* oh-my-zsh and its plugins already in place: those steps need the
         * network and are fatal on failure, and none of them is the point. */
        old_install(LEGACY_ENV_V1, LEGACY_LOCAL);
        osr_sb_write(&sb, "home/.oh-my-zsh/oh-my-zsh.sh", "core\n", 0644);
        osr_sb_mkdir(&sb, "home/.oh-my-zsh/custom/plugins/zsh-autosuggestions/.git");
        osr_sb_mkdir(&sb, "home/.oh-my-zsh/custom/plugins/zsh-syntax-highlighting/.git");
        osr_sb_mkdir(&sb, "home/.oh-my-zsh/custom/plugins/zsh-autocomplete/.git");
        /* The account starts on /bin/sh, so there is something to change. */
        {
            HStr line;
            hs_init(&line);
            hs_add(&line, "root:x:0:0:root:/root:/bin/sh\n");
            hs_add(&line, "tester:x:1000:1000::");
            hs_add(&line, hs_text(&sb.home));
            hs_add(&line, ":/bin/sh\n");
            osr_sb_write(&sb, "home/.passwd", hs_text(&line), 0644);
            hs_free(&line);
        }
        osr_sb_write(&sb, "home/.shells", "/bin/sh\n/bin/bash\n", 0644);
        osr_sb_stub_body(&sb, "chsh", rewriter);
        run();
        holds("home/.passwd", hs_text(&zsh_path),
            "login shell: the account is moved to zsh when it was on /bin/sh");
        holds("home/.shells", hs_text(&zsh_path),
            "login shell: zsh is registered in /etc/shells FIRST -- an "
            "unregistered shell makes chsh refuse for a non-root user, and "
            "makes some login managers treat the account as broken");

        /* SS2: an account already on zsh is left alone. On a box where chsh
         * prompts, doing it anyway would ask for a password every install. */
        run();
        osr_assert_true(
            strstr(osr_sb_capture_both(&sb), "Setting default shell") == NULL,
            "login shell: an account already on zsh is not touched again (SS2)");

        /* Nothing that can change it: no chsh, no usermod, and an account the
         * passwd file does not carry. Warn, and let the run finish -- a shell
         * that did not change is a worse shell, not a broken install. */
        osr_sb_rm(&sb, "bin/chsh");
        osr_sb_rm(&sb, "bin/usermod");
        osr_sb_write(&sb, "home/.passwd", "root:x:0:0:root:/root:/bin/sh\n", 0644);
        osr_assert_rc(run(), 0,
            "login shell: with no mechanism that works, the run still succeeds");
        osr_assert_err(&sb, "could not set the login shell",
            "login shell: and says so, rather than reporting a shell it did "
            "not set");

        hs_free(&zsh_path);
    }

    /* ================================================================
     * 6. lsd: installed is not the same as working
     *
     * 20-aliases.zsh aliases ls to lsd, so lsd is not one tool among the
     * others here -- it is the command the user types most. The distro build
     * is dynamically linked (libgit2 -> libssh2), which means a package that
     * is present, correct, and up to date still stops at the dynamic loader
     * when something further down that chain goes missing: every `ls` in every
     * shell answers with a loader error, and pkg_install's presence probe is
     * satisfied, so a rerun changes nothing. The guard asks whether the binary
     * RUNS, and the statically linked release tarball is the repair.
     * ================================================================ */
    {
        /* A working lsd first: the repair must not fire on a healthy box, or
         * every install downloads a binary the distro already provides. */
        old_install(LEGACY_ENV_V1, LEGACY_LOCAL);
        osr_sb_stub_body(&sb, "lsd", "printf 'lsd 1.2.0\\n'\n");
        run();
        osr_assert_true(
            strstr(osr_sb_capture_both(&sb), "does not run") == NULL,
            "lsd: a working lsd is left to the package manager (SS2)");

        /* Then the broken one, verbatim in the shape a missing libssh2 gives:
         * the loader writes to stderr and nothing reaches stdout, so a probe
         * reading stdout sees exactly what it sees for an absent binary. */
        osr_sb_stub_body(&sb, "lsd",
            "printf 'lsd: error while loading shared libraries: "
            "libssh2.so.1: cannot open shared object file\\n' >&2\n"
            "exit 127\n");
        /* Hermetic: the builder must not be able to reach the network even if
         * the guard wrongly lets it through. */
        osr_sb_stub_body(&sb, "curl", "exit 1\n");
        old_install(LEGACY_ENV_V1, LEGACY_LOCAL);
        run();
        osr_assert_out(&sb, "does not run",
            "lsd: a present-but-unstartable lsd IS replaced -- `command -v` "
            "finds it and the package manager considers it installed, so "
            "nothing else in the run would ever notice");
    }

    hs_free(&p);
    osr_sb_free(&sb);
    return osr_finish();
}
