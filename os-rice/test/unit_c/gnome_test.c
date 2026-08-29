/* test/unit_c/gnome_test.c -- what lib/gnome.c must do to a GNOME session.
 *
 * Three verbs, and each exists because GNOME does not let a rice simply write
 * a config file:
 *
 *   is-session   -- am I on GNOME at all? Everything else is gated on it,
 *                   because poking gsettings on a box running Hyprland writes
 *                   settings nothing will ever read.
 *   free-binding -- take a chord away from whatever GNOME Shell has bound it
 *                   to. A custom shortcut that collides with a built-in one
 *                   simply never fires, with nothing anywhere saying why.
 *   keybind      -- register a custom shortcut, which in GNOME means writing
 *                   three keys under a synthesised path AND appending that
 *                   path to a list every other application also appends to.
 *
 * That last list is the dangerous part: it is read-modify-write against
 * shared state, so a careless implementation drops somebody else's shortcuts.
 * Several scenarios below exist only to hold that line.
 *
 * Hermetic: $PATH is a directory of stubs, so gsettings is recorded rather
 * than run and every answer comes from a file the scenario wrote. Nothing here
 * reads or writes the dconf of whoever runs the suite.
 *
 * Replaces test/unit/gnome_c_parity.sh. See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

#define MK_PATH "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/"
#define MK_SCHEMA "org.gnome.settings-daemon.plugins.media-keys.custom-keybinding:"

/* fresh -- no schema holds anything and no custom shortcuts are registered. */
static void fresh(void) {
    osr_sb_rm(&sb, "state");
    osr_sb_mkdir(&sb, "state");
    osr_sb_reset(&sb);
}

/* schema_holds -- what `gsettings list-recursively <schema>` prints. */
static void schema_holds(const char *schema, const char *listing) {
    HStr rel;
    hs_init(&rel);
    hs_add(&rel, "state/");
    hs_add(&rel, schema);
    hs_add(&rel, ".list");
    osr_sb_write(&sb, hs_text(&rel), listing, 0644);
    hs_free(&rel);
}

/* registered -- what `gsettings get ... custom-keybindings` prints. */
static void registered(const char *value) {
    osr_sb_write(&sb, "state/custom", value, 0644);
}

/* desktop -- the two variables a session is identified by. */
static void desktop(const char *current, const char *session) {
    osr_sb_env(&sb, "XDG_CURRENT_DESKTOP", current);
    osr_sb_env(&sb, "XDG_SESSION_DESKTOP", session);
}

/* is_session -- `osr gnome is-session`, as a boolean. */
static void on_gnome(int expected, const char *label) {
    int rc;
    osr_sb_reset(&sb);
    rc = osr_sb_run_core(&sb, "gnome", "is-session", (const char *)NULL);
    osr_assert_true((rc == 0) == (expected != 0), label);
}

/* free_binding -- `osr gnome free-binding <chord>`. */
static void free_binding(const char *chord) {
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "gnome", "free-binding", chord, (const char *)NULL);
}

/* keybind -- `osr gnome keybind <id> <name> <chord> <command>`. */
static int keybind(const char *id, const char *name, const char *chord,
                   const char *cmd) {
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "gnome", "keybind", id, name, chord, cmd,
                           (const char *)NULL);
}

int main(void) {
    osr_sb_init(&sb);

    /* gsettings logs every call and answers reads from the scenario's files:
     *   state/<schema>.list  what list-recursively prints for that schema
     *   state/custom         what `get ... custom-keybindings` prints */
    osr_sb_stub_body(&sb, "gsettings",
        "printf 'gsettings %s\\n' \"$*\" >>\"$LOG\"\n"
        "case \"$1\" in\n"
        "  list-recursively) [ -f \"$STATE/$2.list\" ] && cat \"$STATE/$2.list\" ;;\n"
        "  get) if [ \"$3\" = custom-keybindings ] && [ -f \"$STATE/custom\" ]; then\n"
        "         cat \"$STATE/custom\"\n"
        "       fi ;;\n"
        "esac\n"
        "exit 0\n");
    {
        HStr p;
        hs_init(&p);
        hs_path(&p, hs_text(&sb.root), "state");
        osr_sb_env(&sb, "STATE", hs_text(&p));
        hs_free(&p);
    }

    /* ================================================================
     * 1. Session detection
     *
     * Two variables, because neither is reliable alone: Ubuntu prefixes
     * XDG_CURRENT_DESKTOP with its own name, GNOME Classic suffixes it, and
     * some display managers set only XDG_SESSION_DESKTOP.
     * ================================================================ */
    fresh();
    desktop("GNOME", "");
    on_gnome(1, "session: plain GNOME");
    desktop("ubuntu:GNOME", "");
    on_gnome(1, "session: Ubuntu's colon-prefixed spelling still counts");
    desktop("GNOME-Classic", "");
    on_gnome(1, "session: GNOME Classic is GNOME");
    desktop("", "gnome");
    on_gnome(1, "session: XDG_SESSION_DESKTOP alone is enough, lowercase");
    desktop("", "GNOME");
    on_gnome(1, "session: and in upper case");
    desktop("Hyprland", "");
    on_gnome(0, "session: Hyprland is not GNOME");
    desktop("KDE", "sway");
    on_gnome(0, "session: neither variable naming GNOME means no");
    desktop("", "");
    on_gnome(0, "session: an empty environment is not GNOME");
    desktop("GNOME", "");

    /* ================================================================
     * 2. free-binding
     *
     * GNOME Shell binds a lot of Super chords out of the box. A custom
     * shortcut that collides simply never fires -- the built-in wins and
     * nothing reports a conflict -- so the chord is taken away first.
     * ================================================================ */
    fresh();
    free_binding("<Super>r");
    osr_assert_log_is(&sb,
        "sudo -u tester gsettings list-recursively org.gnome.shell.keybindings\n"
        "gsettings list-recursively org.gnome.shell.keybindings\n"
        "sudo -u tester gsettings list-recursively org.gnome.desktop.wm.keybindings\n"
        "gsettings list-recursively org.gnome.desktop.wm.keybindings\n"
        "sudo -u tester gsettings list-recursively org.gnome.mutter.keybindings\n"
        "gsettings list-recursively org.gnome.mutter.keybindings\n"
        "sudo -u tester gsettings list-recursively org.gnome.mutter.wayland.keybindings\n"
        "gsettings list-recursively org.gnome.mutter.wayland.keybindings\n",
        "free-binding: with nothing holding the chord, all four schemas are "
        "read and nothing is written");

    fresh();
    schema_holds("org.gnome.shell.keybindings",
        "org.gnome.shell.keybindings show-screen-recording-ui "
        "['<Super><Ctrl><Shift>r', '<Super>r']\n");
    free_binding("<Super>r");
    osr_assert_log(&sb,
        "gsettings set org.gnome.shell.keybindings show-screen-recording-ui []",
        "free-binding: the key holding the chord is emptied");

    /* A near miss in the same listing. <Shift><Super>r is a DIFFERENT chord,
     * and a substring match on 'Super>r' would unbind it -- taking a working
     * shortcut away from the user to make room for ours. */
    fresh();
    schema_holds("org.gnome.shell.keybindings",
        "org.gnome.shell.keybindings toggle-overview ['<Shift><Super>r']\n");
    free_binding("<Super>r");
    osr_refute_log(&sb, "gsettings set",
        "free-binding: a longer chord ending in the same key is left bound -- "
        "the quotes around the value are what make the match exact");

    /* Every schema is searched, not just the first that matches: one chord can
     * be bound in Shell and in mutter at once, and freeing only one leaves the
     * collision in place. */
    fresh();
    schema_holds("org.gnome.shell.keybindings",
        "org.gnome.shell.keybindings open-application-menu ['<Super>r']\n"
        "org.gnome.shell.keybindings focus-active-notification ['<Super>n']\n");
    schema_holds("org.gnome.desktop.wm.keybindings",
        "org.gnome.desktop.wm.keybindings begin-resize ['<Super>r']\n");
    schema_holds("org.gnome.mutter.keybindings",
        "org.gnome.mutter.keybindings toggle-tiled-right ['<Super>r']\n");
    free_binding("<Super>r");
    osr_assert_log_is(&sb,
        "sudo -u tester gsettings list-recursively org.gnome.shell.keybindings\n"
        "gsettings list-recursively org.gnome.shell.keybindings\n"
        "sudo -u tester gsettings set org.gnome.shell.keybindings open-application-menu []\n"
        "gsettings set org.gnome.shell.keybindings open-application-menu []\n"
        "sudo -u tester gsettings list-recursively org.gnome.desktop.wm.keybindings\n"
        "gsettings list-recursively org.gnome.desktop.wm.keybindings\n"
        "sudo -u tester gsettings set org.gnome.desktop.wm.keybindings begin-resize []\n"
        "gsettings set org.gnome.desktop.wm.keybindings begin-resize []\n"
        "sudo -u tester gsettings list-recursively org.gnome.mutter.keybindings\n"
        "gsettings list-recursively org.gnome.mutter.keybindings\n"
        "sudo -u tester gsettings set org.gnome.mutter.keybindings toggle-tiled-right []\n"
        "gsettings set org.gnome.mutter.keybindings toggle-tiled-right []\n"
        "sudo -u tester gsettings list-recursively org.gnome.mutter.wayland.keybindings\n"
        "gsettings list-recursively org.gnome.mutter.wayland.keybindings\n",
        "free-binding: every schema holding the chord is freed, and the key "
        "bound to a DIFFERENT chord in the same schema is not touched");

    /* The case of the chord as gsettings reports it is not ours to choose. */
    fresh();
    schema_holds("org.gnome.mutter.wayland.keybindings",
        "org.gnome.mutter.wayland.keybindings restore-shortcuts ['<super>V']\n");
    free_binding("<Super>v");
    osr_assert_log(&sb,
        "gsettings set org.gnome.mutter.wayland.keybindings restore-shortcuts []",
        "free-binding: the match is case-insensitive, because how gsettings "
        "spells a chord is not ours to decide");

    /* gsettings wraps long listings, and a continuation line carries a value
     * with no key name in front of it. Setting something there would name the
     * VALUE as the key. */
    fresh();
    schema_holds("org.gnome.shell.keybindings",
        "org.gnome.shell.keybindings\n['<Super>r']\n");
    free_binding("<Super>r");
    osr_refute_log(&sb, "gsettings set",
        "free-binding: a wrapped line with no key name sets nothing -- it "
        "carries a value, and naming that value as a key would issue a set "
        "against a key that does not exist");

    /* ================================================================
     * 3. keybind
     *
     * Registering a custom shortcut in GNOME is three writes under a
     * synthesised path plus an append to a shared list. The list is what
     * every other application also appends to, so it is read-modify-write
     * against state os-rice does not own.
     * ================================================================ */
    fresh();
    registered("@as []\n");
    keybind("wofi", "Application Launcher", "<Super>r", "wofi --show drun");
    osr_assert_log_is(&sb,
        "sudo -u tester gsettings get org.gnome.settings-daemon.plugins.media-keys "
        "custom-keybindings\n"
        "gsettings get org.gnome.settings-daemon.plugins.media-keys "
        "custom-keybindings\n"
        "sudo -u tester gsettings set " MK_SCHEMA MK_PATH "wofi/ name Application Launcher\n"
        "gsettings set " MK_SCHEMA MK_PATH "wofi/ name Application Launcher\n"
        "sudo -u tester gsettings set " MK_SCHEMA MK_PATH "wofi/ binding <Super>r\n"
        "gsettings set " MK_SCHEMA MK_PATH "wofi/ binding <Super>r\n"
        "sudo -u tester gsettings set " MK_SCHEMA MK_PATH "wofi/ command wofi --show drun\n"
        "gsettings set " MK_SCHEMA MK_PATH "wofi/ command wofi --show drun\n"
        "sudo -u tester gsettings set org.gnome.settings-daemon.plugins.media-keys "
        "custom-keybindings ['" MK_PATH "wofi/']\n"
        "gsettings set org.gnome.settings-daemon.plugins.media-keys "
        "custom-keybindings ['" MK_PATH "wofi/']\n",
        "keybind: the shortcut is DEFINED first -- name, binding, command -- "
        "and only then is its path advertised in the shared list, so the list "
        "never names a shortcut that does not exist yet");

    /* GNOME spells 'no custom shortcuts' three different ways depending on
     * version and whether the key was ever touched. All three mean empty, and
     * treating any of them as a value produces a list with a garbage entry
     * that GNOME then ignores -- taking our real shortcut with it. */
    fresh();
    registered("[]\n");
    keybind("wofi", "Application Launcher", "<Super>r", "wofi --show drun");
    osr_assert_log(&sb,
        "custom-keybindings ['" MK_PATH "wofi/']",
        "keybind: '[]' is an empty list");

    fresh();
    registered("''\n");
    keybind("wofi", "Application Launcher", "<Super>r", "wofi --show drun");
    osr_assert_log(&sb,
        "custom-keybindings ['" MK_PATH "wofi/']",
        "keybind: \"''\" is an empty list too");

    fresh();
    registered("");
    keybind("wofi", "Application Launcher", "<Super>r", "wofi --show drun");
    osr_assert_log(&sb,
        "custom-keybindings ['" MK_PATH "wofi/']",
        "keybind: gsettings answering nothing at all is an empty list");

    /* A second shortcut is APPENDED. The command here carries a nested quote,
     * which is the shape every real clipboard binding has. */
    fresh();
    registered("['" MK_PATH "wofi/']\n");
    keybind("cliphist", "Clipboard History", "<Super>v",
            "sh -c 'cliphist list | wofi -S dmenu'");
    osr_assert_log(&sb,
        "custom-keybindings ['" MK_PATH "wofi/', '" MK_PATH "cliphist/']",
        "keybind: a second shortcut is appended, and the first survives");
    osr_assert_log(&sb,
        "command sh -c 'cliphist list | wofi -S dmenu'",
        "keybind: a command with quotes and a pipe is passed through intact");

    /* SS2: re-registering the same id must not append a duplicate. GNOME does
     * not deduplicate, and a list with the same path twice is how a shortcut
     * ends up bound and then immediately shadowed by itself. */
    fresh();
    registered("['" MK_PATH "wofi/']\n");
    keybind("wofi", "Application Launcher", "<Super>r", "wofi --show drun");
    osr_assert_log_is(&sb,
        "sudo -u tester gsettings get org.gnome.settings-daemon.plugins.media-keys "
        "custom-keybindings\n"
        "gsettings get org.gnome.settings-daemon.plugins.media-keys "
        "custom-keybindings\n",
        "keybind: an id already in the list is a complete skip -- one read and "
        "nothing else (SS2)");
    osr_assert_out(&sb, "wofi <Super>r shortcut already registered",
        "keybind: and the skip says which shortcut it recognised");
    /* Note what this means: a shortcut the USER later rebound in Settings is
     * not put back. That is deliberate and is the same rule as everywhere
     * else -- os-rice does not override a decision the user made (G2). */

    /* Somebody else's shortcuts are not ours to drop. This is the one
     * assertion that says os-rice does not own this list (SS5). */
    fresh();
    registered("['" MK_PATH "custom0/', '" MK_PATH "custom1/']\n");
    keybind("wofi", "Application Launcher", "<Super>r", "wofi --show drun");
    osr_assert_log(&sb,
        "custom-keybindings ['" MK_PATH "custom0/', '" MK_PATH "custom1/', '"
        MK_PATH "wofi/']",
        "keybind: shortcuts somebody else registered are preserved, in order "
        "-- this list is shared state, not ours (SS5)");

    osr_sb_free(&sb);
    return osr_finish();
}
