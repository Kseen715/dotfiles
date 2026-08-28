/* modules/pipewire.c -- PipeWire audio stack, replacing PulseAudio/JACK. ONE
 * copy, POSIX (was .../linux-arch-x86_64-hyprland-glass/pulseaudio-to-pipewire.sh).
 * Mirror image of modules/pulseaudio.sh: the two are mutually exclusive, listing
 * one removes the other. Package names are Arch's; the swap is pacman-only.
 * ponytail: no enable_service — the units are per-user and socket-activated by
 * the Arch packages. Add `enable_service pipewire` if a headless/system-wide
 * setup ever needs it.
 *
 * Port of modules/pipewire.sh, kept as the reference at
 * test/ref/pipewire_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <string.h>

int osrm_pipewire(void) {
    static const char *const old[] = {
        "pulseaudio", "pulseaudio-ctl", "pulseaudio-equalizer", "pulseaudio-jack",
        "pulseaudio-lirc", "pulseaudio-rtp", "jack2", "jack2-dbus", NULL
    };
    static const char *const pkgs[] = {
        "pipewire", "pipewire-pulse", "pipewire-alsa", "pipewire-jack",
        "wireplumber", "pipewire-audio", NULL
    };
    int ok;

    if (strcmp(osr_mod_pkg(), "pacman") != 0) {
        osr_info("PipeWire swap is Arch-only - skipping");
        return 1;
    }
    ok = osr_pkg_remove_step("Removing PulseAudio/JACK", old);
    return osr_pkg_install_step("Installing PipeWire + WirePlumber", pkgs) && ok;
}
