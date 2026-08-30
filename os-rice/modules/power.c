/* modules/power.c -- battery, thermals and idle policy (i3-sugg §7.3).
 *
 * tlp is the pick; `power-profiles-daemon` and `auto-cpufreq` are the
 * alternatives and running two of them means two things fighting over the same
 * sysfs knobs, so this module *removes* power-profiles-daemon if it is present
 * rather than installing on top of it (the same mirror-module pattern as
 * pipewire/pulseaudio).
 *
 * batsignal is the battery alert: a bar module shows the percentage, but nothing
 * warns you at 5% unless a daemon does. thermald is Intel-only and is skipped
 * elsewhere — it does nothing on AMD and its absence is not a failure.
 *
 * Not packaged on Void (i3-void-packages.md): auto-cpufreq, optimus-manager,
 * envycontrol, nvidia-prime. For hybrid graphics, switcheroo-control is the
 * packaged option and is installed when two GPUs are detected.
 * thermald only ships Intel thermal tables; installing it on AMD is noise.
 * Hybrid graphics: switcheroo-control is the D-Bus service GTK apps use for
 * "Launch using Discrete Graphics Card".
 *
 * Was modules/power.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/service.h"

#include <stddef.h>
#include <string.h>

int osrm_power(void) {
    static const char *const pkgs[] = {
        "tlp", "tlp-rdw", "upower", "acpid", "powertop", "cpupower", "batsignal", NULL
    };
    static const char *const ppd[] = { "power-profiles-daemon", NULL };
    static const char *const thermal[] = { "thermald", NULL };
    static const char *const switcheroo[] = { "switcheroo-control", NULL };
    const char *cpu = env_str("OSR_CPU_MODEL", "");
    int ok;

    /* Never both: tlp and power-profiles-daemon each own the same knobs, and a
     * box with the pair installed flips between two policies at random. */
    if (osr_pkg_installed("power-profiles-daemon")) {
        osr_warn("power-profiles-daemon is installed - removing it before tlp (never both)");
        (void)osr_service_disable("power-profiles-daemon");
        if (!osr_pkg_remove(ppd)) osr_warn("could not remove power-profiles-daemon");
    }
    ok = osr_pkg_install_step("Installing power management", pkgs);

    if (strstr(cpu, "Intel") != NULL || strstr(cpu, "intel") != NULL) {
        ok = osr_pkg_install_step("Installing thermald (Intel)", thermal) && ok;
        if (!osr_service_enable("thermald")) osr_warn("could not enable thermald");
    } else {
        osr_info("non-Intel CPU - skipping thermald");
    }
    if (env_long("OSR_GPU_COUNT", 0) > 1)
        ok = osr_pkg_install_step("Installing switcheroo-control (hybrid GPU)",
                                  switcheroo) && ok;

    if (!osr_service_enable("tlp"))   osr_warn("could not enable tlp (needs a real init)");
    if (!osr_service_enable("acpid")) osr_warn("could not enable acpid (needs a real init)");
    return ok;
}
