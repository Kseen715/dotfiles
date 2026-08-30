/* modules/hypridle.c -- hypridle idle daemon + config. ONE copy, POSIX
 * (was .../modules/hypridle.sh).
 *
 * Was modules/hypridle.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_hypridle(void) {
    static const char *const pkgs[] = { "hypridle", NULL };
    Str src, dst;
    int ok;

    ok = osr_pkg_install_step("Installing hypridle", pkgs);
    /* The theme's own file, not a rendered template: this config is one block
     * of colours and nothing else reads it, so the theme owns the whole file --
     * and a theme that ships none leaves the package default alone. */
    if (*osr_mod_theme_dir() != '\0') {
        str_init(&src); str_init(&dst);
        str_addz(&src, osr_mod_theme_dir());
        str_addz(&src, "/config/hypr/hypridle.conf");
        str_addz(&dst, osr_mod_home());
        str_addz(&dst, "/.config/hypr/hypridle.conf");
        if (file_exists(str_text(&src)))
            ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
        str_free(&src); str_free(&dst);
    }
    return ok;
}
