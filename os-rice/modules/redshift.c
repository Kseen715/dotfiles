/* modules/redshift.c -- color temperature, the X11 replacement for luminance /
 * gammastep (i3-sugg §2). Config is dotfiles-owned: it is a personal preference
 * (latitude, day/night temperature), not a rice theme, so a rice switch leaves
 * it alone.
 *
 * Without a location redshift refuses to start, so the shipped config sets one
 * explicitly rather than relying on geoclue, which needs a D-Bus provider that
 * i3 does not run.
 *
 * Was modules/redshift.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_redshift(void) {
    static const char *const pkgs[] = { "redshift", NULL };
    Str src, dst;
    int ok;

    ok = osr_pkg_install_step("Installing redshift", pkgs);
    str_init(&src); str_init(&dst);
    str_addz(&src, osr_mod_dotfiles());
    str_addz(&src, "/redshift/redshift.conf");
    str_addz(&dst, osr_mod_home());
    str_addz(&dst, "/.config/redshift.conf");
    if (file_exists(str_text(&src)))
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
    str_free(&src); str_free(&dst);
    return ok;
}
