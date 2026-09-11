/* modules/waybar.c -- Waybar status bar + rice-owned config. ONE copy, POSIX
 * (was .../modules/waybar.sh). gsimplecal (calendar popup) and ddcutil (monitor
 * brightness via the custom ddc module) are companions the config invokes.
 *
 * Was modules/waybar.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_waybar(void) {
    static const char *const pkgs[] = { "waybar", "gsimplecal", "ddcutil", NULL };
    Str dir, src, dst;
    int ok;

    ok = osr_pkg_install_step("Installing Waybar", pkgs);
    if (*osr_mod_theme_dir() == '\0') return ok;

    str_init(&dir);
    str_addzz(&dir, osr_mod_theme_dir(), "/config/waybar", (const char *)NULL);
    if (!dir_exists(str_text(&dir))) { str_free(&dir); return ok; }

    str_initv(&src, &dst, (Str *)NULL);
    str_addzz(&src, str_text(&dir), "/config.jsonc", (const char *)NULL);
    str_addzz(&dst, osr_mod_home(), "/.config/waybar/config.jsonc", (const char *)NULL);
    ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;

    /* The stylesheet is the palette half and comes from the shared template
     * (§6b); config.jsonc and the ddc script are this rice's own layout. */
    str_setz(&dst, osr_mod_home(), "/.config/waybar/style.css", (const char *)NULL);
    if (!osr_install_theme_layer("waybar", "style.css", str_text(&dst))) {
        str_setz(&src, str_text(&dir), "/style.css", (const char *)NULL);
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
    }

    str_setz(&src, str_text(&dir), "/waybar-ddc-module.sh", (const char *)NULL);
    str_setz(&dst, osr_mod_home(), "/.config/waybar/waybar-ddc-module.sh", (const char *)NULL);
    ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
    (void)osr_chmod("+x", dst.p, 0);

    str_freev(&dir, &src, &dst, (Str *)NULL);
    return ok;
}
