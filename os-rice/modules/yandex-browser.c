/* modules/yandex-browser.c -- Yandex Browser + a low-RAM flags layer. ONE copy,
 * POSIX.
 *
 * Install route by target (§1, all of it in pkgmap):
 *   pacman  aur:yandex-browser
 *   apt     source:provide_yandex_browser -- the vendor's own apt repo, which no
 *           Debian/Ubuntu archive carries
 *   xbps    source:provide_yandex_browser_deb -- Void packages it nowhere and
 *           the vendor ships deb/rpm only, so the .deb is unpacked into /opt
 * Anything else has no package and fails loudly rather than installing a
 * lookalike, same convention as vscode on apt.
 *
 * Config split (§5). Yandex Browser is Chromium, so there is no user.js to
 * write: the memory knobs are command-line switches, and the only place a switch
 * can be attached without touching /usr is the .desktop entry. dotfiles owns the
 * switch list (yandex-browser/flags.conf) and this module stamps it into a
 * user-level copy of each launcher in ~/.local/share/applications, which XDG
 * resolves before the packaged one. No rice layer: Chromium takes no user
 * stylesheet.
 *
 * Consequence worth knowing: the flags reach the browser when it is started from
 * the menu/rofi/a mailto handler, i.e. every normal launch. Typing
 * `yandex-browser` in a terminal bypasses the .desktop entry and gets defaults.
 *
 * Was modules/yandex-browser.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"
#include <stddef.h>

/* flatten_flags -- the sh module's sed/tr pipeline: one switch per line with
 * `#` comments, the comments dropped, the lines joined, runs of spaces squeezed
 * and the ends trimmed -- a single argument string. */
static void flatten_flags(Str *out, const char *text, size_t len) {
    Str joined;
    const char *p;
    size_t i;
    int space = 0;

    str_init(&joined);
    for (i = 0; i < len; i++) {
        if (text[i] == '#') {                       /* to the end of that line */
            while (i < len && text[i] != '\n') i++;
            if (i >= len) break;
        }
        str_addc(&joined, text[i] == '\n' ? ' ' : text[i]);
    }
    /* tr -s ' ': runs of spaces collapse; then the ends are trimmed. */
    p = str_text(&joined);
    for (i = 0; p[i] != '\0'; i++) {
        if (p[i] == ' ') { space = 1; continue; }
        if (space && out->len > 0) str_addc(out, ' ');
        space = 0;
        str_addc(out, p[i]);
    }
    str_free(&joined);
}

int osrm_yandex_browser(void) {
    static const char *const pkgs[] = { "yandex-browser", NULL };
    Str path, flags;
    char *buf;
    size_t len;
    int ok;

    ok = osr_pkg_install_step("Installing Yandex Browser", pkgs);

    str_init(&path);
    str_addzz(&path, osr_mod_dotfiles(), "/yandex-browser/flags.conf", (const char *)NULL);
    buf = slurp(str_text(&path), &len);
    if (buf == NULL) { str_free(&path); return ok; }

    str_init(&flags);
    flatten_flags(&flags, buf, len);
    free(buf);

    /* Two entries ship with the deb under different names -- the reverse-DNS
     * ru.yandex.desktop.browser.desktop and the plain yandex-browser.desktop
     * (the one that is the http handler). Both exec the same binary and both
     * get the flags: which one a launcher picks is not ours to predict.
     *
     * The browser is installed either way, but without a launcher nothing
     * carries the flags -- say so instead of leaving the tuning silently
     * unapplied. */
    if (osr_desktop_add_flags("*yandex*browser*.desktop", str_text(&flags)) == 0)
        osr_warn("no yandex-browser .desktop found - the low-RAM flags are not "
                 "applied (rerun this module after the install)");

    str_freev(&path, &flags, (Str *)NULL);
    return ok;
}
