/* modules/pulseaudio.c -- PulseAudio + JACK audio stack, replacing PipeWire.
 * ONE copy, POSIX. Mirror image of modules/pipewire.sh: the two are mutually
 * exclusive, listing one removes the other. Arch package names, pacman-only.
 * ponytail: the pipewire *core* is deliberately kept — xdg-desktop-portal-wlr
 * (screen sharing) depends on it, so `pacman -R pipewire` would fail or break
 * the Wayland session. Only the PulseAudio/JACK/ALSA replacement shims and the
 * session manager go, which is what actually hands audio back to PulseAudio.
 *
 * Was modules/pulseaudio.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>
#include <string.h>

int osrm_pulseaudio(void) {
    static const char *const shims[] = {
        "pipewire-pulse", "pipewire-jack", "pipewire-alsa", "pipewire-audio",
        "wireplumber", NULL
    };
    static const char *const pkgs[] = {
        "pulseaudio", "pulseaudio-alsa", "pulseaudio-jack", "pavucontrol", "jack2", NULL
    };
    int ok;

    if (strcmp(osr_mod_pkg(), "pacman") != 0) {
        osr_info("PulseAudio swap is Arch-only - skipping");
        return 1;
    }
    /* Removal first: the two stacks provide the same sockets, and installing
     * over the other one leaves whichever the package manager resolved last. */
    ok = osr_pkg_remove_step("Removing PipeWire audio shims", shims);
    return osr_pkg_install_step("Installing PulseAudio + JACK", pkgs) && ok;
}
