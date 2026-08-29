/* modules/lightdm.c -- LightDM display manager + GTK greeter (i3-sugg §1.4).
 * The lighter alternative to modules/sddm.c for an X11 rice: no Qt, one config
 * file, and it runs the PAM stack that unlocks the keyring at login (which
 * `startx` does not).
 *
 * The greeter theme is rice-owned but lives in /etc (root), not ~/.config --
 * the greeter runs as its own user before yours exists, so it cannot read your
 * home. That is why this is the one theme layer written with as_root.
 *
 * Three things are installed here and the last two are what make the boot look
 * like a boot into a desktop rather than a boot into a console:
 *
 *   1. lightdm + lightdm-gtk-greeter                (a display manager at all)
 *   2. /etc/lightdm/lightdm.conf.d/10-osr.conf      (WHICH vt, WHICH session)
 *   3. the greeter theme: conf + user CSS           (§6b, palette-driven)
 *
 * Port of modules/lightdm.sh, kept as the reference at
 * test/ref/lightdm_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"
#include "../lib/render.h"
#include "../lib/service.h"

#include <glob.h>
#include <stddef.h>
#include <unistd.h>

/* The systemd drop-in, verbatim. */
static const char *const VT1_DROPIN =
    "# Written by os-rice (modules/lightdm.sh): the greeter is on vt1, so the getty\n"
    "# that would otherwise print `login:` over it must give way.\n"
    "[Unit]\n"
    "Conflicts=getty@tty1.service\n"
    "After=getty@tty1.service\n";

/* conf_has_owned_key -- `grep -qE '^[[:space:]]*(minimum-vt|greeter-session|
 * user-session)[[:space:]]*='`. */
static int conf_has_owned_key(const char *path) {
    static const char *const keys[] = { "minimum-vt", "greeter-session", "user-session", NULL };
    char *buf;
    size_t len, pos = 0;
    Line line;
    int found = 0;

    buf = slurp(path, &len);
    if (buf == NULL) return 0;
    while (!found && next_line(buf, len, &pos, &line)) {
        size_t i = 0, k;
        while (i < line.len && is_space(line.start[i])) i++;
        for (k = 0; keys[k] != NULL && !found; k++) {
            size_t n = strlen(keys[k]);
            size_t j = i + n;
            if (line.len - i < n || strncmp(line.start + i, keys[k], n) != 0) continue;
            while (j < line.len && is_space(line.start[j])) j++;
            if (j < line.len && line.start[j] == '=') found = 1;
        }
    }
    free(buf);
    return found;
}

/* pick_session -- which session the greeter logs you into. Left unset, lightdm
 * picks whatever .desktop sorts first in /usr/share/xsessions and that is rarely
 * the rice's WM. Prefer i3, fall back to the first entry that actually exists
 * rather than naming a session this machine cannot start. */
static void pick_session(Str *out) {
    static const char *const want[] = { "i3", "i3-with-shmlog", "openbox", "xfce", NULL };
    glob_t g;
    size_t i;

    for (i = 0; want[i] != NULL; i++) {
        Str p;
        int here;
        str_init(&p);
        str_addz(&p, "/usr/share/xsessions/");
        str_addz(&p, want[i]);
        str_addz(&p, ".desktop");
        here = file_exists(str_text(&p));
        str_free(&p);
        if (here) { str_addz(out, want[i]); return; }
    }
    if (glob("/usr/share/xsessions/*.desktop", 0, NULL, &g) == 0) {
        for (i = 0; i < g.gl_pathc; i++) {
            Str base;
            if (!file_exists(g.gl_pathv[i])) continue;
            str_init(&base);
            base_of(&base, g.gl_pathv[i]);
            if (base.len > 8) str_add(out, str_text(&base), base.len - 8); /* drop .desktop */
            str_free(&base);
            break;
        }
    }
    globfree(&g);
}

/* substitute -- `sed "s#{{WALLPAPER_PATH}}#<path>#g"` over one file. */
static void substitute(Str *out, const char *path, const char *value) {
    char *buf;
    size_t len, i;

    buf = slurp(path, &len);
    if (buf == NULL) return;
    for (i = 0; i < len; i++) {
        if (len - i >= 18 && strncmp(buf + i, "{{WALLPAPER_PATH}}", 18) == 0) {
            str_addz(out, value);
            i += 17;
            continue;
        }
        str_addc(out, buf[i]);
    }
    free(buf);
}

/* greeter_home -- `getent passwd lightdm | cut -d: -f6`, /var/lib/lightdm when
 * the account is not there to ask. */
static void greeter_home(Str *out) {
    Str line;
    char *argv[4];

    str_init(&line);
    argv[0] = (char *)"getent"; argv[1] = (char *)"passwd";
    argv[2] = (char *)"lightdm"; argv[3] = NULL;
    if (osr_run_capture(argv, &line)) {
        const char *p = str_text(&line);
        int field = 1;
        while (*p != '\0' && *p != '\n' && field < 6) { if (*p++ == ':') field++; }
        while (*p != '\0' && *p != '\n' && *p != ':') str_addc(out, *p++);
    }
    str_free(&line);
    if (out->len == 0) str_addz(out, "/var/lib/lightdm");
}

int osrm_lightdm(void) {
    static const char *const pkgs[] = { "lightdm", "lightdm-gtk-greeter", NULL };
    Str session, conf, layer, css;
    char *argv[6];
    int is_temp = 0;
    int ok;

    ok = osr_pkg_install_step("Installing LightDM", pkgs);

    str_init(&session);
    pick_session(&session);

    /* --- the boot handoff: greeter on vt1, not behind a login prompt -------
     * LightDM's built-in minimum-vt is 7, so on every init that starts a getty
     * on tty1 the console wins the race: you get `login:` on tty1 and the
     * greeter appears on tty7 seconds later. That is the "booted to CLI, then
     * LightDM opened" symptom, and it is a vt choice, not a slow service.
     *
     * conf.d is read BEFORE /etc/lightdm/lightdm.conf, so a distro that spells
     * minimum-vt out in that file would still win -- those keys are commented
     * out below (with a marker, and a .bak kept) rather than fought with. */
    osr_info("pointing LightDM at vt1 (greeter instead of a console login)");
    argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p";
    argv[2] = (char *)"/etc/lightdm/lightdm.conf.d"; argv[3] = NULL;
    (void)osr_run_root(argv);

    str_init(&conf);
    str_addz(&conf,
             "# Written by os-rice (modules/lightdm.sh). Edit lightdm.conf instead:\n"
             "# it is read after this directory and overrides everything here.\n"
             "[LightDM]\n"
             "minimum-vt=1\n"
             "\n"
             "[Seat:*]\n"
             "greeter-session=lightdm-gtk-greeter\n");
    if (session.len > 0) {
        str_addz(&conf, "user-session=");
        str_addz(&conf, str_text(&session));
        str_addc(&conf, '\n');
    }
    (void)osr_write_root("/etc/lightdm/lightdm.conf.d/10-osr.conf", str_text(&conf));

    if (file_exists("/etc/lightdm/lightdm.conf")
        && conf_has_owned_key("/etc/lightdm/lightdm.conf")) {
        if (!file_exists("/etc/lightdm/lightdm.conf.bak")) {
            argv[0] = (char *)"cp"; argv[1] = (char *)"-f";
            argv[2] = (char *)"/etc/lightdm/lightdm.conf";
            argv[3] = (char *)"/etc/lightdm/lightdm.conf.bak"; argv[4] = NULL;
            (void)osr_run_root(argv);
        }
        osr_info("commenting out minimum-vt/greeter-session/user-session in "
                 "lightdm.conf (10-osr.conf owns them)");
        argv[0] = (char *)"sed"; argv[1] = (char *)"-i"; argv[2] = (char *)"-E";
        argv[3] = (char *)"s/^[[:space:]]*(minimum-vt|greeter-session|user-session)[[:space:]]*=/#osr# &/";
        argv[4] = (char *)"/etc/lightdm/lightdm.conf"; argv[5] = NULL;
        (void)osr_run_root(argv);
    }

    /* Nothing else may hold vt1, or the greeter and a getty repaint over each
     * other. */
    if (strcmp(osr_mod_init(), "runit") == 0) {
        /* Void enables agetty-tty1 by default. tty2..tty6 stay, so a greeter
         * that fails to start still leaves a way in. */
        Str link;
        str_init(&link);
        str_addz(&link, env_str("OSR_SERVICE_DIR", "/var/service"));
        str_addz(&link, "/agetty-tty1");
        if (file_exists(str_text(&link)) || dir_exists(str_text(&link))) {
            osr_info("disabling agetty-tty1 (LightDM owns vt1; tty2-tty6 unchanged)");
            (void)osr_service_disable("agetty-tty1");
        }
        str_free(&link);
    } else if (strcmp(osr_mod_init(), "systemd") == 0) {
        Str target;
        /* Debian's lightdm.service already conflicts with getty@tty1; a drop-in
         * makes that true on any host and is a no-op where it is already set. */
        osr_info("adding lightdm.service drop-in (conflicts with getty@tty1)");
        argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p";
        argv[2] = (char *)"/etc/systemd/system/lightdm.service.d"; argv[3] = NULL;
        (void)osr_run_root(argv);
        (void)osr_write_root("/etc/systemd/system/lightdm.service.d/10-osr-vt1.conf",
                             VT1_DROPIN);
        argv[0] = (char *)"systemctl"; argv[1] = (char *)"daemon-reload"; argv[2] = NULL;
        (void)osr_run_root_quiet(argv);
        /* Enabled but never reached is the other half of this bug: with
         * multi-user.target as the default, nothing ever pulls in the DM. */
        str_init(&target);
        argv[0] = (char *)"systemctl"; argv[1] = (char *)"get-default"; argv[2] = NULL;
        (void)osr_run_capture(argv, &target);
        str_trim_trailing(&target, '\n');
        if (strcmp(str_text(&target), "graphical.target") != 0) {
            osr_info("setting the default systemd target to graphical.target");
            argv[0] = (char *)"systemctl"; argv[1] = (char *)"set-default";
            argv[2] = (char *)"graphical.target"; argv[3] = NULL;
            if (osr_run_root_quiet(argv) != 0)
                osr_warn("could not set graphical.target as default");
        }
        str_free(&target);
    } else if (strcmp(osr_mod_init(), "sysvinit") == 0) {
        /* tty1's getty is an /etc/inittab line here, and rewriting inittab is
         * machine territory. LightDM starting after it is cosmetic, not broken. */
        osr_warn("sysvinit: tty1's getty is set in /etc/inittab - comment its line "
                 "out by hand if the console login still flashes before the greeter");
    }

    /* --- greeter theme (§6b) ----------------------------------------------
     * The greeter needs the same GTK theme + icons as the session, or the login
     * screen is stock grey while everything after it is themed. */
    str_init(&layer);
    if (osr_theme_source(&layer, "lightdm", "lightdm-gtk-greeter.conf", &is_temp)) {
        Str wp, body;

        osr_info("installing LightDM greeter theme");
        argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p";
        argv[2] = (char *)"/etc/lightdm"; argv[3] = NULL;
        (void)osr_run_root(argv);
        if (file_exists("/etc/lightdm/lightdm-gtk-greeter.conf")
            && !file_exists("/etc/lightdm/lightdm-gtk-greeter.conf.bak")) {
            argv[0] = (char *)"cp"; argv[1] = (char *)"-f";
            argv[2] = (char *)"/etc/lightdm/lightdm-gtk-greeter.conf";
            argv[3] = (char *)"/etc/lightdm/lightdm-gtk-greeter.conf.bak"; argv[4] = NULL;
            (void)osr_run_root(argv);
        }

        /* The greeter background needs its own copy under /usr/share, and this
         * is not tidiness. The greeter runs as `lightdm`, and a home directory
         * is 0700 on Void, Debian and most others - so `lightdm` cannot even
         * traverse into ~/Pictures, let alone read the image. GTK does not
         * report that: it draws the fallback grey and says nothing, which reads
         * as "the theme is broken" rather than "one file is unreadable".
         *
         * So: install the wallpaper once for the user (§6, what the session
         * uses), then place a world-readable copy for the greeter and point the
         * conf at it. */
        str_init(&wp);
        osr_install_wallpaper(&wp);
        if (wp.len > 0 && file_exists(str_text(&wp))) {
            Str base, sys;
            str_init(&base); str_init(&sys);
            base_of(&base, str_text(&wp));
            str_addz(&sys, "/usr/share/backgrounds/osr/");
            str_addz(&sys, str_text(&base));
            argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p";
            argv[2] = (char *)"/usr/share/backgrounds/osr"; argv[3] = NULL;
            (void)osr_run_root(argv);
            argv[0] = (char *)"cp"; argv[1] = (char *)"-f"; argv[2] = wp.p;
            argv[3] = sys.p; argv[4] = NULL;
            (void)osr_run_root(argv);
            argv[0] = (char *)"chmod"; argv[1] = (char *)"0644"; argv[2] = sys.p;
            argv[3] = NULL;
            (void)osr_run_root(argv);
            osr_infof("greeter background: %s", str_text(&sys));
            str_reset(&wp);
            str_addz(&wp, str_text(&sys));
            str_free(&base); str_free(&sys);
        } else {
            osr_warn("this theme ships no wallpaper - the greeter keeps its plain background");
        }

        str_init(&body);
        substitute(&body, str_text(&layer), str_text(&wp));
        (void)osr_write_root("/etc/lightdm/lightdm-gtk-greeter.conf", str_text(&body));
        str_free(&body);
        str_free(&wp);
        if (is_temp) (void)unlink(str_text(&layer));
    }
    str_free(&layer);

    /* The .conf above only names a GTK theme; the rice palette itself reaches
     * the greeter through GTK's user stylesheet, which GTK3 reads from the HOME
     * of the user the greeter runs as (lightdm, /var/lib/lightdm). There is no
     * `css=` key in lightdm-gtk-greeter.conf - this is the whole mechanism. */
    str_init(&css);
    is_temp = 0;
    if (osr_theme_source(&css, "lightdm", "gtk-greeter.css", &is_temp)) {
        Str home;
        str_init(&home);
        greeter_home(&home);
        if (dir_exists(str_text(&home))) {
            Str dir, dst;
            char *buf;
            size_t len;

            str_init(&dir); str_init(&dst);
            str_addz(&dir, str_text(&home)); str_addz(&dir, "/.config/gtk-3.0");
            str_addz(&dst, str_text(&dir));  str_addz(&dst, "/gtk.css");
            osr_infof("installing greeter CSS into %s", str_text(&dst));
            argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p"; argv[2] = dir.p;
            argv[3] = NULL;
            (void)osr_run_root(argv);
            buf = slurp(str_text(&css), &len);
            if (buf != NULL) {
                Str text;
                str_init(&text);
                str_add(&text, buf, len);
                (void)osr_write_root(str_text(&dst), str_text(&text));
                str_free(&text);
                free(buf);
            }
            /* GTK silently ignores a stylesheet it cannot read, and the greeter
             * is not root - the chown is what makes this file do anything. */
            {
                Str cfg;
                str_init(&cfg);
                str_addz(&cfg, str_text(&home)); str_addz(&cfg, "/.config");
                argv[0] = (char *)"chown"; argv[1] = (char *)"-R";
                argv[2] = (char *)"lightdm:lightdm"; argv[3] = cfg.p; argv[4] = NULL;
                if (osr_run_root_quiet(argv) != 0)
                    osr_warnf("could not chown %s to lightdm - greeter CSS may be ignored",
                              str_text(&cfg));
                str_free(&cfg);
            }
            str_free(&dir); str_free(&dst);
        } else {
            osr_warnf("no greeter home at %s - skipping greeter CSS", str_text(&home));
        }
        str_free(&home);
        if (is_temp) (void)unlink(str_text(&css));
    }
    str_free(&css);

    if (!osr_service_enable("lightdm"))
        osr_warn("could not enable lightdm (needs a real init)");

    str_free(&session); str_free(&conf);
    return ok;
}
