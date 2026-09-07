/* test/unit_c/weston_rdp_test.c -- the Weston RDP compositor module.
 *
 * This module is not a package list: it is a lifecycle that took a box's worth
 * of coredumps to arrive at, and every part of it looks arbitrary until it is
 * gone. The four that a rewrite silently loses:
 *
 *   THE LISTENER IS ON LOCALHOST. weston's rdp backend asks connecting clients
 *   for no password. RDP_ADDRESS=0.0.0.0 in the seeded knobs would hand the
 *   desktop to the network.
 *
 *   THE COMPOSITOR RENDERS WITH GL. Left to itself weston picks Pixman on a
 *   box with no GPU, and Pixman segfaults compositing Plasma
 *   (pixman_image_composite32). --renderer=gl plus the llvmpipe/surfaceless
 *   environment is the software path that does not crash.
 *
 *   THE SESSION IS ITS OWN UNIT, WAITING FOR A SEAT. The rdp backend builds
 *   its wl_seat out of the connecting client, so a session started before
 *   anyone connects dies in kwin dereferencing a seat it never got; and weston
 *   supervising the session itself ([autolaunch] watch=true) takes the
 *   compositor down with it. Hence: no autolaunch, a second unit BindsTo the
 *   first, and a starter that blocks on wl_seat.
 *
 *   THE FILES ARE TRUE UNDER ANY INIT, THE UNIT IS NOT. Off systemd the module
 *   still sets /etc/weston-rdp up, says so, and writes no unit.
 *
 * Root-owned writes go out through `tee`, so `tee` here is a stub that mirrors
 * the file under $SBFILES instead: that is how the content of a /etc path can
 * be asserted from a test that is not root and must not touch the real one.
 *
 * See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;
static HStr mirror;

/* at -- a mirrored root-owned path, e.g. at("/etc/weston-rdp/weston.ini"). */
static const char *at(const char *abs) {
    static HStr ring[4];
    static int ready = 0;
    static int next = 0;
    HStr *p;
    if (!ready) { int i; for (i = 0; i < 4; i++) hs_init(&ring[i]); ready = 1; }
    p = &ring[next];
    next = (next + 1) % 4;
    hs_reset(p);
    hs_add(p, hs_text(&mirror));
    hs_add(p, abs);
    return hs_text(p);
}

static void holds(const char *abs, const char *needle, const char *label) {
    char *got = h_slurp(at(abs));
    osr_assert_true(strstr(got, needle) != NULL, label);
    free(got);
}
static void lacks(const char *abs, const char *needle, const char *label) {
    char *got = h_slurp(at(abs));
    osr_assert_true(strstr(got, needle) == NULL, label);
    free(got);
}
static void absent(const char *abs, const char *label) {
    osr_assert_true(access(at(abs), F_OK) != 0, label);
}
static void ran(const char *needle, const char *label) {
    osr_assert_log(&sb, needle, label);
}
static void said(const char *needle, const char *label) {
    osr_assert_true(strstr(osr_sb_capture_both(&sb), needle) != NULL, label);
}

/* run -- the module, into a fresh mirror. Each scenario gets its own mirror
 * directory so that "no unit was written" is a real absence and not a leftover
 * from the run before it. */
static void run(const char *mirror_rel) {
    hs_reset(&mirror);
    hs_path(&mirror, hs_text(&sb.root), mirror_rel);
    osr_sb_env(&sb, "SBFILES", hs_text(&mirror));
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "module", "run", "weston-rdp", (const char *)NULL);
}

int main(void) {
    osr_sb_init(&sb);
    hs_init(&mirror);

    osr_sb_env(&sb, "OSR_PKG", "apt");
    osr_sb_env(&sb, "OSR_INIT", "systemd");
    osr_sb_stub_body(&sb, "dpkg", "exit 1\n");
    osr_sb_stub_body(&sb, "apt-mark", "exit 0\n");
    osr_sb_stub_body(&sb, "apt-get",
        "printf 'apt-get %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    /* is-enabled/is-active answer no, so the enable path is the one a fresh
     * box takes rather than the "already enabled + running" short-circuit. */
    osr_sb_stub_body(&sb, "systemctl",
        "printf 'systemctl %s\\n' \"$*\" >>\"$LOG\"\n"
        "case \"$1\" in is-*) exit 1 ;; esac\nexit 0\n");
    osr_sb_stub_body(&sb, "openssl",
        "printf 'openssl %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");
    /* Present, so the session unit is the one a real box gets: a Plasma
     * session with no session bus comes up grey. */
    osr_sb_stub_body(&sb, "dbus-run-session", "exec \"$@\"\n");
    /* The real mkdir, except for the system paths this test has no business
     * creating -- the module's `mkdir -p /etc/weston-rdp` is allowed to fail
     * here exactly as it would for an unprivileged user. */
    osr_sb_stub_body(&sb, "mkdir",
        "for a in \"$@\"; do\n"
        "    case \"$a\" in /etc/*|/run/*|/usr/*|/tmp/.X11-unix) exit 0 ;; esac\n"
        "done\n"
        "exec /bin/mkdir \"$@\"\n");
    /* tee, mirrored: the content of a root-owned file, which the argv log
     * never carries, lands under $SBFILES for the assertions below. */
    osr_sb_stub_body(&sb, "tee",
        "f=$1\n"
        "case \"$f\" in -a) shift; f=$1 ;; esac\n"
        "/bin/mkdir -p \"$SBFILES${f%/*}\"\n"
        "cat >\"$SBFILES$f\"\n");

    /* ================================================================
     * A systemd box: compositor, session, and the knobs between them.
     * ================================================================ */
    run("files");

    ran("apt-get", "the packages go in through the package step");
    ran("wayland-utils",
        "wayland-utils is named as a package: session.sh's start condition is "
        "`wayland-info` reporting a seat, so it is a dependency, not a nicety");

    holds("/etc/weston-rdp/weston-rdp.conf", "RDP_ADDRESS=127.0.0.1",
        "the listener is seeded on localhost -- the rdp backend authenticates "
        "nobody, so the way in is an SSH tunnel");
    lacks("/etc/weston-rdp/weston-rdp.conf", "\nRDP_ADDRESS=0.0.0.0",
        "the network address appears only in the comment warning against it, "
        "never as the setting");

    ran("openssl", "a self-signed certificate is generated for the TLS the "
        "rdp backend insists on");

    holds("/etc/weston-rdp/weston.ini", "backend=rdp-backend.so",
        "the compositor IS the RDP server -- no X server, no screen scraper");
    holds("/etc/weston-rdp/weston.ini", "shell=kiosk-shell.so",
        "kiosk shell: one client, fullscreen, no weston chrome around the "
        "session");
    lacks("/etc/weston-rdp/weston.ini", "autolaunch",
        "weston does NOT supervise the session: [autolaunch] watch=true takes "
        "the compositor down with SIGSEGV when the session ends");

    holds("/etc/systemd/system/weston-rdp.service", "--renderer=gl",
        "GL, not the Pixman weston would pick by itself -- Pixman segfaults in "
        "pixman_image_composite32 compositing Plasma");
    holds("/etc/systemd/system/weston-rdp.service", "GALLIUM_DRIVER=llvmpipe",
        "with llvmpipe behind it, because the box has no GPU");
    holds("/etc/systemd/system/weston-rdp.service", "--address=${RDP_ADDRESS}",
        "the unit reads the address from the EnvironmentFile, so widening it "
        "is one word in a config file and not a reinstall");
    holds("/etc/systemd/system/weston-rdp.service", "RuntimeDirectory=weston-rdp",
        "XDG_RUNTIME_DIR comes from systemd: a compositor that owns no seat "
        "gets no logind session to inherit one from");
    holds("/etc/systemd/system/weston-rdp.service", "/tmp/.X11-unix",
        "and the X socket directory is made at start -- systemd's own rule for "
        "it is a `D!` line that only --boot applies");

    holds("/etc/weston-rdp/session.sh", "wl_seat",
        "the session waits for a seat: the rdp backend builds one out of the "
        "connecting client, and kwin segfaults on the seat it did not get");
    holds("/etc/weston-rdp/session.sh", "exec plasma_session",
        "plasma_session, not startplasma-wayland -- that one starts kwin with "
        "--xwayland, whose Xwayland dies in mesa here");
    holds("/etc/weston-rdp/session.sh", "QT_QPA_PLATFORM=wayland",
        "without it every Qt part of the session exits with `no Qt platform "
        "plugin could be initialized`");
    ran("chmod 0755 /etc/weston-rdp/session.sh",
        "and the starter is executable, or the unit is a 203/EXEC loop");

    holds("/etc/systemd/system/weston-rdp-session.service",
        "BindsTo=weston-rdp.service",
        "the session is bound to the compositor: no weston, no session, and a "
        "restarted weston restarts this too");
    holds("/etc/systemd/system/weston-rdp-session.service",
        "dbus-run-session", "started on a session bus, or Plasma never reaches "
        "kded and ksmserver and leaves a grey screen");
    holds("/etc/systemd/system/weston-rdp-session.service", "Restart=always",
        "a disconnect takes the seat away and ends the session; this is what "
        "puts the box back to waiting for the next connection");

    ran("systemctl enable --now weston-rdp", "the compositor is enabled at boot");
    ran("systemctl enable --now weston-rdp-session", "and so is the session");

    /* ================================================================
     * Anything else for an init: the files are still right, the unit is
     * not written, and the user is told which half they own.
     * ================================================================ */
    osr_sb_env(&sb, "OSR_INIT", "openrc");
    run("files-openrc");

    holds("/etc/weston-rdp/weston.ini", "backend=rdp-backend.so",
        "off systemd the compositor is still configured -- none of it is "
        "systemd's");
    absent("/etc/systemd/system/weston-rdp.service",
        "but no unit is written for an init that would never read it");
    absent("/etc/systemd/system/weston-rdp-session.service",
        "neither of them");
    said("not systemd",
        "and the module says so, rather than reporting a remote desktop that "
        "nothing will ever start");

    hs_free(&mirror);
    osr_sb_free(&sb);
    return osr_finish();
}
