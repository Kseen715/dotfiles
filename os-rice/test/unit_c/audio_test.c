/* test/unit_c/audio_test.c -- modules/pipewire.c and modules/pulseaudio.c,
 * which are mirror images of each other.
 *
 * The two audio stacks cannot coexist: both want to own the ALSA and JACK
 * device names, and a box with half of each has no working audio at all. So
 * each module REMOVES its rival before installing itself -- the only place in
 * the tree where a module uninstalls something, and therefore the only place
 * where getting the package list wrong takes something away that was working.
 *
 * Two rules follow, and they are what this file is for:
 *
 *   THE REMOVAL LIST IS EXACT. `pipewire` the core package is NOT part of the
 *   PipeWire stack for this purpose -- xdg-desktop-portal-wlr depends on it,
 *   and taking it out along with the pulse/jack shims is what silently breaks
 *   screen sharing on a box the user only meant to move back to PulseAudio.
 *
 *   NEITHER MODULE DOES ANYTHING OFF PACMAN. Every other distro ships one
 *   stack or resolves the conflict in packaging, so the swap is an Arch
 *   problem and running it elsewhere would remove packages for no reason.
 *
 * Hermetic: $PATH is a directory of stubs; pacman answers "is it installed"
 * from a list the scenario states and logs everything else.
 *
 * Replaces test/unit/audio_switch.sh. See test/harness.h.
 */
#include "../harness.c"

static OsrSandbox sb;

/* box_has -- which packages pacman reports as installed. */
static void box_has(const char *packages) {
    osr_sb_env(&sb, "INSTALLED", packages);
}

static int run(const char *module, const char *pkg_mgr) {
    osr_sb_env(&sb, "OSR_PKG", pkg_mgr);
    osr_sb_reset(&sb);
    return osr_sb_run_core(&sb, "module", "run", module, (const char *)NULL);
}

int main(void) {
    osr_sb_init(&sb);

    osr_sb_env(&sb, "OSR_DISTRO", "arch");
    osr_sb_env(&sb, "OSR_ID_LIKE", "");
    osr_sb_env(&sb, "OSR_CODENAME", "");
    osr_sb_env(&sb, "OSR_VERSION_ID", "");
    osr_sb_env(&sb, "OSR_INIT", "systemd");

    /* pacman is the one stub with behaviour: `-Q <pkg>` is the "is it
     * installed" probe, and which packages answer yes is the scenario. */
    osr_sb_stub_body(&sb, "pacman",
        "printf 'pacman %s\\n' \"$*\" >>\"$LOG\"\n"
        "if [ \"$1\" = \"-Q\" ] || [ \"$1\" = \"-Qq\" ]; then\n"
        "  case \" $INSTALLED \" in *\" $2 \"*) exit 0 ;; *) exit 1 ;; esac\n"
        "fi\n"
        "exit 0\n");
    osr_sb_stub_body(&sb, "dpkg", "exit 1\n");
    osr_sb_stub_body(&sb, "apt-mark", "exit 0\n");
    osr_sb_stub_body(&sb, "apt-get",
        "printf 'apt-get %s\\n' \"$*\" >>\"$LOG\"\nexit 0\n");

    /* ================================================================
     * 1. The swap, in both directions
     *
     * The box has the OTHER stack and not its own, which is the situation the
     * swap exists for: there is a rival to remove, and nothing the module
     * installs drops out of the batch as already present.
     * ================================================================ */
    box_has("pulseaudio pulseaudio-ctl pulseaudio-equalizer pulseaudio-jack "
            "pulseaudio-lirc pulseaudio-rtp jack2 jack2-dbus");
    run("pipewire", "pacman");
    osr_assert_log(&sb, "pacman -R --noconfirm",
        "pipewire: the rival stack is removed");
    osr_assert_log(&sb, "pulseaudio",
        "pipewire: and PulseAudio is named in the removal");
    osr_assert_log(&sb, "jack2",
        "pipewire: JACK too -- pipewire-jack takes over those device names");
    osr_assert_log(&sb, "pacman -S --needed --noconfirm",
        "pipewire: then its own stack is installed");
    osr_assert_log(&sb, "pipewire-pulse",
        "pipewire: including the PulseAudio shim, which is what makes existing "
        "applications keep working");
    osr_assert_log(&sb, "wireplumber",
        "pipewire: and a session manager, without which nothing routes");

    box_has("pipewire-pulse pipewire-jack pipewire-alsa pipewire-audio "
            "wireplumber pipewire");
    run("pulseaudio", "pacman");
    osr_assert_log(&sb, "pipewire-pulse",
        "pulseaudio: the PipeWire shims are removed");
    osr_assert_log(&sb, "wireplumber",
        "pulseaudio: the session manager goes with them");
    osr_assert_log(&sb, "pacman -S --needed --noconfirm",
        "pulseaudio: then its own stack is installed");

    /* The core `pipewire` package must survive. It is not part of the audio
     * conflict -- xdg-desktop-portal-wlr depends on it for screen capture --
     * and removing it alongside the shims breaks screen sharing on a box whose
     * owner only asked to go back to PulseAudio. */
    osr_refute_log(&sb, "pacman -R --noconfirm pipewire ",
        "pulseaudio: the pipewire CORE package is kept -- the portals need it "
        "for screen sharing");
    osr_refute_log(&sb, " pipewire\n",
        "pulseaudio: and it is not the last name in the removal list either");

    /* ================================================================
     * 2. Off pacman, both modules do nothing at all
     * ================================================================ */
    box_has("pulseaudio jack2");
    osr_sb_env(&sb, "OSR_DISTRO", "ubuntu");
    osr_sb_env(&sb, "OSR_ID_LIKE", "debian");
    run("pipewire", "apt");
    osr_assert_log_empty(&sb,
        "pipewire: off pacman the module is a complete no-op -- every other "
        "distro resolves this conflict in packaging");
    run("pulseaudio", "apt");
    osr_assert_log_empty(&sb, "pulseaudio: the same, in the same way");
    osr_sb_env(&sb, "OSR_DISTRO", "arch");
    osr_sb_env(&sb, "OSR_ID_LIKE", "");

    /* ================================================================
     * 3. The removal narrows to what is actually installed
     *
     * A first run has none of the rival stack. Handing pacman a package that
     * is not installed makes it error, which would make a FIRST run fatal for
     * every user -- the exact opposite of what SS2 asks for.
     * ================================================================ */
    box_has("pulseaudio");
    osr_sb_env(&sb, "OSR_PKG", "pacman");
    osr_sb_reset(&sb);
    osr_sb_run_core(&sb, "pkg", "remove", "pulseaudio", "jack2-dbus",
                    (const char *)NULL);
    osr_assert_log_is(&sb,
        "pacman -Q pulseaudio\n"
        "pacman -Q jack2-dbus\n"
        "sudo pacman -R --noconfirm pulseaudio\n"
        "pacman -R --noconfirm pulseaudio\n",
        "remove: only the package that is actually installed reaches pacman -- "
        "the absent one is filtered out rather than passed down to error");

    osr_sb_free(&sb);
    return osr_finish();
}
