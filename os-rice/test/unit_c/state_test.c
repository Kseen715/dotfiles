/* test/unit_c/state_test.c -- ~/.config/osr/state.yaml: what is applied to
 * this box, and what is installed on it.
 *
 * One file, two halves. The scalars are the state: which rice, which theme,
 * which wallpaper, when it was applied -- the only thing os-rice remembers
 * between runs. The `modules` sequence is what this machine actually has
 * installed, which is what a theme apply repaints (lib/apply.c): a rice
 * manifest says what a rice SHIPS, this says what is really here.
 *
 * So a parse that quietly returns the wrong value re-applies the wrong rice,
 * and a write that quietly drops the sequence stops repainting half the
 * desktop on the next switch.
 *
 * THE FILE IS THE USER'S. It lives in their home, and someone will edit it by
 * hand. Which makes the interesting cases the ones nothing here writes: flow
 * style, quoted scalars, comments, a nested mapping, no trailing newline.
 * That is why the read side is the vendored parser (thirdparty/yaml.h) while
 * the write side is plain always-double-quoted text.
 *
 * THE LEGACY FILE. Before this, state was a flat `key=value` file at
 * ~/.config/osr/state. A machine that has one still reads it, and the first
 * write migrates: state.yaml appears, the flat file goes. One source of
 * truth, or the next apply reads a stale one.
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

#define STATE  "home/.config/osr/state.yaml"
#define LEGACY "home/.config/osr/state"

/* HEAD -- the header every write puts back, since libyaml does not report
 * comments and there is nothing to carry over from the old file. */
#define HEAD \
    "# os-rice state -- what is applied to this machine, and what is\n" \
    "# installed on it. Written by `osr install`, `osr theme` and\n" \
    "# `osr module <name>`; read by all three.\n" \
    "#\n" \
    "# `modules` is the set a theme apply repaints: a rice manifest says\n" \
    "# what a rice SHIPS, this says what is really here. Safe to hand-edit\n" \
    "# -- drop a line to stop repainting that app, add one to start.\n"

/* seed -- the state file this scenario starts from; NULL for none at all. */
static void seed(const char *contents) {
    osr_sb_rm(&sb, "home");
    osr_sb_mkdir(&sb, "home");
    if (contents != NULL) osr_sb_write(&sb, STATE, contents, 0644);
    osr_sb_reset(&sb);
}

/* seed_legacy -- a machine that was riced before the file became YAML. */
static void seed_legacy(const char *contents) {
    osr_sb_rm(&sb, "home");
    osr_sb_mkdir(&sb, "home");
    osr_sb_write(&sb, LEGACY, contents, 0644);
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

static void installed_is(const char *expected, const char *label) {
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "state", "installed", (const char *)NULL);
    osr_assert_out_is(&sb, expected, label);
}

static void installed_add(const char *name) {
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "state", "installed", "add", name, (const char *)NULL);
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
    seed("rice: \"i3-rosemary\"\ntheme: \"nord\"\n"
         "wallpaper: \"/img/a.png\"\napplied: \"1754\"\n");
    get_is("rice", "i3-rosemary\n", "get: a key returns its value");
    get_is("theme", "nord\n", "get: and so does the next one");
    get_is("absent", "",
        "get: a key that is not there prints nothing -- and prints it as "
        "nothing, not as an empty line");

    /* A file someone edited by hand can have the same key twice. The LAST
     * assignment wins, because that is what a file appended to means. */
    seed("theme: nord\ntheme: gruvbox\ntheme: xin\n");
    get_is("theme", "xin\n", "get: the last assignment of a repeated key wins");

    /* A wallpaper path is the value most likely to contain something that
     * used to be a delimiter, back when the file was `key=value`. */
    seed("wallpaper: \"/img/a=b=c.png\"\ntheme: nord\n");
    get_is("wallpaper", "/img/a=b=c.png\n",
        "get: a value containing = is just a value -- the old flat file split "
        "on the first one, and wallpaper paths are where that showed");

    /* Comments, blank lines and unquoted scalars are all things a hand edit
     * leaves, and none of them is what this program writes. */
    seed("# what I run\n\nrice: i3\n\ntheme: nord\n");
    get_is("theme", "nord\n",
        "get: a comment, blank lines and bare scalars read the same as the "
        "quoted form this program writes");

    /* Only the ROOT mapping is state. A nested structure someone adds is
     * skipped rather than flattened into keys that were never set. */
    seed("rice: i3\nnotes:\n  theme: gruvbox\n  why: testing\ntheme: nord\n");
    get_is("theme", "nord\n",
        "get: a key nested under another one is not a root key -- the theme "
        "inside `notes` does not shadow the real one");
    get_is("why", "",
        "get: and nothing inside that nested mapping becomes a key at all");

    /* A file whose last line has no newline -- which is what a hand edit in a
     * careless editor leaves. */
    seed("rice: i3\ntheme: nord");
    get_is("theme", "nord\n", "get: a final line with no trailing newline is "
        "still a line, and the parser ends the value at end of file");

    seed("");
    get_is("theme", "", "get: an empty file has no keys");
    seed(NULL);
    get_is("theme", "",
        "get: a machine that has never been riced is not an error -- there is "
        "simply nothing recorded yet");

    /* The dot. `wallpaper.nord` is composed per theme by lib/config.c, and a
     * regex lookup would match `wallpaperXnord` -- returning the decoy. */
    seed("wallpaperXnord: \"/img/decoy.png\"\nwallpaper.nord: \"/img/real.png\"\n");
    get_is("wallpaper.nord", "/img/real.png\n",
        "get: a key is matched LITERALLY -- the dot in wallpaper.nord is a "
        "dot, not a regex wildcard that would match the decoy above it");

    /* ================================================================
     * 2. Writing
     * ================================================================ */
    seed("rice: \"i3-rosemary\"\ntheme: \"nord\"\n"
         "wallpaper: \"/img/a.png\"\napplied: \"1754\"\n");
    set("theme", "gruvbox");
    file_is(HEAD
        "rice: \"i3-rosemary\"\n"
        "wallpaper: \"/img/a.png\"\n"
        "applied: \"1754\"\n"
        "theme: \"gruvbox\"\n",
        "set: an existing key is dropped and re-appended, so the file records "
        "the order things were last written in -- and there is exactly one "
        "line for the key afterwards, which is the part that matters");

    set("newkey", "newvalue");
    file_is(HEAD
        "rice: \"i3-rosemary\"\n"
        "wallpaper: \"/img/a.png\"\n"
        "applied: \"1754\"\n"
        "theme: \"gruvbox\"\n"
        "newkey: \"newvalue\"\n",
        "set: a key that was not there is appended");

    /* Rewriting collapses a hand-duplicated key to one line, which is the
     * repair a user would want and is safe: the last one was already the
     * effective value. */
    seed("theme: nord\ntheme: gruvbox\ntheme: xin\n");
    set("theme", "catppuccin");
    file_is(HEAD "theme: \"catppuccin\"\n",
        "set: a key duplicated by hand collapses to one line -- the last was "
        "already the effective value, so nothing is lost");

    /* A rewrite of one scalar leaves the other half of the file alone: the
     * modules are what the next theme apply repaints, and a `set theme` that
     * dropped them would stop repainting everything on this box. */
    seed("theme: nord\nmodules:\n  - btop\n  - telegram\n");
    set("theme", "gruvbox");
    file_is(HEAD
        "theme: \"gruvbox\"\n"
        "modules:\n"
        "  - btop\n"
        "  - telegram\n",
        "set: the modules survive a scalar write untouched -- they are what "
        "the next apply repaints");

    /* Values are always double-quoted, so a value that YAML would otherwise
     * read as something else comes back as the string it was written as. */
    seed(NULL);
    set("applied", "1754000000");
    set("wallpaper", "/img/a: b \"c\"\\d.png");
    get_is("applied", "1754000000\n",
        "set: a digits-only value round-trips as a string, not as a number");
    get_is("wallpaper", "/img/a: b \"c\"\\d.png\n",
        "set: a value with a colon, quotes and a backslash survives the round "
        "trip -- always quoting means exactly one escaping rule");

    seed(NULL);
    set("theme", "nord");
    file_is(HEAD "theme: \"nord\"\n",
        "set: the first write to a machine that has never been riced creates "
        "the file");
    osr_assert_tree_is(&sb, "home",
        "home\n"
        "home/.config\n"
        "home/.config/osr\n"
        "home/.config/osr/state.yaml\n",
        "set: and the directories leading to it");

    /* ================================================================
     * 3. The sequence an install actually performs
     * ================================================================ */
    seed(NULL);
    set("rice", "i3-rosemary");
    set("theme", "nord");
    set("applied", "1754000000");
    set("theme", "gruvbox");
    set("wallpaper", "/img/b.png");
    file_is(HEAD
        "rice: \"i3-rosemary\"\n"
        "applied: \"1754000000\"\n"
        "theme: \"gruvbox\"\n"
        "wallpaper: \"/img/b.png\"\n",
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
        hs_add(&expect, "ROOT/some home/.config/osr/state.yaml");
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
        "sudo -u tester tee ROOT/home/.config/osr/state.yaml\n",
        "both the mkdir and the write run as the target user, so everything "
        "under their home stays theirs (SS8)");
    file_is(HEAD "theme: \"nord\"\n",
        "and the file the escalated write produced is the composed one");

    /* ================================================================
     * 6. modules -- what is actually on this machine
     *
     * A rice manifest is what a rice SHIPS. This is what is really here, and
     * it is what a theme apply repaints (lib/apply.c), so a module added
     * later with `osr module <name>` stops being skipped by every switch.
     * ================================================================ */
    seed(NULL);
    installed_is("",
        "installed: a machine that has never installed anything lists nothing "
        "-- a missing file is the ordinary first case, not an error");

    installed_add("btop");
    installed_add("telegram");
    installed_add("btop");
    installed_is("btop\ntelegram\n",
        "installed add: each module once, in the order first recorded -- "
        "re-adding one is a no-op, because every install of an already "
        "installed module would otherwise lengthen the list forever");

    /* The two halves are one file, and neither write may lose the other. */
    set("theme", "nord");
    installed_add("alacritty");
    get_is("theme", "nord\n",
        "installed add: recording a module keeps the scalars -- one file, and "
        "a write of either half that dropped the other would lose the rice");
    installed_is("btop\ntelegram\nalacritty\n",
        "and the modules are still all there after the scalar write");

    /* Flow style, a quoted scalar and a comment: none of them is what this
     * program writes, all of them are valid YAML, and the reason the read
     * side is a parser rather than a `while read` loop is that it has to
     * survive a hand edit. */
    seed("# what I actually run\nmodules: [alacritty, \"btop\", rofi]\n");
    installed_is("alacritty\nbtop\nrofi\n",
        "installed: a hand-edited file in flow style, with a quoted name and "
        "a comment, reads back as the same three modules");

    /* Anything after the sequence is not a module name. */
    seed("modules:\n  - btop\nrice: xin\n");
    installed_is("btop\n",
        "installed: only the `modules` sequence is read -- a key someone "
        "added after it is not mistaken for a module");
    get_is("rice", "xin\n",
        "and that key is still read as a key");

    /* Same escalation as the state file: same file, in fact (SS8). */
    seed(NULL);
    installed_add("btop");
    osr_assert_log_is(&sb,
        "sudo -u tester mkdir -p ROOT/home/.config/osr\n"
        "sudo -u tester tee ROOT/home/.config/osr/state.yaml\n",
        "installed add: written as the target user, exactly as a scalar write "
        "is -- a root-owned file here would break the next apply they run");

    /* ================================================================
     * 7. The flat file that came before
     *
     * A machine riced by an older build has ~/.config/osr/state, a flat
     * `key=value` file. It keeps working until the first write, which
     * migrates it -- and then it is GONE, because two files that both claim
     * to say what is applied is how the next apply reads a stale one.
     * ================================================================ */
    seed_legacy("rice=i3-rosemary\ntheme=nord\nwallpaper=/img/a=b c.png\n");
    get_is("theme", "nord\n",
        "legacy: the flat file is still read, so an old machine does not "
        "forget its rice the moment it updates");
    get_is("wallpaper", "/img/a=b c.png\n",
        "legacy: and still splits on the FIRST = only, so a path with one in "
        "it comes back whole");

    set("theme", "catppuccin");
    file_is(HEAD
        "rice: \"i3-rosemary\"\n"
        "wallpaper: \"/img/a=b c.png\"\n"
        "theme: \"catppuccin\"\n",
        "legacy: the first write carries every key over into the YAML file");
    osr_assert_tree_is(&sb, "home",
        "home\n"
        "home/.config\n"
        "home/.config/osr\n"
        "home/.config/osr/state.yaml\n",
        "legacy: and the flat file is removed -- one source of truth, or the "
        "next apply reads whichever one it happens to find");

    /* The same migration from the other half: recording a module is a write. */
    seed_legacy("rice=i3\ntheme=nord");
    installed_add("btop");
    file_is(HEAD
        "rice: \"i3\"\n"
        "theme: \"nord\"\n"
        "modules:\n"
        "  - btop\n",
        "legacy: a module write migrates the same way, missing final newline "
        "and all");

    osr_sb_free(&sb);
    return osr_finish();
}
