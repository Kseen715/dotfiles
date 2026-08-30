/* test/unit_c/theme_layers_test.c -- how a theme reaches an application's
 * config file, and who asks for one.
 *
 * SS6b again, from the other end. theme_test.c asserts that a theme.list is
 * READ correctly; this file asserts what is done with it:
 *
 *   THE TEMPLATE. One `<app>.tmpl` beside that app's dotfiles, with `{{role}}`
 *   placeholders. Writing a new theme is a theme.list and nothing else,
 *   because every app's layer is generated from the palette.
 *
 *   THE ESCAPE HATCH. A theme may ship a LITERAL file for an app, and it wins
 *   over the template. Nothing in the tree needs this today, but it is what
 *   let the migration land one app at a time and it is the answer for a layer
 *   that is genuinely not a palette substitution.
 *
 *   THE MARKER. `osr module themable <name>` says whether a module reads a
 *   theme at all -- which is what lets an install of non-themable modules skip
 *   theme resolution entirely, instead of stopping to ask the user which
 *   theme they want before installing docker.
 *
 * DEGRADE, NEVER BREAK A SWITCH (SS9). A placeholder no theme defines does not
 * fail the render. The file lands with `{{nosuchrole}}` visible and a warning
 * names it -- so the gap is obvious, local, and does not take a whole rice
 * switch down with it.
 *
 * Replaces test/unit/theme_template.sh, theme_picker.sh and
 * module_themable.sh. See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

static void run_module(const char *name) {
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "module", "run", name, (const char *)NULL);
}

/* rendered -- the contents of a file the module wrote into the sandbox home. */
static char *rendered(const char *rel) {
    HStr path;
    char *got;
    hs_init(&path);
    hs_path(&path, hs_text(&sb.root), rel);
    got = h_slurp(hs_text(&path));
    hs_free(&path);
    return got;
}

static void holds(const char *rel, const char *needle, const char *label) {
    char *got = rendered(rel);
    osr_assert_true(strstr(got, needle) != NULL, label);
    free(got);
}
static void lacks(const char *rel, const char *needle, const char *label) {
    char *got = rendered(rel);
    osr_assert_true(strstr(got, needle) == NULL, label);
    free(got);
}

/* themable -- `osr module themable <name>`, as a boolean. */
static int themable(const char *name) {
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "module", "themable", name,
                           (const char *)NULL) == 0;
}

int main(void) {
    HStr p;

    osr_sb_init(&sb);
    hs_init(&p);

    osr_sb_env(&sb, "OSR_PKG", "apt");
    osr_sb_env(&sb, "OSR_INIT", "systemd");
    osr_sb_stub_body(&sb, "dpkg", "exit 1\n");
    osr_sb_stub_body(&sb, "apt-mark", "exit 0\n");
    osr_sb_stub_body(&sb, "apt-get",
        "printf 'apt-get %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");

    /* ================================================================
     * 1. Rendering a template
     *
     * A synthetic theme and a synthetic app, so the assertions state the
     * substitution rules rather than tracking whatever the real themes hold.
     * ================================================================ */
    osr_sb_write(&sb, "root/themes/testtheme/theme.list",
        "display: Test Theme\n"
        "polarity: dark\n"
        "gtk_theme: Test-Adwaita\n"
        "\n"
        "color: background  #101010\n"
        "color: foreground  #f0f0f0\n"
        "color: accent      #00ff00\n", 0644);
    /* btop's theme layer is `btop/btop.theme.tmpl` rendered to
     * ~/.config/btop/themes/rice.theme, under a fixed name so btop.conf can
     * point at it without knowing which theme is current. */
    osr_sb_write(&sb, "dotfiles/btop/btop.theme.tmpl",
        "name = {{THEME}} ({{display}})\n"
        "polarity = {{polarity}}\n"
        "gtk = {{gtk_theme}}\n"
        "background = {{background}}\n"
        "foreground = {{foreground}}\n"
        "accent = {{accent}}\n"
        "accent_bare = {{accent_rgb}}\n", 0644);
    osr_sb_write(&sb, "dotfiles/btop/btop.theme",
        "theme[main_fg]=\"#dddddd\"\n", 0644);
    osr_sb_write(&sb, "dotfiles/btop/btop.conf",
        "color_theme = \"rice\"\n", 0644);

    hs_path(&p, hs_text(&sb.root), "root");
    osr_sb_env(&sb, "OSR_ROOT", hs_text(&p));
    hs_path(&p, hs_text(&sb.root), "dotfiles");
    osr_sb_env(&sb, "OSR_DOTFILES", hs_text(&p));
    osr_sb_env(&sb, "OSR_THEME", "testtheme");
    hs_path(&p, hs_text(&sb.root), "root/themes/testtheme");
    osr_sb_env(&sb, "OSR_THEME_DIR", hs_text(&p));

    run_module("btop");
    holds("home/.config/btop/themes/rice.theme", "background = #101010",
        "template: a colour role substitutes");
    holds("home/.config/btop/themes/rice.theme", "accent = #00ff00",
        "template: every colour role substitutes, not just the first");
    holds("home/.config/btop/themes/rice.theme", "polarity = dark",
        "template: a metadata field substitutes too");
    holds("home/.config/btop/themes/rice.theme", "gtk = Test-Adwaita",
        "template: a non-colour field substitutes -- a theme is not only hexes");
    holds("home/.config/btop/themes/rice.theme", "name = testtheme (Test Theme)",
        "template: {{THEME}} is the theme's own directory name, alongside its "
        "display field");
    holds("home/.config/btop/themes/rice.theme", "accent_bare = 00ff00",
        "template: every colour also has an _rgb spelling with no leading "
        "hash -- foot and others write bare RRGGBB");
    lacks("home/.config/btop/themes/rice.theme", "{{",
        "template: nothing is left unsubstituted when the theme covers it all");
    osr_assert_true(
        strstr(osr_sb_capture_err(&sb), "no value") == NULL,
        "template: a fully covered template renders silently");

    /* SS9: a placeholder the theme has no value for. */
    osr_sb_write(&sb, "dotfiles/btop/btop.theme.tmpl",
        "missing = {{nosuchrole}}\nbackground = {{background}}\n", 0644);
    osr_sb_rm(&sb, "home");
    run_module("btop");
    holds("home/.config/btop/themes/rice.theme", "background = #101010",
        "a missing role: the rest of the file still renders");
    holds("home/.config/btop/themes/rice.theme", "{{nosuchrole}}",
        "a missing role: the placeholder is left VISIBLE, so the gap is "
        "obvious and local rather than a silently wrong colour");
    osr_assert_err(&sb, "nosuchrole",
        "a missing role: and it is named in a warning");

    /* ================================================================
     * 2. The escape hatch -- a literal file in the theme wins
     * ================================================================ */
    osr_sb_write(&sb, "dotfiles/btop/btop.theme.tmpl",
        "background = {{background}}\n", 0644);
    osr_sb_write(&sb, "root/themes/testtheme/config/btop/btop.theme",
        "# RICE-THEME-MARKER\ntheme[main_fg]=\"#123456\"\n", 0644);
    osr_sb_rm(&sb, "home");
    run_module("btop");
    holds("home/.config/btop/themes/rice.theme", "RICE-THEME-MARKER",
        "escape hatch: a literal file the theme ships wins over the dotfiles "
        "default for that app");

    /* And with no literal file the dotfiles default is used, so an app is
     * never left with no theme at all. */
    osr_sb_rm(&sb, "root/themes/testtheme/config");
    osr_sb_rm(&sb, "home");
    run_module("btop");
    holds("home/.config/btop/themes/rice.theme", "background = #101010",
        "escape hatch: with no literal file the app's own TEMPLATE is rendered "
        "-- an app is never left with no theme");
    holds("home/.config/btop/btop.conf", "color_theme = \"rice\"",
        "and the app's ordinary config layer lands alongside it, pointing at "
        "the fixed theme name rather than at whichever theme is current");

    /* ================================================================
     * 3. The themable marker
     *
     * The whole point: an install of modules that read no theme must not stop
     * to ask which theme the user wants.
     * ================================================================ */
    osr_sb_env(&sb, "OSR_ROOT", hs_text(&sb.osr_root));
    hs_path(&p, hs_text(&sb.osr_root), "..");
    osr_sb_env(&sb, "OSR_DOTFILES", hs_text(&p));

    osr_assert_true(themable("btop"),
        "themable: btop installs a theme layer and says so");
    osr_assert_true(themable("fastfetch"),
        "themable: so does fastfetch");
    osr_assert_true(!themable("docker"),
        "themable: docker reads no theme and says so -- an install of only "
        "modules like this never asks the user to pick one");
    osr_assert_true(!themable("no-such-module"),
        "themable: a module that does not exist is not themable");

    /* The count is a canary on the reading itself: a bug that answered "no"
     * for everything would leave the checks above with nothing to disagree
     * about, and this is what would catch it. */
    {
        int marked = 0;
        HStr list;
        const char *q;

        osr_sb_reset(&sb);
        osr_sb_run_core(&sb, "module", "list", (const char *)NULL);
        hs_init(&list);
        hs_add(&list, osr_sb_capture(&sb));
        q = hs_text(&list);
        while (*q != '\0') {
            HStr one;
            hs_init(&one);
            while (*q != '\0' && *q != '\n') hs_addc(&one, *q++);
            if (*q == '\n') q++;
            if (one.len > 0 && themable(hs_text(&one))) marked++;
            hs_free(&one);
        }
        hs_free(&list);
        osr_assert_true(marked >= 20,
            "themable: at least twenty modules across the tree declare "
            "themselves themable -- a reading bug that answered no for "
            "everything would pass every check above and fail this one");
    }

    /* ================================================================
     * 4. The shipped themes are pickable
     *
     * The picker shows a name, a description and a swatch. A theme missing
     * any of the roles the swatch draws is one the picker renders as a gap.
     * ================================================================ */
    {
        static const char *const roles[] = {
            "background", "surface", "foreground", "text_dim", "accent", NULL
        };
        HStr list;
        const char *q;
        int all_hex = 1;
        int themes = 0;

        osr_sb_reset(&sb);
        osr_sb_run_core(&sb, "theme", "list", (const char *)NULL);
        hs_init(&list);
        hs_add(&list, osr_sb_capture(&sb));
        q = hs_text(&list);
        while (*q != '\0') {
            HStr one;
            int i;
            hs_init(&one);
            while (*q != '\0' && *q != '\n') hs_addc(&one, *q++);
            if (*q == '\n') q++;
            if (one.len == 0) { hs_free(&one); continue; }
            themes++;
            for (i = 0; roles[i] != NULL; i++) {
                const char *v;
                osr_sb_reset(&sb);
                osr_sb_run_core(&sb, "theme", "color", hs_text(&one), roles[i],
                                (const char *)NULL);
                v = osr_sb_capture(&sb);
                /* #rrggbb: seven characters, a hash and six hex digits. */
                if (strlen(v) != 7 || v[0] != '#') all_hex = 0;
            }
            hs_free(&one);
        }
        hs_free(&list);
        osr_assert_true(themes >= 6,
            "the repository ships at least six themes for the picker to offer");
        osr_assert_true(all_hex,
            "every shipped theme defines background, surface, foreground, "
            "text_dim and accent as a full #rrggbb -- these five are what the "
            "picker's swatch draws, and a short one renders as a gap");
    }

    /* A swatch is drawn from those five, and it is ASCII like everything else
     * the CLI prints (D-2). */
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "theme", "swatch", "nord", (const char *)NULL);
    {
        const unsigned char *c = (const unsigned char *)osr_sb_capture(&sb);
        int ascii = 1;
        for (; *c != '\0'; c++) {
            if (*c > 0x7f) ascii = 0;
        }
        osr_assert_true(ascii,
            "a theme swatch is 7-bit ASCII with ANSI colour (D-2) -- the block "
            "characters a prettier swatch would use are mojibake on a serial "
            "console");
    }

    hs_free(&p);
    osr_sb_free(&sb);
    return osr_finish();
}
