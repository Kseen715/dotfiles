/* lib/wintweak.h -- the OS-tweak primitives: registry DWORD writes, service
 * start-type/stop control, and recursive path purges.
 *
 * C port of the retired windows-11-x86_64/ PowerShell tree -- the debloat
 * half of this repo's Windows story, which until now lived entirely outside
 * the C core as ~25 tiny .ps1 files. Their whole vocabulary was three verbs:
 *
 *   Set-ItemProperty -Path HK__:... -Name X -Value N   (src/common.ps1's
 *                                                       UpdateRegistryValue,
 *                                                       behind every
 *                                                       microscripts/reg-*.ps1)
 *   Stop-Service -Force / Set-Service -StartupType     (microscripts/disable-*.ps1)
 *   Remove-Item -Recurse -Force -ErrorAction Silently  (the two cache purges)
 *
 * so those three verbs are what this file provides, and the policy -- which
 * key, which value, which service -- stays a plain table in
 * modules/win-tweaks.c. That is exactly the split the ps1 tree already
 * had between src/common.ps1 (mechanism) and setup.ps1 (the list): nothing
 * here knows what a good tweak is, and nothing there knows how a registry
 * write works.
 *
 * Everything is done through the Win32 API directly (RegCreateKeyEx,
 * OpenSCManager/ChangeServiceConfig), never by shelling out to
 * powershell.exe: a compiled core that spawns a PowerShell to set a DWORD
 * would carry the whole PowerShell dependency it exists to remove, and
 * would depend on that shell's execution policy besides.
 *
 * Two deliberate differences from the ps1 originals, both explained where
 * they happen:
 *   - a missing registry KEY is created rather than failing (the ps1's
 *     Set-ItemProperty could not write TaskbarDeveloperSettings or the Sudo
 *     key on a machine that had never had them)
 *   - a service this machine does not have is reported and skipped, not
 *     counted as a failure (`Stop-Service -Name fax` on a machine with no
 *     fax service was always noise, never a problem)
 *
 * The parsing half (osr_wintweak_split_key, the two name helpers, the two
 * needs_admin predicates) is pure and builds everywhere, so the tables can
 * be unit-tested on any host without touching a registry. Every verb that
 * actually changes the machine is Windows-only and returns 0 elsewhere.
 *
 * C89.
 */
#ifndef OSR_WINTWEAK_H
#define OSR_WINTWEAK_H

/* The two hives the ps1 tree used: HKCU for the per-user Explorer/taskbar
 * settings, HKLM for the one machine-wide row (Sudo). No others are
 * accepted -- an HKCR/HKU row would be a new decision, not a port. */
typedef enum {
    OSR_WINTWEAK_HIVE_NONE = 0,
    OSR_WINTWEAK_HIVE_HKCU,
    OSR_WINTWEAK_HIVE_HKLM
} osr_wintweak_hive;

/* Set-Service -StartupType's values, plus KEEP for a row that only stops a
 * service without changing how it starts. */
typedef enum {
    OSR_WINTWEAK_START_KEEP = 0,
    OSR_WINTWEAK_START_AUTOMATIC,
    OSR_WINTWEAK_START_MANUAL,
    OSR_WINTWEAK_START_DISABLED
} osr_wintweak_start;

/* One registry tweak -- the C shape of a microscripts/reg-*.ps1 file plus
 * the value setup.ps1 passed it.
 *
 * enabled == 0 keeps a row the ps1 tree carried but did not apply (a
 * commented-out setup.ps1 line, or a microscript nothing ever called).
 * Those are preserved rather than dropped: the decision not to apply one is
 * itself information, and turning it back on is now a one-character edit
 * instead of a rediscovery. `note` is what the applied line prints.
 */
typedef struct {
    const char *key;      /* "HKCU:Software\\..." -- see osr_wintweak_split_key */
    const char *name;     /* value name, e.g. "HideFileExt" */
    unsigned long value;  /* the DWORD to write */
    int enabled;
    const char *note;     /* short human description, used as the log line */
} osr_wintweak_reg;

/* One service tweak -- the C shape of a microscripts/disable-*.ps1 file. A
 * row may stop the service, change its start type, purge the cache
 * directory it leaves behind, or any combination; that is the exact range
 * those files covered (telemetry stops + disables + purges, wuauserv only
 * changes its start type).
 */
typedef struct {
    const char *service;         /* service key name, e.g. "DiagTrack" */
    int stop;                    /* Stop-Service -Force */
    osr_wintweak_start start;    /* Set-Service -StartupType */
    const char *purge;           /* NULL, or a path that may use %VAR% */
    int enabled;
    const char *note;
} osr_wintweak_service;

/* --- parsing (portable, no OS dependency) --------------------------------- */

/* osr_wintweak_split_key -- split "HKCU:Software\Foo" into a hive and the
 * subkey below it. Accepts the ps1 spelling used throughout the retired
 * tree ("HKCU:...", with or without a separator after the colon) and the
 * long form ("HKEY_CURRENT_USER\..."). Returns 1 and fills *hive and
 * subkey_out on success; 0 (hive NONE, subkey_out empty) for an unknown
 * prefix, a missing subkey, or a subkey too long for the buffer -- a
 * truncated registry path must never be written to.
 */
int osr_wintweak_split_key(const char *spec, osr_wintweak_hive *hive,
                           char *subkey_out, unsigned long out_sz);

/* osr_wintweak_hive_name -- "HKCU" | "HKLM" | "?" (for messages). */
const char *osr_wintweak_hive_name(osr_wintweak_hive hive);

/* osr_wintweak_start_name -- "Automatic" | "Manual" | "Disabled" |
 * "unchanged", the words Set-Service itself uses. */
const char *osr_wintweak_start_name(osr_wintweak_start start);

/* osr_wintweak_needs_admin_reg / _services -- 1 when applying these rows
 * would need Administrator rights: any enabled HKLM row, or any enabled
 * service row at all (the SCM refuses a config change to a non-admin).
 * Asked BEFORE any work, so the one UAC prompt lands at the top of the run
 * the way install.sh warms sudo once (lib/elevate.h) -- which is what
 * setup.ps1's `Invoke-ElevatedScript` on line 3 did, only without having to
 * assume every run needs it. Pure functions of the table.
 */
int osr_wintweak_needs_admin_reg(const osr_wintweak_reg *rows, unsigned long count);
int osr_wintweak_needs_admin_services(const osr_wintweak_service *rows, unsigned long count);

/* --- the three verbs (Windows only; 0 off Windows) ------------------------ */

/* osr_wintweak_set_dword -- write `value` as a REG_DWORD at key_spec\name,
 * creating the key first if this machine does not have it. 1 on success.
 */
int osr_wintweak_set_dword(const char *key_spec, const char *name, unsigned long value);

/* osr_wintweak_stop_service -- Stop-Service -Force: stop the service's
 * running dependents first, then the service itself, and wait for it to
 * report STOPPED. 1 when it is stopped (including "already was" and "this
 * machine has no such service"), 0 on a real failure.
 */
int osr_wintweak_stop_service(const char *service);

/* osr_wintweak_set_service_start -- Set-Service -StartupType. KEEP is a
 * successful no-op. 1 on success (or when there is no such service).
 */
int osr_wintweak_set_service_start(const char *service, osr_wintweak_start start);

/* osr_wintweak_purge_path -- Remove-Item -Recurse -Force -ErrorAction
 * SilentlyContinue: expand %VAR% in `path_spec`, then delete it whole. A
 * path that is not there is success -- these are caches, and the point is
 * only that they are gone.
 */
int osr_wintweak_purge_path(const char *path_spec);

/* --- applying a whole table ----------------------------------------------- */

/* osr_wintweak_apply_reg / _services -- run every enabled row in order,
 * logging one line each. A failing row is warned about and the rest still
 * run: one refused tweak must not abandon the other twenty, the same
 * non-fatal contract as install.sh's run_module. Returns 1 only when every
 * enabled row succeeded.
 */
int osr_wintweak_apply_reg(const osr_wintweak_reg *rows, unsigned long count);
int osr_wintweak_apply_services(const osr_wintweak_service *rows, unsigned long count);

#endif /* OSR_WINTWEAK_H */
