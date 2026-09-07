/* modules/plasma.c -- KDE Plasma, the whole desktop: kwin, plasmashell, the
 * Plasma session .desktop files for both X11 and Wayland.
 *
 * Pure package install, no config. A Plasma session is configured from inside
 * itself (systemsettings writes ~/.config/plasma*rc), so there is nothing here
 * for a module to lay down that the first login would not overwrite; the theme
 * layer for KDE is kde/color-scheme.colors.tmpl, applied by hand today.
 *
 * WHAT STARTS IT is somebody else's job, and deliberately: a workstation gets
 * one from modules/sddm.c, and a headless box gets one from modules/weston-rdp.c
 * (which autolaunches startplasma-wayland inside the RDP compositor). Installing
 * a desktop and choosing how it is entered are two decisions.
 *
 * C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_plasma(void) {
    static const char *const pkgs[] = { "plasma-desktop", "plasma-wayland", NULL };
    return osr_pkg_install_step("Installing KDE Plasma", pkgs);
}
