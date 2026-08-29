/* modules/xorg.c -- the X11 session core, the X sibling of modules/wayland.c
 * (i3-sugg §1, §13). ONE copy, POSIX, distro-agnostic: logical names carry
 * Arch's `xorg-*` spelling and pkgmap translates (Void drops the prefix, Debian
 * bundles most of them into x11-xserver-utils/x11-utils).
 *
 * Three things happen here, and skipping any one of them is a classic "i3 starts
 * but nothing works" bug:
 *
 *   1. server + client utils + input/video drivers   (a session at all)
 *   2. dbus + elogind                                (polkit, udisks, lid/idle,
 *                                                     portals -- all D-Bus)
 *   3. ~/.xprofile as a layered loader (§5)          (env every GUI app inherits)
 *
 * ~/.xinitrc is seeded once (00-env semantics) so `startx` works without a
 * display manager; a DM user simply never reads it. It is user territory after
 * the first write -- os-rice never rewrites it.
 *
 * Port of modules/xorg.sh, kept as the reference at
 * test/ref/xorg_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"
#include "../lib/service.h"

#include <stddef.h>

/* The two root-owned drop-ins this module writes, verbatim. */
static const char *const XINITRC =
    "#!/bin/sh\n"
    "# Seeded once by os-rice (modules/xorg.sh) — yours to edit, never rewritten.\n"
    "[ -r \"$HOME/.xprofile\" ] && . \"$HOME/.xprofile\"\n"
    "[ -r /etc/X11/xinit/xinitrc.d ] && for f in /etc/X11/xinit/xinitrc.d/*.sh; do\n"
    "    [ -r \"$f\" ] && . \"$f\"\n"
    "done\n"
    "exec dbus-run-session i3\n";

static const char *const INPUT_CONF =
    "# Seeded once by os-rice (modules/xorg.sh).\n"
    "Section \"InputClass\"\n"
    "    Identifier \"system-keyboard\"\n"
    "    MatchIsKeyboard \"on\"\n"
    "    Option \"XkbLayout\" \"us,ru\"\n"
    "    Option \"XkbOptions\" \"grp:alt_shift_toggle,grp_led:scroll\"\n"
    "EndSection\n"
    "\n"
    "Section \"InputClass\"\n"
    "    Identifier \"touchpad\"\n"
    "    MatchIsTouchpad \"on\"\n"
    "    Driver \"libinput\"\n"
    "    Option \"Tapping\" \"on\"\n"
    "    Option \"NaturalScrolling\" \"true\"\n"
    "    Option \"DisableWhileTyping\" \"true\"\n"
    "EndSection\n";

#define QUIRKS_HEADER "# Managed by os-rice (modules/xorg.sh) - GPU stability quirks.\n"
#define QUIRKS_CONF   "/etc/X11/xorg.conf.d/20-gpu-quirks.conf"
#define INPUT_PATH    "/etc/X11/xorg.conf.d/30-input.conf"

/* log_has -- `grep -qs <needle> <file>`: a missing file is simply no match. */
static int log_has(const char *path, const char *needle) {
    char *buf;
    size_t len;
    int found;

    buf = slurp(path, &len);
    if (buf == NULL) return 0;
    found = strstr(buf, needle) != NULL;
    free(buf);
    return found;
}

/* glamor_old_gl -- `grep -qs 'glamor: Using OpenGL [012]\.'`: the Xorg log
 * states the context glamor got, and a pre-GL-3 one is exactly the machine that
 * must not use it. */
static int glamor_old_gl(const char *path) {
    char *buf;
    size_t len;
    const char *p;
    int found = 0;

    buf = slurp(path, &len);
    if (buf == NULL) return 0;
    p = buf;
    while (!found && (p = strstr(p, "glamor: Using OpenGL ")) != NULL) {
        p += 21;
        if ((*p == '0' || *p == '1' || *p == '2') && p[1] == '.') found = 1;
    }
    free(buf);
    return found;
}

int osrm_xorg(void) {
    static const char *const pkgs[] = {
        "xorg-server", "xorg-xinit", "xorg-xauth",
        "xorg-xrandr", "xorg-xset", "xorg-xsetroot", "xorg-xprop", "xorg-xev",
        "xorg-xkill", "xorg-xdpyinfo", "xorg-xinput", "xdotool",
        "xkeyboard-config", "setxkbmap",
        "xf86-input-libinput", "mesa-dri",
        "xorg-fonts-misc", "fontconfig",
        "dbus", "dbus-x11", "polkit", "libnotify", NULL
    };
    static const char *const elogind[] = { "elogind", NULL };
    Str dir, src, dst, body, why;
    char *argv[5];
    int ok;

    ok = osr_pkg_install_step("Installing X server + session core", pkgs);

    /* D-Bus and a seat/login manager must be up before any graphical session:
     * without them polkit has no authority to talk to, udisks never auto-mounts,
     * and xss-lock gets no suspend/lid signal to hook (§8). */
    if (!osr_service_enable("dbus")) osr_warn("could not enable dbus (needs a real init)");

    /* elogind is systemd-logind carved out for the inits that have no systemd.
     * On a systemd host it is not merely redundant: apt resolves `elogind` by
     * REMOVING systemd-sysv (they both own /run/systemd/seats and Provides:
     * logind), which takes the running init with it. So the package is chosen by
     * init, not by distro. */
    if (strcmp(osr_mod_init(), "systemd") == 0) {
        osr_info("systemd provides logind - skipping elogind");
    } else {
        ok = osr_pkg_install_step("Installing elogind (seat/login manager)", elogind) && ok;
        if (!osr_service_enable("elogind"))
            osr_warn("could not enable elogind (needs a real init)");
    }

    /* --- ~/.xprofile: loader block + layered drop-ins (§5) ------------------ */
    str_init(&dir); str_init(&src); str_init(&dst);
    str_addz(&dir, osr_mod_home()); str_addz(&dir, "/.config/xprofile.d");
    ok = osr_mkdir_p(str_text(&dir)) && ok;
    str_addz(&dst, osr_mod_home()); str_addz(&dst, "/.xprofile");
    ok = osr_install_xprofile_loader(str_text(&dir), str_text(&dst)) && ok;

    /* 10-session.sh -- dotfiles-owned, rice-independent (toolkit workarounds,
     * XDG ids). */
    str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/xprofile/10-session.sh");
    if (file_exists(str_text(&src))) {
        str_reset(&dst);
        str_addz(&dst, str_text(&dir)); str_addz(&dst, "/10-session.sh");
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
    }
    /* 90-theme.sh -- rice-owned, swapped on rice switch (§6). */
    str_reset(&dst);
    str_addz(&dst, str_text(&dir)); str_addz(&dst, "/90-theme.sh");
    (void)osr_install_theme_layer("xprofile", "90-theme.sh", str_text(&dst));
    /* 00-env / 99-local -- the user's, never overwritten. */
    str_reset(&dst); str_addz(&dst, str_text(&dir)); str_addz(&dst, "/00-env.sh");
    ok = osr_seed_empty(str_text(&dst)) && ok;
    str_reset(&dst); str_addz(&dst, str_text(&dir)); str_addz(&dst, "/99-local.sh");
    ok = osr_seed_empty(str_text(&dst)) && ok;

    /* --- ~/.xinitrc: startx without a display manager ----------------------- */
    str_reset(&dst); str_addz(&dst, osr_mod_home()); str_addz(&dst, "/.xinitrc");
    if (!file_exists(str_text(&dst))) {
        osr_info("seeding ~/.xinitrc (startx entry point)");
        ok = osr_write_user(str_text(&dst), XINITRC) && ok;
        argv[0] = (char *)"chmod"; argv[1] = (char *)"+x"; argv[2] = dst.p; argv[3] = NULL;
        (void)osr_run_user(argv);
    }

    /* --- GPU quirks (root-owned, /etc/X11/xorg.conf.d) ---------------------
     * Two things that take the whole X SERVER down rather than just an app, both
     * on old laptops, both invisible until they happen.
     *
     * 1. glamor on a pre-GL-3 GPU. The modesetting driver accelerates 2D through
     *    glamor, which is OpenGL - and on gen4/gen5 Intel Mesa can only offer it
     *    a GL 2.1 context. Recent Mesa aborts() out of gallium there, and because
     *    glamor runs INSIDE the server, the abort IS an X crash. Turning accel
     *    off costs software 2D and nothing else - client GL still goes direct to
     *    the kernel via DRI3.
     * 2. A second GPU screen on a hybrid-graphics laptop. Xorg auto-adds the
     *    discrete GPU as a secondary screen and starts a SECOND glamor stack on
     *    it, doubling the surface area for the same class of crash - for a GPU
     *    that is not driving the panel.
     *
     * The glamor half is gated on EVIDENCE, not on a table of PCI IDs. Three
     * signatures, any one of which condemns glamor here, plus a manual override:
     * one was not enough, because the evidence lives in a log that rotates, so a
     * single grep against a single moment is a coin flip. Say WHICH one matched,
     * so a run that does nothing can be told apart from a run that found
     * nothing. */
    str_init(&body); str_init(&why);
    if (strcmp(env_str("OSR_X_DISABLE_GLAMOR", "0"), "1") == 0) {
        str_addz(&why, "OSR_X_DISABLE_GLAMOR=1 was set");
    } else if (glamor_old_gl("/var/log/Xorg.0.log") || glamor_old_gl("/var/log/Xorg.0.log.old")) {
        str_addz(&why, "glamor reported a pre-GL-3 context in the Xorg log");
    } else if (log_has("/var/log/Xorg.0.log.old", "libglamoregl")
               || log_has("/var/log/Xorg.0.log", "libglamoregl")) {
        /* A recorded backtrace through glamor IS the crash this option
         * prevents. */
        str_addz(&why, "a previous X crash has glamor in its backtrace");
    } else if (log_has("/var/log/Xorg.0.log.old", "Caught signal 6")) {
        str_addz(&why, "the X server aborted (signal 6) in a previous session");
    }

    if (why.len > 0) {
        osr_warnf("disabling X 2D acceleration: %s (glamor aborts the whole server here)",
                  str_text(&why));
        /* A Device section, NOT an OutputClass. OutputClass forwards a small
         * fixed set of keys (PrimaryGPU, Driver, ModulePath, ...); AccelMethod
         * is a Device option and is simply dropped there. Without a BusID this
         * matches the first device the driver claimed, which with AutoAddGPU off
         * is the only one. */
        str_addz(&body,
                 "Section \"Device\"\n"
                 "    Identifier \"osr-gpu0\"\n"
                 "    Driver \"modesetting\"\n"
                 "    Option \"AccelMethod\" \"none\"\n"
                 "EndSection\n\n");
    }
    if (env_long("OSR_GPU_COUNT", 1) > 1) {
        osr_infof("hybrid graphics (%s GPUs) - not auto-adding the discrete GPU as a second X screen",
                  env_str("OSR_GPU_COUNT", "1"));
        str_addz(&body,
                 "Section \"ServerFlags\"\n"
                 "    Option \"AutoAddGPU\" \"off\"\n"
                 "EndSection\n\n");
    }

    if (body.len > 0) {
        Str want;
        char *have;
        size_t hlen;
        int current = 0;

        str_init(&want);
        str_addz(&want, QUIRKS_HEADER);
        str_addz(&want, str_text(&body));
        have = slurp(QUIRKS_CONF, &hlen);
        if (have != NULL) {
            /* `[ "$(cat ...)" = "..." ]`: a command substitution drops the
             * trailing newline on both sides, so compare without it. */
            size_t wlen = want.len;
            while (hlen > 0 && have[hlen - 1] == '\n') hlen--;
            while (wlen > 0 && str_text(&want)[wlen - 1] == '\n') wlen--;
            current = hlen == wlen && strncmp(have, str_text(&want), wlen) == 0;
            free(have);
        }
        if (current) {
            osr_infof("%s already current, skipping", QUIRKS_CONF);
        } else {
            osr_infof("installing %s", QUIRKS_CONF);
            argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p";
            argv[2] = (char *)"/etc/X11/xorg.conf.d"; argv[3] = NULL;
            (void)osr_run_root(argv);
            (void)osr_write_root(QUIRKS_CONF, str_text(&want));
            osr_warnf("X must be restarted (log out) for %s to take effect", QUIRKS_CONF);
        }
        str_free(&want);
    } else if (file_exists(QUIRKS_CONF)) {
        osr_infof("no GPU quirks needed on this machine - removing %s", QUIRKS_CONF);
        argv[0] = (char *)"rm"; argv[1] = (char *)"-f"; argv[2] = (char *)QUIRKS_CONF;
        argv[3] = NULL;
        (void)osr_run_root(argv);
    }

    /* --- keyboard layout snippet (root-owned) ------------------------------
     * us,ru with alt+shift toggle, plus tap-to-click and natural scroll on
     * touchpads. Written only if absent: a machine's input config is machine
     * territory. */
    if (!file_exists(INPUT_PATH)) {
        osr_infof("installing %s", INPUT_PATH);
        argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p";
        argv[2] = (char *)"/etc/X11/xorg.conf.d"; argv[3] = NULL;
        (void)osr_run_root(argv);
        (void)osr_write_root(INPUT_PATH, INPUT_CONF);
    }

    str_free(&dir); str_free(&src); str_free(&dst); str_free(&body); str_free(&why);
    return ok;
}
