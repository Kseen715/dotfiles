/* modules/picom.c -- X11 compositor (i3-sugg §1.3). Mandatory, not cosmetic:
 * without a compositor you get tearing, no transparency, Electron/Chromium
 * flicker and broken shadows.
 *
 * Config split (§5): the dotfiles base carries behaviour (backend, vsync, blur
 * method, exclusion lists) and ends with `@include "90-theme.conf"`, which the
 * rice owns — corner radius, opacity, shadow color. picom resolves @include
 * relative to the including file, so both live in ~/.config/picom/.
 * launch.sh, not `picom --daemon` in the i3 config: the glx backend picom.conf
 * asks for is not available on every GPU, and picom EXITS when it cannot get it.
 * The launcher retries on xrender so the session is never left uncomposited —
 * which under i3 is not a cosmetic loss (opaque rofi corners, dead terminal
 * transparency, no shadows). See picom/launch.sh.
 *
 * Was modules/picom.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"

#include <stddef.h>

int osrm_picom(void) {
    static const char *const pkgs[] = { "picom", NULL };
    Str src, dst;
    char *argv[4];
    int ok;

    ok = osr_pkg_install_step("Installing picom", pkgs);

    str_initv(&src, &dst, (Str *)NULL);
    str_addzz(&src, osr_mod_dotfiles(), "/picom/picom.conf", (const char *)NULL);
    str_addzz(&dst, osr_mod_home(), "/.config/picom/picom.conf", (const char *)NULL);
    if (file_exists(str_text(&src)))
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;

    str_reset(&src);
    str_reset(&dst);
    str_addzz(&src, osr_mod_dotfiles(), "/picom/launch.sh", (const char *)NULL);
    str_addzz(&dst, osr_mod_home(), "/.config/picom/launch.sh", (const char *)NULL);
    if (file_exists(str_text(&src))) {
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
        (void)osr_chmod("+x", dst.p, 0);
    }

    str_reset(&src);
    str_setz(&dst, osr_mod_home(), "/.config/picom/90-theme.conf", (const char *)NULL);
    if (*osr_mod_theme_dir() != '\0') {
        str_addzz(&src, osr_mod_theme_dir(), "/config/picom/90-theme.conf", (const char *)NULL);
    }
    if (src.len > 0 && file_exists(str_text(&src))) {
        ok = osr_install_layer(str_text(&src), str_text(&dst)) && ok;
    } else {
        /* The base @includes it unconditionally, so a rice that ships no picom
         * theme must still leave a readable file behind or picom refuses to
         * start - no compositor, and nothing saying why. */
        ok = osr_seed_empty(str_text(&dst)) && ok;
    }
    str_freev(&src, &dst, (Str *)NULL);
    return ok;
}
