/* modules/firefox.c -- Firefox + a low-RAM prefs layer + the rice's colors.
 *
 * Config split (§5), realized through Mozilla's two profile-level hooks:
 *
 *   user.js                 dotfiles-owned (10) -- the low-memory tuning.
 *                           Re-applied at every start, overwritten on update.
 *   chrome/userChrome.css   rice-owned (90) -- chrome colors, swapped on switch.
 *
 * Both land in EVERY profile (osr_install_mozilla_layer walks profiles.ini),
 * because a Mozilla profile directory has a random name and there is no fixed
 * path to install into.
 *
 * Why user.js and not prefs.js: prefs.js is rewritten by the browser on exit, so
 * anything written there is lost. user.js is read-only input, applied on top.
 *
 * The low-RAM set targets the two things that actually dominate Firefox's RSS on
 * a small machine: the number of content processes, and how many back/forward
 * page states are kept alive in memory. See dotfiles/firefox/user.js.
 *
 * Was modules/firefox.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"
#include "../lib/render.h"

#include <stddef.h>
#include <unistd.h>

int osrm_firefox(void) {
    static const char *const pkgs[] = { "firefox", NULL };
    /* Firefox's own profile-root resolution order, so os-rice writes into the
     * profile the browser will read rather than into the one it would have made.
     *
     *   ~/.mozilla/firefox          the classic root. Firefox still prefers it
     *                               when it exists, so it stays first.
     *   ~/.config/mozilla/firefox   XDG base directories, honoured by default as
     *                               of 154. A machine that first ran Firefox on
     *                               154+ has ONLY this one. Spelled from
     *                               OSR_HOME and not from $XDG_CONFIG_HOME on
     *                               purpose: this module runs as root, where
     *                               that variable points at /root.
     *   ~/snap, ~/.var/app          a sandboxed build keeps its profile inside
     *                               the sandbox and leaves every classic root
     *                               empty. Ubuntu's `firefox` deb is a snap stub.
     */
    static const char *const alts[] = {
        "/.config/mozilla/firefox",
        "/snap/firefox/common/.mozilla/firefox",
        "/.var/app/org.mozilla.firefox/.mozilla/firefox",
        NULL
    };
    Str root, js, css, profiles;
    char *argv[4];
    size_t i;
    int is_temp = 0;
    int ok;

    ok = osr_pkg_install_step("Installing Firefox", pkgs);

    /* Where the profile actually is. There is no single answer any more, and
     * every wrong guess has the same symptom: the module reports success and
     * Firefox is untouched, because the layer landed in a directory the browser
     * never reads. */
    str_init(&root);
    str_addz(&root, osr_mod_home()); str_addz(&root, "/.mozilla/firefox");
    if (!dir_exists(str_text(&root))) {
        for (i = 0; alts[i] != NULL; i++) {
            Str alt;
            str_init(&alt);
            str_addz(&alt, osr_mod_home()); str_addz(&alt, alts[i]);
            if (dir_exists(str_text(&alt))) {
                str_reset(&root);
                str_addz(&root, str_text(&alt));
                osr_infof("profile root is %s (not the classic ~/.mozilla/firefox)",
                          str_text(&root));
                str_free(&alt);
                break;
            }
            str_free(&alt);
        }
    }

    str_init(&js); str_init(&css);
    {
        Str base;
        str_init(&base);
        str_addz(&base, osr_mod_dotfiles()); str_addz(&base, "/firefox/user.js");
        if (file_exists(str_text(&base))) str_addz(&js, str_text(&base));
        str_free(&base);
    }
    (void)osr_theme_source(&css, "firefox", "userChrome.css", &is_temp);

    /* Say so when the theme half resolved to nothing. Without this the module
     * still reports success, installs user.js, and leaves a Firefox that is
     * half-themed -- the prefs applied, the colors not -- with no line anywhere
     * naming the reason. */
    if (css.len == 0)
        osr_warnf("no Firefox theme layer: neither themes/%s/config/firefox/userChrome.css "
                  "nor a rendered firefox/userChrome.css.tmpl - Firefox keeps its default chrome",
                  *osr_mod_theme() != '\0' ? osr_mod_theme() : "?");

    /* A machine that has never launched Firefox has no profile directory, and
     * osr_install_mozilla_layer can only warn and return -- which is why a fresh
     * rice install ends with an unstyled, default-light Firefox and no obvious
     * reason why. Create the profile instead of waiting for the user to:
     * -CreateProfile is headless, takes under a second, and writes the
     * profiles.ini that the browser then adopts on its first real start. Do it
     * BEFORE installing the layers so the same run installs into it. */
    str_init(&profiles);
    osr_mozilla_profiles(&profiles, str_text(&root));
    if (profiles.len == 0 && osr_have_cmd("firefox")) {
        argv[0] = (char *)"sh"; argv[1] = (char *)"-c";
        argv[2] = (char *)"firefox -CreateProfile default-release >/dev/null 2>&1 || true";
        argv[3] = NULL;
        ok = osr_run_step_user("Firefox: creating the initial profile (none exists yet)",
                               argv) && ok;
    }

    if (js.len > 0 || css.len > 0)
        ok = osr_install_mozilla_layer(str_text(&root), str_text(&js),
                                       str_text(&css)) && ok;

    /* Verify rather than assume. userChrome.css is the one layer here with no
     * visible failure mode of its own: Firefox reads it silently or ignores it
     * silently, so the only place the truth can be told is right after writing
     * it. Checked per profile, because a machine with two profiles and one
     * styled is exactly the case that reads as "the theme is broken". */
    if (css.len > 0) {
        size_t pos = 0;
        Line line;
        str_reset(&profiles);
        osr_mozilla_profiles(&profiles, str_text(&root));
        while (next_line(str_text(&profiles), profiles.len, &pos, &line)) {
            Str path;
            str_init(&path);
            str_add(&path, line.start, line.len);
            str_addz(&path, "/chrome/userChrome.css");
            if (!file_exists(str_text(&path))) {
                Str dir;
                str_init(&dir);
                str_add(&dir, line.start, line.len);
                osr_warnf("userChrome.css did not land in %s - Firefox there stays unstyled",
                          str_text(&dir));
                str_free(&dir);
            }
            str_free(&path);
        }
    }
    if (is_temp && css.len > 0) (void)unlink(str_text(&css));

    str_free(&root); str_free(&js); str_free(&css); str_free(&profiles);
    return ok;
}
