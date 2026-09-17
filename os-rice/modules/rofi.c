/* modules/rofi.c -- rofi launcher, the X11 replacement for wofi (i3-sugg §2).
 * Also the app switcher and the logout menu, so it replaces wleave/wlogout
 * too — a rofi-modi script, not another package.
 *
 * Config split (§5): config.rasi + the launcher/powermenu layouts are
 * dotfiles-owned; colors.rasi is rice-owned and `@import`ed by both, so a rice
 * switch recolors every rofi surface at once.
 *
 * Was modules/rofi.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/gnome.h"

#include <stddef.h>

int osrm_rofi(void) {
    /* rofi alone. The rofi-emoji and rofi-calc plugins are 1.7-ABI and are in
     * no repo that ships rofi 2.x; emoji is rofimoji's job (modules/fcitx5.c)
     * and nothing here ever asked for calc. */
    static const char *const pkgs[] = { "rofi", NULL };
    static const char *const files[] = {
        "config.rasi", "launcher.rasi", "powermenu.rasi", NULL
    };
    Str dir, src, dst;
    size_t i;
    int ok;

    ok = osr_pkg_install_step("Installing rofi", pkgs);

    str_init(&dir);
    str_addzz(&dir, osr_mod_home(), "/.config/rofi", (const char *)NULL);
    ok = osr_mkdir_p(str_text(&dir)) && ok;

    /* The layouts are the dotfiles' unless this rice ships its own. rofi has no
     * cascade past @import - colors.rasi is imported at the top of a layout, so
     * a property set there loses to the same property in the layout's own
     * blocks - which means a rice that wants a different border-radius (or any
     * other structural value) has no way to express it except a whole layout.
     * i3-rosemary is square-cornered and does exactly that. */
    str_initv(&src, &dst, (Str *)NULL);
    for (i = 0; files[i] != NULL; i++) {
        str_reset(&src);
        str_setz(&dst, str_text(&dir), "/", files[i], (const char *)NULL);
        if (osr_install_theme_layer("rofi", files[i], str_text(&dst))) continue;
        str_addzz(&src, osr_mod_dotfiles(), "/rofi/", files[i], (const char *)NULL);
        if (file_exists(str_text(&src)))
            ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
    }
    str_setz(&dst, str_text(&dir), "/colors.rasi", (const char *)NULL);
    (void)osr_install_theme_layer("rofi", "colors.rasi", str_text(&dst));

    /* Super+R on a GNOME session. rofi is the fastest launcher that actually
     * opens under mutter: ~100ms to a window against wofi's ~360ms with icons
     * off and ~850ms with them on. Its Wayland backend wants wlr-layer-shell,
     * which mutter does not implement ("Rofi on wayland requires support for
     * the layer shell protocol"), so the chord unsets WAYLAND_DISPLAY and rofi
     * takes the X11 path through XWayland - where icons cost nothing.
     *
     * -normal-window is not cosmetic, it is what makes the launcher usable.
     * rofi's default X11 window is override-redirect: unmanaged, invisible to
     * mutter, and typed on only because rofi holds an XGrabKeyboard. That grab
     * covers XWayland, not the compositor - so with a NATIVE Wayland client
     * focused (ghostty, nautilus) mutter never routes a key into XWayland and
     * every keystroke lands in the window behind rofi. It only looked fine
     * from an X11 client (VS Code, the browser), where input was already
     * going there. As a normal window mutter manages and focuses it like any
     * other toplevel, and rofi still maps it undecorated and centred
     * (_MOTIF_WM_HINTS decorations=0, no _NET_FRAME_EXTENTS).
     *
     * wofi's custom shortcut has to be removed, not just unbound: two custom
     * shortcuts on one chord is a state gsettings accepts and GNOME resolves
     * arbitrarily. Last launcher module installed owns Super+R; wofi does the
     * same to rofi. */
    if (osr_gnome_is_session()) {
        osr_info("rofi unbind Super+R from GNOME Shell");
        (void)osr_gnome_unkeybind("wofi");
        (void)osr_gnome_free_binding("<Super>r");
        osr_info("rofi Super+R shortcut");
        (void)osr_gnome_keybind("rofi", "Application Launcher", "<Super>r",
                                "sh -c 'pkill rofi || env -u WAYLAND_DISPLAY rofi -normal-window -show drun'");
    }

    str_freev(&dir, &src, &dst, (Str *)NULL);
    return ok;
}
