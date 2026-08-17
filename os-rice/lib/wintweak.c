/* lib/wintweak.c -- see lib/wintweak.h. C89. */
#include "wintweak.h"

#include <stddef.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * parsing -- pure, builds on every host so the tables in
 * modules/windows/tweaks.c can be unit-tested without a registry
 * ---------------------------------------------------------------------- */

/* prefix_len -- length of `prefix` if `spec` starts with it (case
 * insensitively, since the ps1 files were inconsistent about "HKCU:" vs
 * "hkcu:") AND the next character actually ends the hive name, else 0.
 * That last condition is what stops "HKCUSTOM\Foo" from being read as
 * HKCU\STOM\Foo -- a prefix match alone would silently write the tweak to
 * a real key that is not the one asked for. */
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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "ui.h"

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
        osr_warn("%s: access denied -- this needs Administrator rights", what);
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
        osr_warn("registry: unusable key path '%s'", key_spec);
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
        osr_warn("registry: cannot open %s\\%s (error %lu)",
                 osr_wintweak_hive_name(hive), subkey, (unsigned long)rc);
        denied_hint((DWORD)rc, "registry");
        return 0;
    }

    dword = (DWORD)value;
    rc = RegSetValueExA(key, name, 0, REG_DWORD, (const BYTE *)&dword, (DWORD)sizeof(dword));
    RegCloseKey(key);

    if (rc != ERROR_SUCCESS) {
        osr_warn("registry: cannot write %s\\%s\\%s (error %lu)",
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
        osr_warn("services: cannot open the service control manager (error %lu)",
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
            osr_info("services: stopping %s first (%s depends on it)",
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
        osr_warn("services: %s has more dependents than can be listed at once "
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
            osr_info("services: %s is not present on this system -- skipped", service);
            return 1;
        }
        osr_warn("services: cannot open %s (error %lu)", service, (unsigned long)err);
        denied_hint(err, "services");
        return 0;
    }

    if (!QueryServiceStatus(svc, &status)) {
        osr_warn("services: cannot query %s (error %lu)", service, (unsigned long)GetLastError());
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
        osr_warn("services: cannot stop %s (error %lu)", service, (unsigned long)err);
        denied_hint(err, "services");
        return 0;
    }

    if (!wait_for_stopped(svc)) {
        osr_warn("services: %s did not stop within %d seconds", service, STOP_TIMEOUT_MS / 1000);
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
            osr_info("services: %s is not present on this system -- skipped", service);
            return 1;
        }
        osr_warn("services: cannot open %s for config (error %lu)", service, (unsigned long)err);
        denied_hint(err, "services");
        return 0;
    }

    /* SERVICE_NO_CHANGE everywhere but the start type: this is a
     * Set-Service -StartupType, not a reconfiguration. */
    ok = ChangeServiceConfigA(svc, SERVICE_NO_CHANGE, start_type, SERVICE_NO_CHANGE,
                              NULL, NULL, NULL, NULL, NULL, NULL, NULL) != FALSE;
    if (!ok) {
        err = GetLastError();
        osr_warn("services: cannot set %s start type (error %lu)", service, (unsigned long)err);
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
        osr_warn("purge: cannot expand '%s'", path_spec);
        return 0;
    }
    if (GetFileAttributesA(expanded) == INVALID_FILE_ATTRIBUTES) return 1;

    if (!remove_tree(expanded)) {
        /* Windows keeps handles open on some of these caches while the
         * service is shutting down, so a partial delete is common and is
         * not worth failing the whole tweak over -- the ps1 original
         * discarded the error entirely (-ErrorAction SilentlyContinue).
         * Reporting it and carrying on is the middle ground. */
        osr_warn("purge: %s could not be fully removed (files still in use?)", expanded);
        return 0;
    }
    osr_success("purged %s", expanded);
    return 1;
}

int osr_wintweak_apply_reg(const osr_wintweak_reg *rows, unsigned long count) {
    unsigned long i;
    int all_ok = 1;

    for (i = 0; i < count; i++) {
        if (!rows[i].enabled) continue;
        if (osr_wintweak_set_dword(rows[i].key, rows[i].name, rows[i].value)) {
            osr_success("%s = %lu (%s)", rows[i].name, rows[i].value, rows[i].note);
        } else {
            osr_warn("%s: not applied", rows[i].name);
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

        osr_info("service %s: %s", rows[i].service, rows[i].note);

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
            osr_success("%s: %s%s", rows[i].service,
                        rows[i].stop ? "stopped, " : "",
                        osr_wintweak_start_name(rows[i].start));
        } else {
            osr_warn("%s: not fully applied", rows[i].service);
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
