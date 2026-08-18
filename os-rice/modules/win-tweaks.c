/* modules/win-tweaks.c -- port of the retired windows-11-x86_64/setup.ps1
 * and the ~19 microscripts it called: the OS debloat pass
 * (telemetry, diagnostics, search indexing, superfetch, Windows Update's
 * start type) plus the Explorer/taskbar/snap registry settings and the
 * `sudo` switch.
 *
 * The ps1 tree spread this over one file per setting -- 12 near-identical
 * reg-*.ps1 files that differed only in a key, a value name and a default,
 * and 6 disable-*.ps1 files that differed only in a service name. All of
 * that collapses to the two tables below, which is what those files were
 * always trying to be: `setup.ps1` was already just a list of
 * (microscript, value) pairs read top to bottom. The verbs they called
 * (Set-ItemProperty / Stop-Service / Set-Service / Remove-Item) are now
 * lib/wintweak.c; this file is only the list.
 *
 * Each row keeps the rationale its ps1 file carried in a comment header --
 * that reasoning (what the service costs, what breaks if it is off) is the
 * actual content of those files, and losing it in a mechanical port would
 * cost more than the code did. Translated to English here because every
 * other comment in this tree is English and because DESIGN's ASCII-only
 * rule applies to source as much as to output.
 *
 * The win- prefix, not a modules/windows/ folder, is what marks this group
 * apart from modules/fastfetch.c and its siblings: every module is one file
 * in modules/, and the OS a module can run on is a question its #ifdefs
 * answer, not its directory. What the prefix says is that these are not
 * standalone app modules (no package, no theme layer, nothing to install) --
 * they are one OS-level pass over a Windows machine, and they only make
 * sense as a group. See modules/WINDOWS.md for the group as a whole.
 *
 * Not win11-, despite where the ps1 files came from: that name overclaimed. Every service row here exists on Windows 7 or 10, as do
 * HideFileExt/Hidden/DontPrettyPath (XP-era), DisallowShaking (7) and
 * SnapAssist (10). Only the four snap-layout rows, TaskbarEndTask and sudo
 * are genuinely 11-only, and ShowCortanaButton is the opposite case -- a 10
 * setting that 11 no longer reads. Each row is marked below.
 *
 * Rows are applied unconditionally rather than gated on a detected version,
 * which is safe in exactly one direction and worth being explicit about:
 * writing an Explorer DWORD that this build does not read, or creating the
 * Sudo key on a build that has no sudo, changes nothing and is undone by
 * the same row on a machine that does read it. The reverse -- guessing a
 * version wrong and skipping a setting the machine wanted -- fails
 * silently, so it is not done.
 *
 * C89.
 */
#include "src/common.h"

#include "../lib/wintweak.h"
#include "../lib/elevate.h"
#include "../lib/winui.h"

#include <stddef.h>

/* --- registry tweaks -------------------------------------------------------
 *
 * Values are the ones setup.ps1 actually passed. Where a microscript's own
 * fallback default (used when it was run by hand with no argument) differed
 * from what setup.ps1 passed, the comment says so -- that default was the
 * "restore it" value, and it is the only other value any of these rows ever
 * took.
 *
 * enabled == 0 rows: reg-dont-pretty-path.ps1 existed but setup.ps1 never
 * called it. Kept, off, rather than dropped -- see wintweak.h.
 */
#define ADVANCED "HKCU:Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced"

static const osr_wintweak_reg g_reg_tweaks[] = {
    /* Explorer -- all three are XP-era settings every Windows since reads. */
    { ADVANCED, "HideFileExt", 0, 1,
      "show file extensions" },                          /* microscript default: 1 */
    { ADVANCED, "Hidden", 1, 1,
      "show hidden files" },
    { ADVANCED, "DontPrettyPath", 1, 0,
      "keep the on-disk casing of paths in the address bar" },

    /* Taskbar */
    { ADVANCED, "ShowCortanaButton", 0, 1,                /* Windows 10; 11 has no */
      "hide the Cortana button" },                       /* such button. Default: 1 */
    { ADVANCED "\\TaskbarDeveloperSettings", "TaskbarEndTask", 1, 1,
      "End task in the taskbar right-click menu" },      /* Windows 11 22H2+ */
    { ADVANCED, "DisallowShaking", 1, 1,                  /* Windows 7+ */
      "no aero-shake minimize (shaking a window stops hiding the rest)" },

    /* Multitasking / snap. Every one of these is off: Windows 11's snap
     * layer is the single most intrusive part of its window management,
     * and this whole group exists to get a plain tiling-friendly desktop
     * back. Each microscript's own default was 1 (Windows' own).
     * SnapAssist goes back to Windows 10; the other four are 11's own
     * snap-layouts UI and are simply unread on 10. */
    { ADVANCED, "EnableTaskGroups", 0, 1,                 /* Windows 11 */
      "no alt-tab task groups" },
    { ADVANCED, "SnapAssist", 0, 1,                       /* Windows 10+ */
      "no snap assist" },
    { ADVANCED, "EnableSnapBar", 0, 1,                    /* Windows 11 */
      "no snap bar at the top of the screen" },
    { ADVANCED, "EnableSnapAssistFlyout", 0, 1,           /* Windows 11 */
      "no snap assist flyout" },
    { ADVANCED, "DITest", 0, 1,                           /* Windows 11 */
      "no soft-bound snapping" },

    /* sudo -- Windows 11 24H2 and later only, and the one machine-wide row,
     * so the only reason this module needs Administrator for its registry
     * half at all. 3 is the "inline" mode (the elevated command runs in the
     * current window); 0 disables it, 1 is a new window, 2 is
     * input-disabled. On an older build this just creates a key nothing
     * reads. */
    { "HKLM:Software\\Microsoft\\Windows\\CurrentVersion\\Sudo", "Enabled", 3, 1,
      "enable sudo, inline mode" }
};
#define REG_COUNT (sizeof(g_reg_tweaks) / sizeof(g_reg_tweaks[0]))

/* --- service tweaks --------------------------------------------------------
 *
 * In setup.ps1's own order. The per-service notes below are the substance
 * of the disable-*.ps1 headers. Nothing in this table is Windows 11
 * specific: DiagTrack and dmwappushservice arrived with 8.1/10, the other
 * five go back to XP or Vista, and a service a given edition does not ship
 * is reported and skipped (lib/wintweak.c's service_missing).
 *
 *   DiagTrack (Connected User Experiences and Telemetry)
 *       Collects usage, app, performance and error data and sends it to
 *       Microsoft -- the main telemetry channel. Costs 50-150 MB RAM,
 *       5-10% CPU while uploading, and constant network traffic.
 *       Consequences of disabling: none. The system behaves identically.
 *
 *   DPS (Diagnostic Policy Service)
 *       Analyses Windows problems, runs diagnostic scripts, collects
 *       reports. 20-40 MB RAM, wakes on trouble. Consequences: the
 *       built-in troubleshooters stop working. Not a loss if you diagnose
 *       with third-party tools (HWiNFO, CrystalDiskInfo).
 *
 *   dmwappushservice (WAP Push Service)
 *       Carries Microsoft's push notifications -- in practice, the Start
 *       menu ads and Explorer "recommendations". 10-15 MB RAM, always
 *       resident. Consequences: those ads stop. That is the point.
 *
 *   WSearch (Windows Search)
 *       Indexes every file on every disk. 100-500 MB RAM and 30-50%
 *       sustained disk activity. Consequences: Explorer search becomes a
 *       live scan, so it is slower -- imperceptibly so on an SSD -- and
 *       the disk goes quiet.
 *
 *   SysMain (Superfetch)
 *       Preloads frequently used applications into RAM based on usage
 *       analysis. Useful in the HDD era, an anachronism now. 50-200 MB
 *       RAM plus continuous profiling. Consequences: none on an SSD; on an
 *       HDD, under half a second more to start an app.
 *
 *   Fax
 *       Fax support, 5-10 MB RAM. Consequences: none whatsoever. Carried
 *       disabled because setup.ps1 had this line commented out -- see
 *       wintweak.h on why an off row is kept rather than deleted.
 *
 *   wuauserv (Windows Update) -- deliberately NOT disabled
 *       A double-edged one. Turning updates off entirely leaves the
 *       machine unpatched; leaving them automatic means they install at
 *       the worst possible moment. The compromise this tree settled on is
 *       Manual: nothing installs behind your back, and you update on
 *       purpose -- which is what the win-update module does.
 */
static const osr_wintweak_service g_service_tweaks[] = {
    { "DiagTrack", 1, OSR_WINTWEAK_START_DISABLED,
      "%ProgramData%\\Microsoft\\Diagnosis\\ETLLogs", 1,
      "telemetry upload -- the main channel to Microsoft" },

    { "DPS", 1, OSR_WINTWEAK_START_DISABLED, NULL, 1,
      "diagnostic policy service -- built-in troubleshooters" },

    { "dmwappushservice", 1, OSR_WINTWEAK_START_DISABLED, NULL, 1,
      "WAP push -- Start menu ads and Explorer recommendations" },

    { "WSearch", 1, OSR_WINTWEAK_START_DISABLED,
      "%ProgramData%\\Microsoft\\Search\\Data\\Applications\\Windows", 1,
      "search indexer -- constant disk activity" },

    { "SysMain", 1, OSR_WINTWEAK_START_DISABLED, NULL, 1,
      "superfetch -- an HDD-era preloader" },

    { "Fax", 1, OSR_WINTWEAK_START_DISABLED, NULL, 0,
      "fax support" },

    { "wuauserv", 0, OSR_WINTWEAK_START_MANUAL, NULL, 1,
      "Windows Update -> Manual: updates when you ask, not mid-game" }
};
#define SERVICE_COUNT (sizeof(g_service_tweaks) / sizeof(g_service_tweaks[0]))

const osr_wintweak_reg *osrm_win_reg_tweaks(unsigned long *count) {
    if (count != NULL) *count = REG_COUNT;
    return g_reg_tweaks;
}

const osr_wintweak_service *osrm_win_service_tweaks(unsigned long *count) {
    if (count != NULL) *count = SERVICE_COUNT;
    return g_service_tweaks;
}

#ifdef _WIN32

/* apply_reg_rows -- the registry half. Without Administrator the HKLM row
 * cannot be written, but the eleven HKCU ones can: rather than letting that
 * row fail loudly in the middle of a run, it is turned off in a local copy
 * of the table and reported once. A partial pass that says what it skipped
 * beats an all-or-nothing one that leaves the user with neither. */
static int apply_reg_rows(int have_admin) {
    osr_wintweak_reg rows[REG_COUNT];
    unsigned long i;
    int skipped = 0;

    for (i = 0; i < REG_COUNT; i++) {
        osr_wintweak_hive hive;
        char subkey[512];
        rows[i] = g_reg_tweaks[i];
        if (!rows[i].enabled) continue;
        if (have_admin) continue;
        if (!osr_wintweak_split_key(rows[i].key, &hive, subkey, sizeof(subkey))) continue;
        if (hive == OSR_WINTWEAK_HIVE_HKLM) { rows[i].enabled = 0; skipped++; }
    }

    if (skipped > 0) {
        osr_warn("win-tweaks: %d machine-wide setting(s) skipped -- they need "
                 "Administrator rights", skipped);
    }
    return osr_wintweak_apply_reg(rows, REG_COUNT);
}

int osrm_win_tweaks(const char *repo_root, const char *themes_root, const char *map_path,
                      const char *theme, int theme_only) {
    int have_admin;
    int ok = 1;

    (void)repo_root; (void)themes_root; (void)map_path; (void)theme;

    /* No package, no font, no config file -- so there is no theme-owned
     * layer to re-render either. A --theme-only run must not silently
     * reconfigure the operating system. */
    if (theme_only) return 1;

    /* setup.ps1 called Invoke-ElevatedScript unconditionally on line 3.
     * This asks only when the tables actually contain something that needs
     * it, and asks once, before any work -- the port of install.sh's sudo
     * warm-up (lib/elevate.h). A declined prompt is not fatal: the per-user
     * settings are most of this module and still apply. */
    if (osr_wintweak_needs_admin_reg(g_reg_tweaks, REG_COUNT)
        || osr_wintweak_needs_admin_services(g_service_tweaks, SERVICE_COUNT)) {
        osr_elevate_now("disabling services and enabling sudo are machine-wide changes.");
    }
    have_admin = osr_is_admin();

    osr_info("win-tweaks: services");
    if (have_admin) {
        if (!osr_wintweak_apply_services(g_service_tweaks, SERVICE_COUNT)) ok = 0;
    } else {
        osr_warn("win-tweaks: service changes skipped -- they need Administrator rights");
        ok = 0;
    }

    osr_info("win-tweaks: Explorer, taskbar and snap settings");
    if (!apply_reg_rows(have_admin)) ok = 0;

    /* Explorer reads most of Advanced\ once, at startup. Saying so is the
     * difference between "it did not work" and "it has not been reread
     * yet"; restarting Explorer from under the user is not this module's
     * call to make. */
    osr_info("win-tweaks: sign out (or restart explorer.exe) for the Explorer "
             "and taskbar settings to take effect");

    if (ok) osr_success("win-tweaks: applied");
    return ok;
}

#else /* !_WIN32 */

int osrm_win_tweaks(const char *repo_root, const char *themes_root, const char *map_path,
                      const char *theme, int theme_only) {
    (void)repo_root; (void)themes_root; (void)map_path; (void)theme; (void)theme_only;
    return 0;
}

#endif /* _WIN32 */
