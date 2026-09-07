/* modules/weston-rdp.c -- a Wayland desktop reachable over RDP on a box with no
 * screen: weston driving its rdp-backend, with the session (Plasma, when
 * modules/plasma.c put one there) autolaunched inside it.
 *
 * WHY WESTON AND NOT THE SESSION'S OWN COMPOSITOR. RDP has to come from the
 * compositor -- it is the only thing holding the pixels -- and kwin has no RDP
 * backend before Plasma 6's krdp. weston does: rdp-backend.so is a real output,
 * so `weston --backend=rdp-backend.so` IS the remote desktop server, no X server
 * and no screen scraper in the middle. The session then runs as an ordinary
 * nested Wayland client of it, which is what the kiosk shell is for: one client,
 * fullscreen, no weston chrome around it.
 *
 * THE SESSION STARTS WHEN THE FIRST CLIENT CONNECTS, and that is not a policy
 * choice. weston's rdp backend has no input devices of its own: it creates the
 * wl_seat out of the connecting client's keyboard and mouse, so until somebody
 * connects there is no seat on that compositor at all -- and kwin does not
 * check, it dereferences the seat it did not get and segfaults in
 * KWayland::Client::Seat::hasKeyboard(). Autolaunching the session directly is
 * therefore a crash loop that ends with weston giving up. session.sh below is
 * the fix: it waits for a seat, then execs the session. A client disconnecting
 * takes the seat away again, which ends the session and (Restart=always) puts
 * the box back to waiting for the next connection.
 *
 * IT LISTENS ON LOCALHOST. The rdp backend authenticates nobody by default:
 * whoever completes the handshake gets the desktop. So the listener is bound to
 * 127.0.0.1 and the way in is an SSH tunnel --
 *
 *     ssh -L 3389:127.0.0.1:3389 <host>    then point the client at localhost
 *
 * -- which is also the encryption that matters, the TLS below being a
 * self-signed certificate this module generates. Widening it is one word in
 * /etc/weston-rdp/weston-rdp.conf (RDP_ADDRESS), an EnvironmentFile the unit
 * reads and this module seeds once, and it should be paired with something that
 * actually checks a password.
 *
 * The unit is systemd-only. It needs no logind session: RuntimeDirectory= makes
 * /run/weston-rdp for it, which is all XDG_RUNTIME_DIR has to be for a
 * compositor that owns no seat.
 *
 * C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <string.h>

#define RDP_DIR  "/etc/weston-rdp"
#define RDP_CRT  RDP_DIR "/tls.crt"
#define RDP_KEY  RDP_DIR "/tls.key"
#define RDP_INI  RDP_DIR "/weston.ini"
#define RDP_ENV  RDP_DIR "/weston-rdp.conf"
#define RDP_SESSION RDP_DIR "/session.sh"
#define RDP_SUNIT "/etc/systemd/system/weston-rdp-session.service"
#define RDP_UNIT "/etc/systemd/system/weston-rdp.service"

/* The knobs, seeded once and then the machine's (§5). */
static const char *const ENV_FILE =
    "# Read by weston-rdp.service. Seeded once by os-rice; yours after that.\n"
    "#\n"
    "# RDP_ADDRESS=0.0.0.0 exposes the desktop to the network, and the rdp\n"
    "# backend asks connecting clients for no password -- keep 127.0.0.1 and\n"
    "# reach it through `ssh -L 3389:127.0.0.1:3389 <host>` instead.\n"
    "RDP_ADDRESS=127.0.0.1\n"
    "RDP_PORT=3389\n"
    "RDP_WIDTH=1600\n"
    "RDP_HEIGHT=900\n";

/* The session starter: block until weston has a seat, then become the session.
 * `wayland-info` is the ask -- it lists the compositor's globals, and wl_seat
 * appearing in them IS "a client has connected". */
/* Split into chunks because C90 only promises 509 bytes per string
 * literal and this script is three times that; seed_session joins them. */
static const char *const SESSION_SH[] = {
    "#!/bin/sh\n"
    "# Written by os-rice (modules/weston-rdp.c) if it was not here.\n"
    "#\n"
    "# Run by weston-rdp-session.service, which starts with the compositor and\n"
    "# is expected to sit here waiting: weston's rdp backend builds its wl_seat\n"
    "# out of the connecting client's keyboard and mouse, so before anyone\n"
    "# connects there is no seat, and a Plasma session started then dies in\n"
    "# kwin dereferencing the seat it did not get.\n",

    ": \"${XDG_RUNTIME_DIR:=/run/weston-rdp}\"\n"
    "export XDG_RUNTIME_DIR\n"
    "while :; do\n"
    "    for sock in \"$XDG_RUNTIME_DIR\"/wayland-[0-9]*; do\n"
    "        [ -S \"$sock\" ] || continue\n"
    "        WAYLAND_DISPLAY=${sock##*/}\n"
    "        export WAYLAND_DISPLAY\n"
    "        break\n"
    "    done\n"
    "    [ -n \"${WAYLAND_DISPLAY:-}\" ] &&\n"
    "        wayland-info 2>/dev/null | grep -q \"interface: 'wl_seat'\" && break\n"
    "    sleep 2\n"
    "done\n",

    "# Let the connection settle before Plasma starts drawing into it.\n"
    "sleep 5\n"
    "\n"
    "# plasma_session, not startplasma-wayland: startplasma-wayland exists to\n"
    "# BE the top-level compositor and starts kwin with --xwayland, whose\n"
    "# Xwayland dies in mesa on this kind of box and hangs the startup behind\n"
    "# its splash. plasma_session assumes a compositor is already there, which\n"
    "# is exactly the situation, and brings up its own kwin under weston.\n",

    "# The environment startplasma-wayland would have exported is ours to set:\n"
    "# without QT_QPA_PLATFORM every Qt part of the session exits with 'no Qt\n"
    "# platform plugin could be initialized'.\n",

    "export QT_QPA_PLATFORM=wayland\n"
    "export XDG_CURRENT_DESKTOP=KDE\n"
    "export KDE_FULL_SESSION=true\n"
    "export XDG_SESSION_TYPE=wayland\n"
    "exec plasma_session\n"
};

/* seed_ini -- weston.ini for the RDP compositor. Deliberately NO [autolaunch]:
 * letting weston spawn and watch the session looks tidy and takes the whole
 * compositor down with a SIGSEGV once Plasma is up under it. The session is a
 * unit of its own (RDP_SUNIT) that connects to weston like any other client.
 */
static int seed_ini(void) {
    Str ini;
    int ok;

    str_init(&ini);
    str_addz(&ini,
             "# Written by os-rice (modules/weston-rdp.c) if it was not here.\n"
             "# Ports and geometry live in " RDP_ENV ", not here: weston takes\n"
             "# those on the command line only.\n"
             "[core]\n"
             "backend=rdp-backend.so\n"
             "shell=kiosk-shell.so\n"
             "idle-time=0\n"
             "require-input=false\n"
             "\n"
             "[rdp]\n"
             "tls-cert=" RDP_CRT "\n"
             "tls-key=" RDP_KEY "\n"
             "\n");
    if (!file_exists("/usr/bin/plasma_session"))
        osr_warn("no plasma_session here - weston will come up empty; "
                 "run `osr module run plasma`, then rerun this module");
    ok = osr_seed_file_root(RDP_INI, str_text(&ini));
    str_free(&ini);
    return ok;
}

/* make_cert -- a self-signed certificate for the rdp backend, which refuses to
 * start without one. Generated once, on the box, and never regenerated: the
 * client pins what it was shown the first time. */
static int make_cert(void) {
    char *argv[16];
    int ok = 1;
    Str subj;

    if (file_exists(RDP_KEY) && file_exists(RDP_CRT)) return 1;

    str_init(&subj);
    str_addz(&subj, "/CN=weston-rdp");

    argv[0] = (char *)"openssl";   argv[1] = (char *)"req";
    argv[2] = (char *)"-x509";     argv[3] = (char *)"-newkey";
    argv[4] = (char *)"rsa:2048";  argv[5] = (char *)"-nodes";
    argv[6] = (char *)"-keyout";   argv[7] = (char *)RDP_KEY;
    argv[8] = (char *)"-out";      argv[9] = (char *)RDP_CRT;
    argv[10] = (char *)"-days";    argv[11] = (char *)"3650";
    argv[12] = (char *)"-subj";    argv[13] = subj.p;
    argv[14] = NULL;
    ok = osr_run_step_root("Generating the RDP TLS certificate", argv);
    str_free(&subj);

    /* weston reads the key as the session user, not as root. */
    argv[0] = (char *)"chown"; argv[1] = (char *)osr_mod_user();
    argv[2] = (char *)RDP_KEY; argv[3] = (char *)RDP_CRT; argv[4] = NULL;
    (void)osr_run_root_quiet(argv);
    argv[0] = (char *)"chmod"; argv[1] = (char *)"600";
    argv[2] = (char *)RDP_KEY; argv[3] = NULL;
    (void)osr_run_root_quiet(argv);
    return ok;
}

/* write_unit -- the service, which this module OWNS and rewrites (the tunables
 * it would otherwise hardcode are in the seeded EnvironmentFile instead).
 * dbus-run-session is not decoration: a Plasma session with no session bus
 * starts, fails to reach kded and ksmserver, and leaves a grey screen. */
static int write_unit(void) {
    Str unit;
    char *argv[4];
    int ok;

    str_init(&unit);
    str_addz(&unit,
             "# Written by os-rice (modules/weston-rdp.c). Edit " RDP_ENV "\n"
             "# for the address, port and geometry.\n"
             "[Unit]\n"
             "Description=Weston compositor serving RDP (os-rice)\n"
             "After=network.target\n"
             "\n"
             "[Service]\n"
             "Type=simple\n"
             "User=");
    str_addz(&unit, osr_mod_user());
    str_addz(&unit,
             "\n"
             "RuntimeDirectory=weston-rdp\n"
             "RuntimeDirectoryMode=0700\n"
             "Environment=XDG_RUNTIME_DIR=/run/weston-rdp\n"
             "Environment=XDG_SESSION_TYPE=wayland\n"
             /* No GPU here and none wanted: llvmpipe through surfaceless EGL.
              * The alternative is weston's pixman renderer, which it picks by
              * itself when EGL is missing and which segfaults compositing a
              * Plasma session (pixman_image_composite32, in the rdp repaint).
              * The same vars keep the session's own GL -- including kwin's
              * Xwayland, which aborts in gallium without them -- on llvmpipe. */
             "Environment=LIBGL_ALWAYS_SOFTWARE=1\n"
             "Environment=GALLIUM_DRIVER=llvmpipe\n"
             "Environment=EGL_PLATFORM=surfaceless\n"
             "EnvironmentFile=" RDP_ENV "\n"
             /* Xwayland inside the session opens its socket in /tmp/.X11-unix,
              * and on a box that has never run an X server that directory is
              * not there -- /tmp is a tmpfs and nothing recreated it. Without
              * it kwin's Xwayland dies, kdeinit5 aborts on an unset $DISPLAY,
              * and the session hangs on its splash forever. systemd already
              * ships the rule for it, in x11.conf -- but as a `D!` line, which
              * systemd-tmpfiles applies only under --boot, so asking for the
              * rule here would silently do nothing. One mkdir, and the mode is
              * the 1777 that file asks for. */
             "ExecStartPre=+/usr/bin/mkdir -p -m 1777 /tmp/.X11-unix\n"
             "ExecStart=");
    str_addz(&unit,
             "weston --config=" RDP_INI
             " --backend=rdp-backend.so --renderer=gl"
             " --address=${RDP_ADDRESS} --port=${RDP_PORT}"
             " --width=${RDP_WIDTH} --height=${RDP_HEIGHT}\n"
             /* always, not on-failure: on a box whose only way in is this
              * service, a clean exit is still the machine disappearing. */
             "Restart=always\n"
             "RestartSec=3\n"
             "\n"
             "[Install]\n"
             "WantedBy=multi-user.target\n");
    ok = osr_write_root(RDP_UNIT, str_text(&unit));
    str_free(&unit);

    argv[0] = (char *)"systemctl"; argv[1] = (char *)"daemon-reload"; argv[2] = NULL;
    (void)osr_run_root_quiet(argv);
    return ok;
}

/* write_session_unit -- the Plasma session, kept in a unit of its own so that
 * weston does not have to supervise it (see seed_ini). BindsTo ties it to the
 * compositor: no weston, no session, and a restarted weston restarts this too.
 * dbus-run-session is not decoration -- a Plasma session with no session bus
 * comes up, fails to reach kded and ksmserver, and leaves a grey screen.
 * Restart=always is the whole disconnect story: the client leaving takes the
 * seat away, the session ends, and this puts it back to waiting for the next
 * connection without touching the listener. */
static int write_session_unit(void) {
    Str unit;
    char *argv[4];
    int ok;

    str_init(&unit);
    str_addz(&unit,
             "# Written by os-rice (modules/weston-rdp.c).\n"
             "[Unit]\n"
             "Description=Plasma session on the Weston RDP compositor (os-rice)\n"
             "BindsTo=weston-rdp.service\n"
             "After=weston-rdp.service\n"
             "\n"
             "[Service]\n"
             "Type=simple\n"
             "User=");
    str_addz(&unit, osr_mod_user());
    str_addz(&unit,
             "\n"
             "Environment=XDG_RUNTIME_DIR=/run/weston-rdp\n"
             "Environment=XDG_SESSION_TYPE=wayland\n"
             "ExecStart=");
    if (osr_have_cmd("dbus-run-session")) str_addz(&unit, "dbus-run-session -- ");
    str_addz(&unit,
             RDP_SESSION "\n"
             "Restart=always\n"
             "RestartSec=3\n"
             "\n"
             "[Install]\n"
             "WantedBy=weston-rdp.service\n");
    ok = osr_write_root(RDP_SUNIT, str_text(&unit));
    str_free(&unit);

    argv[0] = (char *)"systemctl"; argv[1] = (char *)"daemon-reload"; argv[2] = NULL;
    (void)osr_run_root_quiet(argv);
    return ok;
}

int osrm_weston_rdp(void) {
    /* wayland-utils is not a nicety: session.sh asks `wayland-info` whether the
     * compositor has a seat yet, which is the whole start condition. */
    static const char *const pkgs[] = {
        "weston", "openssl", "dbus", "wayland-utils", NULL
    };
    char *argv[4];
    Str session;
    size_t i;
    int ok;

    ok = osr_pkg_install_step("Installing the Weston RDP compositor", pkgs);

    argv[0] = (char *)"mkdir"; argv[1] = (char *)"-p";
    argv[2] = (char *)RDP_DIR; argv[3] = NULL;
    (void)osr_run_root(argv);

    ok = make_cert() && ok;
    (void)osr_seed_file_root(RDP_ENV, ENV_FILE);
    str_init(&session);
    for (i = 0; i < sizeof SESSION_SH / sizeof SESSION_SH[0]; i++)
        str_addz(&session, SESSION_SH[i]);
    if (osr_seed_file_root(RDP_SESSION, str_text(&session))) {
        argv[0] = (char *)"chmod"; argv[1] = (char *)"0755";
        argv[2] = (char *)RDP_SESSION; argv[3] = NULL;
        (void)osr_run_root_quiet(argv);
    }
    str_free(&session);
    (void)seed_ini();

    /* Everything above is a file on disk and is true under any init; only the
     * thing that STARTS it is systemd's. Elsewhere the files are still right
     * and the box needs its own unit/service script. */
    if (strcmp(osr_mod_init(), "systemd") != 0) {
        osr_warnf("init is %s, not systemd - " RDP_DIR " is set up, but starting "
                  "weston at boot is yours to wire", osr_mod_init());
        return ok;
    }
    ok = write_unit() && ok;
    ok = write_session_unit() && ok;
    if (!osr_service_enable("weston-rdp"))
        osr_warn("could not enable weston-rdp");
    if (!osr_service_enable("weston-rdp-session"))
        osr_warn("could not enable weston-rdp-session");
    osr_info("RDP is on 127.0.0.1:3389 - tunnel in with "
             "`ssh -L 3389:127.0.0.1:3389 <host>`");
    return ok;
}
