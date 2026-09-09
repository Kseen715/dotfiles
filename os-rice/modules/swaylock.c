/* modules/swaylock.c -- swaylock screen locker + rice-owned config. ONE copy,
 * POSIX (was .../modules/swaylock.sh). Alternative locker (gtklock is default);
 * kept as an available module.
 *
 * Was modules/swaylock.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_swaylock(void) {
    static const char *const pkgs[] = { "swaylock", NULL };
    Str src, dst;
    int ok;

    ok = osr_pkg_install_step("Installing swaylock", pkgs);
    /* The theme's own file, not a rendered template: this config is one block
     * of colours and nothing else reads it, so the theme owns the whole file --
     * and a theme that ships none leaves the package default alone. */
    if (*osr_mod_theme_dir() != '\0') {
        str_initv(&src, &dst, (Str *)NULL);
        str_addzz(&src, osr_mod_theme_dir(), "/config/swaylock/config", (const char *)NULL);
        str_addzz(&dst, osr_mod_home(), "/.config/swaylock/config", (const char *)NULL);
        if (file_exists(str_text(&src)))
            ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
        str_freev(&src, &dst, (Str *)NULL);
    }
    return ok;
}
