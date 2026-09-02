/* modules/win-update.c -- port of the retired windows-11-x86_64/
 * win-update.ps1 + microscripts/update-windows.ps1: trigger a Windows
 * Update run on demand.
 *
 * This is the other half of modules/win-tweaks.c's wuauserv row. That row
 * sets Windows Update to Manual so nothing installs behind your back; this
 * module is how you then ask for updates on purpose. Neither makes much
 * sense without the other, which is why they share the win- prefix.
 *
 * The ps1 ran `wuauclt /detectnow` + `wuauclt /updatenow`. Those verbs are
 * a Windows 7-era interface: wuauclt.exe still exists on Windows 10/11 and
 * still exits 0, but it stopped acting on them -- the Update Orchestrator
 * (usoclient.exe) took the job over. Running only wuauclt on a Windows 11
 * machine would be a module that reports success and does nothing, so both
 * are issued: wuauclt for the older targets this repo still aims at (Win7,
 * see PLAN_UNIVERSAL.md's reach targets), usoclient for the ones where it
 * is the real trigger. Whichever is not the live interface on a given
 * machine is a no-op there, which is exactly the property that makes
 * sending both safe.
 *
 * C89.
 */
#include "../lib/module.h"
#include "../lib/elevate.h"
#include "../lib/ui.h"

#include <stddef.h>

#ifdef _WIN32

/* try_step -- run one trigger, reporting a non-zero exit as a warning
 * rather than a failure. Every command here is "ask the update stack to
 * look now"; a machine where one of the two interfaces is absent or inert
 * is the normal case, not an error to abort on. */
static void try_step(const char *desc, const char *cmd) {
    if (osr_run_step_cmd(desc, cmd) != 0) {
        osr_warnf("win-update: '%s' did not report success (may not be this "
                 "Windows version's interface)", cmd);
    }
}

int osrm_win_update(void) {
    if (osr_theme_only()) return osr_theme_only_skip("win-update");

    /* win-update.ps1 opened with Invoke-ElevatedScript. Both interfaces
     * below drive a machine-wide service, so this is the same one prompt,
     * asked before any of them run. */
    osr_elevate_now("installing Windows updates is a machine-wide change.");

    /* Windows 7-era interface, kept for the older reach targets. */
    try_step("windows update: scan (wuauclt)", "wuauclt /detectnow");
    try_step("windows update: install (wuauclt)", "wuauclt /updatenow");

    /* Windows 10/11: the Update Orchestrator. Scan, then download, then
     * install -- three separate verbs, in that order, because each one
     * only queues work for the next. */
    if (osr_have_cmd("usoclient")) {
        try_step("windows update: scan (usoclient)", "usoclient StartScan");
        try_step("windows update: download (usoclient)", "usoclient StartDownload");
        try_step("windows update: install (usoclient)", "usoclient StartInstall");
    }

    /* Deliberately no claim that anything was installed: every interface
     * here is asynchronous -- it hands work to a service and returns long
     * before that service is done. */
    osr_successf("win-update: update run requested -- Windows Update continues "
                "in the background");
    return 1;
}

#else /* !_WIN32 */

/* Nothing to ask: there is no Windows Update here. */
int osrm_win_update(void) { return 0; }

#endif /* _WIN32 */
