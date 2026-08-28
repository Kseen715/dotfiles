/* modules/gnome-focus.c -- make notification clicks raise the window (DRAFT).
 *
 * GNOME Shell's focus-stealing prevention: when an app asks for focus without a
 * fresh user-interaction timestamp (Telegram, Thunderbird, anything raising a
 * window from a tray/notification), the Shell refuses and shows a second
 * "<App> is ready" notification instead. Clicking that one finally raises it.
 * Two clicks for every message.
 *
 * There is no gsettings key for this on Wayland — the behaviour lives in
 * MetaDisplay's focus policy, so the only fix is a Shell extension that catches
 * `demands-attention` and activates the window itself.
 * extensions.gnome.org serves a different zip per Shell major, so ask for ours.
 * Enabling only sticks once the Shell has loaded the new extension; on a live
 * session that means a logout (Wayland) or Alt+F2 r (X11). Best-effort (§9).
 *
 * Port of modules/gnome-focus.sh, kept as the reference at
 * test/ref/gnome-focus_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"
#include "../lib/fetch.h"

#include <stddef.h>
#include <unistd.h>

#define GF_UUID "stealmyfocus@kleinernik.gmail.com"

/* gf_install -- the whole install as ONE step: resolve the build for this Shell
 * major from the extensions.gnome.org API, download the zip, hand it to
 * gnome-extensions. There is no package anywhere for this, and the API answer
 * is per Shell version, which is why the version goes into the query. */
static int gf_install(void *ctx) {
    Str json, url, zip, full;
    char *argv[5];
    int ok = 0;

    str_init(&json); str_init(&url); str_init(&zip); str_init(&full);
    str_addz(&zip, env_str("TMPDIR", "/tmp"));
    str_addz(&zip, "/" GF_UUID ".zip");
    str_addz(&full, "https://extensions.gnome.org/extension-info/?uuid=" GF_UUID
                    "&shell_version=");
    str_addz(&full, (const char *)ctx);

    if (osr_fetch_buffer(&json, str_text(&full)) &&
        osr_json_string_field(&url, str_text(&json), "download_url") && url.len > 0) {
        Str dl;
        str_init(&dl);
        str_addz(&dl, "https://extensions.gnome.org");
        str_addz(&dl, str_text(&url));
        if (osr_fetch_download(str_text(&dl), zip.p, 0)) {
            argv[0] = (char *)"gnome-extensions"; argv[1] = (char *)"install";
            argv[2] = (char *)"--force"; argv[3] = zip.p; argv[4] = NULL;
            ok = osr_run_user(argv) == 0;
        }
        str_free(&dl);
        (void)unlink(str_text(&zip));
    } else {
        osr_die("no build of " GF_UUID " for GNOME %s", (const char *)ctx);
    }
    str_free(&json); str_free(&url); str_free(&zip); str_free(&full);
    return ok;
}

int osrm_gnome_focus(void) {
    Str major, raw;
    char *argv[4];
    int ok;

    if (!osr_have_cmd("gnome-shell")) {
        osr_warn("gnome-shell not found - skipping " GF_UUID);
        return 1;
    }
    str_init(&major); str_init(&raw);
    argv[0] = (char *)"gnome-shell"; argv[1] = (char *)"--version"; argv[2] = NULL;
    (void)osr_run_capture(argv, &raw);
    {
        /* "GNOME Shell 50.1" -> "50": the API keys its builds on the major. */
        const char *p = str_text(&raw);
        const char *d = NULL;
        while (*p != '\0') {
            if (*p >= '0' && *p <= '9') { d = p; break; }
            p++;
        }
        if (d != NULL) while (*d >= '0' && *d <= '9') str_addc(&major, *d++);
    }
    ok = osr_step("Installing Steal My Focus Window", gf_install,
                  (void *)str_text(&major));

    argv[0] = (char *)"gnome-extensions"; argv[1] = (char *)"enable";
    argv[2] = (char *)GF_UUID; argv[3] = NULL;
    if (osr_run_user_quiet(argv) != 0)
        osr_warn(GF_UUID " installed but not enabled yet - log out and back in");

    str_free(&major); str_free(&raw);
    return ok;
}
