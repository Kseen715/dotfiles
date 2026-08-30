/* test/unit_c/theme_test.c -- reading a theme: the manifest parser, the
 * palette, and the substitution script built from it.
 *
 * SS6b: a theme IS its palette. One `theme.list` per theme, in the same
 * newline `key: value` shape as a rice.list -- deliberately not TOML, because
 * a `while read` loop is the whole parser and a parser is a dependency.
 *
 * Which puts the risk in one place: a manifest people hand-write, parsed by
 * something with no grammar. So most of this file is a HOSTILE fixture --
 * comments in every position, tabs instead of spaces, runs of whitespace, a
 * value with no colour, a role name with a hyphen in it, no trailing newline.
 * A parser that silently mis-reads one line does not fail here; it produces a
 * theme with one wrong colour, which nobody traces back to the parser.
 *
 * The rules the fixture pins:
 *
 *   A `#` starts a comment ANYWHERE except inside a colour value, where it
 *   introduces the hex. That ambiguity is the whole reason this needs a test.
 *
 *   A role a template asks for and a theme omits is a WARNING, not a failure:
 *   the file still lands with the placeholder visible, so the gap is obvious
 *   and local rather than fatal and global.
 *
 * Hermetic: $OSR_ROOT points at a fixture tree for the hostile scenarios, and
 * at the real one for the scenarios that say the shipped themes are real.
 *
 * Replaces test/unit/theme_c_parity.sh. See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

static void use_tree(const char *rel) {
    if (rel == NULL) {
        osr_sb_env(&sb, "OSR_ROOT", hs_text(&sb.osr_root));
        return;
    }
    {
        HStr p;
        hs_init(&p);
        hs_path(&p, hs_text(&sb.root), rel);
        osr_sb_env(&sb, "OSR_ROOT", hs_text(&p));
        hs_free(&p);
    }
}

static void theme_cmd(const char *a, const char *b, const char *c) {
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "theme", a, b, c, (const char *)NULL);
}

static void out_is(const char *expected, const char *label) {
    osr_assert_out_is(&sb, expected, label);
}

/* colour -- `osr theme color <theme> <role>`. */
static void colour_is(const char *theme, const char *role, const char *expected,
                      const char *label) {
    theme_cmd("color", theme, role);
    out_is(expected, label);
}

/* meta -- `osr theme meta <theme> <key>`. */
static void meta_is(const char *theme, const char *key, const char *expected,
                    const char *label) {
    theme_cmd("meta", theme, key);
    out_is(expected, label);
}

int main(void) {
    osr_sb_init(&sb);

    /* ================================================================
     * 1. The hostile manifest
     *
     * Every line here is a real shape a hand-written theme.list can take.
     * ================================================================ */
    osr_sb_mkdir(&sb, "tree/rices/demo");
    osr_sb_write(&sb, "tree/themes/hostile/theme.list",
        "# a whole-line comment\n"
        "   # an indented comment\n"
        "display: Hostile   # trailing comment\n"
        "description:    spaces   everywhere\n"
        "polarity:dark\n"
        "color: background #2e3440\n"
        "color: background_blur 0\n"
        "\tcolor: foreground\t#d8dee9\t\n"
        "color: accent #88c0d0 # with a comment\n"
        "color: accent_red #bf616a\n"
        "color: half #abc\n"
        "color: text_muted #4c566a\n"
        "color: NotAWord-role #123456\n"
        "color: novalue\n"
        "config: gtk-3.0 fontconfig\n"
        "config: xsettingsd\n"
        "session: wayland\n"
        "UPPER: ignored by the generic rule\n"
        "number9: fine\n"
        "trailing_hash: value #\n", 0644);
    osr_sb_write(&sb, "tree/themes/bare/theme.list", "display: Bare\n", 0644);
    osr_sb_write(&sb, "tree/rices/demo/rice.list",
        "theme: hostile\nthemes: hostile bare\nzsh\n", 0644);
    use_tree("tree");

    theme_cmd("list", NULL, NULL);
    out_is("bare\nhostile\n",
        "themes are listed in sorted order, so a picker is reproducible");

    osr_assert_rc(osr_sb_run_core(&sb, "theme", "exists", "hostile",
                                  (const char *)NULL), 0,
        "a theme with a manifest exists");
    osr_assert_true(osr_sb_run_core(&sb, "theme", "exists", "nosuchtheme",
                                    (const char *)NULL) != 0,
        "one without does not");
    osr_assert_true(osr_sb_run_core(&sb, "theme", "exists", "",
                                    (const char *)NULL) != 0,
        "and neither does the empty name");

    /* --- metadata --- */
    meta_is("hostile", "display", "Hostile",
        "meta: a trailing comment is stripped, and so is the whitespace before it");
    meta_is("hostile", "description", "spaces   everywhere",
        "meta: leading whitespace is stripped but INTERNAL runs are kept -- "
        "they are part of the text a picker shows");
    meta_is("hostile", "polarity", "dark",
        "meta: a key with no space after the colon still parses");
    meta_is("hostile", "trailing_hash", "value",
        "meta: a value ending in a bare # loses it -- an empty comment is "
        "still a comment");
    meta_is("hostile", "number9", "fine",
        "meta: a key with a digit in it is a key");
    meta_is("hostile", "no-such-key", "",
        "meta: a key the theme does not declare is empty, not an error");

    /* --- colours --- */
    colour_is("hostile", "background", "#2e3440",
        "color: the ordinary case");
    colour_is("hostile", "foreground", "#d8dee9",
        "color: tabs work as separators, and trailing whitespace is stripped");
    colour_is("hostile", "accent", "#88c0d0",
        "color: the # that starts the VALUE is kept and the # that starts a "
        "comment after it is not -- the one genuine ambiguity in the format");
    colour_is("hostile", "background_blur", "0",
        "color: a role whose value is not a colour at all is passed through");
    colour_is("hostile", "half", "#abc",
        "color: a three-digit hex is not rejected -- the theme author's "
        "shorthand is theirs, and the apps that read it understand it");
    colour_is("hostile", "NotAWord-role", "#123456",
        "color: a role name with a hyphen is a role name");
    colour_is("hostile", "novalue", "",
        "color: a role declared with no value is empty rather than garbage");
    colour_is("hostile", "nope", "",
        "color: an undeclared role is empty -- which is what makes a missing "
        "role a warning at substitution time rather than a failure here");

    /* A near miss: `accent_red` must not be found when `accent` is asked for,
     * and asking for `accent` must not return `accent_red`'s value. */
    colour_is("hostile", "accent_red", "#bf616a",
        "color: a role that is a PREFIX of another resolves to its own value");

    theme_cmd("configs", "hostile", NULL);
    out_is("gtk-3.0\nfontconfig\nxsettingsd\n",
        "configs: several config: lines accumulate, and a line naming more "
        "than one directory yields one per line -- the caller loops over them");

    theme_cmd("session", "hostile", NULL);
    out_is("wayland", "session: a declared session is reported");
    theme_cmd("session", "bare", NULL);
    out_is("any",
        "session: a theme that does not say defaults to `any` -- a theme is "
        "about colour, and most are session-agnostic");

    theme_cmd("rice-themes", "demo", NULL);
    out_is("hostile\nbare\n",
        "a rice lists the themes it offers, one per line and in the order the "
        "rice declared them");
    theme_cmd("rice-default", "demo", NULL);
    out_is("hostile", "and names one of them as its default");

    /* A manifest whose last line has no newline still yields that directive.
     * An editor without the setting is enough to produce one. */
    osr_sb_write(&sb, "tree/themes/bare/theme.list",
        "display: NoNewline\ncolor: accent #ffffff", 0644);
    meta_is("bare", "display", "NoNewline",
        "a manifest with no trailing newline still yields its metadata");
    colour_is("bare", "accent", "#ffffff",
        "and its last colour, which is the directive most likely to be lost");

    /* ================================================================
     * 2. The substitution script
     *
     * What turns `{{accent}}` in a template into a colour. Every role the
     * theme declares becomes a rule, plus THEME for the directory name.
     * ================================================================ */
    osr_sb_write(&sb, "tree/themes/bare/theme.list",
        "display: Bare\ncolor: accent #ffffff\n", 0644);
    theme_cmd("sed", "bare", NULL);
    osr_assert_out(&sb, "accent",
        "sed: a declared role becomes a substitution rule");
    osr_assert_out(&sb, "#ffffff",
        "sed: mapped to its value");
    osr_assert_out(&sb, "THEME",
        "sed: and THEME is substitutable too, so a template can name its own "
        "theme directory");

    /* ================================================================
     * 3. The palette arithmetic
     *
     * A hex colour split into decimal components, comma-separated, for the
     * templates whose config format takes `r,g,b` rather than a hex pair --
     * several do, and none of them accepts the hex.
     * ================================================================ */
    use_tree(NULL);
    theme_cmd("hex-dec", "#000000", NULL);
    out_is("0,0,0", "hex-dec: black");
    theme_cmd("hex-dec", "#ffffff", NULL);
    out_is("255,255,255", "hex-dec: white -- ff is 255, not 15 or 0");
    theme_cmd("hex-dec", "#010203", NULL);
    out_is("1,2,3",
        "hex-dec: each pair is read on its own, so a leading zero is not "
        "dropped and the components do not run together");
    theme_cmd("hex-dec", "#2e3440", NULL);
    out_is("46,52,64", "hex-dec: a real theme colour");

    /* ================================================================
     * 4. The shipped themes are real
     *
     * The rules above are asserted against a fixture, so they say what the
     * parser does. These say the themes in the repository parse -- a theme
     * whose manifest is malformed produces a rice with placeholder colours
     * visible in every config file.
     * ================================================================ */
    theme_cmd("list", NULL, NULL);
    osr_assert_true(osr_sb_capture(&sb)[0] != '\0',
        "the repository ships at least one theme");
    {
        /* Every shipped theme must declare a display name and a background --
         * the two things the picker and every template need. */
        const char *list = osr_sb_capture(&sb);
        HStr names;
        const char *p;
        int all_named = 1;
        int all_coloured = 1;

        hs_init(&names);
        hs_add(&names, list);
        p = hs_text(&names);
        while (*p != '\0') {
            HStr one;
            hs_init(&one);
            while (*p != '\0' && *p != '\n') hs_addc(&one, *p++);
            if (*p == '\n') p++;
            if (one.len > 0) {
                theme_cmd("meta", hs_text(&one), "display");
                if (osr_sb_capture(&sb)[0] == '\0') all_named = 0;
                theme_cmd("color", hs_text(&one), "background");
                if (osr_sb_capture(&sb)[0] == '\0') all_coloured = 0;
            }
            hs_free(&one);
        }
        hs_free(&names);
        osr_assert_true(all_named,
            "every shipped theme declares a display name -- the picker shows "
            "it, and a theme with none is unpickable");
        osr_assert_true(all_coloured,
            "every shipped theme declares a background colour, which is the "
            "one role effectively every template asks for");
    }

    /* ================================================================
     * 5. Resolving which theme to apply
     * ================================================================ */
    osr_sb_reset(&sb);
    osr_assert_true(osr_sb_run_core(&sb, "theme", "exists", "nosuchtheme",
                                    (const char *)NULL) != 0,
        "resolve: a name no theme carries does not exist");

    osr_sb_free(&sb);
    return osr_finish();
}
