/* modules/micro.c -- micro terminal editor + layered config. ONE copy, POSIX,
 * distro-agnostic (native everywhere).
 *
 * Config split (§5): micro keeps everything in one settings.json and has no
 * include, so the split is by composition — the dotfiles base carries behaviour
 * and the rice fragment carries the `colorscheme` key naming the palette file it
 * also ships (compose_json_config merges the two).
 * The colorscheme file itself (theme-owned, swapped on switch §6). Named after
 * the theme, which is also what the settings.json fragment selects, so two
 * themes' schemes coexist in the same colorschemes dir.
 *
 * Was modules/micro.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"
#include "../lib/render.h"

#include <stddef.h>
#include <unistd.h>

int osrm_micro(void) {
    static const char *const pkgs[] = { "micro", NULL };
    Str dir, base, dst, frag;
    int is_temp = 0;
    int ok;

    ok = osr_pkg_install_step("Installing micro", pkgs);

    str_init(&dir); str_init(&base); str_init(&dst); str_init(&frag);
    str_addz(&dir, osr_mod_home()); str_addz(&dir, "/.config/micro");
    str_addz(&base, osr_mod_dotfiles()); str_addz(&base, "/micro/settings.json");
    /* settings.json is ONE file micro rewrites itself, so the rice's keys and
     * the theme's are composed into it rather than either owning it. */
    if (file_exists(str_text(&base))) {
        (void)osr_theme_source(&frag, "micro", "settings.json", &is_temp);
        str_addz(&dst, str_text(&dir)); str_addz(&dst, "/settings.json");
        ok = osr_compose_json_config(str_text(&base), str_text(&frag),
                                     str_text(&dst)) && ok;
        if (is_temp) (void)unlink(str_text(&frag));
    }
    /* The colorscheme is a file per theme, named after it: micro loads it by
     * the name settings.json points at. */
    if (*osr_mod_theme() != '\0') {
        str_reset(&dst);
        str_addz(&dst, str_text(&dir)); str_addz(&dst, "/colorschemes/");
        str_addz(&dst, osr_mod_theme()); str_addz(&dst, ".micro");
        (void)osr_install_theme_layer("micro", "theme.micro", str_text(&dst));
    }
    str_free(&dir); str_free(&base); str_free(&dst); str_free(&frag);
    return ok;
}
