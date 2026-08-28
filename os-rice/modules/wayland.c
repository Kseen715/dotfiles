/* modules/wayland.c -- Wayland stack + XWayland + Qt/GTK layer-shell libs that the
 * Hyprland session builds on. ONE copy, POSIX (was .../modules/wayland.sh). Pure
 * package install, no config. Names are Arch packages (rice is Arch-only);
 * duplicates in the legacy list have been removed.
 *
 * Port of modules/wayland.sh, kept as the reference at
 * test/ref/wayland_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_wayland(void) {
    static const char *const pkgs[] = {
        "xorg-xwayland", "xorg-xlsclients", "qt6-base", "qt6-5compat", "qt5-wayland",
        "layer-shell-qt5", "qt6-wayland", "layer-shell-qt", "glfw-wayland", "gtk3",
        "gtk-layer-shell", "gtk4", "gtk4-layer-shell", "meson", "wayland", "libxcb",
        "xcb-util-wm", "xcb-util-keysyms", "pango", "cairo", "libinput", "libglvnd",
        "uwsm", "wayland-protocols", "wayland-utils", "wl-clipboard",
        "xdg-desktop-portal", "xdg-desktop-portal-gtk", "xdg-desktop-portal-wlr",
        "xdg-utils", "wlr-protocols", NULL
    };
    return osr_pkg_install_step("Installing Wayland stack", pkgs);
}
