/* test/unit_c/wallpaper_test.c -- choosing, recording and reporting the
 * wallpaper for a theme.
 *
 * The one part of a rice a user changes on a whim, so it has its own front end
 * (`./wallpaper.sh`, a shim over `osr wallpaper`) that a picker or a hotkey
 * calls directly. Three things have to hold, and each is easy to get subtly
 * wrong:
 *
 *   A CHOICE IS PER THEME. `wallpaper.<theme>` in the state file, not
 *   `wallpaper`. Switching to another theme and back must return the image
 *   that theme was left on, not the last one chosen anywhere.
 *
 *   TWO RECORDS, AND THEY ARE DIFFERENT PATHS. `wallpaper.<theme>` is the
 *   CHOICE -- wherever the user picked the image from. `wallpaper` is the
 *   INSTALLED copy under ~/Pictures/Wallpapers, and that is the one a setter
 *   or a lockscreen config points at, because the source may be inside the
 *   os-rice checkout and the user may move or delete that.
 *
 *   A GONE CHOICE DEGRADES. A pick whose source has been deleted since falls
 *   back to the theme's default rather than resolving to a path that is not
 *   there.
 *
 * Hermetic: a fixture tree of themes and images, a sandboxed home, and no
 * wallpaper setter on $PATH at all -- so the "headless" branch is what runs
 * and nothing tries to repaint the desktop of whoever runs the suite.
 *
 * Replaces test/unit/wallpaper_c_parity.sh, wallpaper_choice.sh and
 * wallpaper_layer.sh. See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

/* wp -- `osr wallpaper <args>`. */
static int wp(const char *a, const char *b) {
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "wallpaper", a, b, (const char *)NULL);
}

/* out_has -- a substring of what the last run printed on either stream. */
static void out_has(const char *needle, const char *label) {
    osr_assert_true(strstr(osr_sb_capture_both(&sb), needle) != NULL, label);
}
static void out_lacks(const char *needle, const char *label) {
    osr_assert_true(strstr(osr_sb_capture_both(&sb), needle) == NULL, label);
}

/* state_has -- a substring of the state file. */
static void state_has(const char *needle, const char *label) {
    HStr path;
    char *got;
    hs_init(&path);
    hs_path(&path, hs_text(&sb.root), "home/.config/osr/state");
    got = h_slurp(hs_text(&path));
    osr_assert_true(strstr(got, needle) != NULL, label);
    free(got);
    hs_free(&path);
}

/* on_theme -- record which theme is applied, and start from an empty home. */
static void on_theme(const char *theme) {
    osr_sb_rm(&sb, "home");
    osr_sb_mkdir(&sb, "home/.config/osr");
    if (theme != NULL) {
        HStr line;
        hs_init(&line);
        hs_add(&line, "theme=");
        hs_add(&line, theme);
        hs_addc(&line, '\n');
        osr_sb_write(&sb, "home/.config/osr/state", hs_text(&line), 0644);
        hs_free(&line);
    }
    osr_sb_reset(&sb);
}

/* img -- a file that looks enough like an image for the type probe. */
static void img(const char *rel) {
    osr_sb_write(&sb, rel, "\211PNG\r\n\032\n-fake-image-bytes\n", 0644);
}

static const char *in_tree(const char *rel) {
    static HStr ring[3];
    static int ready = 0;
    static int next = 0;
    HStr *p;
    HStr full;
    if (!ready) { int i; for (i = 0; i < 3; i++) hs_init(&ring[i]); ready = 1; }
    p = &ring[next];
    next = (next + 1) % 3;
    hs_init(&full);
    hs_add(&full, "root/");
    hs_add(&full, rel);
    hs_path(p, hs_text(&sb.root), hs_text(&full));
    hs_free(&full);
    return hs_text(p);
}

int main(void) {
    HStr p;

    osr_sb_init(&sb);
    hs_init(&p);

    /* Two themes: one with images, one whose wallpapers/ holds only the
     * placeholder README every theme directory ships. */
    osr_sb_write(&sb, "root/themes/nord/theme.list", "display: nord\n", 0644);
    osr_sb_write(&sb, "root/themes/bare/theme.list", "display: bare\n", 0644);
    img("root/themes/nord/wallpapers/01-first.png");
    img("root/themes/nord/wallpapers/02-second.jpg");
    osr_sb_write(&sb, "root/themes/nord/wallpapers/README.txt",
                 "drop a real image here\n", 0644);
    osr_sb_write(&sb, "root/themes/bare/wallpapers/README.txt",
                 "drop a real image here\n", 0644);
    img("extra/outside.png");

    hs_path(&p, hs_text(&sb.root), "root");
    osr_sb_env(&sb, "OSR_ROOT", hs_text(&p));

    /* The front end resolves the account itself rather than trusting
     * $OSR_HOME -- it is called straight from a picker or a hotkey, where no
     * runner has set anything up. So the sandbox has to supply a passwd file
     * for it to resolve THROUGH, or it would find the real user's home and
     * read the real state file. */
    {
        HStr line;
        hs_init(&line);
        hs_add(&line, "tester:x:1000:1000::");
        hs_add(&line, hs_text(&sb.home));
        hs_add(&line, ":/bin/sh\n");
        osr_sb_write(&sb, "etc/passwd", hs_text(&line), 0644);
        hs_free(&line);
        hs_path(&p, hs_text(&sb.root), "etc/passwd");
        osr_sb_env(&sb, "OSR_PASSWD_FILE", hs_text(&p));
        osr_sb_env(&sb, "USER", "tester");
        osr_sb_env(&sb, "SUDO_USER", "");
    }

    /* The image type probe wants `file`; everything else a setter would need
     * is deliberately absent, so the headless branch is what runs. */
    osr_sb_real(&sb, "file");

    /* ================================================================
     * 1. Reporting
     * ================================================================ */
    on_theme("nord");
    wp(NULL, NULL);
    out_has("01-first.png",
        "show: with nothing chosen, the theme's first image is the answer -- "
        "the numeric prefixes in a wallpapers/ directory are what order it");
    out_lacks("README.txt",
        "show: the placeholder README is not an image and is never offered");

    on_theme("bare");
    wp(NULL, NULL);
    out_has("(none)",
        "show: a theme whose wallpapers/ holds only a placeholder says so "
        "rather than printing an empty line");

    /* ================================================================
     * 2. Listing
     * ================================================================ */
    on_theme("nord");
    wp("--list", NULL);
    out_has("01-first.png", "--list: the theme's images are listed");
    out_has("02-second.jpg", "--list: including the .jpg -- extension is not "
        "how an image is recognised");
    out_lacks("README.txt", "--list: and the placeholder is not");

    /* ================================================================
     * 3. Setting one
     * ================================================================ */
    on_theme("nord");
    wp(in_tree("themes/nord/wallpapers/02-second.jpg"), NULL);
    wp(NULL, NULL);
    out_has("02-second.jpg", "set: the pick is what a later show reports");

    state_has("wallpaper.nord=",
        "set: the choice is keyed BY THEME -- switching away and back returns "
        "this image, not whatever was chosen for another theme");

    osr_assert_tree_is(&sb, "home/Pictures/Wallpapers",
        "home/Pictures/Wallpapers\n"
        "home/Pictures/Wallpapers/02-second.jpg\n",
        "set: the image is copied into the user's own Pictures/Wallpapers");
    state_has("wallpaper=",
        "set: and the INSTALLED copy is recorded separately under `wallpaper` "
        "-- that is the path a setter and a lockscreen config point at, "
        "because the choice above may live inside the os-rice checkout");

    /* Another theme does not inherit the choice. */
    on_theme("bare");
    wp(NULL, NULL);
    out_has("(none)",
        "set: another theme keeps its own answer rather than inheriting one");

    /* An image from outside any theme is a legitimate choice: this is a
     * personal setting, and restricting it to the shipped images would make
     * the feature useless. */
    on_theme("nord");
    hs_path(&p, hs_text(&sb.root), "extra/outside.png");
    wp(hs_text(&p), NULL);
    wp(NULL, NULL);
    out_has("outside.png",
        "set: an image from outside any theme can be chosen -- this is the "
        "user's own setting, not a curated list");

    /* ================================================================
     * 4. Cycling
     * ================================================================ */
    on_theme("nord");
    wp("--next", NULL);
    out_has("second",
        "--next: steps off the theme's first image and prints where it landed");
    {
        /* Two steps must not land on the same image: a cycle that does not
         * move is indistinguishable from a hotkey that is not bound. */
        HStr first;
        hs_init(&first);
        wp(NULL, NULL);
        hs_add(&first, osr_sb_capture(&sb));
        wp("--next", NULL);
        wp(NULL, NULL);
        osr_assert_true(strcmp(hs_text(&first), osr_sb_capture(&sb)) != 0,
            "--next: a second step lands on a different image");
        hs_free(&first);
    }

    /* An image whose name contains a space. Splitting the library on
     * whitespace makes such a file two entries that do not exist, so --next
     * steps over it and the user can never reach it. */
    img("root/themes/nord/wallpapers/03 spaced.png");
    on_theme("nord");
    {
        int i;
        int reached = 0;
        for (i = 0; i < 6 && !reached; i++) {
            wp("--next", NULL);
            wp(NULL, NULL);
            if (strstr(osr_sb_capture(&sb), "03 spaced.png") != NULL) reached = 1;
        }
        osr_assert_true(reached,
            "--next: an image whose name contains a space is reachable -- the "
            "library is walked a line at a time, not split on whitespace");
    }

    /* ================================================================
     * 5. Degradation
     * ================================================================ */
    on_theme("nord");
    img("extra/temporary.png");
    hs_path(&p, hs_text(&sb.root), "extra/temporary.png");
    wp(hs_text(&p), NULL);
    wp(NULL, NULL);
    out_has("temporary.png", "a chosen image is reported while it is there");

    /* Now delete what the user picked. A wallpaper chosen from a USB stick, a
     * downloads directory or a checkout that has since been moved is the
     * ordinary case, not an exotic one. */
    osr_sb_rm(&sb, "extra/temporary.png");
    wp(NULL, NULL);
    out_lacks("temporary.png",
        "a choice whose source has been deleted since is not reported");
    out_has("01-first.png",
        "...it falls back to the theme's default rather than naming a path "
        "that is not there -- a setter handed a missing file leaves the "
        "desktop silently unchanged");

    /* ================================================================
     * 6. The error paths
     *
     * All four are user input, so all four have to say what was wrong.
     * ================================================================ */
    on_theme("nord");
    osr_assert_true(wp("--nope", NULL) != 0, "an unknown option fails");
    out_has("--nope", "and names the option it did not understand");

    osr_assert_true(wp("-x", NULL) != 0, "an unknown short option fails too");

    hs_path(&p, hs_text(&sb.root), "extra/nothing-here.png");
    osr_assert_true(wp(hs_text(&p), NULL) != 0, "a file that is not there fails");

    osr_assert_true(wp(in_tree("themes/nord/wallpapers/README.txt"), NULL) != 0,
        "a file that is not an image is refused -- setting a text file as a "
        "wallpaper leaves a desktop that silently did not change");

    /* An option that takes an operand, given none. */
    osr_assert_rc(wp("--user", NULL), 1,
        "an option missing its operand exits 1");
    out_has("user needs a name",
        "...and says which option was missing what");

    /* A recorded theme that has been deleted since. The library it would
     * offer would be another theme's, so it stops rather than guessing. */
    on_theme("nosuchtheme");
    osr_assert_true(wp(NULL, NULL) != 0,
        "a recorded theme that no longer exists is an error, not a guess -- "
        "the images it would otherwise offer belong to a different theme");

    hs_free(&p);
    osr_sb_free(&sb);
    return osr_finish();
}
