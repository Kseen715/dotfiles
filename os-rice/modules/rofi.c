/* modules/rofi.c -- rofi launcher, the X11 replacement for wofi (i3-sugg §2).
 * Also the app switcher, the emoji picker and the logout menu, so it replaces
 * wleave/wlogout too — a rofi-modi script, not another package.
 *
 * Config split (§5): config.rasi + the launcher/powermenu layouts are
 * dotfiles-owned; colors.rasi is rice-owned and `@import`ed by both, so a rice
 * switch recolors every rofi surface at once.
 *
 * Port of modules/rofi.sh, kept as the reference at
 * test/ref/rofi_sh_ref.sh. C89.
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

    /* The layout files are the dotfiles'; only colors.rasi is the theme's, and
     * rofi's own @import in config.rasi is what pulls it in. */
    str_init(&src); str_init(&dst);
    for (i = 0; files[i] != NULL; i++) {
        str_reset(&src); str_reset(&dst);
        str_addz(&src, osr_mod_dotfiles()); str_addz(&src, "/rofi/");
        str_addz(&src, files[i]);
        str_addz(&dst, str_text(&dir)); str_addc(&dst, '/'); str_addz(&dst, files[i]);
        if (file_exists(str_text(&src)))
            ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
    }
    str_reset(&dst);
    str_addz(&dst, str_text(&dir)); str_addz(&dst, "/colors.rasi");
    (void)osr_install_theme_layer("rofi", "colors.rasi", str_text(&dst));

    str_free(&dir); str_free(&src); str_free(&dst);
    return ok;
}
