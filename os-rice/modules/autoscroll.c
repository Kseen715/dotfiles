/* modules/autoscroll.c -- middle-click autoscroll in every browser, the way
 * Windows does it: hold the wheel button down and the page scrolls with the
 * pointer.
 *
 * Nothing on Linux ships this on by default, and the reason is the same in both
 * engine families: X11 gave the middle button to primary-selection paste, so
 * both engines wired the button to paste and left autoscroll off.
 *
 *   Firefox family   three prefs. general.autoScroll turns the behaviour on;
 *                    middlemouse.paste and middlemouse.contentLoadURL are what
 *                    the button does INSTEAD today, and leaving them on means a
 *                    middle click in a page pastes the selection into the
 *                    search box or loads it as a URL. The prefs are also in
 *                    dotfiles config/firefox/user.js, which the firefox module
 *                    installs whole; they are ensured line by line here so a
 *                    profile the firefox module never touched (Zen, or a box
 *                    riced without it) still gets them, in whichever order the
 *                    two modules run.
 *
 *   Chromium family  no pref and no chrome://flags entry: the Blink feature is
 *                    reachable only from the command line, so it goes into the
 *                    launcher the same way the low-RAM switches do (SS5), which
 *                    means it applies to a browser started from the menu/rofi
 *                    /a link handler -- not to one typed into a terminal.
 *
 * A browser that is not installed simply has no launcher and no profile, so it
 * costs nothing to name them all here.
 *
 * C89 + POSIX.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"

#include <stddef.h>

#define AS_BLINK "--enable-blink-features=MiddleClickAutoscroll"

/* Mozilla profile roots: the classic one, the XDG one Firefox 154+ uses, the
 * sandboxed builds, and Zen's own. Same list the firefox module resolves, plus
 * Zen -- spelled from OSR_HOME and not $XDG_CONFIG_HOME because this module can
 * run as root, where that variable points at /root. */
static const char *const moz_roots[] = {
    "/.mozilla/firefox",
    "/.config/mozilla/firefox",
    "/snap/firefox/common/.mozilla/firefox",
    "/.var/app/org.mozilla.firefox/.mozilla/firefox",
    "/.zen",
    NULL
};

static const char *const moz_prefs[] = {
    "user_pref(\"general.autoScroll\", true);",
    "user_pref(\"middlemouse.paste\", false);",
    "user_pref(\"middlemouse.contentLoadURL\", false);",
    NULL
};

/* Chromium-family launchers. Patterns rather than names: the same browser ships
 * as chromium.desktop on one distro and chromium-browser.desktop on the next. */
static const char *const chromium_desktops[] = {
    "*chromium*.desktop",
    "google-chrome*.desktop",
    "*vivaldi*.desktop",
    "*brave*.desktop",
    "*microsoft-edge*.desktop",
    "*yandex*browser*.desktop",
    NULL
};

int osrm_autoscroll(void) {
    Str root, js, profiles;
    size_t i, k;
    int moz = 0, chrome = 0;

    str_initv(&root, &js, &profiles, (Str *)NULL);
    for (i = 0; moz_roots[i] != NULL; i++) {
        size_t pos = 0;
        Line line;

        str_setz(&root, osr_mod_home(), moz_roots[i], (const char *)NULL);
        if (!dir_exists(str_text(&root))) continue;

        str_reset(&profiles);
        osr_mozilla_profiles(&profiles, str_text(&root));
        while (next_line(str_text(&profiles), profiles.len, &pos, &line)) {
            if (line.len == 0) continue;
            str_reset(&js);
            str_add(&js, line.start, line.len);
            str_addz(&js, "/user.js");
            for (k = 0; moz_prefs[k] != NULL; k++)
                (void)osr_ensure_line(str_text(&js), moz_prefs[k]);
            moz++;
        }
    }
    if (moz > 0)
        osr_infof("middle-click autoscroll: %d Firefox-family profile(s)", moz);
    str_freev(&root, &js, &profiles, (Str *)NULL);

    for (i = 0; chromium_desktops[i] != NULL; i++)
        chrome += osr_desktop_add_flags(chromium_desktops[i], AS_BLINK);
    if (chrome > 0)
        osr_infof("middle-click autoscroll: %d Chromium-family launcher(s)", chrome);

    /* Both halves found nothing: no browser is installed yet, or Firefox has
     * never been launched and so has no profile to write into. Say it, because
     * the module otherwise reports success having changed nothing. */
    if (moz == 0 && chrome == 0)
        osr_warn("no browser profile or launcher found - middle-click autoscroll "
                 "is not applied anywhere (install a browser, launch it once, "
                 "then rerun this module)");
    /* An already-running browser reads neither its launcher nor user.js again. */
    osr_warn("restart the browser for middle-click autoscroll to take effect");
    return 1;
}
