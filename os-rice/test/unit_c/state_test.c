/* test/unit_c/state_test.c -- ~/.config/osr/state: what is applied to this box.
 *
 * A tiny `key=value` file, and the only thing os-rice remembers between runs.
 * `osr apply theme` reads the rice out of it; the wallpaper picker reads which
 * image goes with which theme; the runner writes it at the end of every
 * install. So a parse that quietly returns the wrong value re-applies the
 * wrong rice, and a write that quietly drops a line loses the record of what
 * this machine is.
 *
 * THE FILE IS THE USER'S. It lives in their home, and someone will edit it by
 * hand. Which makes the interesting cases the malformed ones: a duplicated
 * key, a value containing `=`, blank lines, a comment, no trailing newline.
 * None of them may make a write throw away what it did not understand.
 *
 * ON THE DOT IN A KEY
 *
 * `wallpaper.nord` is a real key -- lib/config.c composes one per theme. The
 * shell tier looked it up with `sed`, where `.` is a BASIC REGULAR EXPRESSION
 * matching any character, so `wallpaperXnord` would have matched it. The
 * scenario below pins that the lookup is literal.
 *
 * Hermetic: $OSR_HOME is inside the sandbox, and the escalated write goes
 * through the sandbox's sudo stub.
 *
 * Replaces test/unit/state_c_parity.sh. See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

#define STATE "home/.config/osr/state"

/* seed -- the state file this scenario starts from; NULL for none at all. */
static void seed(const char *contents) {
    osr_sb_rm(&sb, "home");
    osr_sb_mkdir(&sb, "home");
    if (contents != NULL) osr_sb_write(&sb, STATE, contents, 0644);
    osr_sb_reset(&sb);
}

/* get -- `osr state get <key>`, and what it printed. */
static void get_is(const char *key, const char *expected, const char *label) {
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "state", "get", key, (const char *)NULL);
    osr_assert_out_is(&sb, expected, label);
}

static int set(const char *key, const char *value) {
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "state", "set", key, value, (const char *)NULL);
}

/* file_is -- the state file, byte for byte. */
static void file_is(const char *expected, const char *label) {
    HStr path;
    char *got;
    hs_init(&path);
    hs_path(&path, hs_text(&sb.root), STATE);
    got = h_slurp(hs_text(&path));
    osr_assert_eq(expected, got, label);
    free(got);
    hs_free(&path);
}

int main(void) {
    osr_sb_init(&sb);

    /* ================================================================
     * 1. Reading
     * ================================================================ */
    seed("rice=i3-rosemary\ntheme=nord\nwallpaper=/img/a.png\napplied=1754\n");
    get_is("rice", "i3-rosemary\n", "get: a key returns its value");
    get_is("theme", "nord\n", "get: and so does the next one");
    get_is("absent", "",
        "get: a key that is not there prints nothing -- and prints it as "
        "nothing, not as an empty line");

    /* A file someone edited by hand can have the same key twice. The LAST
     * assignment wins, because that is what a file appended to means. */
    seed("theme=nord\ntheme=gruvbox\ntheme=xin\n");
    get_is("theme", "xin\n", "get: the last assignment of a repeated key wins");

    /* A wallpaper path is the value most likely to contain the delimiter. */
    seed("wallpaper=/img/a=b=c.png\ntheme=nord\n");
    get_is("wallpaper", "/img/a=b=c.png\n",
        "get: only the FIRST = separates -- a value may contain as many more "
        "as it likes, and wallpaper paths do");

    seed("rice=i3\n\n\ntheme=nord\n\n\n");
    get_is("theme", "nord\n", "get: blank lines are skipped, not parsed");

    seed("not a key value line\n=leading\nrice=i3\n# comment\n");
    get_is("rice", "i3\n",
        "get: junk lines, a comment and a line with an empty key are all "
        "ignored rather than confusing the lookup");

    /* A file whose last line has no newline -- which is what a hand edit in a
     * careless editor leaves. */
    seed("rice=i3\ntheme=nord");
    get_is("theme", "nord",
        "get: a final line with no trailing newline is still a line -- and the "
        "value comes back without one, which no caller can tell apart because "
        "every one of them reads it through $(...), and that strips trailing "
        "newlines either way");

    seed("");
    get_is("theme", "", "get: an empty file has no keys");
    seed(NULL);
    get_is("theme", "",
        "get: a machine that has never been riced is not an error -- there is "
        "simply nothing recorded yet");

    /* The dot. `wallpaper.nord` is composed per theme by lib/config.c, and a
     * regex lookup would match `wallpaperXnord` -- returning the decoy. */
    seed("wallpaperXnord=/img/decoy.png\nwallpaper.nord=/img/real.png\n");
    get_is("wallpaper.nord", "/img/real.png\n",
        "get: a key is matched LITERALLY -- the dot in wallpaper.nord is a "
        "dot, not a regex wildcard that would match the decoy above it");

    /* ================================================================
     * 2. Writing
     * ================================================================ */
    seed("rice=i3-rosemary\ntheme=nord\nwallpaper=/img/a.png\napplied=1754\n");
    set("theme", "gruvbox");
    file_is(
        "rice=i3-rosemary\n"
        "wallpaper=/img/a.png\n"
        "applied=1754\n"
        "theme=gruvbox\n",
        "set: an existing key is dropped and re-appended, so the file records "
        "the order things were last written in -- and there is exactly one "
        "line for the key afterwards, which is the part that matters");

    set("newkey", "newvalue");
    file_is(
        "rice=i3-rosemary\n"
        "wallpaper=/img/a.png\n"
        "applied=1754\n"
        "theme=gruvbox\n"
        "newkey=newvalue\n",
        "set: a key that was not there is appended");

    /* Rewriting collapses a hand-duplicated key to one line, which is the
     * repair a user would want and is safe: the last one was already the
     * effective value. */
    seed("theme=nord\ntheme=gruvbox\ntheme=xin\n");
    set("theme", "catppuccin");
    file_is("theme=catppuccin\n",
        "set: a key duplicated by hand collapses to one line -- the last was "
        "already the effective value, so nothing is lost");

    /* Lines the parser does not understand are the user's, and they survive. */
    seed("not a key value line\n=leading\nrice=i3\n# comment\n");
    set("theme", "nord");
    file_is(
        "not a key value line\n"
        "=leading\n"
        "rice=i3\n"
        "# comment\n"
        "theme=nord\n",
        "set: lines the parser does not understand are preserved verbatim -- "
        "this file is in the user's home and they may have written in it");

    seed("rice=i3\ntheme=nord");
    set("theme", "gruvbox");
    file_is("rice=i3\ntheme=gruvbox\n",
        "set: a missing final newline is repaired rather than welded over");

    seed(NULL);
    set("theme", "nord");
    file_is("theme=nord\n",
        "set: the first write to a machine that has never been riced creates "
        "the file");
    osr_assert_tree_is(&sb, "home",
        "home\n"
        "home/.config\n"
        "home/.config/osr\n"
        "home/.config/osr/state\n",
        "set: and the directories leading to it");

    /* A value containing the delimiter has to survive a round trip, not just
     * a read: this is the one that breaks a wallpaper on the second apply. */
    seed(NULL);
    set("wallpaper", "/img/a=b.png");
    get_is("wallpaper", "/img/a=b.png\n",
        "set: a value containing = survives being written and read back");

    /* ================================================================
     * 3. The sequence an install actually performs
     * ================================================================ */
    seed(NULL);
    set("rice", "i3-rosemary");
    set("theme", "nord");
    set("applied", "1754000000");
    set("theme", "gruvbox");
    set("wallpaper", "/img/b.png");
    file_is(
        "rice=i3-rosemary\n"
        "applied=1754000000\n"
        "theme=gruvbox\n"
        "wallpaper=/img/b.png\n",
        "a whole install's writes leave exactly four lines -- theme was set "
        "twice and appears once, having moved to where it was last written");

    /* ================================================================
     * 4. Where the file lives
     * ================================================================ */
    {
        HStr home, expect;
        hs_init(&home);
        hs_init(&expect);
        hs_path(&home, hs_text(&sb.root), "some home");
        osr_sb_env(&sb, "OSR_HOME", hs_text(&home));
        osr_sb_reset(&sb);
        osr_sb_run_core(&sb, "state", "file", (const char *)NULL);
        hs_add(&expect, "ROOT/some home/.config/osr/state");
        osr_assert_out_is(&sb, hs_text(&expect),
            "the path is under $OSR_HOME, and a home with a space in it comes "
            "back whole rather than split into two words");
        hs_free(&home);
        hs_free(&expect);
        osr_sb_env(&sb, "OSR_HOME", hs_text(&sb.home));
    }

    /* ================================================================
     * 5. The write is escalated to the user who owns the home
     *
     * `osr install` usually runs with sudo somewhere in the chain, and a state
     * file written as root in the user's home is one they cannot rewrite
     * afterwards -- so the next `osr apply theme` they run as themselves
     * fails on a file os-rice created (SS8).
     * ================================================================ */
    seed(NULL);
    set("theme", "nord");
    osr_assert_log_is(&sb,
        "sudo -u tester mkdir -p ROOT/home/.config/osr\n"
        "sudo -u tester tee ROOT/home/.config/osr/state\n",
        "both the mkdir and the write run as the target user, so everything "
        "under their home stays theirs (SS8)");
    file_is("theme=nord\n",
        "and the file the escalated write produced is the composed one");

    osr_sb_free(&sb);
    return osr_finish();
}
