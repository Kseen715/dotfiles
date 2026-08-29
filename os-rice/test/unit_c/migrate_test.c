/* test/unit_c/migrate_test.c -- what lib/migrate.c must do to a file os-rice
 * seeded but the USER now owns.
 *
 * SS5 splits config into two kinds. A managed layer os-rice rewrites freely.
 * A SEEDED layer -- ~/.config/zsh/rc.d/00-env.zsh and friends -- is written
 * once and then belongs to the user, so a later release that needs to change
 * something in it cannot just overwrite the file. It has to patch, and it has
 * to be able to tell "the user never touched this" from "the user rewrote it",
 * because getting that wrong destroys work the user did.
 *
 * That is the whole of what is asserted here, in three verbs:
 *
 *   append  -- add a line if it is not already there (idempotent by probe)
 *   replace -- swap a region, but ONLY if it is still byte-for-byte what we
 *              seeded; otherwise refuse and let the caller warn
 *   stale   -- notice a legacy that cannot be patched automatically and say so
 *
 * Every match is LITERAL, never a regex. These regions are shell code, full of
 * `[`, `*`, `.` and `$` -- a regex match would either miss or, worse, match
 * something else and splice a replacement into the middle of it.
 *
 * Hermetic: everything happens to files inside the sandbox.
 *
 * Replaces test/unit/migrate_c_parity.sh. The zsh module's use of these verbs
 * is asserted in zsh_test.c. See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

#define RC "home/00-env.zsh"

/* the region modules/zsh.c actually migrates: a brew probe that depended on
 * $PATH already containing brew, replaced by an absolute path. Real text,
 * because its metacharacters are the point. */
#define OLD_BREW \
    "if command -v brew >/dev/null 2>&1; then\n" \
    "  eval \"$(brew shellenv)\"\n" \
    "fi"
#define NEW_BREW \
    "if [ -x /opt/homebrew/bin/brew ]; then\n" \
    "  eval \"$(/opt/homebrew/bin/brew shellenv)\"\n" \
    "fi"

/* rc_path -- the absolute path of the file under migration. */
static const char *rc_path(void) {
    static HStr p;
    static int ready = 0;
    if (!ready) { hs_init(&p); ready = 1; }
    hs_path(&p, hs_text(&sb.root), RC);
    return hs_text(&p);
}

/* seed -- the file this scenario starts from. NULL means the file does not
 * exist at all, which is its own case: a box that never had the layer. */
static void seed(const char *contents) {
    osr_sb_rm(&sb, "home");
    osr_sb_mkdir(&sb, "home");
    if (contents != NULL) osr_sb_write(&sb, RC, contents, 0644);
    osr_sb_reset(&sb);
}

/* content_is -- the file after the run, byte for byte. The only assertion
 * that matters for a patcher: what the user's file now says. */
static void content_is(const char *rel, const char *expected, const char *label) {
    HStr path;
    char *got;
    hs_init(&path);
    hs_path(&path, hs_text(&sb.root), rel);
    got = h_slurp(hs_text(&path));
    osr_assert_eq(expected, got, label);
    free(got);
    hs_free(&path);
}

/* append -- `osr migrate append <file> <probe> <what>`, with the text to add
 * on stdin. */
static int append(const char *probe, const char *what, const char *text) {
    osr_sb_reset(&sb);
    osr_sb_stdin(&sb, text);
    return osr_sb_run_core(&sb, "migrate", "append", rc_path(), probe, what,
                           (const char *)NULL);
}

/* replace -- `osr migrate replace <file> <what> <old> <new>`. */
static int replace(const char *what, const char *old, const char *new_text) {
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "migrate", "replace", rc_path(), what, old,
                           new_text, (const char *)NULL);
}

/* warned / quiet -- what `stale` actually reports.
 *
 * Its exit status carries nothing: lib/migrate.c returns 1 from every path,
 * met or not, because a legacy it cannot patch is not a failure of the run --
 * it is a note to the user. So the WARNING is the observable, and these two
 * functions are how a scenario states which it expected. */
static void warned(const char *needle, const char *label) {
    osr_assert_err(&sb, needle, label);
}
static void quiet(const char *label) {
    osr_assert_true(osr_sb_capture_err(&sb)[0] == '\0', label);
}

/* stale -- `osr migrate stale <file> <ere> <what>`. */
static int stale(const char *ere, const char *what) {
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "migrate", "stale", rc_path(), ere, what,
                           (const char *)NULL);
}

int main(void) {
    osr_sb_init(&sb);

    /* ================================================================
     * 1. append -- add a line that is not there
     * ================================================================ */
    seed("export EDITOR=vi\n");
    append("typeset -U path", "typeset -U path PATH", "typeset -U path PATH\n");
    content_is(RC,
        "export EDITOR=vi\n"
        "\n"
        "typeset -U path PATH\n",
        "append: the text lands at the end behind a blank line, and what was "
        "there is untouched -- the separator is why a second appended region "
        "does not run into the first");

    /* SS2: the probe is what makes a rerun a no-op. Note it is the PROBE that
     * is searched for, not the text -- so a line the user reformatted still
     * counts as present and is not appended a second time. */
    seed("export EDITOR=vi\ntypeset -U path PATH\n");
    append("typeset -U path", "typeset -U path PATH", "typeset -U path PATH\n");
    content_is(RC,
        "export EDITOR=vi\n"
        "typeset -U path PATH\n",
        "append: an already-migrated file is left byte-identical (SS2)");

    /* A file that does not exist is not a file to patch. Creating it here
     * would put a fragment on disk with none of the seeded layer around it. */
    seed(NULL);
    append("typeset -U path", "typeset -U path PATH", "typeset -U path PATH\n");
    osr_assert_absent(&sb, RC,
        "append: a file that does not exist is skipped, never created");

    /* A file whose last line has no newline: the append must not weld itself
     * onto it and silently corrupt the line that was there. */
    seed("export EDITOR=vi");
    append("typeset -U path", "typeset -U path PATH", "typeset -U path PATH\n");
    content_is(RC,
        "export EDITOR=vi\n"
        "typeset -U path PATH\n",
        "append: a missing trailing newline is supplied, not welded over");

    /* The backup is the file as it was BEFORE any migration ever ran, so it
     * is written once and never refreshed -- a second migration overwriting it
     * would leave the user with a 'backup' that already has the first
     * migration baked in. */
    seed("one\n");
    osr_sb_write(&sb, "home/00-env.zsh.pre-migrate", "ORIGINAL\n", 0644);
    append("two", "add two", "two\n");
    content_is("home/00-env.zsh.pre-migrate", "ORIGINAL\n",
        "append: an existing backup is never overwritten -- it is the "
        "pre-migration state, not the previous state");
    content_is(RC, "one\n\ntwo\n", "append: and the migration still happened");

    seed("one\n");
    append("two", "add two", "two\n");
    content_is("home/00-env.zsh.pre-migrate", "one\n",
        "append: the first migration takes the backup");

    /* ================================================================
     * 2. replace -- swap a region, but only if it is still ours
     * ================================================================ */
    seed("export EDITOR=vi\n" OLD_BREW "\nexport PAGER=less\n");
    osr_assert_rc(replace("brew probe -> absolute path", OLD_BREW, NEW_BREW), 0,
        "replace: an untouched region reports success");
    content_is(RC,
        "export EDITOR=vi\n" NEW_BREW "\nexport PAGER=less\n",
        "replace: the region is swapped and everything around it is preserved");

    /* One edited character and the region is no longer ours. Refusing is the
     * entire point: the alternative is overwriting a file the user tuned by
     * hand, which SS5 forbids and which no backup makes acceptable. */
    seed("export EDITOR=vi\n"
         "if command -v brew >/dev/null 2>&1; then\n"
         "  eval \"$(brew shellenv --no-op)\"\n"
         "fi\n");
    osr_assert_rc(replace("brew probe -> absolute path", OLD_BREW, NEW_BREW), 1,
        "replace: an edited region reports FAILURE, so the caller can warn");
    content_is(RC,
        "export EDITOR=vi\n"
        "if command -v brew >/dev/null 2>&1; then\n"
        "  eval \"$(brew shellenv --no-op)\"\n"
        "fi\n",
        "replace: the user's edit survives byte for byte");

    /* An empty replacement is a deletion, which is how a region that has no
     * successor gets retired. */
    seed("keep\n" OLD_BREW "\nkeep2\n");
    replace("drop the brew probe", OLD_BREW, "");
    content_is(RC, "keep\nkeep2\n",
        "replace: an empty replacement deletes the region outright");

    /* Literal matching is the safety property. This needle is real seeded
     * text and it is nothing but metacharacters: `[`, `$`, `.`, `*`. */
    seed("a\n"
         "[ -s \"$NVM_DIR/nvm.sh\" ] && . \"$NVM_DIR/nvm.sh\"  # *loads* nvm\n"
         "b\n");
    replace("legacy nvm -> 30-tools.zsh",
            "[ -s \"$NVM_DIR/nvm.sh\" ] && . \"$NVM_DIR/nvm.sh\"  # *loads* nvm",
            "source \"$OSR_RCDIR/30-tools.zsh\"");
    content_is(RC,
        "a\n"
        "source \"$OSR_RCDIR/30-tools.zsh\"\n"
        "b\n",
        "replace: the needle is matched LITERALLY -- these regions are shell "
        "code and a regex would match something else entirely");

    /* An empty needle would 'match' at offset 0 under any naive search and
     * splice the replacement in at the top of the user's file. */
    seed("untouched\n");
    replace("empty needle", "", "INJECTED");
    content_is(RC, "untouched\n",
        "replace: an empty region matches nothing -- it never means 'the "
        "start of the file'");

    seed(NULL);
    osr_assert_rc(replace("nothing to do", "x", "y"), 1,
        "replace: a file that does not exist reports failure");
    osr_assert_absent(&sb, RC, "replace: and it is not created");

    /* ================================================================
     * 3. stale -- a legacy that cannot be patched
     *
     * For the case where the old text varies too much to match literally. The
     * verb cannot fix anything, so all it owes the user is an accurate
     * warning -- and a warning that fires on a file that is already fixed is
     * worse than none, because it trains people to ignore it.
     * ================================================================ */
    seed("if command -v brew >/dev/null; then :; fi\n");
    stale("command -v brew", "a PATH-dependent brew probe");
    warned("a PATH-dependent brew probe",
        "stale: a code line still holding the legacy is reported, in the "
        "caller's own words");
    osr_assert_err(&sb, "see zsh/rc.d/ in the dotfiles repo",
        "stale: and the warning says where the current version lives, since "
        "the fix is the user's to make");

    /* The replacement text explains what it replaced, so a naive grep matches
     * the FIX. Skipping comments is what makes a successful migration quiet. */
    seed("# never uses command -v brew any more\nexport EDITOR=vi\n");
    stale("command -v brew", "a PATH-dependent brew probe");
    quiet("stale: a comment mentioning the legacy is not the legacy");

    seed("   # indented comment: command -v brew\n");
    stale("command -v brew", "a PATH-dependent brew probe");
    quiet("stale: an indented comment is still a comment");

    seed("\tif command -v brew; then :; fi\n");
    stale("command -v brew", "a PATH-dependent brew probe");
    warned("a PATH-dependent brew probe",
           "stale: an indented CODE line still counts");

    seed(NULL);
    stale("command -v brew", "a PATH-dependent brew probe");
    quiet("stale: a file that does not exist holds no legacy");

    seed("export EDITOR=vi\n");
    stale("command -v brew", "a PATH-dependent brew probe");
    quiet("stale: a clean file is quiet");

    /* Nothing in this verb writes. It is a report, and a report that edited
     * the file would be the one thing a user could not undo. */
    seed("if command -v brew >/dev/null; then :; fi\n");
    stale("command -v brew", "a PATH-dependent brew probe");
    content_is(RC, "if command -v brew >/dev/null; then :; fi\n",
        "stale: reporting never modifies the file");
    osr_assert_absent(&sb, "home/00-env.zsh.pre-migrate",
        "stale: and it takes no backup, because it changes nothing");

    osr_sb_free(&sb);
    return osr_finish();
}
