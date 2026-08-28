/* modules/thunderbird.c -- mail/calendar. Same Mozilla profile machinery as
 * modules/firefox.sh: a dotfiles-owned user.js and a rice-owned userChrome.css,
 * installed into every profile under ~/.thunderbird (§5/§6).
 *
 * `evolution` is the packaged GTK alternative and `aerc`/`neomutt` the TUI ones
 * (i3-sugg §9) — this module installs one mail client, not three.
 *
 * Note the profile root differs from Firefox's: ~/.thunderbird, not
 * ~/.mozilla/thunderbird, on every current build.
 *
 * Debian/Ubuntu do not go through the archive: `thunderbird` resolves to
 * source:provide_thunderbird_tarball (Mozilla's official Linux build) there,
 * because the archive package is a snap stub on Ubuntu 24.04+ and an ESR too old
 * for Exchange everywhere else. See lib/pkgmap/apt.map. Every other target keeps
 * the native package: Fedora (152), RHEL/Alma/Rocky (140 ESR), Arch, Void and
 * Alpine are all current enough for Exchange, so apt is the only special case.
 *
 * Exchange/Office 365: nothing to install. Thunderbird 140+ has an EWS backend
 * built in, and the prefs that surface it live in dotfiles/thunderbird/user.js.
 * Add the account with Account Setup -> Continue -> "Exchange"; Office 365 signs
 * in through OAuth2 in a popup window. Mail only — Exchange *calendar* and
 * address book still need an add-on (TbSync + its EWS provider), which is a
 * per-user add-on install, not something a module can drop into a profile.
 * De-snap first (apt only). This runs BEFORE pkg_install for a reason: the
 * source: provider's idempotency probe is `command -v thunderbird` (§4), and a
 * snap on PATH as /snap/bin/thunderbird would make the install skip itself — the
 * snap would simply stay, profile in ~/snap and all. The transitional deb goes
 * too: it owns a .desktop that re-launches the snap, and on a failed
 * `snap install` its postinst leaves it half-installed, which plain --purge
 * refuses — hence --force-all.
 * Exchange needs Thunderbird 140+. Every route this module takes should deliver
 * it — Mozilla's tarball on apt, the native package on dnf/pacman/xbps/apk, all
 * of which ship 140+ — so a lower version means this target pinned an old ESR.
 * Say it here, once, instead of leaving someone hunting for an "Exchange" button
 * that the account wizard is never going to draw.
 *
 * Port of modules/thunderbird.sh, kept as the reference at
 * test/ref/thunderbird_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/config.h"
#include "../lib/render.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int osrm_thunderbird(void) {
    static const char *const pkgs[] = { "thunderbird", NULL };
    Str js, css, root, ver;
    char *argv[8];
    int is_temp = 0;
    int ok;

    /* Both of these have to happen BEFORE the install: `command -v thunderbird`
     * finds /snap/bin/thunderbird otherwise, and the source: probe would skip
     * the install entirely. */
    if (strcmp(osr_mod_pkg(), "apt") == 0) {
        char *q[4];
        q[0] = (char *)"snap"; q[1] = (char *)"list"; q[2] = (char *)"thunderbird"; q[3] = NULL;
        if (osr_have_cmd("snap") && osr_run_quiet(q) == 0) {
            osr_info("removing the Thunderbird snap (its profile root is not ~/.thunderbird)");
            argv[0] = (char *)"snap"; argv[1] = (char *)"remove";
            argv[2] = (char *)"--purge"; argv[3] = (char *)"thunderbird"; argv[4] = NULL;
            if (osr_run_root(argv) != 0) osr_warn("snap remove thunderbird failed");
        }
        {
            /* The archive package on 24.04+ is a stub whose only job is to
             * install that snap; its version string says so. */
            Str status;
            char *d[4];
            str_init(&status);
            d[0] = (char *)"dpkg"; d[1] = (char *)"-s"; d[2] = (char *)"thunderbird"; d[3] = NULL;
            if (osr_run_capture(d, &status)) {
                size_t pos = 0;
                Line l;
                int transitional = 0;
                while (!transitional && next_line(str_text(&status), status.len, &pos, &l)) {
                    Str line;
                    str_init(&line);
                    str_add(&line, l.start, l.len);
                    transitional = strncmp(str_text(&line), "Version:", 8) == 0 &&
                                   strstr(str_text(&line), "snap") != NULL;
                    str_free(&line);
                }
                if (transitional) {
                    osr_info("removing the archive's snap-transitional thunderbird package");
                    argv[0] = (char *)"env";
                    argv[1] = (char *)"DEBIAN_FRONTEND=noninteractive";
                    argv[2] = (char *)"dpkg"; argv[3] = (char *)"--purge";
                    argv[4] = (char *)"--force-all"; argv[5] = (char *)"thunderbird";
                    argv[6] = NULL;
                    if (osr_run_root(argv) != 0)
                        osr_warn("could not purge the transitional thunderbird package");
                }
            }
            str_free(&status);
        }
    }
    ok = osr_pkg_install_step("Installing Thunderbird", pkgs);

    /* Native Exchange/EWS accounts need 140+; an ESR looks installed and simply
     * has no such account type. */
    str_init(&ver);
    if (osr_have_cmd("thunderbird")) {
        Str raw;
        char *v[3];
        str_init(&raw);
        v[0] = (char *)"thunderbird"; v[1] = (char *)"--version"; v[2] = NULL;
        if (osr_run_capture(v, &raw)) {
            const char *p = str_text(&raw);
            while (*p != '\0' && (*p < '0' || *p > '9')) p++;
            while (*p >= '0' && *p <= '9') str_addc(&ver, *p++);
        }
        str_free(&raw);
    }
    if (ver.len > 0 && atol(str_text(&ver)) < 140)
        osr_warnf("Thunderbird %s is older than 140 - Exchange/EWS accounts are "
                  "unavailable; on x86_64, replacing it with Mozilla's build "
                  "(provide_thunderbird_tarball, lib/build.c) is the way out",
                  str_text(&ver));
    str_free(&ver);

    str_init(&js); str_init(&css); str_init(&root);
    str_addz(&root, osr_mod_home()); str_addz(&root, "/.thunderbird");
    {
        Str base;
        str_init(&base);
        str_addz(&base, osr_mod_dotfiles()); str_addz(&base, "/thunderbird/user.js");
        if (file_exists(str_text(&base))) str_addz(&js, str_text(&base));
        str_free(&base);
    }
    (void)osr_theme_source(&css, "thunderbird", "userChrome.css", &is_temp);
    if (js.len > 0 || css.len > 0)
        ok = osr_install_mozilla_layer(str_text(&root), str_text(&js),
                                       str_text(&css)) && ok;
    if (is_temp && css.len > 0) (void)unlink(str_text(&css));

    str_free(&js); str_free(&css); str_free(&root);
    return ok;
}
