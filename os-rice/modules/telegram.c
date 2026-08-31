/* modules/telegram.c -- Telegram Desktop from the vendor tarball
 * (telegram.org/desktop), plus the theme palette.
 *
 * The tarball and not the distro package: the packaged builds lag, and Telegram
 * ships its own updater that keeps the unpacked tree current on its own (see
 * provide_telegram, which is why the tree is user-owned). webkit2gtk is the
 * system runtime its in-app browser view uses and is still a native package.
 *
 * provide_telegram is called directly rather than through `pkg_install telegram`
 * for the same reason as datagrip: the source: provider's probe is presence-based
 * (§4), so a rice would install it once and never update it, while the builder
 * compares versions - which makes `osr module telegram` the repair path.
 * Telegram paints its own widgets, so the GTK and Qt layers do nothing to it -
 * its only theming input is a .tdesktop-palette (§6b, telegram's *.tmpl).
 *
 * Was modules/telegram.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/build.h"

#include <stddef.h>

static int build_telegram(void *ctx) {
    (void)ctx;
    return osr_build_run("provide_telegram");
}

int osrm_telegram(void) {
    static const char *const webview[] = { "webkit2gtk-4.1", NULL };
    Str dir, dst;
    int ok;

    ok = osr_step("Installing Telegram", build_telegram, NULL);
    ok = osr_pkg_install_step("Installing the Telegram webview runtime", webview) && ok;

    str_init(&dir); str_init(&dst);
    str_addz(&dir, osr_mod_home()); str_addz(&dir, "/.local/share/TelegramDesktop");
    ok = osr_mkdir_p(str_text(&dir)) && ok;
    str_addz(&dst, str_text(&dir)); str_addz(&dst, "/os-rice.tdesktop-palette");

    if (osr_install_theme_layer("telegram", "os-rice.tdesktop-palette", str_text(&dst))) {
        /* Applying it is a click, and that is Telegram's limitation: the
         * selected theme lives in the encrypted tdata blob, so nothing outside
         * the app can select it. Not a command-line argument either - a file
         * path on the command line goes to the "send this file" flow.
         *
         * The click only has to happen ONCE, though: a theme applied from a real
         * file path is watched, and Telegram re-applies it whenever that file
         * changes. install_layer rewrites this path in place, so a later
         * `osr theme <name>` re-themes a RUNNING Telegram with no interaction. */
        osr_infof("Telegram theme written to %s", str_text(&dst));
        osr_info("apply it once inside Telegram: Settings > Chat Settings > Choose "
                 "from file, and pick that file (or send it to Saved Messages, "
                 "click it, 'Apply This Theme')");
        osr_info("after that Telegram watches the file - every later 'osr theme' "
                 "re-themes it live");
    }
    str_free(&dir); str_free(&dst);
    return ok;
}
