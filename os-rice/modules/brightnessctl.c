/* modules/brightnessctl.c -- backlight + sensors (i3-sugg §7.3/§7.4).
 * brightnessctl is the one that still works on modern kernels (xbacklight talks
 * to a randr property most drivers no longer expose); ddcutil does the same over
 * DDC/CI for external monitors.
 *
 * lm_sensors feeds the bar's temperature module - run `sensors-detect` once
 * after installing. Battery/thermal policy (upower, acpid, tlp, thermald) is
 * modules/power.sh; this module is only about the two things you point at a
 * specific device: its backlight and its sensors.
 *
 * Port of modules/brightnessctl.sh, kept as the reference at
 * test/ref/brightnessctl_sh_ref.sh. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_brightnessctl(void) {
    static const char *const pkgs[] = {
        "brightnessctl", "ddcutil", "lm_sensors", NULL
    };
    return osr_pkg_install_step("Installing backlight + sensors", pkgs);
}
