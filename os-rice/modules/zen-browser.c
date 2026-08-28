/* modules/zen-browser.c -- Zen Browser (AUR). ONE copy, POSIX
 * (was .../apps/zen-browser.sh). Available module.
 * Chrome colors: Zen is a Firefox fork, so it takes the same userChrome.css the
 * firefox module installs - one template, both browsers (§6b). Profiles live
 * under ~/.zen rather than ~/.mozilla/firefox; install_mozilla_layer resolves
 * either from profiles.ini.
 *
 * Port of modules/zen-browser.sh, kept as the reference at
 * test/ref/zen-browser_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"
#include "../lib/render.h"

#include <stddef.h>
#include <unistd.h>

int osrm_zen_browser(void) {
    static const char *const pkgs[] = { "zen-browser", NULL };
    Str css, root;
    int is_temp = 0;
    int ok;

    ok = osr_pkg_install_step("Installing Zen Browser (AUR)", pkgs);

    /* Zen is a Firefox fork and reads the same userChrome.css, so it takes the
     * FIREFOX layer rather than one of its own - there is nothing about the
     * chrome that differs. */
    str_init(&css);
    if (osr_theme_source(&css, "firefox", "userChrome.css", &is_temp)) {
        str_init(&root);
        str_addz(&root, osr_mod_home());
        str_addz(&root, "/.zen");
        ok = osr_install_mozilla_layer(str_text(&root), "", str_text(&css)) && ok;
        str_free(&root);
        if (is_temp) (void)unlink(str_text(&css));
    }
    str_free(&css);
    return ok;
}
