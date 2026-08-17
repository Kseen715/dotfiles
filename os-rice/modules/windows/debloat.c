/* modules/windows/debloat.c -- port of the retired windows-11-x86_64/
 * winutils.ps1 and microscripts/raphire-win11debloat.ps1: the two
 * third-party debloat tools that tree deferred to for the work
 * modules/windows/tweaks.c does not do itself (removing preinstalled apps,
 * the big vendor tweak catalogues).
 *
 * Both ps1 files were one line each -- fetch a vendor script and run it --
 * so both are one call here, to the same helper lib/winbin.c already
 * provides for the map's `script:` provider. What differs between them is
 * only a URL and a name, which is why they share a file rather than having
 * one each.
 *
 * These stay separate, opt-in modules rather than steps inside
 * win-tweaks, exactly as the ps1 tree had them: raphire's line in
 * setup.ps1 was commented out, and winutils.ps1 was its own entry point you
 * ran by hand. Both hand control of the machine to a script fetched at run
 * time from someone else's server; that is a decision to make per run, not
 * something a rice install should do on your behalf.
 *
 * The tweak selection the winutil run was meant to be given
 * (windows-11-x86_64/winutils.json, now modules/windows/data/winutils.json)
 * is carried alongside these -- see that folder's README for how to feed it
 * in. The ps1 never passed it either; it is a saved profile for winutil's
 * own UI, not an argument this module invents.
 *
 * C89.
 */
#include "../src/common.h"

#include "../../lib/elevate.h"
#include "../../lib/winbin.h"
#include "../../lib/ui.h"

#include <stddef.h>

#ifdef _WIN32

/* run_vendor_tool -- elevate once, then hand the URL to
 * osr_winbin_run_script (`irm <url> | iex`, the line each vendor's own
 * README tells you to paste). The elevation is what both ps1 files opened
 * with: these tools uninstall provisioned packages and write HKLM, and a
 * non-elevated run of either just fails deeper in, after the download.
 */
static int run_vendor_tool(const char *name, const char *url, const char *what) {
    osr_info("%s: %s", name, what);
    osr_info("%s: this runs code fetched from %s -- read it there if you have "
             "not before", name, url);

    if (!osr_elevate_now("this tool makes machine-wide changes.")) {
        osr_warn("%s: needs Administrator rights -- skipped", name);
        return 0;
    }

    if (!osr_winbin_run_script(url, name)) {
        osr_warn("%s: the vendor script did not complete", name);
        return 0;
    }
    osr_success("%s: finished", name);
    return 1;
}

int osrm_win_debloat(const char *repo_root, const char *themes_root, const char *map_path,
                       const char *theme, int theme_only) {
    (void)repo_root; (void)themes_root; (void)map_path; (void)theme;
    if (theme_only) return 1;

    return run_vendor_tool("win-debloat", "https://debloat.raphi.re/",
                           "Raphire's Win11Debloat -- removes preinstalled apps and "
                           "the bulk of Microsoft's advertising surfaces");
}

int osrm_win_winutil(const char *repo_root, const char *themes_root, const char *map_path,
                       const char *theme, int theme_only) {
    (void)repo_root; (void)themes_root; (void)map_path; (void)theme;
    if (theme_only) return 1;

    return run_vendor_tool("win-winutil", "https://christitus.com/win",
                           "Chris Titus Tech's WinUtil -- an interactive tweak/install "
                           "GUI; it opens a window and waits for you");
}

#else /* !_WIN32 */

int osrm_win_debloat(const char *repo_root, const char *themes_root, const char *map_path,
                       const char *theme, int theme_only) {
    (void)repo_root; (void)themes_root; (void)map_path; (void)theme; (void)theme_only;
    return 0;
}

int osrm_win_winutil(const char *repo_root, const char *themes_root, const char *map_path,
                       const char *theme, int theme_only) {
    (void)repo_root; (void)themes_root; (void)map_path; (void)theme; (void)theme_only;
    return 0;
}

#endif /* _WIN32 */
