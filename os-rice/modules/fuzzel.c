/* modules/fuzzel.c -- fuzzel application launcher + theme-owned config. ONE
 * copy, POSIX. The fast twin of modules/wofi.c: same job, same palette, a
 * tenth of the startup.
 *
 * WHY, measured on resolute (176 .desktop entries, Papirus-Dark), wall clock
 * to a drawn window:
 *
 *     wofi, icons on    ~850ms   (0.50s CPU)
 *     wofi, icons off   ~360ms   (0.23s CPU)
 *     fuzzel, icons on   ~30ms
 *
 * wofi is a GTK3 client: it pays GTK's init, then rasterises one SVG per entry
 * through the out-of-process glycin pixbuf loader before the first frame, with
 * no icon cache and no lazy row rendering, so every Super+R redoes all of it.
 * fuzzel is a plain Wayland client on cairo with nanosvg compiled in -- no
 * toolkit, icons decoded in-process.
 *
 * ---- WHERE IT RUNS, and where it does NOT -----------------------------------
 * fuzzel is a wlr-layer-shell client with no fallback path. On a compositor
 * without that protocol it prints
 *
 *     compositor is missing support for the Wayland layer surface protocol
 *
 * and exits without ever mapping a window. mutter does not implement it and
 * never has, so fuzzel cannot run on a GNOME session AT ALL -- not slowly, not
 * degraded: not at all. wofi is a layer-shell client too, but it falls back to
 * an ordinary toplevel ("switching to normal window mode"), which is why wofi
 * is what modules/wofi.c binds to Super+R under GNOME and this module binds
 * nothing there.
 *
 * So the split between the two launcher modules is by compositor, not by
 * taste: fuzzel for the wlroots rices (Hyprland), wofi for GNOME and as the
 * portable fallback. A rice manifest naming both gets the package installed
 * and the config written either way; only the GNOME shortcut differs.
 *
 * The package is native on every supported archive (Ubuntu universe carries
 * 1.12.0 on resolute), so pkgmap needs no row: `fuzzel` resolves as-is on apt,
 * pacman, dnf, xbps and apk -- the same as `wofi`.
 *
 * ---- fuzzel config (theme-owned) --------------------------------------------
 * One file, fuzzel.ini, carrying both what wofi split across config and
 * style.css: fuzzel has no stylesheet. config/fuzzel/fuzzel.ini.tmpl documents
 * the two parts of the wofi look that do not survive the move.
 *
 * C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/gnome.h"

#include <stddef.h>

int osrm_fuzzel(void) {
    static const char *const pkgs[] = { "fuzzel", NULL };
    Str dst;
    int ok;

    ok = osr_pkg_install_step("Installing fuzzel", pkgs);

    /* Deliberately no osr_gnome_keybind here. Registering Super+R would take
     * the chord away from wofi -- the launcher that DOES open under mutter --
     * and point it at a binary that exits on sight. A shortcut that does
     * nothing is worse than no shortcut: the package and its config still land
     * (the box may boot a Hyprland session later), the binding does not. */
    if (osr_gnome_is_session()) {
        osr_warn("fuzzel needs wlr-layer-shell, which mutter does not "
                 "implement - it cannot open in a GNOME session");
        osr_info("  leaving Super+R with wofi; use fuzzel from a wlroots "
                 "session (Hyprland)");
    }

    str_init(&dst);
    str_addzz(&dst, osr_mod_home(), "/.config/fuzzel/fuzzel.ini", (const char *)NULL);
    (void)osr_install_theme_layer("fuzzel", "fuzzel.ini", str_text(&dst));
    str_free(&dst);
    return ok;
}
