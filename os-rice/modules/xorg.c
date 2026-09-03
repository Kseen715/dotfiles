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
 * Was modules/xorg.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/cmds.h"
#include "../lib/common.h"
#include "../lib/config.h"
#include "../lib/service.h"

#include <dirent.h>
#include <fnmatch.h>
#include <stddef.h>

/* The two root-owned drop-ins this module writes, verbatim. */
static const char *const XINITRC =
    "#!/bin/sh\n"
    "# Seeded once by os-rice (modules/xorg.c) - yours to edit, never rewritten.\n"
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

#define QUIRKS_HEADER "# Managed by os-rice (modules/xorg.c) - GPU stability quirks.\n"
#define QUIRKS_FILE   "/20-gpu-quirks.conf"
#define INPUT_FILE    "/30-input.conf"
#define NOUVEAU_HEADER "# Managed by os-rice (modules/xorg.c) - GPU stability quirks.\n"
#define NOUVEAU_FILE   "/blacklist-nouveau.conf"

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

/* nouveau_quirk -- blacklist nouveau on the ONE machine shape where it is a
 * liability, and nowhere else. Called from the GPU-quirk block below because it
 * is the same class of problem as the two X options there: an old GPU whose
 * driver takes something bigger than itself down. The difference is only WHERE
 * the option lives - this is a kernel module, so it goes in /etc/modprobe.d and
 * into the initramfs rather than into xorg.conf.d.
 *
 * The case, measured on a 2010 Optimus laptop: nouveau binds the discrete
 * GF108, half-initialises it -
 *
 *   nouveau 0000:01:00.0: drm: failed to create ce channel, -22
 *   failed to evaluate ROM got AE_AML_BUFFER_LIMIT
 *
 * - and then sits there driving nothing, because the panel hangs off the Intel
 * iGPU and AutoAddGPU is off. An unused driver on a GPU it could not set up is
 * pure risk: it is the piece most likely to lock the box, and unloading it
 * costs nothing that is in use.
 *
 * OS-RICE RUNS ON OTHER PEOPLE'S MACHINES, so the interesting half of this is
 * everything it must NOT do. A working nouveau on a modern card is a driver
 * somebody is using; taking it away is the worst thing this module could do.
 * Five guards, ALL required, each one able to veto on its own:
 *
 *   1. More than one GPU. Never blacklist the only display driver on a box.
 *   2. The NVIDIA chip is FERMI OR OLDER. Kepler and up are left alone
 *      entirely - nouveau is maintained there, Turing+ runs on the GSP
 *      firmware, and those cards are used for render offload. The failure this
 *      quirk exists for belongs to the generations behind that line.
 *   3. The proprietary nvidia module is not loaded (then nouveau is not in the
 *      picture and modules/gpu-drivers.c owns that machine).
 *   4. X is NOT running on the nouveau card - established by comparing the DRM
 *      card index nouveau is bound to against the one the Xorg log says the
 *      server opened. Unknown either way vetoes: no answer is not a yes.
 *   5. Evidence in the kernel log, the rule the glamor half already follows.
 *
 * Connector state is deliberately NOT one of them, though it looks like the
 * obvious test. On the very laptop this was written for, nouveau's analog load
 * detection reports card0-VGA-1 as `connected` and `enabled` with five modes
 * and nothing plugged in at all - so "does this card drive an output" answered
 * from sysfs is a coin flip, and a coin flip is not allowed to veto or to
 * approve. Which card X actually opened is a fact.
 *
 * OSR_BLACKLIST_NOUVEAU=1 forces it, =0 forbids it. The operator outranks all
 * five guards in both directions.
 *
 * The honest cost where it does fire: with no driver bound, nothing runs the
 * Optimus power-down DSM either, so the dGPU may stay powered where nouveau's
 * runtime PM would have parked it. On a GPU that never initialised that trade
 * is obviously right - but it is a trade, not a free win. */
/* xorg_conf_path -- a file under /etc/X11/xorg.conf.d, with the directory
 * overridable ($OSR_XORGCONF_DIR) so a test asserts on its own tree. */
static void xorg_conf_path(Str *out, const char *file) {
    str_addz(out, env_str("OSR_XORGCONF_DIR", "/etc/X11/xorg.conf.d"));
    str_addz(out, file);
}

/* glamor_gpu_hang -- the kernel's own record of the crash this option
 * prevents: glamor renders INSIDE the X server, so a hung render ring owned by
 * Xorg is X doing GPU work it cannot finish.
 *
 *   i915 0000:00:02.0: [drm] GPU HANG: ecode 5:1:020fff7f, in Xorg [6117]
 *
 * Measured on the gen5 laptop after the option had been dropped: nine hangs in
 * one boot, each resetting the server, which from the desk looks like "opening
 * a terminal kills my session". The Xorg log says nothing about it - the
 * server does not know it hung - so this signature exists because the other
 * three cannot see this failure at all. */
static int glamor_gpu_hang(void) {
    static const char *const fixed[] = {
        "/var/log/dmesg.log",
        "/var/log/socklog/kernel/current",
        "/var/log/kern.log",
        NULL
    };
    const char *named[2];
    const char *const *logs = fixed;
    size_t i;

    named[0] = env_str("OSR_KERNEL_LOG", "");
    named[1] = NULL;
    if (named[0][0] != '\0') logs = named;

    for (i = 0; logs[i] != NULL; i++) {
        char *buf;
        size_t len;
        const char *at;
        int hit = 0;
        buf = slurp(logs[i], &len);
        if (buf == NULL) continue;
        at = buf;
        while (!hit && (at = strstr(at, "GPU HANG")) != NULL) {
            const char *eol = strchr(at, '\n');
            size_t n = (eol != NULL) ? (size_t)(eol - at) : strlen(at);
            Str line;
            str_init(&line);
            str_add(&line, at, n);
            hit = strstr(str_text(&line), "in Xorg") != NULL;
            str_free(&line);
            at += 8;
        }
        free(buf);
        if (hit) return 1;
    }
    return 0;
}

/* nouveau_conf -- /etc/modprobe.d/blacklist-nouveau.conf, with the directory
 * overridable the same way lib/service.c lets OSR_SV_DIR move /etc/sv: a test
 * of what this writes must not need /etc, and a box with a modprobe.d
 * elsewhere is then not a special case either. */
static void nouveau_conf(Str *out) {
    str_addz(out, env_str("OSR_MODPROBE_DIR", "/etc/modprobe.d"));
    str_addz(out, NOUVEAU_FILE);
}

/* nvidia_module_loaded -- is the proprietary driver the one in charge? sysfs
 * lists every loaded module under /sys/module; $OSR_SYSMOD moves that tree, so
 * a test asks about ITS fixture and never about the developer's own GPU. */
static int nvidia_module_loaded(void) {
    Str path;
    int loaded;
    str_init(&path);
    str_addz(&path, env_str("OSR_SYSMOD", "/sys/module"));
    str_addz(&path, "/nvidia");
    loaded = dir_exists(str_text(&path));
    str_free(&path);
    return loaded;
}

/* nouveau_old_chip -- is the NVIDIA chip on this box Fermi or older?
 *
 * A local list rather than modules/gpu-drivers.c's family table, because it
 * answers a different question. That table maps a codename to the driver
 * BRANCH that still supports it; this is one line drawn through nouveau's own
 * history - Kepler and newer are maintained and, on Turing and up, run the GSP
 * firmware path. Sharing the table would tie the two decisions together, and
 * the next driver branch NVIDIA drops has nothing to do with where this line
 * belongs.
 *
 * An unknown or missing codename answers NO. Unknown means "newer than this
 * lspci knows" far more often than it means "ancient", and the safe direction
 * here is to leave the driver alone. */
static int nouveau_old_chip(void) {
    /* Fermi (GF/NVC), Tesla (G8x/G9x/GT2xx/NV5x), Curie (G7x/NV4x) and the
     * NV3x..NV0x generations behind them. */
    static const char *const old[] = {
        "GF*", "NVC*", "G8*", "G9*", "GT2*", "NV5*", "G7*", "NV4*",
        "NV3*", "NV2*", "NV1*", "NV0*", NULL
    };
    Str chip;
    size_t i;
    int hit = 0;

    str_init(&chip);
    if (!osr_gpu_chip(&chip, "NVIDIA") || chip.len == 0) {
        str_free(&chip);
        return 0;
    }
    for (i = 0; old[i] != NULL && !hit; i++)
        if (fnmatch(old[i], str_text(&chip), 0) == 0) hit = 1;
    if (!hit)
        osr_infof("NVIDIA %s is newer than Fermi - leaving nouveau alone",
                  str_text(&chip));
    str_free(&chip);
    return hit;
}

/* drm_card_of_nouveau -- the N in /sys/class/drm/cardN that nouveau is bound
 * to, or -1. sysfs publishes the bound driver in the device's uevent
 * (DRIVER=nouveau), which is a plain file - readable without root and without
 * following symlinks. $OSR_DRM moves the tree, as it does for lib/detect.c. */
static int drm_card_of_nouveau(void) {
    const char *root = env_str("OSR_DRM", "/sys/class/drm");
    DIR *d = opendir(root);
    struct dirent *ent;
    int card = -1;

    if (d == NULL) return -1;
    while (card < 0 && (ent = readdir(d)) != NULL) {
        Str path;
        const char *n = ent->d_name;
        if (strncmp(n, "card", 4) != 0) continue;
        /* cardN only: cardN-HDMI-A-1 is a connector, not the device. */
        if (strchr(n, '-') != NULL) continue;
        str_init(&path);
        str_addz(&path, root); str_addc(&path, '/'); str_addz(&path, n);
        str_addz(&path, "/device/uevent");
        if (log_has(str_text(&path), "DRIVER=nouveau")) card = atoi(n + 4);
        str_free(&path);
    }
    closedir(d);
    return card;
}

/* drm_card_of_x -- the N in /dev/dri/cardN the X server opened, or -1. The
 * modesetting driver states it once at startup:
 *
 *   (II) modeset(0): using drv /dev/dri/card1
 *
 * which is the only place the pairing is written down. */
static int drm_card_of_x(void) {
    static const char *const fixed[] = {
        "/var/log/Xorg.0.log", "/var/log/Xorg.0.log.old", NULL
    };
    const char *named[2];
    const char *const *logs = fixed;
    size_t i;

    named[0] = env_str("OSR_XORG_LOG", "");
    named[1] = NULL;
    if (named[0][0] != '\0') logs = named;

    for (i = 0; logs[i] != NULL; i++) {
        char *buf;
        size_t len;
        const char *at;
        buf = slurp(logs[i], &len);
        if (buf == NULL) continue;
        at = strstr(buf, "using drv /dev/dri/card");
        if (at != NULL) {
            int card = atoi(at + 23);
            free(buf);
            return card;
        }
        free(buf);
    }
    return -1;
}

/* x_is_elsewhere -- guard 4. Both cards must be known AND different: an
 * unanswerable question vetoes, so a box whose log has rotated away keeps its
 * driver. */
static int x_is_elsewhere(Str *why) {
    int nv = drm_card_of_nouveau();
    int x  = drm_card_of_x();

    if (nv < 0) {
        osr_info("nouveau is not bound to any DRM card here - nothing to blacklist");
        return 0;
    }
    if (x < 0) {
        osr_info("cannot tell which DRM card X opened - leaving nouveau alone");
        return 0;
    }
    if (nv == x) {
        osr_infof("X is running on card%d, which nouveau drives - leaving it alone", nv);
        return 0;
    }
    str_addz(why, "X runs on another card and ");
    return 1;
}

static int nouveau_evidence(Str *why) {
    /* Both strings are nouveau's own; neither can come from another driver.
     * Three log locations, because which one exists depends on the init: Void's
     * boot copy, socklog's kernel stream, dracut/other distros' rotated file. */
    static const char *const fixed[] = {
        "/var/log/dmesg.log",
        "/var/log/socklog/kernel/current",
        "/var/log/kern.log",
        NULL
    };
    const char *named[2];
    const char *const *logs = fixed;
    static const char *const signs[] = {
        "failed to create ce channel",
        "failed to evaluate ROM got AE_AML_BUFFER_LIMIT",
        NULL
    };
    size_t i, j;

    /* One named log wins over the three guesses: a box that keeps its kernel
     * log somewhere else says so once, instead of this growing a fourth path. */
    named[0] = env_str("OSR_KERNEL_LOG", "");
    named[1] = NULL;
    if (named[0][0] != '\0') logs = named;

    for (i = 0; logs[i] != NULL; i++) {
        for (j = 0; signs[j] != NULL; j++) {
            if (log_has(logs[i], signs[j])) {
                str_addz(why, "the kernel log has \"");
                str_addz(why, signs[j]);
                str_addz(why, "\"");
                return 1;
            }
        }
    }
    return 0;
}

static void nouveau_quirk(void) {
    const char *force = env_str("OSR_BLACKLIST_NOUVEAU", "");
    Str why;
    int want;

    str_init(&why);
    want = strcmp(force, "1") == 0;
    if (strcmp(force, "0") == 0) {
        want = 0;
    } else if (want) {
        str_addz(&why, "OSR_BLACKLIST_NOUVEAU=1 was set");
    } else if (env_long("OSR_GPU_COUNT", 1) > 1
               && strstr(env_str("OSR_GPU_VENDOR", ""), "NVIDIA") != NULL
               && !nvidia_module_loaded()
               && nouveau_old_chip()
               && x_is_elsewhere(&why)) {
        want = nouveau_evidence(&why);
    }

    if (want) {
        Str text, conf;
        char *have;
        size_t hlen;

        str_init(&text); str_init(&conf);
        nouveau_conf(&conf);
        str_addz(&text, NOUVEAU_HEADER);
        str_addz(&text,
                 "# The discrete GPU is not driving the panel here (AutoAddGPU off) and\n"
                 "# nouveau failed to initialise it - see modules/xorg.c.\n"
                 "blacklist nouveau\n"
                 "# The blacklist alone is advisory for a modalias autoload; this is what\n"
                 "# makes udev and dracut refuse to load it at all.\n"
                 "install nouveau /bin/false\n");

        have = slurp(str_text(&conf), &hlen);
        if (have != NULL && hlen == text.len
            && strncmp(have, str_text(&text), hlen) == 0) {
            osr_infof("%s already current, skipping", str_text(&conf));
            free(have);
            str_free(&text); str_free(&conf); str_free(&why);
            return;
        }
        free(have);

        osr_warnf("blacklisting nouveau: %s (an unused, half-initialised GPU driver "
                  "is a hard-lock risk)", str_text(&why));
        (void)osr_write_root(str_text(&conf), str_text(&text));
        /* Without this the module is still autoloaded from the initramfs, where
         * the on-disk blacklist has not been read yet. */
        (void)osr_initramfs_regen();
        osr_warn("reboot for the nouveau blacklist to take effect");
        str_free(&text); str_free(&conf);
    } else {
        Str conf;
        str_init(&conf);
        nouveau_conf(&conf);
        /* Removing is NOT simply "the guards said no this time", and getting
         * that wrong is a loop: once the blacklist works, nouveau binds
         * nothing and logs nothing, so guards 4 and 5 stop matching by
         * construction - and a run that read that as "no longer needed" would
         * hand the driver back on every install, undoing itself forever.
         *
         * So the file goes only when something POSITIVELY says it should not
         * be there: the operator asked (=0), the proprietary driver took over,
         * or the box has no NVIDIA GPU at all any more. A hand-written file
         * with no header of ours is the user's and is never touched. */
        int unwarranted = strcmp(force, "0") == 0
                          || nvidia_module_loaded()
                          || strstr(env_str("OSR_GPU_VENDOR", ""), "NVIDIA") == NULL;
        if (unwarranted && file_exists(str_text(&conf))
            && log_has(str_text(&conf), NOUVEAU_HEADER)) {
            char *argv[4];
            osr_infof("nouveau no longer needs blacklisting - removing %s",
                      str_text(&conf));
            argv[0] = (char *)"rm"; argv[1] = (char *)"-f"; argv[2] = conf.p;
            argv[3] = NULL;
            (void)osr_run_root(argv);
            (void)osr_initramfs_regen();
        }
        str_free(&conf);
    }
    str_free(&why);
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
    Str dir, src, dst, body, why, quirks;
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
    str_init(&quirks); xorg_conf_path(&quirks, QUIRKS_FILE);
    if (strcmp(env_str("OSR_X_DISABLE_GLAMOR", ""), "1") == 0) {
        str_addz(&why, "OSR_X_DISABLE_GLAMOR=1 was set");
    } else if (strcmp(env_str("OSR_X_DISABLE_GLAMOR", ""), "0") == 0) {
        /* The one way to get the option back off: asking for it. */
        osr_info("OSR_X_DISABLE_GLAMOR=0 - leaving X 2D acceleration on");
    } else if (log_has(str_text(&quirks), "AccelMethod")) {
        /* STICKY, and this is the whole reason the branch exists. Once accel
         * is off, glamor never loads, never reports a context, never appears in
         * a backtrace - so all three evidence checks below go quiet BECAUSE THE
         * FIX IS WORKING, and a run that read that as "not needed any more"
         * would hand the crash back on the next install. Measured exactly that
         * way: the Device section disappeared from a box that needed it, glamor
         * came back on a gen5 iGPU, and the session then died every time a
         * window mapped. An applied quirk stays applied until asked otherwise. */
        str_addz(&why, "it is already disabled on this machine (glamor cannot "
                       "log evidence for its own absence)");
    } else if (glamor_gpu_hang()) {
        str_addz(&why, "the kernel logged a GPU hang owned by Xorg");
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
        have = slurp(str_text(&quirks), &hlen);
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
            osr_infof("%s already current, skipping", str_text(&quirks));
        } else {
            osr_infof("installing %s", str_text(&quirks));
            argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p";
            argv[2] = (char *)env_str("OSR_XORGCONF_DIR", "/etc/X11/xorg.conf.d");
            argv[3] = NULL;
            (void)osr_run_root(argv);
            (void)osr_write_root(str_text(&quirks), str_text(&want));
            osr_warnf("X must be restarted (log out) for %s to take effect",
                      str_text(&quirks));
        }
        str_free(&want);
    } else if (file_exists(str_text(&quirks))) {
        osr_infof("no GPU quirks needed on this machine - removing %s",
                  str_text(&quirks));
        argv[0] = (char *)"rm"; argv[1] = (char *)"-f"; argv[2] = quirks.p;
        argv[3] = NULL;
        (void)osr_run_root(argv);
    }

    /* Same class of quirk, different file: a kernel module rather than an X
     * option. Kept next to its siblings above so one place answers "what does
     * os-rice turn off on an old GPU". */
    nouveau_quirk();

    /* --- keyboard layout snippet (root-owned) ------------------------------
     * us,ru with alt+shift toggle, plus tap-to-click and natural scroll on
     * touchpads. Written only if absent: a machine's input config is machine
     * territory. */
    str_reset(&dst); xorg_conf_path(&dst, INPUT_FILE);
    if (!file_exists(str_text(&dst))) {
        osr_infof("installing %s", str_text(&dst));
        argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p";
        argv[2] = (char *)env_str("OSR_XORGCONF_DIR", "/etc/X11/xorg.conf.d");
        argv[3] = NULL;
        (void)osr_run_root(argv);
        (void)osr_write_root(str_text(&dst), INPUT_CONF);
    }

    str_free(&dir); str_free(&src); str_free(&dst); str_free(&body);
    str_free(&why); str_free(&quirks);
    return ok;
}
