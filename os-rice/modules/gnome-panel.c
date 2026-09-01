/* modules/gnome-panel.c -- turn the GNOME top bar into a waybar/polybar-class
 * status bar: system metrics, a clock with seconds, and weather.
 *
 * A tiling rice writes its own bar (waybar on Wayland, polybar on X11) and puts
 * whatever it likes in it. A GNOME session has no such file: the panel is
 * GNOME Shell itself, and the only way to add a module to it is a Shell
 * extension. So the three things a bar is expected to show come from three
 * different places here, and none of them is a config this repo can own:
 *
 *   metrics   Astra Monitor -- CPU load and per-core, load average, temperature
 *             (lm_sensors/hwmon), GPU, RAM, swap, disk I/O and free space,
 *             network throughput. One extension covering every module a
 *             polybar config would list separately, configured in its own prefs
 *             dialog rather than a text file.
 *   weather   SimpleWeather -- the maintained successor to OpenWeather Refined,
 *             which upstream has retired.
 *   clock     org.gnome.desktop.interface. Seconds, weekday and date in the
 *             panel clock are plain gsettings keys, so no extension is needed
 *             and none is installed for them.
 *
 * The packages are what the metrics half reads: lm_sensors provides the
 * temperature sensors (without it the temperature modules have nothing to
 * report), libgtop the per-process accounting behind the "top processes" lists.
 * Both are optional to the extension, hence the non-fatal install step.
 *
 * GNOME only, and silently inert elsewhere: an extension unpacked next to a
 * session that has no Shell to load it, and interface keys written into a
 * database nothing reads, are both worse than doing nothing -- they look like
 * success. Extensions load on the NEXT session, so a live session needs a
 * logout (Wayland) or Alt+F2 r (X11) before the modules appear.
 *
 * C89 + POSIX.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/gnome.h"

#include <stddef.h>

#define AM_UUID "monitor@astraext.github.io"
#define SW_UUID "simple-weather@romanlefler.com"

#define IFACE "org.gnome.desktop.interface"

/* The clock detail a status bar is expected to carry. GNOME shows a bare
 * "Sep 1 14:32" by default: no seconds, and the seconds are the whole reason
 * anybody watches a bar clock rather than asking the system. */
static const char *const CLOCK_KEYS[] = {
    "clock-show-seconds",
    "clock-show-weekday",
    "clock-show-date",
    NULL
};

static int clock_detail(void *ctx) {
    char *argv[6];
    size_t i;
    int ok = 1;

    (void)ctx;
    for (i = 0; CLOCK_KEYS[i] != NULL; i++) {
        argv[0] = (char *)"gsettings";
        argv[1] = (char *)"set";
        argv[2] = (char *)IFACE;
        argv[3] = (char *)CLOCK_KEYS[i];
        argv[4] = (char *)"true";
        argv[5] = NULL;
        if (osr_run_user(argv) != 0) ok = 0;
    }
    return ok;
}

int osrm_gnome_panel(void) {
    static const char *const sensors[] = { "lm_sensors", "libgtop", NULL };
    int ok;

    if (!osr_gnome_is_session()) return 1;

    /* Optional to the extension: it degrades to fewer sensors rather than
     * failing, so a distro missing either name must not fail the module. */
    (void)osr_pkg_install_step_try("Installing panel sensor libraries", sensors);

    /* The clock first, and deliberately: it is the half that needs no network
     * and no Shell restart, so it still lands when extensions.gnome.org is
     * unreachable. */
    ok = osr_step("Showing seconds, weekday and date in the clock",
                  clock_detail, NULL);
    if (!osr_gnome_extension_install("Installing Astra Monitor", AM_UUID)) ok = 0;
    if (!osr_gnome_extension_install("Installing SimpleWeather", SW_UUID)) ok = 0;

    osr_infof("  panel modules load on the next session -- log out and back in "
              "(Wayland) or Alt+F2 r (X11)");
    return ok;
}
