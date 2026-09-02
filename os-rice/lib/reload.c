/* lib/reload.c -- tell the running desktop to re-read what just changed. See
 * lib/reload.h.
 *
 * POSIX ONLY, and the Windows branch at the bottom of this file is empty on
 * purpose rather than unwritten. Everything here is "signal a long-running
 * process to re-read its config": xrdb, i3-msg, hyprctl, a SIGUSR1 to a
 * notification daemon. Windows has no equivalent to signal -- Explorer reads
 * its settings once at startup, and the honest instruction is the one
 * modules/win-tweaks.c already prints ("sign out for these to take effect"),
 * not a reload that quietly does nothing. Restarting a user's shell out from
 * under them is not a decision this makes.
 *
 * C89 + POSIX.
 */
#ifndef _WIN32

#define _POSIX_C_SOURCE 200809L

#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "module.h"

#include "cmds.h"
#include "reload.h"

/* What has been reloaded so far, for the closing line. A file static rather
 * than a parameter threaded through five functions, mirroring the OSR_RELOADED
 * the shell tier accumulated in. */
static Str g_reloaded;
static int g_reloaded_ready = 0;

static void reloaded_init(void) {
    if (!g_reloaded_ready) { str_init(&g_reloaded); g_reloaded_ready = 1; }
}

static void reloaded_add(const char *label) {
    reloaded_init();
    if (g_reloaded.len > 0) str_addc(&g_reloaded, ' ');
    str_addz(&g_reloaded, label);
}

/* home_of -- ${OSR_HOME:-$HOME}, the home whose dotfiles were just swapped. */
static const char *home_of(void) {
    const char *h = env_str("OSR_HOME", "");
    return h[0] != '\0' ? h : env_str("HOME", "");
}

/* target_uid -- the uid pgrep is asked about: the account being riced, falling
 * back to whoever is running when that name resolves to nothing. */
static long target_uid(void) {
    const char *user = env_str("OSR_USER", "");
    if (user[0] != '\0') {
        struct passwd *pw = getpwnam(user);
        if (pw != NULL) return (long)pw->pw_uid;
    }
    return (long)getuid();
}

/* field_eq -- does field n (1-based) of this line equal name? n == 0 asks for
 * the LAST field. Together they are awk's `$NF == n || $4 == n`. */
static int field_eq(const Line *line, int n, const char *name) {
    const char *p = line->start;
    const char *end = line->start + line->len;
    size_t want = strlen(name);
    const char *fs = NULL;      /* the field that is current, once found */
    size_t flen = 0;
    int idx = 0;

    for (;;) {
        const char *start;
        while (p < end && is_space(*p)) p++;
        if (p >= end) break;
        start = p;
        while (p < end && !is_space(*p)) p++;
        idx++;
        if (n == 0 || idx == n) {
            fs = start;
            flen = (size_t)(p - start);
            if (n != 0) break;  /* nth field: stop at it */
        }
    }
    return fs != NULL && flen == want && memcmp(fs, name, want) == 0;
}

/* running -- is a process by that name alive for this user? pgrep is not on
 * every base install (busybox has it, some minimal images do not), so fall
 * back to a ps scan rather than skipping the reload entirely. */
static int running(const char *name) {
    char *argv[6];
    char uid[32];
    Str out;
    size_t pos = 0;
    Line line;
    int found = 0;

    if (osr_have_cmd("pgrep")) {
        sprintf(uid, "%ld", target_uid());
        argv[0] = (char *)"pgrep";
        argv[1] = (char *)"-u";
        argv[2] = uid;
        argv[3] = (char *)"-x";
        argv[4] = (char *)name;
        argv[5] = NULL;
        return osr_run_quiet(argv) == 0;
    }

    argv[0] = (char *)"ps";
    argv[1] = (char *)"-e";
    argv[2] = NULL;
    str_init(&out);
    (void)osr_run_capture(argv, &out);
    while (!found && next_line(str_text(&out), out.len, &pos, &line)) {
        /* awk '$NF == n || $4 == n' */
        if (field_eq(&line, 0, name) || field_eq(&line, 4, name)) found = 1;
    }
    str_free(&out);
    return found;
}

/* try_reload -- run a reloader, log it, and swallow the outcome. */
static int try_reload(const char *label, char *const argv[]) {
    if (osr_run_quiet(argv) == 0) {
        osr_debugf("reload: %s", label);
        reloaded_add(label);
        return 1;
    }
    osr_debugf("reload: %s failed (ignored)", label);
    return 1;
}

int osr_reload_x11(void) {
    char *argv[6];

    if (env_str("DISPLAY", "")[0] == '\0') return 1;

    /* Xresources first: i3/rofi/xterm colors are read from the X server's
     * database, so merging must happen BEFORE the WM re-reads its config. */
    {
        Str xres;
        str_init(&xres);
        str_addz(&xres, home_of());
        str_addz(&xres, "/.Xresources");
        if (file_exists(str_text(&xres)) && osr_have_cmd("xrdb")) {
            argv[0] = (char *)"xrdb";
            argv[1] = (char *)"-merge";
            argv[2] = (char *)str_text(&xres);
            argv[3] = NULL;
            (void)try_reload("xrdb", argv);
        }
        str_free(&xres);
    }

    if (osr_have_cmd("i3-msg") && running("i3")) {
        /* `restart` (not `reload`) is what re-reads colors AND re-execs the
         * bar; i3 preserves the layout and every client across it. */
        argv[0] = (char *)"i3-msg";
        argv[1] = (char *)"-q";
        argv[2] = (char *)"restart";
        argv[3] = NULL;
        (void)try_reload("i3", argv);
    }

    /* polybar has no reload IPC: the launcher script the polybar module
     * installs is the supported way to bring the bars back with the new
     * colors.ini. */
    if (running("polybar")) {
        Str launch;
        str_init(&launch);
        str_addz(&launch, home_of());
        str_addz(&launch, "/.config/polybar/launch.sh");
        if (access(str_text(&launch), X_OK) == 0) {
            argv[0] = (char *)str_text(&launch);
            argv[1] = NULL;
            (void)try_reload("polybar", argv);
        } else {
            argv[0] = (char *)"pkill";
            argv[1] = (char *)"-USR1";
            argv[2] = (char *)"-x";
            argv[3] = (char *)"polybar";
            argv[4] = NULL;
            (void)try_reload("polybar", argv);
        }
        str_free(&launch);
    }

    /* picom: SIGUSR1 re-reads the config in every version that has ever
     * shipped. */
    if (running("picom")) {
        argv[0] = (char *)"pkill";
        argv[1] = (char *)"-USR1";
        argv[2] = (char *)"-x";
        argv[3] = (char *)"picom";
        argv[4] = NULL;
        (void)try_reload("picom", argv);
    }

    /* xsettingsd carries the GTK2/3 theme name to running apps. */
    if (running("xsettingsd")) {
        argv[0] = (char *)"pkill";
        argv[1] = (char *)"-HUP";
        argv[2] = (char *)"-x";
        argv[3] = (char *)"xsettingsd";
        argv[4] = NULL;
        (void)try_reload("xsettingsd", argv);
    }
    return 1;
}

int osr_reload_wayland(void) {
    char *argv[6];

    if (env_str("WAYLAND_DISPLAY", "")[0] == '\0') return 1;

    if (osr_have_cmd("hyprctl") && running("Hyprland")) {
        argv[0] = (char *)"hyprctl";
        argv[1] = (char *)"reload";
        argv[2] = NULL;
        (void)try_reload("hyprland", argv);
    }

    /* waybar reloads on SIGUSR2 (SIGUSR1 toggles visibility -- sending that
     * would hide the bar and look exactly like a crash). */
    if (running("waybar")) {
        argv[0] = (char *)"pkill";
        argv[1] = (char *)"-USR2";
        argv[2] = (char *)"-x";
        argv[3] = (char *)"waybar";
        argv[4] = NULL;
        (void)try_reload("waybar", argv);
    }
    return 1;
}

int osr_reload_notify(void) {
    char *argv[6];

    if (osr_have_cmd("dunstctl") && running("dunst")) {
        argv[0] = (char *)"dunstctl";
        argv[1] = (char *)"reload";
        argv[2] = NULL;
        (void)try_reload("dunst", argv);
    }

    /* mako gained `makoctl reload` in 1.7; older builds re-read on SIGUSR2. */
    if (osr_have_cmd("makoctl") && running("mako")) {
        argv[0] = (char *)"makoctl";
        argv[1] = (char *)"reload";
        argv[2] = NULL;
        if (osr_run_quiet(argv) == 0) {
            /* Counted, but with no debug line: the shell version reaches this
             * one through the plain success branch rather than _osr_try. */
            reloaded_add("mako");
        } else {
            argv[0] = (char *)"pkill";
            argv[1] = (char *)"-USR2";
            argv[2] = (char *)"-x";
            argv[3] = (char *)"mako";
            argv[4] = NULL;
            (void)try_reload("mako", argv);
        }
    }
    return 1;
}

int osr_reload_gtk(void) {
    static const char *iface = "org.gnome.desktop.interface";
    char *argv[5];
    Str theme;
    Str cmd;

    if (!osr_have_cmd("gsettings")) return 1;

    argv[0] = (char *)"gsettings";
    argv[1] = (char *)"get";
    argv[2] = (char *)iface;
    argv[3] = (char *)"gtk-theme";
    argv[4] = NULL;
    str_init(&theme);
    if (!osr_run_capture(argv, &theme)) { str_free(&theme); return 1; }
    /* `$( )` dropped the trailing newline before the emptiness test. */
    str_trim_trailing(&theme, '\n');
    if (theme.len == 0) { str_free(&theme); return 1; }

    /* Set it to something else and back: GTK only reacts to a CHANGE, and
     * after a theme swap the name is usually identical while the files behind
     * it are not. The value comes back from gsettings already quoted, and is
     * spliced in unquoted for exactly that reason. */
    str_init(&cmd);
    str_addz(&cmd, "gsettings set ");
    str_addz(&cmd, iface);
    str_addz(&cmd, " gtk-theme 'Adwaita' && gsettings set ");
    str_addz(&cmd, iface);
    str_addz(&cmd, " gtk-theme ");
    str_addz(&cmd, str_text(&theme));

    argv[0] = (char *)"sh";
    argv[1] = (char *)"-c";
    argv[2] = (char *)str_text(&cmd);
    argv[3] = NULL;
    (void)try_reload("gtk", argv);

    str_free(&cmd);
    str_free(&theme);
    return 1;
}

int osr_reload_all(void) {
    reloaded_init();
    g_reloaded.len = 0;

    /* No display server at all (a container, an ssh session, a CI box): there
     * is nothing on screen to repaint. Returning here keeps a theme apply from
     * poking dbus/gsettings on a machine with no session behind it, which
     * would be both pointless and, for dconf, a mutation nobody asked for. */
    if (env_str("DISPLAY", "")[0] == '\0' &&
        env_str("WAYLAND_DISPLAY", "")[0] == '\0') {
        osr_infof("no display server - layers are on disk for the next start");
        return 1;
    }

    osr_reload_x11();
    osr_reload_wayland();
    osr_reload_notify();
    osr_reload_gtk();

    if (g_reloaded.len > 0)
        osr_infof("reloaded: %s", str_text(&g_reloaded));
    else
        osr_infof("nothing running to reload (layers are on disk for the next start)");
    return 1;
}

static int reload_usage(void) {
    fputs("usage: osr reload [all|x11|wayland|notify|gtk]\n\n", stderr);
    fputs("  all       every reloader that applies to this session (default)\n", stderr);
    fputs("  x11       X resources, the i3 stack, the X compositor\n", stderr);
    fputs("  wayland   the Hyprland stack\n", stderr);
    fputs("  notify    dunst / mako\n", stderr);
    fputs("  gtk       the GTK theme name, for apps already running\n", stderr);
    return 2;
}

int osr_reload_main(int argc, char **argv) {
    const char *what = argc > 1 ? argv[1] : "all";

    if (argc > 2) return reload_usage();
    if (strcmp(what, "all") == 0)     { osr_reload_all();     return 0; }
    if (strcmp(what, "x11") == 0)     { osr_reload_x11();     return 0; }
    if (strcmp(what, "wayland") == 0) { osr_reload_wayland(); return 0; }
    if (strcmp(what, "notify") == 0)  { osr_reload_notify();  return 0; }
    if (strcmp(what, "gtk") == 0)     { osr_reload_gtk();     return 0; }
    return reload_usage();
}

#else /* _WIN32 */

#include <stdio.h>

#include "common.h"
#include "cmds.h"
#include "reload.h"

/* Nothing to signal -- see the file header. Each returns 1 because "there was
 * nothing to reload" is not a failure, and a caller that treated it as one
 * would report a theme apply as broken. */
int osr_reload_x11(void)     { return 1; }
int osr_reload_wayland(void) { return 1; }
int osr_reload_notify(void)  { return 1; }
int osr_reload_gtk(void)     { return 1; }
int osr_reload_all(void)     { return 1; }

int osr_reload_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    fputs("osr reload: nothing here re-reads its config on a signal -- sign out "
          "for Explorer and taskbar settings to take effect\n", stderr);
    return 0;
}

#endif /* _WIN32 */
