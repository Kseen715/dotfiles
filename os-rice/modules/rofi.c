/* modules/rofi.c -- rofi launcher, the X11 replacement for wofi (i3-sugg §2).
 * Also the app switcher, the emoji picker and the logout menu, so it replaces
 * wleave/wlogout too — a rofi-modi script, not another package.
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

#include <stddef.h>

int osrm_rofi(void) {
    static const char *const pkgs[] = { "rofi", "rofi-emoji", "rofi-calc", NULL };
    static const char *const files[] = {
        "config.rasi", "launcher.rasi", "powermenu.rasi", NULL
    };
    Str dir, src, dst;
    size_t i;
    int ok;

    ok = osr_pkg_install_step("Installing rofi", pkgs);

    str_init(&dir);
    str_addz(&dir, osr_mod_home());
    str_addz(&dir, "/.config/rofi");
    ok = osr_mkdir_p(str_text(&dir)) && ok;

    /* The layouts are the dotfiles' unless this rice ships its own. rofi has no
     * cascade past @import - colors.rasi is imported at the top of a layout, so
     * a property set there loses to the same property in the layout's own
     * blocks - which means a rice that wants a different border-radius (or any
     * other structural value) has no way to express it except a whole layout.
     * i3-rosemary is square-cornered and does exactly that. */
    str_init(&src); str_init(&dst);
    for (i = 0; files[i] != NULL; i++) {
        str_reset(&src); str_reset(&dst);
        str_addz(&dst, str_text(&dir)); str_addc(&dst, '/'); str_addz(&dst, files[i]);
        if (osr_install_theme_layer("rofi", files[i], str_text(&dst))) continue;
        str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/rofi/");
        str_addz(&src, files[i]);
        if (file_exists(str_text(&src)))
            ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
    }
    str_reset(&dst);
    str_addz(&dst, str_text(&dir)); str_addz(&dst, "/colors.rasi");
    (void)osr_install_theme_layer("rofi", "colors.rasi", str_text(&dst));

    str_free(&dir); str_free(&src); str_free(&dst);
    return ok;
}
