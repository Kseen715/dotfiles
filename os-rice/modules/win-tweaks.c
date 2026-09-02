/* modules/win-tweaks.c -- the OS debloat/tweak pass: telemetry, diagnostics,
 * search indexing, superfetch and Windows Update's start type, plus the
 * Explorer/taskbar/snap registry settings and the `sudo` switch.
 *
 * Port of the retired windows-11-x86_64/setup.ps1 and the ~19 microscripts it
 * called. That tree spread this over one file per setting -- 12 near-identical
 * reg-*.ps1 files differing only in a key, a value name and a default, and 6
 * disable-*.ps1 files differing only in a service name. All of it collapses to
 * the two tables below, which is what those files were always trying to be:
 * setup.ps1 was already just a list of (microscript, value) pairs read top to
 * bottom.
 *
 * THE MECHANISM IS HERE TOO, below the tables. It was lib/wintweak.c while the
 * Windows core needed a place to put it, but nothing outside this module has
 * ever called it and nothing else will: "write this registry DWORD, set this
 * service's start type, purge this path" is not a verb lib/module.h offers,
 * it is what this module does. The split that matters is still the one the ps1
 * tree had between src/common.ps1 and setup.ps1 -- nothing below knows what a
 * good tweak is, and nothing above knows how a registry write works -- and it
 * is now a split within one file rather than between a module and a library.
 *
 * Each row keeps the rationale its ps1 file carried in a comment header. That
 * reasoning -- what the service costs, what breaks if it is off -- is the
 * actual content of those files, and losing it in a mechanical port would cost
 * more than the code did.
 *
 * THE VERBS GO THROUGH THE WIN32 API DIRECTLY (RegCreateKeyEx,
 * OpenSCManager/ChangeServiceConfig), never by shelling out to
 * powershell.exe: a compiled core that spawns a PowerShell to set a DWORD
 * would carry the whole PowerShell dependency it exists to remove, and would
 * depend on that shell's execution policy besides.
 *
 * Two deliberate differences from the ps1 originals, both explained where they
 * happen: a missing registry KEY is created rather than failing (the ps1's
 * Set-ItemProperty could not write TaskbarDeveloperSettings or the Sudo key on
 * a machine that had never had them), and a service this machine does not have
 * is reported and skipped rather than counted as a failure (`Stop-Service
 * -Name fax` on a machine with no fax service was always noise).
 *
 * NOT win11-, despite where the ps1 files came from: that name overclaimed.
 * Every service row here exists on Windows 7 or 10, as do HideFileExt/Hidden/
 * DontPrettyPath (XP-era), DisallowShaking (7) and SnapAssist (10). Only the
 * four snap-layout rows, TaskbarEndTask and sudo are genuinely 11-only, and
 * ShowCortanaButton is the opposite case -- a 10 setting that 11 no longer
 * reads. Each row is marked below.
 *
 * Rows are applied unconditionally rather than gated on a detected version,
 * which is safe in exactly one direction and worth being explicit about:
 * writing an Explorer DWORD this build does not read, or creating the Sudo key
 * on a build with no sudo, changes nothing and is undone by the same row on a
 * machine that does read it. The reverse -- guessing a version wrong and
 * skipping a setting the machine wanted -- fails silently, so it is not done.
 *
 * The win- prefix, not a modules/windows/ folder, is what marks this group
 * apart from modules/fastfetch.c and its siblings: every module is one file in
 * modules/, and the OS a module runs on is a question its #ifdefs answer, not
 * its directory. What the prefix says is that these are not standalone app
 * modules -- no package, no font, no config, no theme layer -- they are one
 * OS-level pass over a Windows machine, and they only make sense as a group.
 * See modules/WINDOWS.md.
 *
 * The parsing half (osr_wintweak_split_key, the two needs_admin predicates)
 * and both tables are pure and build everywhere, so the policy can be
 * asserted on any host without touching a registry --
 * test/unit_c/wintweak_test.c does exactly that, and it is the only part of
 * this module that CAN be tested: every other line changes the machine it
 * runs on.
 *
 * C89.
 */
#include "../lib/module.h"
#include "../lib/elevate.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* ===========================================================================
 * the vocabulary: three verbs, and the two row types that drive them
 * ======================================================================== */

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

/* --- the verbs ---------------------------------------------------------- */

static unsigned long prefix_len(const char *spec, const char *prefix) {
    unsigned long i;
    char next;

    for (i = 0; prefix[i] != '\0'; i++) {
        char a = spec[i];
        char b = prefix[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
        if (a != b) return 0;
    }
    next = spec[i];
    if (next != ':' && next != '\\' && next != '/' && next != '\0') return 0;
    return i;
}

int osr_wintweak_split_key(const char *spec, osr_wintweak_hive *hive,
                           char *subkey_out, unsigned long out_sz) {
    unsigned long n;
    unsigned long len;
    osr_wintweak_hive found = OSR_WINTWEAK_HIVE_NONE;

    if (hive != NULL) *hive = OSR_WINTWEAK_HIVE_NONE;
    if (out_sz > 0) subkey_out[0] = '\0';
    if (spec == NULL || subkey_out == NULL || out_sz == 0) return 0;

    n = prefix_len(spec, "HKEY_CURRENT_USER");
    if (n > 0) { found = OSR_WINTWEAK_HIVE_HKCU; }
    if (found == OSR_WINTWEAK_HIVE_NONE) {
        n = prefix_len(spec, "HKEY_LOCAL_MACHINE");
        if (n > 0) found = OSR_WINTWEAK_HIVE_HKLM;
    }
    if (found == OSR_WINTWEAK_HIVE_NONE) {
        n = prefix_len(spec, "HKCU");
        if (n > 0) found = OSR_WINTWEAK_HIVE_HKCU;
    }
    if (found == OSR_WINTWEAK_HIVE_NONE) {
        n = prefix_len(spec, "HKLM");
        if (n > 0) found = OSR_WINTWEAK_HIVE_HKLM;
    }
    if (found == OSR_WINTWEAK_HIVE_NONE) return 0;

    /* Everything between the hive name and the subkey is separator noise:
     * PowerShell's own provider path is "HKCU:\Foo", the ps1 files here
     * mostly wrote "HKCU:Foo", and the long form uses a plain backslash. */
    spec += n;
    while (*spec == ':' || *spec == '\\' || *spec == '/') spec++;
    if (*spec == '\0') return 0;

    len = (unsigned long)strlen(spec);
    if (len >= out_sz) return 0;
    memcpy(subkey_out, spec, len);
    subkey_out[len] = '\0';

    if (hive != NULL) *hive = found;
    return 1;
}

const char *osr_wintweak_hive_name(osr_wintweak_hive hive) {
    if (hive == OSR_WINTWEAK_HIVE_HKCU) return "HKCU";
    if (hive == OSR_WINTWEAK_HIVE_HKLM) return "HKLM";
    return "?";
}

const char *osr_wintweak_start_name(osr_wintweak_start start) {
    if (start == OSR_WINTWEAK_START_AUTOMATIC) return "Automatic";
    if (start == OSR_WINTWEAK_START_MANUAL) return "Manual";
    if (start == OSR_WINTWEAK_START_DISABLED) return "Disabled";
    return "unchanged";
}

int osr_wintweak_needs_admin_reg(const osr_wintweak_reg *rows, unsigned long count) {
    unsigned long i;
    for (i = 0; i < count; i++) {
        osr_wintweak_hive hive;
        char subkey[512];
        if (!rows[i].enabled) continue;
        if (!osr_wintweak_split_key(rows[i].key, &hive, subkey, sizeof(subkey))) continue;
        if (hive == OSR_WINTWEAK_HIVE_HKLM) return 1;
    }
    return 0;
}

int osr_wintweak_needs_admin_services(const osr_wintweak_service *rows, unsigned long count) {
    unsigned long i;
    for (i = 0; i < count; i++) {
        if (rows[i].enabled) return 1;
    }
    return 0;
}

#ifdef _WIN32

/* STOP_TIMEOUT_MS -- how long to wait for a stopping service to actually
 * report STOPPED. Stop-Service's own default wait is 30s; nothing here has
 * any reason to differ. */
#define STOP_TIMEOUT_MS 30000
#define STOP_POLL_MS 250

/* DEPENDENT_DEPTH -- EnumDependentServices reports direct dependents, so a
 * dependent that itself has dependents needs another pass. Three levels is
 * far past anything Windows actually ships (DiagTrack's chain is one deep)
 * and keeps the recursion provably finite. */
#define DEPENDENT_DEPTH 3

static HKEY hive_handle(osr_wintweak_hive hive) {
    if (hive == OSR_WINTWEAK_HIVE_HKCU) return HKEY_CURRENT_USER;
    if (hive == OSR_WINTWEAK_HIVE_HKLM) return HKEY_LOCAL_MACHINE;
    return NULL;
}

/* denied_hint -- the one error worth translating for the user: everything
 * else is a number they can look up, but ACCESS_DENIED here always means
 * the same thing (this run is not elevated) and always has the same fix. */
static void denied_hint(DWORD err, const char *what) {
    if (err == ERROR_ACCESS_DENIED) {
        osr_warnf("%s: access denied -- this needs Administrator rights", what);
    }
}

int osr_wintweak_set_dword(const char *key_spec, const char *name, unsigned long value) {
    osr_wintweak_hive hive;
    char subkey[512];
    HKEY root;
    HKEY key;
    DWORD dword;
    DWORD disposition;
    LONG rc;

    if (!osr_wintweak_split_key(key_spec, &hive, subkey, sizeof(subkey))) {
        osr_warnf("registry: unusable key path '%s'", key_spec);
        return 0;
    }
    root = hive_handle(hive);
    if (root == NULL) return 0;

    /* RegCreateKeyEx, not RegOpenKeyEx: the ps1 original used
     * Set-ItemProperty, which fails outright when the KEY does not exist
     * yet -- and two of these keys genuinely do not on a stock machine
     * (Explorer\Advanced\TaskbarDeveloperSettings until the taskbar
     * developer settings page has been opened once, and the Sudo key on
     * any build older than the feature). Creating the key is what the
     * setting means; refusing to is just an artifact of the cmdlet. */
    rc = RegCreateKeyExA(root, subkey, 0, NULL, REG_OPTION_NON_VOLATILE,
                         KEY_SET_VALUE, NULL, &key, &disposition);
    if (rc != ERROR_SUCCESS) {
        osr_warnf("registry: cannot open %s\\%s (error %lu)",
                 osr_wintweak_hive_name(hive), subkey, (unsigned long)rc);
        denied_hint((DWORD)rc, "registry");
        return 0;
    }

    dword = (DWORD)value;
    rc = RegSetValueExA(key, name, 0, REG_DWORD, (const BYTE *)&dword, (DWORD)sizeof(dword));
    RegCloseKey(key);

    if (rc != ERROR_SUCCESS) {
        osr_warnf("registry: cannot write %s\\%s\\%s (error %lu)",
                 osr_wintweak_hive_name(hive), subkey, name, (unsigned long)rc);
        denied_hint((DWORD)rc, "registry");
        return 0;
    }
    return 1;
}

/* open_scm -- the service control manager, or NULL with one warning. */
static SC_HANDLE open_scm(DWORD access) {
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, access);
    if (scm == NULL) {
        DWORD err = GetLastError();
        osr_warnf("services: cannot open the service control manager (error %lu)",
                 (unsigned long)err);
        denied_hint(err, "services");
    }
    return scm;
}

/* service_missing -- did this OpenService fail only because the machine has
 * no such service? That is not an error here: the tables carry rows for
 * services (fax, and anything a future Windows edition drops) that plenty
 * of machines simply do not have, and the ps1 originals papered over the
 * same case with -ErrorAction SilentlyContinue. */
static int service_missing(DWORD err) {
    return err == ERROR_SERVICE_DOES_NOT_EXIST;
}

static int wait_for_stopped(SC_HANDLE svc) {
    SERVICE_STATUS status;
    DWORD waited = 0;

    while (waited < STOP_TIMEOUT_MS) {
        if (!QueryServiceStatus(svc, &status)) return 0;
        if (status.dwCurrentState == SERVICE_STOPPED) return 1;
        Sleep(STOP_POLL_MS);
        waited += STOP_POLL_MS;
    }
    return 0;
}

static int stop_service_handle(SC_HANDLE scm, const char *service, int depth);

/* stop_dependents -- Stop-Service's -Force, which is only about this: a
 * running service that depends on the one being stopped makes the SCM
 * refuse the stop until it is stopped too. */
static void stop_dependents(SC_HANDLE scm, SC_HANDLE svc, const char *service, int depth) {
    ENUM_SERVICE_STATUSA *deps;
    /* A union rather than a plain byte array: EnumDependentServices writes
     * an array of structs into the front of this buffer and their name
     * strings into the tail, so the storage has to be aligned for the
     * struct, which a char array is not required to be. */
    union {
        ENUM_SERVICE_STATUSA aligned;
        BYTE raw[8192];
    } buf;
    DWORD needed = 0;
    DWORD returned = 0;
    DWORD i;

    if (depth <= 0) return;
    if (EnumDependentServicesA(svc, SERVICE_ACTIVE, &buf.aligned,
                               (DWORD)sizeof(buf), &needed, &returned)) {
        deps = &buf.aligned;
        for (i = 0; i < returned; i++) {
            osr_infof("services: stopping %s first (%s depends on it)",
                     deps[i].lpServiceName, service);
            stop_service_handle(scm, deps[i].lpServiceName, depth - 1);
        }
        return;
    }
    if (GetLastError() == ERROR_MORE_DATA) {
        /* More dependents than the fixed buffer holds. Nothing in the
         * tables is anywhere near this, and growing the buffer at runtime
         * to chase it would be a heap allocation for a case that does not
         * occur; say so rather than silently stopping only some of them. */
        osr_warnf("services: %s has more dependents than can be listed at once "
                 "-- stopping it may fail", service);
    }
}

static int stop_service_handle(SC_HANDLE scm, const char *service, int depth) {
    SC_HANDLE svc;
    SERVICE_STATUS status;
    DWORD err;

    svc = OpenServiceA(scm, service,
                       SERVICE_STOP | SERVICE_QUERY_STATUS | SERVICE_ENUMERATE_DEPENDENTS);
    if (svc == NULL) {
        err = GetLastError();
        if (service_missing(err)) {
            osr_infof("services: %s is not present on this system -- skipped", service);
            return 1;
        }
        osr_warnf("services: cannot open %s (error %lu)", service, (unsigned long)err);
        denied_hint(err, "services");
        return 0;
    }

    if (!QueryServiceStatus(svc, &status)) {
        osr_warnf("services: cannot query %s (error %lu)", service, (unsigned long)GetLastError());
        CloseServiceHandle(svc);
        return 0;
    }
    if (status.dwCurrentState == SERVICE_STOPPED) {
        CloseServiceHandle(svc);
        return 1;
    }

    stop_dependents(scm, svc, service, depth);

    if (!ControlService(svc, SERVICE_CONTROL_STOP, &status)) {
        err = GetLastError();
        CloseServiceHandle(svc);
        /* A service that stopped between the query and the control call is
         * a success, not a race to report. */
        if (err == ERROR_SERVICE_NOT_ACTIVE) return 1;
        osr_warnf("services: cannot stop %s (error %lu)", service, (unsigned long)err);
        denied_hint(err, "services");
        return 0;
    }

    if (!wait_for_stopped(svc)) {
        osr_warnf("services: %s did not stop within %d seconds", service, STOP_TIMEOUT_MS / 1000);
        CloseServiceHandle(svc);
        return 0;
    }
    CloseServiceHandle(svc);
    return 1;
}

int osr_wintweak_stop_service(const char *service) {
    SC_HANDLE scm;
    int ok;

    scm = open_scm(SC_MANAGER_CONNECT);
    if (scm == NULL) return 0;
    ok = stop_service_handle(scm, service, DEPENDENT_DEPTH);
    CloseServiceHandle(scm);
    return ok;
}

int osr_wintweak_set_service_start(const char *service, osr_wintweak_start start) {
    SC_HANDLE scm;
    SC_HANDLE svc;
    DWORD start_type;
    DWORD err;
    int ok;

    if (start == OSR_WINTWEAK_START_KEEP) return 1;
    if (start == OSR_WINTWEAK_START_AUTOMATIC) start_type = SERVICE_AUTO_START;
    else if (start == OSR_WINTWEAK_START_MANUAL) start_type = SERVICE_DEMAND_START;
    else start_type = SERVICE_DISABLED;

    scm = open_scm(SC_MANAGER_CONNECT);
    if (scm == NULL) return 0;

    svc = OpenServiceA(scm, service, SERVICE_CHANGE_CONFIG);
    if (svc == NULL) {
        err = GetLastError();
        CloseServiceHandle(scm);
        if (service_missing(err)) {
            osr_infof("services: %s is not present on this system -- skipped", service);
            return 1;
        }
        osr_warnf("services: cannot open %s for config (error %lu)", service, (unsigned long)err);
        denied_hint(err, "services");
        return 0;
    }

    /* SERVICE_NO_CHANGE everywhere but the start type: this is a
     * Set-Service -StartupType, not a reconfiguration. */
    ok = ChangeServiceConfigA(svc, SERVICE_NO_CHANGE, start_type, SERVICE_NO_CHANGE,
                              NULL, NULL, NULL, NULL, NULL, NULL, NULL) != FALSE;
    if (!ok) {
        err = GetLastError();
        osr_warnf("services: cannot set %s start type (error %lu)", service, (unsigned long)err);
        denied_hint(err, "services");
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}

/* remove_tree -- Remove-Item -Recurse -Force. Read-only attributes are
 * cleared as it goes (that is what -Force means for a file), and a path
 * that is already gone is success. */
static int remove_tree(const char *path) {
    char pattern[1024];
    char child[1024];
    WIN32_FIND_DATAA find;
    HANDLE h;
    DWORD attrs;
    int ok = 1;

    attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return 1; /* not there: nothing to do */

    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        if (attrs & FILE_ATTRIBUTE_READONLY) SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
        return DeleteFileA(path) != FALSE;
    }

    if (strlen(path) + 3 >= sizeof(pattern)) return 0;
    strcpy(pattern, path);
    strcat(pattern, "\\*");

    h = FindFirstFileA(pattern, &find);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(find.cFileName, ".") == 0 || strcmp(find.cFileName, "..") == 0) continue;
            if (strlen(path) + 1 + strlen(find.cFileName) >= sizeof(child)) { ok = 0; continue; }
            strcpy(child, path);
            strcat(child, "\\");
            strcat(child, find.cFileName);
            /* A reparse point is followed by nobody here: deleting the link
             * is the intent, never the directory it points at. */
            if ((find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                && !(find.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                if (!remove_tree(child)) ok = 0;
            } else {
                if (find.dwFileAttributes & FILE_ATTRIBUTE_READONLY) {
                    SetFileAttributesA(child, FILE_ATTRIBUTE_NORMAL);
                }
                if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    if (!RemoveDirectoryA(child)) ok = 0;
                } else if (!DeleteFileA(child)) {
                    ok = 0;
                }
            }
        } while (FindNextFileA(h, &find));
        FindClose(h);
    }

    if (attrs & FILE_ATTRIBUTE_READONLY) SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
    if (!RemoveDirectoryA(path)) ok = 0;
    return ok;
}

int osr_wintweak_purge_path(const char *path_spec) {
    char expanded[1024];
    DWORD n;

    if (path_spec == NULL || path_spec[0] == '\0') return 1;

    n = ExpandEnvironmentStringsA(path_spec, expanded, (DWORD)sizeof(expanded));
    if (n == 0 || n > sizeof(expanded)) {
        osr_warnf("purge: cannot expand '%s'", path_spec);
        return 0;
    }
    if (GetFileAttributesA(expanded) == INVALID_FILE_ATTRIBUTES) return 1;

    if (!remove_tree(expanded)) {
        /* Windows keeps handles open on some of these caches while the
         * service is shutting down, so a partial delete is common and is
         * not worth failing the whole tweak over -- the ps1 original
         * discarded the error entirely (-ErrorAction SilentlyContinue).
         * Reporting it and carrying on is the middle ground. */
        osr_warnf("purge: %s could not be fully removed (files still in use?)", expanded);
        return 0;
    }
    osr_successf("purged %s", expanded);
    return 1;
}

int osr_wintweak_apply_reg(const osr_wintweak_reg *rows, unsigned long count) {
    unsigned long i;
    int all_ok = 1;

    for (i = 0; i < count; i++) {
        if (!rows[i].enabled) continue;
        if (osr_wintweak_set_dword(rows[i].key, rows[i].name, rows[i].value)) {
            osr_successf("%s = %lu (%s)", rows[i].name, rows[i].value, rows[i].note);
        } else {
            osr_warnf("%s: not applied", rows[i].name);
            all_ok = 0;
        }
    }
    return all_ok;
}

int osr_wintweak_apply_services(const osr_wintweak_service *rows, unsigned long count) {
    unsigned long i;
    int all_ok = 1;

    for (i = 0; i < count; i++) {
        int ok = 1;
        if (!rows[i].enabled) continue;

        osr_infof("service %s: %s", rows[i].service, rows[i].note);

        /* Stop, then purge, then set the start type. The purge has to
         * happen after the stop (a running service holds its cache open)
         * and before the disable is meaningful; the ps1 files disagreed
         * with each other on this order -- disable-telemetry.ps1 purged
         * last, disable-windows-search.ps1 purged in the middle -- and one
         * consistent order is strictly better than reproducing that. */
        if (rows[i].stop && !osr_wintweak_stop_service(rows[i].service)) ok = 0;
        if (rows[i].purge != NULL && !osr_wintweak_purge_path(rows[i].purge)) ok = 0;
        if (!osr_wintweak_set_service_start(rows[i].service, rows[i].start)) ok = 0;

        if (ok) {
            osr_successf("%s: %s%s", rows[i].service,
                        rows[i].stop ? "stopped, " : "",
                        osr_wintweak_start_name(rows[i].start));
        } else {
            osr_warnf("%s: not fully applied", rows[i].service);
            all_ok = 0;
        }
    }
    return all_ok;
}

#else /* !_WIN32 */

/* There is no Linux equivalent of any of this -- a registry tweak and a
 * Windows service are not concepts POSIX has, and the Linux side's own
 * service handling lives in lib/service.sh where it belongs. The stubs
 * exist so the tables and their tests still compile on a CI host. */

int osr_wintweak_set_dword(const char *key_spec, const char *name, unsigned long value) {
    (void)key_spec; (void)name; (void)value;
    return 0;
}

int osr_wintweak_stop_service(const char *service) {
    (void)service;
    return 0;
}

int osr_wintweak_set_service_start(const char *service, osr_wintweak_start start) {
    (void)service; (void)start;
    return 0;
}

int osr_wintweak_purge_path(const char *path_spec) {
    (void)path_spec;
    return 0;
}

int osr_wintweak_apply_reg(const osr_wintweak_reg *rows, unsigned long count) {
    (void)rows; (void)count;
    return 0;
}

int osr_wintweak_apply_services(const osr_wintweak_service *rows, unsigned long count) {
    (void)rows; (void)count;
    return 0;
}

#endif /* _WIN32 */

/* ===========================================================================
 * the policy: what setup.ps1 actually said
 * ======================================================================== */

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
        osr_warnf("win-tweaks: %d machine-wide setting(s) skipped -- they need "
                 "Administrator rights", skipped);
    }
    return osr_wintweak_apply_reg(rows, REG_COUNT);
}

int osrm_win_tweaks(void) {
    int have_admin;
    int ok = 1;

    /* No package, no font, no config file -- so there is no theme-owned layer
     * to re-render either, and a --theme-only run must not silently
     * reconfigure the operating system. Every other module reaches the same
     * outcome by having its install verbs neutralized and its config verbs
     * left running; this one says so outright, because "the config verbs it
     * has left" would be a registry write. */
    if (osr_theme_only()) return osr_theme_only_skip("win-tweaks");

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

    osr_infof("win-tweaks: services");
    if (have_admin) {
        if (!osr_wintweak_apply_services(g_service_tweaks, SERVICE_COUNT)) ok = 0;
    } else {
        osr_warnf("win-tweaks: service changes skipped -- they need Administrator rights");
        ok = 0;
    }

    osr_infof("win-tweaks: Explorer, taskbar and snap settings");
    if (!apply_reg_rows(have_admin)) ok = 0;

    /* Explorer reads most of Advanced\ once, at startup. Saying so is the
     * difference between "it did not work" and "it has not been reread
     * yet"; restarting Explorer from under the user is not this module's
     * call to make. */
    osr_infof("win-tweaks: sign out (or restart explorer.exe) for the Explorer "
             "and taskbar settings to take effect");

    if (ok) osr_successf("win-tweaks: applied");
    return ok;
}

#else /* !_WIN32 */

/* The tables above are read by test/unit_c/wintweak_test.c wherever the suite
 * runs, which is why they are not inside the guard. The pass itself has no
 * meaning here: there is no registry and no SCM to apply it to. */
int osrm_win_tweaks(void) { return 0; }

#endif /* _WIN32 */
