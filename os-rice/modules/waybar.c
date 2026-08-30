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
    char *argv[4];
    int ok;

    ok = osr_pkg_install_step("Installing Waybar", pkgs);
    if (*osr_mod_theme_dir() == '\0') return ok;

    str_init(&dir);
    str_addz(&dir, osr_mod_theme_dir());
    str_addz(&dir, "/config/waybar");
    if (!dir_exists(str_text(&dir))) { str_free(&dir); return ok; }

    str_init(&src); str_init(&dst);
    str_addz(&src, str_text(&dir)); str_addz(&src, "/config.jsonc");
    str_addz(&dst, osr_mod_home());  str_addz(&dst, "/.config/waybar/config.jsonc");
    ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;

    /* The stylesheet is the palette half and comes from the shared template
     * (§6b); config.jsonc and the ddc script are this rice's own layout. */
    str_reset(&dst);
    str_addz(&dst, osr_mod_home()); str_addz(&dst, "/.config/waybar/style.css");
    if (!osr_install_theme_layer("waybar", "style.css", str_text(&dst))) {
        str_reset(&src);
        str_addz(&src, str_text(&dir)); str_addz(&src, "/style.css");
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
    }

    str_reset(&src);
    str_addz(&src, str_text(&dir)); str_addz(&src, "/waybar-ddc-module.sh");
    str_reset(&dst);
    str_addz(&dst, osr_mod_home()); str_addz(&dst, "/.config/waybar/waybar-ddc-module.sh");
    ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
    argv[0] = (char *)"chmod"; argv[1] = (char *)"+x"; argv[2] = dst.p; argv[3] = NULL;
    (void)osr_run_user(argv);

    str_free(&dir); str_free(&src); str_free(&dst);
    return ok;
}
