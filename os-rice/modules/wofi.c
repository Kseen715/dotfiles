/* modules/wofi.c -- wofi application launcher + theme-owned config. ONE copy,
 * POSIX. Wayland-only by construction (wofi is a layer-shell client); the X11
 * half of the same job is modules/rofi.sh.
 *
 * Two homes, one module:
 * - Hyprland rices bind the launcher in the theme's hyprland.conf ($menu).
 * - A GNOME/Wayland session (Ubuntu resolute) has no compositor config to
 * write into, so the Super+R (Win+R) shortcut is registered through
 * gsettings via lib/gnome.sh — the same route modules/cliphist.sh takes for
 * Super+V.
 *
 * The package is native on every supported archive (Ubuntu universe carries
 * 1.5.1 on resolute), so pkgmap needs no row: `wofi` resolves as-is on apt,
 * pacman, dnf, xbps and apk.
 * ---- GNOME: Super+R -> wofi ------------------------------------------------
 * Helpers live in lib/gnome.sh (shared with cliphist's Super+V). The command is
 * a toggle, not a bare launch: wofi has no single-instance lock, so a second
 * Win+R on an open launcher would stack a second copy over the first.
 * `pkill wofi || wofi --show drun` closes the open one instead.
 * ---- wofi config (theme-owned) ----------------------------------------------
 *
 * Was modules/wofi.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/gnome.h"

#include <stddef.h>

int osrm_wofi(void) {
    static const char *const pkgs[] = { "wofi", NULL };
    Str dst;
    int ok;

    ok = osr_pkg_install_step("Installing wofi", pkgs);

    /* Under GNOME the Shell owns Super+R until the binding is freed, so the
     * launcher would simply never open. Elsewhere the WM config binds it. */
    if (osr_gnome_is_session()) {
        osr_info("wofi unbind Super+R from GNOME Shell");
        (void)osr_gnome_free_binding("<Super>r");
        osr_info("wofi Super+R shortcut");
        (void)osr_gnome_keybind("wofi", "Application Launcher", "<Super>r",
                                "sh -c 'pkill wofi || wofi --show drun'");
    }
    str_init(&dst);
    str_addz(&dst, osr_mod_home()); str_addz(&dst, "/.config/wofi/config");
    (void)osr_install_theme_layer("wofi", "config", str_text(&dst));
    str_reset(&dst);
    str_addz(&dst, osr_mod_home()); str_addz(&dst, "/.config/wofi/style.css");
    (void)osr_install_theme_layer("wofi", "style.css", str_text(&dst));
    str_free(&dst);
    return ok;
}
