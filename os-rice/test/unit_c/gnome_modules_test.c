/* test/unit_c/gnome_modules_test.c -- the modules whose behaviour CHANGES on a
 * GNOME session: wofi, cliphist and gnome-overview.
 *
 * lib/gnome.c's verbs are asserted in gnome_test.c. What is asserted here is
 * the thing those verbs are wired into: which module registers which chord,
 * which built-in it has to take that chord away from first, and -- the half
 * that matters most -- that NONE of it happens off GNOME.
 *
 * That last rule is why these three share a file. A gsettings write on a box
 * running i3 does not fail; it succeeds, into a database nothing on that box
 * reads, and the shortcut the user was promised silently does not exist. The
 * only way to catch it is to assert that the writes did not happen, which is
 * an assertion about absence and therefore easy to forget. Here it is first
 * for every module.
 *
 * Hermetic: $PATH is a directory of stubs. gsettings keeps its settings in a
 * FILE this test reads back, so a scenario can say what the database now holds
 * rather than only what commands ran -- which is what makes the "a rerun adds
 * nothing" assertions meaningful.
 *
 * Replaces test/unit/wofi_module.sh, cliphist_module.sh and
 * gnome_overview_module.sh. See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

#define MK "org.gnome.settings-daemon.plugins.media-keys"
#define MK_PATH "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/"
#define MK_CHILD MK ".custom-keybinding:"

/* gs -- the settings database as it stands, one `schema key value` per line. */
static const char *gs(void) {
    static HStr held;
    static int ready = 0;
    HStr path;
    char *raw;
    if (!ready) { hs_init(&held); ready = 1; }
    hs_init(&path);
    hs_path(&path, hs_text(&sb.root), "gsettings.db");
    raw = h_slurp(hs_text(&path));
    hs_reset(&held);
    hs_add(&held, osr_sb_scrub(&sb, raw));
    free(raw);
    hs_free(&path);
    return hs_text(&held);
}

static void db_holds(const char *needle, const char *label) {
    osr_assert_true(strstr(gs(), needle) != NULL, label);
}
static void db_empty(const char *label) {
    osr_assert_true(gs()[0] == '\0', label);
}

/* db_lines -- how many settings the database holds. The idempotency assertion:
 * a rerun that registers the same shortcut twice shows up as a line count. */
static int db_lines(void) {
    const char *p = gs();
    int n = 0;
    while (*p != '\0') {
        if (*p == '\n') n++;
        p++;
    }
    return n;
}

/* fresh -- an empty home and an empty settings database. */
static void fresh(void) {
    osr_sb_rm(&sb, "home");
    osr_sb_mkdir(&sb, "home");
    osr_sb_write(&sb, "gsettings.db", "", 0644);
    osr_sb_reset(&sb);
}

/* on_desktop -- which session the next run believes it is in. */
static void on_desktop(const char *current, const char *session) {
    osr_sb_env(&sb, "XDG_CURRENT_DESKTOP", current);
    osr_sb_env(&sb, "XDG_SESSION_DESKTOP", session);
}

/* preset -- a setting already in the database before the run. */
static void preset(const char *line) {
    HStr all;
    HStr path;
    char *raw;
    hs_init(&path);
    hs_path(&path, hs_text(&sb.root), "gsettings.db");
    raw = h_slurp(hs_text(&path));
    hs_init(&all);
    hs_add(&all, raw);
    hs_add(&all, line);
    hs_addc(&all, '\n');
    osr_sb_write(&sb, "gsettings.db", hs_text(&all), 0644);
    free(raw);
    hs_free(&all);
    hs_free(&path);
}

static int run_module(const char *name) {
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "module", "run", name, (const char *)NULL);
}

int main(void) {
    HStr p;

    osr_sb_init(&sb);
    hs_init(&p);

    osr_sb_env(&sb, "OSR_PKG", "apt");
    osr_sb_env(&sb, "OSR_INIT", "systemd");
    hs_path(&p, hs_text(&sb.osr_root), "..");
    osr_sb_env(&sb, "OSR_DOTFILES", hs_text(&p));
    hs_path(&p, hs_text(&sb.root), "gsettings.db");
    osr_sb_env(&sb, "GSDB", hs_text(&p));

    /* gsettings against a FILE rather than dconf. `set` replaces the line for
     * that schema+key, so the database behaves like a real one: writing the
     * same key twice leaves one value, and the line count is a real answer to
     * "did the rerun add anything". */
    osr_sb_stub_body(&sb, "gsettings",
        "case \"$1\" in\n"
        "  get) sed -n \"s|^$2 $3 ||p\" \"$GSDB\" ;;\n"
        "  set) grep -v \"^$2 $3 \" \"$GSDB\" >\"$GSDB.tmp\" 2>/dev/null || true\n"
        "       mv \"$GSDB.tmp\" \"$GSDB\"\n"
        "       printf '%s %s %s\\n' \"$2\" \"$3\" \"$4\" >>\"$GSDB\" ;;\n"
        "  list-recursively) grep \"^$2 \" \"$GSDB\" 2>/dev/null || true ;;\n"
        "  *) exit 1 ;;\n"
        "esac\n"
        "exit 0\n");
    /* Package tooling: apt-get logs, dpkg says nothing is installed so the
     * install path is exercised rather than skipped. */
    osr_sb_stub_body(&sb, "apt-get",
        "printf 'apt-get %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    osr_sb_stub_body(&sb, "dpkg", "exit 1\n");
    osr_sb_stub_body(&sb, "apt-mark", "exit 0\n");
    osr_sb_stub_body(&sb, "dnf", "printf 'dnf %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    /* go is present, so the cliphist module neither installs a toolchain nor
     * takes its self-heal path: what is under test here is the GNOME wiring,
     * not the Go build. There is deliberately no curl or wget in the sandbox
     * PATH either, so the /usr/local/bin shim fetch degrades to a warning
     * rather than reaching the network. */
    osr_sb_stub_body(&sb, "go", "printf 'go %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");

    /* ================================================================
     * 1. gnome-overview -- the Super key itself
     *
     * GNOME watches a bare Super TAP and opens the Activities overview. Every
     * Super chord a rice binds therefore fights the overview for the key, and
     * the fix is to stop the overview watching for it at all.
     * ================================================================ */
    fresh();
    on_desktop("i3", "i3");
    run_module("gnome-overview");
    db_empty("gnome-overview: nothing is written on a session that is not GNOME");

    fresh();
    on_desktop("ubuntu:GNOME", "gnome");
    preset("org.gnome.mutter overlay-key Super_L");   /* the stock value */
    run_module("gnome-overview");
    db_holds("org.gnome.mutter overlay-key \n",
        "gnome-overview: overlay-key is set to the EMPTY string, so a bare "
        "Super tap watches nothing and the chords below are reachable");

    {
        int before = db_lines();
        run_module("gnome-overview");
        osr_assert_true(db_lines() == before,
            "gnome-overview: a rerun writes no second value (SS2)");
    }

    /* ================================================================
     * 2. wofi -- Super+R, the launcher
     * ================================================================ */
    fresh();
    on_desktop("i3", "i3");
    osr_sb_env(&sb, "OSR_THEME", "");
    osr_sb_env(&sb, "OSR_THEME_DIR", "");
    run_module("wofi");
    osr_assert_log(&sb, "apt-get install",
        "wofi: the package is installed regardless of session -- it is a "
        "launcher, not a GNOME extension");
    db_empty("wofi: no shortcut is registered off GNOME");
    osr_assert_absent(&sb, "home/.config/wofi/style.css",
        "wofi: no theme layer is applied when no theme is resolved");

    fresh();
    on_desktop("ubuntu:GNOME", "gnome");
    osr_sb_env(&sb, "OSR_THEME", "xin");
    hs_path(&p, hs_text(&sb.osr_root), "themes/xin");
    osr_sb_env(&sb, "OSR_THEME_DIR", hs_text(&p));
    run_module("wofi");
    db_holds(MK " custom-keybindings ['" MK_PATH "wofi/']",
        "wofi: the shortcut path is registered in the shared list");
    db_holds(MK_CHILD MK_PATH "wofi/ name Application Launcher",
        "wofi: the shortcut is named, so it is identifiable in GNOME Settings");
    db_holds(MK_CHILD MK_PATH "wofi/ binding <Super>r",
        "wofi: the chord is Super+R -- the Windows Run key");
    db_holds(MK_CHILD MK_PATH "wofi/ command",
        "wofi: a command is registered for it");
    osr_assert_true(strstr(gs(), "home/.config/wofi/style.css") == NULL,
        "wofi: the stylesheet is a file, not a setting");
    {
        char *css;
        HStr cssp;
        hs_init(&cssp);
        hs_path(&cssp, hs_text(&sb.root), "home/.config/wofi/style.css");
        css = h_slurp(hs_text(&cssp));
        osr_assert_true(css[0] != '\0',
            "wofi: the theme-owned stylesheet is applied when a theme resolves");
        free(css);
        hs_free(&cssp);
    }

    {
        int before = db_lines();
        run_module("wofi");
        osr_assert_true(db_lines() == before,
            "wofi: a rerun registers nothing a second time (SS2)");
    }

    /* Somebody else's shortcut is already in the list. This is the assertion
     * that says the list is shared state (SS5): appending must keep what is
     * there, and the other shortcut's own keys must be untouched. */
    fresh();
    on_desktop("ubuntu:GNOME", "gnome");
    preset(MK " custom-keybindings ['" MK_PATH "cliphist/']");
    preset(MK_CHILD MK_PATH "cliphist/ binding <Super>v");
    run_module("wofi");
    db_holds(MK " custom-keybindings ['" MK_PATH "cliphist/', '" MK_PATH "wofi/']",
        "wofi: its path is APPENDED and the cliphist entry survives (SS5)");
    db_holds(MK_CHILD MK_PATH "cliphist/ binding <Super>v",
        "wofi: the other shortcut's own binding is not disturbed");

    /* The collision. GNOME Shell binds Super+R to the screen recorder, and a
     * custom shortcut on a taken chord simply never fires -- so the built-in
     * is unbound first. The near-miss chord in the same run is what says the
     * unbind is exact rather than eager. */
    fresh();
    on_desktop("ubuntu:GNOME", "gnome");
    preset("org.gnome.shell.keybindings show-screen-recording-ui ['<Super>r']");
    preset("org.gnome.desktop.wm.keybindings begin-resize ['<Shift><Super>r']");
    run_module("wofi");
    db_holds("org.gnome.shell.keybindings show-screen-recording-ui []",
        "wofi: Super+R is taken away from the Shell built-in that held it");
    db_holds("org.gnome.desktop.wm.keybindings begin-resize ['<Shift><Super>r']",
        "wofi: Shift+Super+R is a different chord and stays bound");
    db_holds(MK_CHILD MK_PATH "wofi/ binding <Super>r",
        "wofi: and the shortcut is registered after the unbind, not before");

    /* ================================================================
     * 3. cliphist -- Super+V, the clipboard history
     * ================================================================ */
    osr_sb_env(&sb, "OSR_PKG", "dnf");
    osr_sb_env(&sb, "OSR_DISTRO", "fedora");
    osr_sb_env(&sb, "OSR_ID_LIKE", "");
    osr_sb_stub_body(&sb, "rpm",
        "printf 'rpm %s\n' \"$*\" >>\"$LOG\"\nexit 1\n");
    fresh();
    on_desktop("i3", "i3");
    run_module("cliphist");
    osr_assert_log(&sb, "dnf install",
        "cliphist: the packages are installed regardless of session");
    db_empty("cliphist: no shortcut is registered off GNOME");
    osr_assert_true(
        strstr(osr_sb_capture_both(&sb), "GNOME autostart") == NULL,
        "cliphist: the GNOME autostart step is not even announced off GNOME");

    fresh();
    on_desktop("ubuntu:GNOME", "gnome");
    run_module("cliphist");
    {
        char *desk;
        HStr dp;
        hs_init(&dp);
        hs_path(&dp, hs_text(&sb.root),
                "home/.config/autostart/cliphist-store.desktop");
        desk = h_slurp(hs_text(&dp));
        /* GNOME has no `exec-once`, so the watcher that feeds the history has
         * to be an autostart entry -- without it every shortcut below opens an
         * empty list. */
        osr_assert_true(strstr(desk, "Cliphist Store") != NULL,
            "cliphist: a GNOME autostart entry is written for the store watcher");
        osr_assert_true(strstr(desk, "cliphist store") != NULL,
            "cliphist: and it runs the watcher that actually fills the history");
        osr_assert_true(strstr(desk, "NoDisplay=true") != NULL,
            "cliphist: the entry is hidden -- it is a daemon, not something the "
            "user should find in their applications list");
        free(desk);
        hs_free(&dp);
    }
    db_holds(MK_PATH "cliphist/",
        "cliphist: the shortcut path is registered");
    db_holds(MK_CHILD MK_PATH "cliphist/ name Clipboard History",
        "cliphist: the shortcut is named");
    db_holds(MK_CHILD MK_PATH "cliphist/ binding <Super>v",
        "cliphist: the chord is Super+V -- the Windows clipboard-history key");

    {
        int before = db_lines();
        run_module("cliphist");
        osr_assert_true(db_lines() == before,
            "cliphist: a rerun registers nothing a second time (SS2)");
    }

    /* Super+V is GNOME's message tray by default. */
    fresh();
    on_desktop("ubuntu:GNOME", "gnome");
    preset("org.gnome.shell.keybindings toggle-message-tray ['<Super>v']");
    run_module("cliphist");
    db_holds("org.gnome.shell.keybindings toggle-message-tray []",
        "cliphist: Super+V is taken away from the message tray first");
    db_holds(MK_CHILD MK_PATH "cliphist/ binding <Super>v",
        "cliphist: and the clipboard shortcut takes the freed chord");

    fresh();
    on_desktop("ubuntu:GNOME", "gnome");
    preset(MK " custom-keybindings ['" MK_PATH "custom0/']");
    run_module("cliphist");
    db_holds(MK " custom-keybindings ['" MK_PATH "custom0/', '" MK_PATH "cliphist/']",
        "cliphist: a shortcut somebody else registered is preserved (SS5)");

    hs_free(&p);
    osr_sb_free(&sb);
    return osr_finish();
}
