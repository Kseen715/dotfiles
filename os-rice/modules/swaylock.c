/* modules/swaylock.c -- swaylock screen locker + rice-owned config. ONE copy,
 * POSIX (was .../modules/swaylock.sh). Alternative locker (gtklock is default);
 * kept as an available module.
 *
 * Port of modules/swaylock.sh, kept as the reference at
 * test/ref/swaylock_sh_ref.sh. C89.
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
        str_init(&src); str_init(&dst);
        str_addz(&src, osr_mod_theme_dir());
        str_addz(&src, "/config/swaylock/config");
        str_addz(&dst, osr_mod_home());
        str_addz(&dst, "/.config/swaylock/config");
        if (file_exists(str_text(&src)))
            ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
        str_free(&src); str_free(&dst);
    }
    return ok;
}
