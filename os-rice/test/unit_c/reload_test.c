/* test/unit_c/reload_test.c -- what lib/reload.c must do once the new config
 * layers are on disk: tell the running programs to re-read them.
 *
 * This is the last step of `osr apply theme` (SS6a), and its blast radius is
 * the worst in the tree. Everything here runs inside the session it is
 * changing, so a wrong signal does not produce a wrong colour -- it produces a
 * logged-out user with unsaved work gone. The rules that follow from that are
 * asserted here, by name:
 *
 *   PROBE FIRST, ACT SECOND. A program that is installed but not running is
 *   never signalled, because `pkill -x waybar` on a box where waybar was never
 *   started is at best noise and at worst hits something else.
 *
 *   NEVER RESTART WHAT WOULD LOSE STATE. i3 is restarted because `i3-msg
 *   restart` preserves the layout by design; a compositor is reloaded, never
 *   restarted, because restarting it takes every client down with it.
 *
 *   NEVER FATAL. A reloader that fails leaves a program running with the old
 *   config, which is cosmetic. A reloader that aborts the apply leaves the
 *   config half-written, which is not.
 *
 * Hermetic: $PATH is a directory of stubs, so every reloader is recorded
 * rather than run -- nothing here may signal a real process or poke the dconf
 * of whoever runs the suite. `pgrep` answers from marker files, so "what is
 * running" is a property of the scenario.
 *
 * Replaces test/unit/reload_c_parity.sh and reload.sh. See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

/* fresh -- nothing running, no Xresources, no GTK theme reported. */
static void fresh(void) {
    osr_sb_rm(&sb, "state");
    osr_sb_rm(&sb, "home");
    osr_sb_mkdir(&sb, "state");
    osr_sb_mkdir(&sb, "home");
    osr_sb_reset(&sb);
}

/* alive -- a process by that name is running for this user. */
static void alive(const char *name) {
    HStr rel;
    hs_init(&rel);
    hs_add(&rel, "state/proc.");
    hs_add(&rel, name);
    osr_sb_write(&sb, hs_text(&rel), "", 0644);
    hs_free(&rel);
}

/* fails -- that tool exits non-zero when the run reaches it. */
static void fails(const char *tool) {
    HStr rel;
    hs_init(&rel);
    hs_add(&rel, "state/fail.");
    hs_add(&rel, tool);
    osr_sb_write(&sb, hs_text(&rel), "", 0644);
    hs_free(&rel);
}

/* session -- which display servers the scenario claims. An empty string is
 * how a headless box (a container, a TTY install) spells it. */
static void session(const char *x11, const char *wayland) {
    osr_sb_env(&sb, "DISPLAY", x11);
    osr_sb_env(&sb, "WAYLAND_DISPLAY", wayland);
}

/* reload -- `osr reload <what>`. */
static int reload(const char *what) {
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "reload", what, (const char *)NULL);
}

/* before -- does `first` appear earlier in the log than `second`? Order is
 * the assertion for the pairs where one reloader feeds another. */
static int before(const char *first, const char *second) {
    const char *log = osr_sb_log(&sb);
    const char *a = strstr(log, first);
    const char *b = strstr(log, second);
    return a != NULL && b != NULL && a < b;
}

/* strip_comments -- `src` with every C comment replaced by a space.
 *
 * For the source-inspection guard at the end. lib/reload.c DOCUMENTS the verbs
 * it deliberately does not use, so searching the raw file for "killall" would
 * match the comment saying it never calls killall -- a check that fails on
 * prose and passes on code is worse than no check. */
static void strip_comments(HStr *out, const char *src) {
    int in_block = 0, in_line = 0, in_str = 0;
    const char *p;

    for (p = src; *p != '\0'; p++) {
        if (in_block) {
            if (p[0] == '*' && p[1] == '/') { in_block = 0; p++; hs_addc(out, ' '); }
            continue;
        }
        if (in_line) {
            if (*p == '\n') { in_line = 0; hs_addc(out, '\n'); }
            continue;
        }
        if (!in_str && p[0] == '/' && p[1] == '*') { in_block = 1; p++; continue; }
        if (!in_str && p[0] == '/' && p[1] == '/') { in_line = 1; p++; continue; }
        if (*p == '"' && (p == src || p[-1] != '\\')) in_str = !in_str;
        hs_addc(out, *p);
    }
}

int main(void) {
    osr_sb_init(&sb);

    /* pgrep answers from marker files, so "is waybar running" is the
     * scenario's to state. -x <name> is always the last argument. */
    osr_sb_stub_body(&sb, "pgrep",
        "printf 'pgrep %s\\n' \"$*\" >>\"$LOG\"\n"
        "eval _name=\\${$#}\n"
        "[ -f \"$STATE/proc.$_name\" ]\n");
    {
        static const char *tools[] = {
            "pkill", "xrdb", "i3-msg", "hyprctl", "dunstctl", "makoctl", NULL
        };
        int i;
        for (i = 0; tools[i] != NULL; i++) {
            HStr body;
            hs_init(&body);
            hs_add(&body, "printf '");
            hs_add(&body, tools[i]);
            hs_add(&body, " %s\\n' \"$*\" >>\"$LOG\"\n");
            hs_add(&body, "[ -f \"$STATE/fail.");
            hs_add(&body, tools[i]);
            hs_add(&body, "\" ] && exit 1\nexit 0\n");
            osr_sb_stub_body(&sb, tools[i], hs_text(&body));
            hs_free(&body);
        }
    }
    /* gsettings reports the current GTK theme from a file, so this test never
     * reads or writes the dconf of the account running it. */
    osr_sb_stub_body(&sb, "gsettings",
        "printf 'gsettings %s\\n' \"$*\" >>\"$LOG\"\n"
        "case \"$1\" in\n"
        "  get) [ -f \"$STATE/gtk-theme\" ] && cat \"$STATE/gtk-theme\"\n"
        "       [ -f \"$STATE/fail.gsettings\" ] && exit 1 ;;\n"
        "  set) [ -f \"$STATE/fail.gsettings\" ] && exit 1 ;;\n"
        "esac\n"
        "exit 0\n");
    {
        HStr p;
        hs_init(&p);
        hs_path(&p, hs_text(&sb.root), "state");
        osr_sb_env(&sb, "STATE", hs_text(&p));
        hs_free(&p);
    }

    /* The probe asks pgrep about a UID, resolved from $OSR_USER -- and in the
     * sandbox that account does not exist on this machine, so it falls back to
     * the uid running the suite. Which number that is says nothing about what
     * the reload decided, so it is masked. That the probe is SCOPED to a user
     * at all is the behaviour, and it survives the mask: without -u, a pgrep on
     * a multi-seat box finds another user's waybar and signals it. */
    osr_sb_mask(&sb, "pgrep -u ");

    /* ================================================================
     * 1. No session at all
     *
     * A container, a TTY install, an `osr install` over ssh. Nothing to tell,
     * so nothing is told -- and it is not an error.
     * ================================================================ */
    fresh();
    session("", "");
    alive("i3");
    alive("Hyprland");
    alive("dunst");
    osr_assert_rc(reload("all"), 0, "a headless box is not an error");
    osr_assert_log_empty(&sb,
        "with neither DISPLAY nor WAYLAND_DISPLAY, nothing is signalled at all "
        "-- not even the processes that are running");

    /* ================================================================
     * 2. X11
     * ================================================================ */
    fresh();
    session(":0", "");
    osr_sb_write(&sb, "home/.Xresources", "! xresources\n", 0644);

    /* xrdb is the one reloader that fires with nothing running: it merges into
     * the X SERVER's resource database, not into a client, so it is what every
     * program started AFTER the theme switch will read. */
    reload("all");
    osr_assert_log_is(&sb,
        "xrdb -merge ROOT/home/.Xresources\n"
        "pgrep -u X -x i3\n"
        "pgrep -u X -x polybar\n"
        "pgrep -u X -x picom\n"
        "pgrep -u X -x xsettingsd\n"
        "pgrep -u X -x dunst\n"
        "pgrep -u X -x mako\n"
        "gsettings get org.gnome.desktop.interface gtk-theme\n",
        "an X session with nothing running still refreshes the X resource "
        "database, and probes for the rest without signalling anything");

    /* A full desktop. The complete list is the assertion: an extra signal is
     * as much a defect as a missing one when the thing being signalled is the
     * session the user is sitting in. */
    fresh();
    session(":0", "");
    osr_sb_write(&sb, "home/.Xresources", "! xresources\n", 0644);
    alive("i3");
    alive("polybar");
    alive("picom");
    alive("xsettingsd");
    reload("x11");
    osr_assert_log_is(&sb,
        "xrdb -merge ROOT/home/.Xresources\n"
        "pgrep -u X -x i3\n"
        "i3-msg -q restart\n"
        "pgrep -u X -x polybar\n"
        "pkill -USR1 -x polybar\n"
        "pgrep -u X -x picom\n"
        "pkill -USR1 -x picom\n"
        "pgrep -u X -x xsettingsd\n"
        "pkill -HUP -x xsettingsd\n",
        "a full X11 desktop: merge the resources, restart i3, signal the three "
        "daemons -- each one only after its own probe answered yes");

    /* Order is load-bearing here, not incidental: i3 reads its colours out of
     * the X resource database at startup, so restarting i3 before merging the
     * new Xresources brings the bar back in the OLD palette. */
    osr_assert_true(before("xrdb -merge", "i3-msg -q restart"),
        "xrdb merges BEFORE i3 restarts -- i3 reads its colours from the "
        "resource database, so the other order restores the old palette");

    /* i3 is RESTARTED where the others are signalled, and that asymmetry is
     * deliberate: `i3-msg restart` preserves the layout and re-execs the bar,
     * which is the only way a new bar config takes effect. */
    osr_assert_log(&sb, "i3-msg -q restart",
        "i3 is restarted, because that is what re-reads a bar config -- and "
        "i3's restart is layout-preserving by design");

    /* The signals are specific and wrong ones are dangerous. xsettingsd takes
     * SIGHUP; SIGUSR1 means something else to it entirely. */
    osr_refute_log(&sb, "pkill -USR1 -x xsettingsd",
        "xsettingsd is sent SIGHUP, never SIGUSR1");

    /* Nothing Wayland-side may fire on an X11 box. */
    osr_refute_log(&sb, "hyprctl", "X11: no compositor reloader fires");
    osr_refute_log(&sb, "waybar", "X11: waybar is not touched");

    /* A reloader that fails is swallowed: the program keeps its old config,
     * which is cosmetic, where aborting the apply is not. */
    fresh();
    session(":0", "");
    osr_sb_write(&sb, "home/.Xresources", "! xresources\n", 0644);
    fails("xrdb");
    alive("i3");
    osr_assert_rc(reload("x11"), 0, "a failed xrdb does not fail the apply");
    osr_assert_log(&sb, "i3-msg -q restart",
        "and the reloaders after the failed one still run");

    /* polybar has no reload IPC at all, so the launcher script the polybar
     * module installs is the supported way to bring the bars back with the new
     * colors.ini. The signal is the fallback for a box that has polybar but
     * not our launcher. */
    fresh();
    session(":0", "");
    alive("polybar");
    osr_sb_write(&sb, "home/.config/polybar/launch.sh",
                 "#!/bin/sh\nprintf 'launch.sh %s\\n' \"$*\" >>\"$LOG\"\n", 0755);
    reload("x11");
    osr_assert_log_is(&sb,
        "pgrep -u X -x i3\n"
        "pgrep -u X -x polybar\n"
        "launch.sh \n"
        "pgrep -u X -x picom\n"
        "pgrep -u X -x xsettingsd\n",
        "polybar: the launcher is preferred, because polybar has no reload IPC "
        "and only a relaunch picks up a new colors.ini");
    osr_refute_log(&sb, "pkill -USR1 -x polybar",
        "polybar: the signal is not also sent when the launcher ran");

    /* A launcher that is present but not executable is not a launcher. */
    fresh();
    session(":0", "");
    alive("polybar");
    osr_sb_write(&sb, "home/.config/polybar/launch.sh", "#!/bin/sh\n", 0644);
    reload("x11");
    osr_assert_log(&sb, "pkill -USR1 -x polybar",
        "polybar: a non-executable launcher falls back to the signal rather "
        "than failing to reload at all");

    /* ================================================================
     * 3. Wayland
     * ================================================================ */
    fresh();
    session("", "wayland-0");
    alive("Hyprland");
    alive("waybar");
    alive("mako");
    reload("wayland");
    osr_assert_log_is(&sb,
        "pgrep -u X -x Hyprland\n"
        "hyprctl reload\n"
        "pgrep -u X -x waybar\n"
        "pkill -USR2 -x waybar\n",
        "a Wayland session: the compositor reloads through its own IPC and "
        "waybar is signalled");

    /* SIGUSR2, never SIGUSR1. waybar's SIGUSR1 TOGGLES VISIBILITY -- sending
     * it after a theme switch makes the bar vanish, which every user reads as
     * a crash rather than as a reload. */
    osr_refute_log(&sb, "pkill -USR1 -x waybar",
        "waybar gets SIGUSR2: SIGUSR1 toggles visibility and looks like a crash");

    /* Hyprland reloads, and is never restarted or killed: restarting a
     * compositor takes every client in the session down with it. */
    osr_assert_log(&sb, "hyprctl reload",
        "Hyprland is reloaded through hyprctl, never restarted");
    osr_refute_log(&sb, "pkill -x Hyprland",
        "the compositor is never killed -- that ends the session");

    osr_refute_log(&sb, "xrdb", "Wayland: no X11 reloader fires");
    osr_refute_log(&sb, "i3-msg", "Wayland: i3 is not touched");

    /* ================================================================
     * 4. Notifications
     * ================================================================ */
    fresh();
    session(":0", "");
    alive("dunst");
    reload("notify");
    osr_assert_log_is(&sb,
        "pgrep -u X -x dunst\n"
        "dunstctl reload\n"
        "pgrep -u X -x mako\n",
        "dunst reloads through dunstctl");

    /* mako grew `makoctl reload` late, so the signal is still the fallback --
     * a distro shipping an older mako must not silently stop reloading. */
    fresh();
    session("", "wayland-0");
    alive("mako");
    reload("notify");
    osr_assert_log_is(&sb,
        "pgrep -u X -x dunst\n"
        "pgrep -u X -x mako\n"
        "makoctl reload\n",
        "a modern mako reloads through makoctl");

    fresh();
    session("", "wayland-0");
    alive("mako");
    fails("makoctl");
    reload("notify");
    osr_assert_log_is(&sb,
        "pgrep -u X -x dunst\n"
        "pgrep -u X -x mako\n"
        "makoctl reload\n"
        "pkill -USR2 -x mako\n",
        "an older mako, whose makoctl has no reload verb, falls back to SIGUSR2");

    /* ================================================================
     * 5. GTK
     *
     * There is no "reload" for a GTK theme: the toolkit only notices a change
     * when the SETTING changes. So the setting is written to something else
     * and back, which is what makes an already-applied theme re-read.
     * ================================================================ */
    fresh();
    osr_sb_write(&sb, "state/gtk-theme", "'Adwaita-dark'\n", 0644);
    reload("gtk");
    osr_assert_log(&sb, "gsettings get",
        "gtk: the current theme is read before anything is changed");
    osr_assert_log(&sb, "gsettings set",
        "gtk: the theme setting is rewritten, which is the only way GTK "
        "notices a theme it already believes is applied");

    /* Nothing reported means nothing to toggle -- and writing a made-up theme
     * name would be worse than doing nothing. */
    fresh();
    reload("gtk");
    osr_refute_log(&sb, "gsettings set",
        "gtk: with no theme reported, nothing is written");

    fresh();
    osr_sb_write(&sb, "state/gtk-theme", "'Adwaita-dark'\n", 0644);
    fails("gsettings");
    osr_assert_rc(reload("gtk"), 0, "gtk: a failed gsettings is not fatal");

    /* ================================================================
     * 6. Both at once (XWayland, or a nested session)
     *
     * DISPLAY and WAYLAND_DISPLAY are both set on any Wayland session running
     * XWayland, which is most of them. Both branches run, and the probe is
     * what keeps that from signalling programs that are not there.
     * ================================================================ */
    fresh();
    session(":0", "wayland-1");
    alive("i3");
    alive("waybar");
    alive("dunst");
    osr_sb_write(&sb, "home/.Xresources", "x\n", 0644);
    osr_sb_write(&sb, "state/gtk-theme", "'Nord'\n", 0644);
    osr_assert_rc(reload("all"), 0, "both display servers set is not an error");
    osr_assert_log(&sb, "i3-msg -q restart",
        "both set: the X11 branch runs for the programs that are running");
    osr_assert_log(&sb, "pkill -USR2 -x waybar",
        "both set: the Wayland branch runs too");
    osr_assert_log(&sb, "dunstctl reload",
        "both set: the notification daemon is reloaded once");
    osr_refute_log(&sb, "hyprctl",
        "both set: a compositor that is not running is not reloaded -- the "
        "probe is what makes running both branches safe");

    /* ================================================================
     * 7. The verbs that must never appear
     *
     * Asserted by INSPECTION of lib/reload.c rather than by running anything,
     * because the failure mode is a logged-out user and there is no safe way
     * to provoke it in a test. This is the one place in the suite where
     * reading the source is the right assertion: the list below is short,
     * exact, and every entry on it would end a session.
     *
     * Comments are stripped first -- the file DOCUMENTS what it deliberately
     * does not do, and matching that prose would make the check pass or fail
     * on how a comment is worded.
     * ================================================================ */
    {
        static const char *forbidden[] = {
            "killall", "pkill -9", "kill -9", "dispatch exit",
            "systemctl restart", "loginctl",
            "pkill -x Hyprland", "pkill -x i3", "pkill -x sway", NULL
        };
        HStr path, code;
        char *buf;
        int i;

        hs_init(&path);
        hs_path(&path, hs_text(&sb.osr_lib), "reload.c");
        buf = h_slurp(hs_text(&path));
        hs_init(&code);
        strip_comments(&code, buf);
        free(buf);

        osr_assert_true(hs_text(&code)[0] != '\0', "lib/reload.c is readable");
        for (i = 0; forbidden[i] != NULL; i++) {
            HStr label;
            hs_init(&label);
            hs_add(&label, "lib/reload.c contains no session-ending verb: ");
            hs_add(&label, forbidden[i]);
            osr_assert_true(strstr(hs_text(&code), forbidden[i]) == NULL,
                            hs_text(&label));
            hs_free(&label);
        }
        /* The positive form of the same rule: waybar's signal is spelled out
         * in the code, so a future edit reaching for USR1 has to delete this
         * assertion to do it. */
        osr_assert_true(strstr(hs_text(&code), "USR2") != NULL,
                        "lib/reload.c still names USR2, which is waybar's reload");
        hs_free(&code);
        hs_free(&path);
    }

    osr_sb_free(&sb);
    return osr_finish();
}
