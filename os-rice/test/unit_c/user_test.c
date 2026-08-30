/* test/unit_c/user_test.c -- the user model, and the file primitives every
 * module writes through.
 *
 * SS8: os-rice runs with root somewhere in the chain but rices ONE account, and
 * everything it puts in that account's home has to end up owned by them. So
 * "which account" is a decision made once, here, and getting it wrong means a
 * home directory full of root-owned files the user cannot edit.
 *
 * The rest of this file is the two primitives that own text inside a file
 * someone else wrote -- ~/.zshrc, /etc/passwd, ~/.profile:
 *
 *   needs-line   -- ensure_line's probe. Idempotency for an appended line.
 *   compose-block -- the SS5 owned region: rewrite between the markers, keep
 *                    every byte outside them.
 *
 * The block is the sharp one. It is the mechanism by which os-rice writes into
 * a file it does not own, and the whole promise is that anything outside the
 * markers survives untouched -- forever, across every rerun and every version.
 *
 * Hermetic: /etc/passwd and /etc/shells are fixture paths the unit lets the
 * caller name, so nothing here reads or rewrites the real ones.
 *
 * Replaces test/unit/user_c_parity.sh and login_shell.sh. See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

/* file_is -- one sandbox file, byte for byte. */
static void file_is(const char *rel, const char *expected, const char *label) {
    HStr path;
    char *got;
    hs_init(&path);
    hs_path(&path, hs_text(&sb.root), rel);
    got = h_slurp(hs_text(&path));
    osr_assert_eq(expected, got, label);
    free(got);
    hs_free(&path);
}

/* abs_of -- the absolute path of a sandbox file.
 *
 * Rotates over four buffers rather than reusing one, because several call
 * sites pass two paths to the same command -- and a single static buffer
 * would hand both arguments the same string. */
static const char *abs_of(const char *rel) {
    static HStr ring[4];
    static int ready = 0;
    static int next = 0;
    HStr *p;
    if (!ready) {
        int i;
        for (i = 0; i < 4; i++) hs_init(&ring[i]);
        ready = 1;
    }
    p = &ring[next];
    next = (next + 1) % 4;
    hs_path(p, hs_text(&sb.root), rel);
    return hs_text(p);
}

static int user_cmd(const char *a, const char *b, const char *c, const char *d) {
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "user", a, b, c, d, (const char *)NULL);
}

/* block -- `osr user compose-block <file> <name>`, body on stdin. The command
 * PRINTS the composed file rather than writing it, so the test writes it back
 * -- which is exactly what the caller in lib/config.c does. */
static void block(const char *rel, const char *name, const char *body) {
    osr_sb_reset(&sb);
    osr_sb_stdin(&sb, body);
    osr_sb_run_core(&sb, "user", "compose-block", abs_of(rel), name,
                    (const char *)NULL);
    osr_sb_write(&sb, rel, osr_sb_capture(&sb), 0644);
}

int main(void) {
    osr_sb_init(&sb);

    osr_sb_env(&sb, "OSR_PASSWD_FILE", abs_of("etc/passwd"));
    osr_sb_env(&sb, "OSR_SHELLS_FILE", abs_of("etc/shells"));
    osr_sb_write(&sb, "etc/passwd",
        "root:x:0:0:root:/root:/bin/bash\n"
        "tester:x:1000:1000:Me:/home/tester:/bin/sh\n"
        "svc:x:999:999::/:/usr/sbin/nologin\n", 0644);
    osr_sb_write(&sb, "etc/shells", "/bin/sh\n/bin/bash\n", 0644);

    /* ================================================================
     * 1. Reading an account
     * ================================================================ */
    user_cmd("passwd", "tester", NULL, NULL);
    osr_assert_out_is(&sb, "tester:x:1000:1000:Me:/home/tester:/bin/sh\n",
        "passwd: the account's whole line comes back");
    osr_assert_rc(user_cmd("passwd", "nosuchuser", NULL, NULL), 1,
        "passwd: an account that does not exist reports failure");

    user_cmd("shell", "tester", NULL, NULL);
    osr_assert_out_is(&sb, "/bin/sh\n", "shell: field 7 is the login shell");
    user_cmd("shell", "nosuchuser", NULL, NULL);
    osr_assert_out_is(&sb, "",
        "shell: an unknown account has no shell, and that is not an error -- "
        "the caller asks this before deciding whether to act");

    /* ================================================================
     * 2. shell-is -- the idempotency probe for the login shell
     *
     * /bin and /usr/bin are the same directory on every modern distro, so
     * `/bin/zsh` and `/usr/bin/zsh` are the same shell. Comparing the strings
     * would make every run "fix" a login shell that was already right, which
     * on a box where chsh prompts means asking for a password every install.
     * ================================================================ */
    osr_assert_rc(user_cmd("shell-is", "tester", "/bin/sh", NULL), 0,
        "shell-is: an exact match is a match");
    osr_assert_true(user_cmd("shell-is", "tester", "/nope/zsh", NULL) != 0,
        "shell-is: a different shell is not");
    osr_assert_true(user_cmd("shell-is", "nosuchuser", "/bin/sh", NULL) != 0,
        "shell-is: an account with no passwd entry is not logged in with "
        "anything");

    /* The aliasing case, made concrete: two paths, one of them a symlink to
     * the other's directory. */
    osr_sb_mkdir(&sb, "usr/bin");
    osr_sb_write(&sb, "usr/bin/zsh", "#!/bin/sh\n", 0755);
    osr_sb_symlink(&sb, "usr/bin", "bin2");
    {
        HStr line;
        hs_init(&line);
        hs_add(&line, "root:x:0:0:root:/root:/bin/bash\n");
        hs_add(&line, "tester:x:1000:1000:Me:/home/tester:");
        hs_add(&line, abs_of("usr/bin/zsh"));
        hs_addc(&line, '\n');
        osr_sb_write(&sb, "etc/passwd", hs_text(&line), 0644);
        hs_free(&line);
    }
    osr_assert_rc(user_cmd("shell-is", "tester", abs_of("bin2/zsh"), NULL), 0,
        "shell-is: two paths that canonicalise to the same file are the same "
        "shell -- otherwise every run would 'fix' a shell that was correct");

    osr_sb_write(&sb, "etc/passwd",
        "root:x:0:0:root:/root:/bin/bash\n"
        "tester:x:1000:1000:Me:/home/tester:/bin/sh\n"
        "svc:x:999:999::/:/usr/sbin/nologin\n", 0644);

    /* ================================================================
     * 3. shell-registered -- /etc/shells
     *
     * An unlisted shell makes chsh refuse for a non-root user and makes some
     * login managers treat the account as broken. So it is registered BEFORE
     * anything tries to set it.
     * ================================================================ */
    osr_assert_rc(user_cmd("shell-registered", "/bin/sh", NULL, NULL), 0,
        "shell-registered: a listed shell is registered");
    osr_assert_true(user_cmd("shell-registered", "/usr/bin/zsh", NULL, NULL) != 0,
        "shell-registered: an unlisted one is not");
    osr_assert_rc(user_cmd("shell-registered", "", NULL, NULL), 0,
        "shell-registered: an empty shell is not a question worth failing on");

    /* ================================================================
     * 4. The /etc/passwd rewrite
     *
     * The last resort when neither chsh nor usermod exists -- a busybox box.
     * Everything except field 7 of one line must come back verbatim: this
     * file is how the machine logs anyone in.
     * ================================================================ */
    user_cmd("passwd-shell-file", abs_of("etc/passwd"), "tester", "/usr/bin/zsh");
    osr_assert_out_is(&sb,
        "root:x:0:0:root:/root:/bin/bash\n"
        "tester:x:1000:1000:Me:/home/tester:/usr/bin/zsh\n"
        "svc:x:999:999::/:/usr/sbin/nologin\n",
        "passwd-shell: field 7 of the named account is rewritten and every "
        "other byte of every other line comes back verbatim");

    osr_assert_true(
        user_cmd("passwd-shell-file", abs_of("etc/passwd"), "nosuchuser",
                 "/usr/bin/zsh") != 0,
        "passwd-shell: an account that is not there fails rather than "
        "rewriting nothing and reporting success");

    /* ================================================================
     * 5. needs-line -- ensure_line's probe
     *
     * The match is a SUBSTRING, deliberately: a module that appends a longer
     * line containing an earlier one still counts as already applied, which
     * is what keeps a rerun from stacking near-duplicates in a user's rc file.
     * ================================================================ */
    osr_sb_write(&sb, "rc", "unrelated\nlines here\n", 0644);
    osr_assert_rc(user_cmd("needs-line", abs_of("rc"), "export PATH=/opt/bin",
                           NULL), 0,
        "needs-line: an absent line needs appending");

    osr_sb_write(&sb, "rc", "unrelated\nexport PATH=/opt/bin:$PATH\nmore\n", 0644);
    osr_assert_true(user_cmd("needs-line", abs_of("rc"), "export PATH=/opt/bin",
                             NULL) != 0,
        "needs-line: a line already there does not need appending (SS2)");

    osr_sb_write(&sb, "rc", "# export PATH=/opt/bin (commented out)\n", 0644);
    osr_assert_true(user_cmd("needs-line", abs_of("rc"), "export PATH=/opt/bin",
                             NULL) != 0,
        "needs-line: a COMMENTED-OUT occurrence still counts as present -- the "
        "match is a substring, and a user who commented our line out did so "
        "deliberately (G2)");

    osr_sb_rm(&sb, "rc");
    osr_assert_rc(user_cmd("needs-line", abs_of("rc"), "anything", NULL), 0,
        "needs-line: a file that does not exist needs the line");

    /* ================================================================
     * 6. compose-block -- the SS5 owned region
     *
     * Everything outside the markers survives. That is the entire contract,
     * and it is what makes it acceptable for os-rice to write into ~/.zshrc
     * at all.
     * ================================================================ */
    osr_sb_write(&sb, "zshrc", "before\nafter\n", 0644);
    block("zshrc", "loader", "SOURCE THE RC DIR\n");
    file_is("zshrc",
        "before\n"
        "after\n"
        "# >>> os-rice:loader >>>\n"
        "SOURCE THE RC DIR\n"
        "# <<< os-rice:loader <<<\n",
        "block: a file with no block yet gets one appended, and what was there "
        "is untouched");

    /* A rerun with the same body changes nothing at all -- not even the
     * marker positions. */
    block("zshrc", "loader", "SOURCE THE RC DIR\n");
    file_is("zshrc",
        "before\n"
        "after\n"
        "# >>> os-rice:loader >>>\n"
        "SOURCE THE RC DIR\n"
        "# <<< os-rice:loader <<<\n",
        "block: a rerun with the same body leaves the file byte-identical (SS2)");

    osr_sb_write(&sb, "zshrc",
        "before\n"
        "# >>> os-rice:loader >>>\n"
        "OLD BODY\n"
        "stale\n"
        "# <<< os-rice:loader <<<\n"
        "after\n", 0644);
    block("zshrc", "loader", "NEW BODY\n");
    file_is("zshrc",
        "before\n"
        "after\n"
        "# >>> os-rice:loader >>>\n"
        "NEW BODY\n"
        "# <<< os-rice:loader <<<\n",
        "block: an existing block is REMOVED and rewritten at the end -- every "
        "line the user wrote survives in its own order, and the block lands "
        "last, which is where a loader wants to be anyway");

    /* A block belonging to something else is not ours to touch. Several
     * modules own a block in the same file. */
    osr_sb_write(&sb, "zshrc",
        "# >>> os-rice:other >>>\n"
        "X\n"
        "# <<< os-rice:other <<<\n", 0644);
    block("zshrc", "loader", "MINE\n");
    file_is("zshrc",
        "# >>> os-rice:other >>>\n"
        "X\n"
        "# <<< os-rice:other <<<\n"
        "# >>> os-rice:loader >>>\n"
        "MINE\n"
        "# <<< os-rice:loader <<<\n",
        "block: another module's block is left alone and ours is added beside "
        "it -- several modules own regions in the same file");

    /* An indented marker is not a marker. Someone quoting our block inside a
     * comment or a heredoc must not have it rewritten out from under them. */
    osr_sb_write(&sb, "zshrc",
        "  # >>> os-rice:loader >>>\n"
        "indented marker is not a marker\n", 0644);
    block("zshrc", "loader", "MINE\n");
    file_is("zshrc",
        "  # >>> os-rice:loader >>>\n"
        "indented marker is not a marker\n"
        "# >>> os-rice:loader >>>\n"
        "MINE\n"
        "# <<< os-rice:loader <<<\n",
        "block: an INDENTED marker is not a marker -- someone quoting our "
        "block in a comment keeps their text");

    osr_sb_write(&sb, "zshrc", "before\nno newline at eof", 0644);
    block("zshrc", "loader", "MINE\n");
    file_is("zshrc",
        "before\n"
        "no newline at eof\n"
        "# >>> os-rice:loader >>>\n"
        "MINE\n"
        "# <<< os-rice:loader <<<\n",
        "block: a file with no trailing newline gets one rather than having "
        "the marker welded onto its last line");

    osr_sb_rm(&sb, "zshrc");
    block("zshrc", "loader", "MINE\n");
    file_is("zshrc",
        "# >>> os-rice:loader >>>\n"
        "MINE\n"
        "# <<< os-rice:loader <<<\n",
        "block: a file that does not exist yet is created with just the block");

    /* ================================================================
     * 7. same-content -- the write-avoidance probe
     * ================================================================ */
    osr_sb_write(&sb, "a", "same\n", 0644);
    osr_sb_write(&sb, "b", "same\n", 0644);
    osr_assert_rc(user_cmd("same-content", abs_of("a"), abs_of("b"), NULL), 0,
        "same-content: identical files compare equal");
    osr_sb_write(&sb, "b", "different\n", 0644);
    osr_assert_true(user_cmd("same-content", abs_of("a"), abs_of("b"), NULL) != 0,
        "same-content: different files do not");
    osr_assert_true(
        user_cmd("same-content", abs_of("a"), abs_of("nope"), NULL) != 0,
        "same-content: a missing file is not equal to anything -- so the copy "
        "that follows actually happens");

    /* ================================================================
     * 8. resolve -- which account is being riced (SS8)
     *
     * The precedence is --user, then $SUDO_USER, then $USER, then whoever we
     * are. $SUDO_USER is the important one: `sudo osr install` runs as root
     * and must rice the person who typed it, not root.
     * ================================================================ */
    osr_sb_env(&sb, "SUDO_USER", "");
    osr_sb_env(&sb, "USER", "fromuser");
    user_cmd("resolve", "explicit", NULL, NULL);
    osr_assert_out(&sb, "OSR_USER='explicit'",
        "resolve: an explicit --user wins over everything");

    osr_sb_env(&sb, "SUDO_USER", "fromsudo");
    user_cmd("resolve", NULL, NULL, NULL);
    osr_assert_out(&sb, "OSR_USER='fromsudo'",
        "resolve: $SUDO_USER wins over $USER -- `sudo osr install` must rice "
        "the person who typed it, not root");

    /* SUDO_USER=root means someone was already root and ran sudo anyway; it
     * says nothing about who is being riced. */
    osr_sb_env(&sb, "SUDO_USER", "root");
    user_cmd("resolve", NULL, NULL, NULL);
    osr_assert_out(&sb, "OSR_USER='fromuser'",
        "resolve: SUDO_USER=root is ignored -- it names nobody in particular, "
        "so $USER is the better answer");

    osr_sb_env(&sb, "SUDO_USER", "");
    user_cmd("resolve", NULL, NULL, NULL);
    osr_assert_out(&sb, "OSR_USER='fromuser'",
        "resolve: $USER is the fallback");

    /* The home directory travels with the user: everything a module writes is
     * relative to it, so resolving one without the other would put files in
     * the wrong place while reporting the right account. */
    osr_assert_out(&sb, "OSR_HOME=",
        "resolve: and a home directory is always published alongside it");

    osr_sb_free(&sb);
    return osr_finish();
}
