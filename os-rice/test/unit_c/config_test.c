/* test/unit_c/config_test.c -- SS5, the layering rules: which files os-rice
 * owns, which it seeds and then never touches again, and which it owns only a
 * marked region of.
 *
 * This is the unit with the most direct route to destroying somebody's work,
 * so the rules are worth stating plainly:
 *
 *   A MANAGED LAYER (install_layer) is os-rice's. It is overwritten on every
 *   run -- but the previous contents are backed up ONCE, so a user who edited
 *   one and lost it can get it back.
 *
 *   A SEEDED LAYER (seed_once, seed_empty) is written when it does not exist
 *   and NEVER again. 00-env.zsh and 99-local.zsh are the user's the moment
 *   they exist; changes to them ship as migrations (lib/migrate.c), not as
 *   rewrites.
 *
 *   AN OWNED BLOCK is a marked region inside a file that belongs to somebody
 *   else -- ~/.zshrc, ~/.xprofile. Everything outside the markers survives.
 *
 *   A COMPOSED CONFIG merges a rice fragment over a base, so a rice states
 *   only its differences and a base change reaches every rice.
 *
 * ...and one more, which is not a layering rule but lives here because it is
 * the same kind of promise: a config ADAPTED to the installed version of an
 * app. foot renamed its palette sections and Alacritty moved its options
 * between releases; writing the current spelling to an older binary makes it
 * refuse to start, which for a terminal emulator means no way back in.
 *
 * Hermetic: every path is inside the sandbox and the version probes are stubs.
 *
 * Replaces test/unit/config_c_parity.sh and config_layering.sh. See
 * test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

static const char *at(const char *rel) {
    static HStr ring[4];
    static int ready = 0;
    static int next = 0;
    HStr *p;
    if (!ready) { int i; for (i = 0; i < 4; i++) hs_init(&ring[i]); ready = 1; }
    p = &ring[next];
    next = (next + 1) % 4;
    hs_path(p, hs_text(&sb.root), rel);
    return hs_text(p);
}

static char *read_rel(const char *rel) {
    return h_slurp(at(rel));
}

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
static void file_is(const char *rel, const char *expected, const char *label) {
    char *got = read_rel(rel);
    osr_assert_eq(expected, got, label);
    free(got);
}

/* count -- how many times `needle` appears in a file. The idempotency
 * assertion for anything that appends. */
static int count(const char *rel, const char *needle) {
    char *got = read_rel(rel);
    const char *p = got;
    int n = 0;
    while ((p = strstr(p, needle)) != NULL) { n++; p++; }
    free(got);
    return n;
}

static int cfg(const char *a, const char *b, const char *c, const char *d) {
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "config", a, b, c, d, (const char *)NULL);
}

/* ver_stub -- an app that reports a version, for the adapted configs. */
static void ver_stub(const char *name, const char *line) {
    HStr body;
    hs_init(&body);
    hs_add(&body, "printf '");
    hs_add(&body, line);
    hs_add(&body, "\\n'\n");
    osr_sb_stub_body(&sb, name, hs_text(&body));
    hs_free(&body);
}

/* fresh -- an empty home and theme dir. */
static void fresh(void) {
    osr_sb_rm(&sb, "home");
    osr_sb_rm(&sb, "theme");
    osr_sb_mkdir(&sb, "home");
    osr_sb_mkdir(&sb, "theme");
    osr_sb_reset(&sb);
}

int main(void) {
    HStr p;

    osr_sb_init(&sb);
    hs_init(&p);
    hs_path(&p, hs_text(&sb.root), "theme");
    osr_sb_env(&sb, "OSR_THEME_DIR", hs_text(&p));
    osr_sb_env(&sb, "OSR_THEME", "nord");

    /* ================================================================
     * 1. Seeded layers -- written once, then the user's
     * ================================================================ */
    fresh();
    osr_sb_write(&sb, "src", "export A=1\n", 0644);
    cfg("seed-once", at("src"), at("home/.config/zsh/rc.d/00-env.zsh"), NULL);
    file_is("home/.config/zsh/rc.d/00-env.zsh", "export A=1\n",
        "seed-once: an absent layer is created, directories and all");

    /* The whole point: a second run does NOT overwrite the edit. */
    osr_sb_write(&sb, "home/.config/zsh/rc.d/00-env.zsh",
                 "export A=1\nUSER-EDIT\n", 0644);
    cfg("seed-once", at("src"), at("home/.config/zsh/rc.d/00-env.zsh"), NULL);
    file_is("home/.config/zsh/rc.d/00-env.zsh", "export A=1\nUSER-EDIT\n",
        "seed-once: a second run keeps what the user wrote -- this file is "
        "theirs from the moment it exists, and changes to it ship as "
        "migrations rather than as rewrites (SS5)");

    /* seed_empty is the same rule for a file whose shipped content is
     * nothing: 99-local.zsh exists so the user has somewhere to put things. */
    fresh();
    cfg("seed-empty", at("home/99-local.zsh"), NULL, NULL);
    file_is("home/99-local.zsh", "",
        "seed-empty: the layer is created empty, as a place for the user's "
        "own lines");
    osr_sb_write(&sb, "home/99-local.zsh", "alias mine=ls\n", 0644);
    cfg("seed-empty", at("home/99-local.zsh"), NULL, NULL);
    file_is("home/99-local.zsh", "alias mine=ls\n",
        "seed-empty: and a second run never truncates it");

    /* ================================================================
     * 2. Owned blocks -- a marked region in somebody else's file
     * ================================================================ */
    fresh();
    osr_sb_write(&sb, "home/.zshrc", "# my zshrc\nexport FOO=1\n", 0644);
    cfg("zsh-loader", at("home/.config/zsh/rc.d"), at("home/.zshrc"), NULL);
    holds("home/.zshrc", "# >>> os-rice:loader >>>",
        "zsh-loader: the region is marked, so the next run can find it again");
    holds("home/.zshrc", "export FOO=1",
        "zsh-loader: the user's own lines survive");

    cfg("zsh-loader", at("home/.config/zsh/rc.d"), at("home/.zshrc"), NULL);
    osr_assert_true(count("home/.zshrc", "os-rice:loader") == 2,
        "zsh-loader: a rerun leaves exactly one block -- two begin markers "
        "would make the shell source the drop-in directory twice");
    holds("home/.zshrc", "export FOO=1",
        "zsh-loader: and the user's lines are still there after the rerun");

    /* A stale block body is replaced, and only the body. */
    fresh();
    osr_sb_write(&sb, "home/.zshrc",
        "BEFORE\n"
        "# >>> os-rice:loader >>>\n"
        "old loader\n"
        "# <<< os-rice:loader <<<\n"
        "AFTER\n", 0644);
    cfg("zsh-loader", at("home/.config/zsh/rc.d"), at("home/.zshrc"), NULL);
    holds("home/.zshrc", "BEFORE",
        "zsh-loader: the user's lines above the block survive");
    holds("home/.zshrc", "AFTER",
        "zsh-loader: and the ones below");
    lacks("home/.zshrc", "old loader",
        "zsh-loader: the previous body is gone -- an out-of-date loader left "
        "beside the new one would source the directory twice");

    /* ~/.zshenv is a separate block, and it exists for one line. */
    fresh();
    osr_sb_write(&sb, "home/.zshenv", "user zshenv\n", 0644);
    cfg("zsh-zshenv", at("home/.zshenv"), NULL, NULL);
    holds("home/.zshenv", "skip_global_compinit=1",
        "zsh-zshenv: the compinit opt-out is what this block is for -- "
        "~/.zshenv is the only file early enough to suppress Ubuntu's "
        "duplicate global compinit");
    holds("home/.zshenv", "user zshenv",
        "zsh-zshenv: and the user's own zshenv survives");

    fresh();
    cfg("xprofile-loader", at("home/.config/xprofile.d"), at("home/.xprofile"),
        NULL);
    holds("home/.xprofile", "os-rice",
        "xprofile-loader: the X session loader is an owned block too");

    /* ================================================================
     * 3. Composed configs -- a rice states only its differences
     * ================================================================ */
    /* The JSON merge is python3's -- a real parser rather than a sed script,
     * because a settings.json is nested and hand-rolling that is how a config
     * file ends up subtly malformed. */
    osr_sb_real(&sb, "python3");
    fresh();
    osr_sb_write(&sb, "base.json",
        "{\n  \"editor.fontSize\": 13,\n  \"workbench.colorTheme\": \"Default\"\n}\n",
        0644);
    osr_sb_write(&sb, "frag.json",
        "{\n  \"workbench.colorTheme\": \"Nord\"\n}\n", 0644);
    cfg("json", at("base.json"), at("frag.json"), at("home/settings.json"));
    holds("home/settings.json", "\"workbench.colorTheme\": \"Nord\"",
        "json: the rice's value wins over the base's");
    holds("home/settings.json", "\"editor.fontSize\": 13",
        "json: and a base key the rice says nothing about survives -- which is "
        "what lets a base change reach every rice");

    fresh();
    osr_sb_write(&sb, "base.json", "{\n  \"editor.fontSize\": 13\n}\n", 0644);
    cfg("json", at("base.json"), at("missing.json"), at("home/settings.json"));
    holds("home/settings.json", "\"editor.fontSize\": 13",
        "json: a rice with no fragment of its own gets the base");
    osr_assert_true(strstr(osr_sb_capture_both(&sb), "installing the base") != NULL,
        "json: and that is reported rather than being silent -- a missing "
        "fragment is usually a typo in a rice");

    /* A box without python3 still gets a usable config -- the base, with a
     * warning. SS9 again: degrade, do not break. */
    fresh();
    osr_sb_rm(&sb, "bin/python3");
    osr_sb_write(&sb, "base.json", "{\n  \"editor.fontSize\": 13\n}\n", 0644);
    osr_sb_write(&sb, "frag.json", "{\n  \"a\": 1\n}\n", 0644);
    cfg("json", at("base.json"), at("frag.json"), at("home/settings.json"));
    holds("home/settings.json", "\"editor.fontSize\": 13",
        "json: with no python3 the base is installed unmerged rather than "
        "nothing at all");
    osr_assert_true(strstr(osr_sb_capture_both(&sb), "python3") != NULL,
        "json: and the missing interpreter is named, so the un-themed result "
        "is explained rather than mysterious");
    osr_sb_real(&sb, "python3");

    fresh();
    osr_sb_write(&sb, "base.toml",
        "format = \"$all\"\n\n[palettes.theme]\nred = \"#000000\"\n", 0644);
    osr_sb_write(&sb, "palette.toml",
        "[palettes.theme]\nred = \"#bf616a\"\n", 0644);
    cfg("starship", at("base.toml"), at("palette.toml"), at("home/starship.toml"));
    holds("home/starship.toml", "#bf616a",
        "starship: the theme's palette table replaces the base's");
    lacks("home/starship.toml", "#000000",
        "starship: and the base's colours are gone, not merely shadowed -- "
        "a duplicate table is a parse error in TOML");
    holds("home/starship.toml", "format = \"$all\"",
        "starship: everything outside the palette table is the base's");

    /* ================================================================
     * 4. Configs adapted to the installed version
     *
     * The failure mode is specific and nasty: a terminal emulator handed a
     * config it cannot parse REFUSES TO START, and the terminal is how the
     * user would have fixed it.
     * ================================================================ */
    fresh();
    osr_sb_write(&sb, "palette.ini",
        "[colors-dark]\nbackground=2e3440\n"
        "[colors-light]\nbackground=eceff4\n", 0644);
    ver_stub("foot", "foot version: 1.26.0");
    cfg("foot-palette", at("palette.ini"), at("home/colors.ini"), NULL);
    holds("home/colors.ini", "[colors-dark]",
        "foot: a current foot gets the sections under their current names");

    fresh();
    osr_sb_write(&sb, "palette.ini",
        "[colors-dark]\nbackground=2e3440\n"
        "[colors-light]\nbackground=eceff4\n", 0644);
    ver_stub("foot", "foot version: 1.20.2");
    cfg("foot-palette", at("palette.ini"), at("home/colors.ini"), NULL);
    lacks("home/colors.ini", "[colors-dark]",
        "foot: an older foot never learnt those section names, so they are "
        "downgraded rather than written and ignored");

    fresh();
    osr_sb_write(&sb, "palette.ini", "[colors-dark]\nbackground=2e3440\n", 0644);
    osr_sb_rm(&sb, "bin/foot");
    cfg("foot-palette", at("palette.ini"), at("home/colors.ini"), NULL);
    lacks("home/colors.ini", "[colors-dark]",
        "foot: with foot not installed yet the OLD spelling is assumed -- the "
        "old names still work on a new foot, and the new ones do not work on "
        "an old one, so the safe guess is the compatible one");

    fresh();
    osr_sb_write(&sb, "alacritty.toml",
        "[general]\nimport = []\n\n[font]\nsize = 11\n", 0644);
    ver_stub("alacritty", "alacritty 0.15.1 (abcdef)");
    cfg("alacritty", at("alacritty.toml"), at("home/alacritty.toml"), NULL);
    holds("home/alacritty.toml", "[general]",
        "alacritty: 0.14+ understands [general]");

    fresh();
    osr_sb_write(&sb, "alacritty.toml",
        "[general]\nimport = []\n\n[font]\nsize = 11\n", 0644);
    ver_stub("alacritty", "alacritty 0.13.2 (abcdef)");
    cfg("alacritty", at("alacritty.toml"), at("home/alacritty.toml"), NULL);
    lacks("home/alacritty.toml", "[general]",
        "alacritty: 0.13 does not, and an unknown section is a hard parse "
        "error -- so it is stripped");
    holds("home/alacritty.toml", "[font]",
        "alacritty: while the rest of the config is untouched");

    fresh();
    osr_sb_write(&sb, "alacritty.toml", "[general]\nimport = []\n", 0644);
    ver_stub("alacritty", "alacritty 0.12.3 (abcdef)");
    cfg("alacritty", at("alacritty.toml"), at("home/alacritty.toml"), NULL);
    osr_assert_true(strstr(osr_sb_capture_both(&sb), "0.12") != NULL ||
                    strstr(osr_sb_capture_both(&sb), "yaml") != NULL ||
                    strstr(osr_sb_capture_both(&sb), "YAML") != NULL,
        "alacritty: the YAML era predates TOML entirely, so it is warned "
        "about rather than silently handed a file it cannot read");

    fresh();
    osr_sb_write(&sb, "alacritty.toml", "[general]\nimport = []\n", 0644);
    osr_sb_rm(&sb, "bin/alacritty");
    cfg("alacritty", at("alacritty.toml"), at("home/alacritty.toml"), NULL);
    holds("home/alacritty.toml", "[general]",
        "alacritty: with alacritty not installed yet the CURRENT spelling is "
        "assumed -- the opposite guess from foot, because here it is the new "
        "form that is compatible with what will be installed");

    /* ================================================================
     * 5. Whole theme-owned directories
     * ================================================================ */
    fresh();
    osr_sb_write(&sb, "theme/config/gtk-3.0/settings.ini",
        "[Settings]\ngtk-theme-name=Nord\n", 0644);
    cfg("apply", "gtk-3.0", NULL, NULL);
    holds("home/.config/gtk-3.0/settings.ini", "gtk-theme-name=Nord",
        "apply: a config directory the theme ships is copied into ~/.config");

    fresh();
    cfg("apply", "gtk-3.0", NULL, NULL);
    osr_assert_absent(&sb, "home/.config/gtk-3.0",
        "apply: a config the theme does not ship is skipped rather than "
        "creating an empty directory");

    /* ================================================================
     * 6. Mozilla profiles
     *
     * Firefox and Thunderbird keep their settings in a randomly named profile
     * directory, so the layer has to be installed into whichever one the app
     * actually made -- and into ALL of them, because a user with two profiles
     * would otherwise get a themed one and an unthemed one.
     * ================================================================ */
    fresh();
    osr_sb_write(&sb, "home/.mozilla/firefox/profiles.ini",
        "[Profile0]\nName=default\nIsRelative=1\nPath=abc123.default\n"
        "\n[Profile1]\nName=work\nIsRelative=1\nPath=def456.work\n", 0644);
    osr_sb_mkdir(&sb, "home/.mozilla/firefox/abc123.default");
    osr_sb_mkdir(&sb, "home/.mozilla/firefox/def456.work");
    cfg("mozilla-profiles", at("home/.mozilla/firefox"), NULL, NULL);
    osr_assert_out(&sb, "abc123.default",
        "mozilla: profiles.ini is read, and the first profile found");
    osr_assert_out(&sb, "def456.work",
        "mozilla: and the second -- a user with two profiles gets both themed");

    /* An app that has never been started has no profiles.ini, but may have
     * left a directory behind. */
    fresh();
    osr_sb_mkdir(&sb, "home/.mozilla/firefox/xyz789.default-release");
    cfg("mozilla-profiles", at("home/.mozilla/firefox"), NULL, NULL);
    osr_assert_out(&sb, "xyz789.default-release",
        "mozilla: with no profiles.ini the directories are globbed instead");

    hs_free(&p);
    osr_sb_free(&sb);
    return osr_finish();
}
