/* lib/winpkg.c -- see lib/winpkg.h. C89. */
#include "winpkg.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static char *ltrim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void rtrim(char *s) {
    unsigned long len = (unsigned long)strlen(s);
    while (len > 0) {
        char c = s[len - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { s[len - 1] = '\0'; len--; }
        else break;
    }
}

static void copy_bounded(char *dst, unsigned long dst_sz, const char *src, unsigned long src_len) {
    if (src_len >= dst_sz) src_len = dst_sz - 1;
    memcpy(dst, src, src_len);
    dst[src_len] = '\0';
}

const char *osr_winpkg_mgr_name(osr_winpkg_mgr mgr) {
    switch (mgr) {
        case OSR_WINPKG_MGR_SCOOP:  return "scoop";
        case OSR_WINPKG_MGR_CHOCO:  return "choco";
        case OSR_WINPKG_MGR_WINGET: return "winget";
        default:                    return "none";
    }
}

/* -------------------------------------------------------------------------
 * pure parsing -- plain fopen/fgets, no OS dependency, unit-tested against
 * the real windows.map plus a fixture map holding the malformed rows the
 * real one must never contain.
 * ---------------------------------------------------------------------- */

static int make_bare_key(char *dst, unsigned long dst_sz, const char *name) {
    unsigned long name_len = (unsigned long)strlen(name);
    if (name_len >= dst_sz) return 0;
    memcpy(dst, name, name_len + 1);
    return 1;
}

/* make_key -- write `name@facet` into dst. Returns 0 when the facet is unset
 * (that facet contributes no candidate, exactly as ${VAR:+...} drops one in
 * lib/pkg.sh -- an unset facet must NOT collapse into the bare name, or it
 * would outrank the facets below it) or when the key would not fit, since a
 * truncated key could collide with a different row.
 */
static int make_key(char *dst, unsigned long dst_sz, const char *name, const char *facet) {
    unsigned long name_len = (unsigned long)strlen(name);
    unsigned long facet_len;

    if (facet == NULL || facet[0] == '\0') return 0;

    facet_len = (unsigned long)strlen(facet);
    if (name_len + 1 + facet_len >= dst_sz) return 0;
    memcpy(dst, name, name_len);
    dst[name_len] = '@';
    memcpy(dst + name_len + 1, facet, facet_len + 1);
    return 1;
}

/* strip_inline_comment -- drop a trailing ` # ...`, matching lib/pkg.sh's
 * rule that whitespace must precede the '#' (so an id containing '#'
 * survives).
 */
static void strip_inline_comment(char *s) {
    char *p;
    for (p = s; *p != '\0'; p++) {
        if (*p == '#' && p > s && (p[-1] == ' ' || p[-1] == '\t')) { *p = '\0'; return; }
    }
}

/* parse_mgr_token -- `<mgr>:<id>` into out. 0 when it is not one. */
static int parse_mgr_token(const char *tok, osr_winpkg_spec *out) {
    const char *colon = strchr(tok, ':');
    const char *id;
    const char *slash;
    unsigned long mgr_len;

    if (colon == NULL) return 0;
    mgr_len = (unsigned long)(colon - tok);
    id = colon + 1;

    if (id[0] == '\0') return 0;
    if (strlen(id) >= sizeof(out->id)) return 0;

    if (mgr_len == 5 && strncmp(tok, "scoop", 5) == 0)       out->mgr = OSR_WINPKG_MGR_SCOOP;
    else if (mgr_len == 5 && strncmp(tok, "choco", 5) == 0)  out->mgr = OSR_WINPKG_MGR_CHOCO;
    else if (mgr_len == 6 && strncmp(tok, "winget", 6) == 0) out->mgr = OSR_WINPKG_MGR_WINGET;
    else return 0;

    strcpy(out->id, id);

    /* scoop only: `bucket/name` names a bucket that has to be added first. */
    slash = strchr(out->id, '/');
    if (slash != NULL && out->mgr == OSR_WINPKG_MGR_SCOOP) {
        if (slash == out->id || slash[1] == '\0') return 0;
        copy_bounded(out->bucket, sizeof(out->bucket), out->id, (unsigned long)(slash - out->id));
    }

    return 1;
}

/* parse_rhs -- turn one row's right-hand side into a spec.
 *
 * EXACTLY ONE provider per row. Not "one manager plus a fallback", not "the
 * first one that works" -- one. A row says where this package comes from on
 * this machine, and if that has to differ (per architecture, per Windows
 * release) the difference is expressed as a second row with an @facet key,
 * resolved before this function ever sees it. Anything else is a map error.
 *
 * That is the same shape the lib/pkgmap maps have always had on the Linux
 * side, and it is what keeps the guarantee decision 10 was written for: a
 * package resolves in one namespace, chosen deliberately, never fallen into.
 */
static int parse_rhs(char *rhs, osr_winpkg_spec *out) {
    char *tok;

    strip_inline_comment(rhs);
    rtrim(rhs);

    tok = strtok(rhs, " \t");
    if (tok == NULL) return OSR_WINPKG_BAD_ROW;
    if (strtok(NULL, " \t") != NULL) return OSR_WINPKG_BAD_ROW;

    /* bin: is a provider like any other, not a modifier on a manager. */
    if (strncmp(tok, "bin:", 4) == 0) {
        if (tok[4] == '\0' || strlen(tok + 4) >= sizeof(out->bin)) return OSR_WINPKG_BAD_ROW;
        strcpy(out->bin, tok + 4);
        return OSR_WINPKG_OK;
    }

    if (!parse_mgr_token(tok, out)) return OSR_WINPKG_BAD_ROW;
    return OSR_WINPKG_OK;
}

int osr_winpkg_lookup(const char *map_path, const char *name,
                      const osr_winpkg_facets *facets, osr_winpkg_spec *out) {
    char cand[4][OSR_WINPKG_KEY_MAX];
    int cand_count;
    int best_rank;
    char best_rhs[512];
    FILE *fp;
    char line[512];

    memset(out, 0, sizeof(*out));
    best_rhs[0] = '\0';
    best_rank = -1;
    cand_count = 0;

    /* Candidate keys, most specific first -- the same order lib/pkg.sh
     * builds its own (codename, version_id, arch, bare name). An unset
     * facet simply contributes no key. */
    if (facets != NULL) {
        if (make_key(cand[cand_count], OSR_WINPKG_KEY_MAX, name, facets->release)) cand_count++;
        if (make_key(cand[cand_count], OSR_WINPKG_KEY_MAX, name, facets->version)) cand_count++;
        if (make_key(cand[cand_count], OSR_WINPKG_KEY_MAX, name, facets->arch)) cand_count++;
    }
    if (!make_bare_key(cand[cand_count], OSR_WINPKG_KEY_MAX, name)) return OSR_WINPKG_NOT_FOUND;
    cand_count++;

    fp = fopen(map_path, "r");
    if (fp == NULL) return OSR_WINPKG_NOT_FOUND;

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p;
        char *eq;
        char *rhs;
        unsigned long lhs_len;
        int rank;

        rtrim(line);
        p = ltrim(line);
        if (*p == '\0' || *p == '#') continue;

        eq = strchr(p, '=');
        if (eq == NULL) continue;

        lhs_len = (unsigned long)(eq - p);
        while (lhs_len > 0 && (p[lhs_len - 1] == ' ' || p[lhs_len - 1] == '\t')) lhs_len--;

        for (rank = 0; rank < cand_count; rank++) {
            unsigned long key_len = (unsigned long)strlen(cand[rank]);
            if (lhs_len != key_len || strncmp(p, cand[rank], key_len) != 0) continue;

            /* More specific wins; among rows of equal specificity the first
             * one does, so a later duplicate can never quietly override. */
            if (best_rank >= 0 && rank >= best_rank) break;

            rhs = ltrim(eq + 1);
            best_rank = rank;
            copy_bounded(out->key, sizeof(out->key), cand[rank], key_len);
            copy_bounded(best_rhs, sizeof(best_rhs), rhs, (unsigned long)strlen(rhs));
            break;
        }

        if (best_rank == 0) break; /* nothing can outrank the most specific key */
    }

    fclose(fp);

    if (best_rank < 0) return OSR_WINPKG_NOT_FOUND;
    return parse_rhs(best_rhs, out);
}

/* -------------------------------------------------------------------------
 * package manager dispatch -- per platform.
 * ---------------------------------------------------------------------- */

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "elevate.h"
#include "ui.h"
#include "winbin.h"

#ifndef PROCESSOR_ARCHITECTURE_ARM64
#define PROCESSOR_ARCHITECTURE_ARM64 12
#endif

/* refresh_one_scope -- copy every value under an Environment registry key
 * (HKLM's Machine scope or HKCU's User scope) into this process's own
 * environment. C port of common.ps1's Update-SessionEnvironment: a
 * package manager that just installed something (e.g. oh-my-posh setting
 * POSH_THEMES_PATH) only writes the registry -- this process would never
 * see it without re-reading it back out, normally requiring a new shell.
 * PATH itself is skipped here and rebuilt separately below, since the
 * running value is Machine;User joined, not either alone.
 */
static void refresh_one_scope(HKEY root, const char *subkey) {
    HKEY key;
    DWORD index;
    char name[256];
    char value[4096];

    if (RegOpenKeyExA(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) return;

    index = 0;
    for (;;) {
        DWORD name_len = (DWORD)sizeof(name);
        DWORD value_len = (DWORD)sizeof(value);
        DWORD type;
        LONG rc = RegEnumValueA(key, index, name, &name_len, NULL, &type, (BYTE *)value, &value_len);
        if (rc == ERROR_NO_MORE_ITEMS) break;
        if (rc != ERROR_SUCCESS) break;

        /* RegEnumValueA does not guarantee a null terminator for a REG_SZ
         * value that was stored without one -- force one within bounds. */
        if (value_len >= sizeof(value)) value_len = sizeof(value) - 1;
        value[value_len] = '\0';

        if ((type == REG_SZ || type == REG_EXPAND_SZ) && _stricmp(name, "Path") != 0) {
            SetEnvironmentVariableA(name, value);
        }
        index++;
    }

    RegCloseKey(key);
}

/* reg_read_str -- one REG_SZ value into out, 1 on success (non-empty). */
static int reg_read_str(HKEY root, const char *subkey, const char *value,
                        char *out, unsigned long out_sz) {
    HKEY key;
    DWORD len;
    LONG rc;

    out[0] = '\0';
    if (RegOpenKeyExA(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) return 0;

    len = (DWORD)out_sz - 1;
    rc = RegQueryValueExA(key, value, NULL, NULL, (BYTE *)out, &len);
    RegCloseKey(key);

    if (rc != ERROR_SUCCESS) { out[0] = '\0'; return 0; }
    if (len >= out_sz) len = (DWORD)out_sz - 1;
    out[len] = '\0';
    return out[0] != '\0';
}

static void refresh_path(void) {
    char machine[4096];
    char user[4096];
    char joined[8192];

    reg_read_str(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
        "Path", machine, sizeof(machine));
    reg_read_str(HKEY_CURRENT_USER, "Environment", "Path", user, sizeof(user));

    sprintf(joined, "%s;%s", machine, user);
    SetEnvironmentVariableA("Path", joined);
}

/* osr_winpkg_refresh_env -- see winpkg.h. Called after every install
 * attempt below, the same point Update-SessionEnvironment is called from
 * pkg.ps1.
 */
void osr_winpkg_refresh_env(void) {
    refresh_one_scope(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment");
    refresh_one_scope(HKEY_CURRENT_USER, "Environment");
    refresh_path();
}

#define OSR_WINVER_KEY "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"

void osr_winpkg_detect_facets(osr_winpkg_facets *out) {
    char build[32];
    SYSTEM_INFO si;

    memset(out, 0, sizeof(*out));

    /* DisplayVersion is the modern name (21H2 onward); ReleaseId is what
     * older builds wrote instead. Either one is the "codename" analogue. */
    if (!reg_read_str(HKEY_LOCAL_MACHINE, OSR_WINVER_KEY, "DisplayVersion",
                      out->release, sizeof(out->release))) {
        reg_read_str(HKEY_LOCAL_MACHINE, OSR_WINVER_KEY, "ReleaseId",
                     out->release, sizeof(out->release));
    }

    /* Windows 11 still reports major version 10, so the build number is the
     * only honest way to tell the two apart: 22000 is the 11 threshold. */
    if (reg_read_str(HKEY_LOCAL_MACHINE, OSR_WINVER_KEY, "CurrentBuild",
                     build, sizeof(build))) {
        strcpy(out->version, atoi(build) >= 22000 ? "11" : "10");
    }

    /* GetNativeSystemInfo, not GetSystemInfo: under WOW64 the latter reports
     * the emulated architecture, which is the wrong facet to key a package
     * choice on. Names match lib/detect.sh's OSR_ARCH values. */
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: strcpy(out->arch, "x86_64"); break;
        case PROCESSOR_ARCHITECTURE_ARM64: strcpy(out->arch, "arm64");  break;
        case PROCESSOR_ARCHITECTURE_INTEL: strcpy(out->arch, "x86");    break;
        default: break;
    }
}

int osr_winpkg_have_command(const char *name) {
    static const char *exts[4] = { NULL, ".exe", ".cmd", ".bat" };
    unsigned long i;
    char buf[MAX_PATH];

    for (i = 0; i < 4; i++) {
        DWORD len = SearchPathA(NULL, name, exts[i], (DWORD)sizeof(buf), buf, NULL);
        if (len > 0 && len < sizeof(buf)) return 1;
    }
    return 0;
}

/* mgr_bootstrap_needs_admin -- can this manager be installed without
 * elevation? Only scoop can: it deploys per-user under %USERPROFILE%.
 * choco writes C:\ProgramData and winget's prerequisites are machine-wide
 * packages, so both need Administrator to install (note: to *install the
 * manager* -- installing packages with an already-present winget usually
 * does not).
 */
static int mgr_bootstrap_needs_admin(osr_winpkg_mgr mgr) {
    return mgr != OSR_WINPKG_MGR_SCOOP;
}

/* bootstrap_scoop -- scoop's own documented installer (scoop.sh /
 * ScoopInstaller/Install). It deploys per-user under %USERPROFILE%\scoop
 * and needs no elevation; from an elevated run it refuses unless
 * -RunAsAdmin is passed, so both forms are covered. The admin variant uses
 * a script block so it can take that parameter without nesting quotes
 * inside the already-quoted -Command argument.
 */
static void bootstrap_scoop(void) {
    if (osr_is_admin()) {
        osr_run_step("installing scoop (elevated)",
            "powershell -NoProfile -ExecutionPolicy Bypass -Command "
            "\"& ([scriptblock]::Create((irm get.scoop.sh))) -RunAsAdmin\"");
    } else {
        osr_run_step("installing scoop (no admin required)",
            "powershell -NoProfile -ExecutionPolicy Bypass -Command "
            "\"irm get.scoop.sh | iex\"");
    }
}

/* bootstrap_choco -- the install one-liner from chocolatey.org/install. The
 * TLS 1.2 opt-in (3072) is part of it: the script is fetched by .NET's
 * WebClient, whose default protocol set is older than what
 * community.chocolatey.org accepts.
 */
static int bootstrap_choco(void) {
    if (!osr_is_admin()) {
        osr_warn("choco is not installed, and its installer requires Administrator rights.");
        osr_warn("  or run this yourself in an Administrator PowerShell:");
        osr_warn("  Set-ExecutionPolicy Bypass -Scope Process -Force; iex ((New-Object "
                 "System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))");
        osr_warn("  https://chocolatey.org/install");
        return 0;
    }
    osr_run_step("installing chocolatey",
        "powershell -NoProfile -ExecutionPolicy Bypass -Command "
        "\"[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor 3072; "
        "iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))\"");
    return 1;
}

/* bootstrap_winget -- winget ships with Windows 11 and current Windows 10
 * (as part of App Installer), so reaching here means an image that never
 * got it. Microsoft's documented route for that case is the Store, with no
 * supported command-line installer, so this uses asheroto/winget-install,
 * which installs the prerequisites (VCLibs, UI.Xaml) plus App Installer
 * itself. Fetched from the project's own release asset rather than its URL
 * shortener, so the command says what it runs. Needs elevation, and
 * Windows 10 1809+ -- older builds cannot run winget at all.
 */
static int bootstrap_winget(void) {
    if (!osr_is_admin()) {
        osr_warn("winget is not installed, and installing it requires Administrator rights.");
        osr_warn("  or run this yourself in an Administrator PowerShell:");
        osr_warn("  irm https://github.com/asheroto/winget-install/releases/latest/download/"
                 "winget-install.ps1 | iex");
        osr_warn("  https://github.com/asheroto/winget-install");
        return 0;
    }
    osr_run_step("installing winget (App Installer + prerequisites)",
        "powershell -NoProfile -ExecutionPolicy Bypass -Command "
        "\"irm https://github.com/asheroto/winget-install/releases/latest/download/"
        "winget-install.ps1 | iex\"");
    return 1;
}

int osr_winpkg_ensure_manager(osr_winpkg_mgr mgr) {
    const char *exe = osr_winpkg_mgr_name(mgr);
    char reason[200];

    if (mgr == OSR_WINPKG_MGR_NONE) return 0;
    if (osr_winpkg_have_command(exe)) return 1;

    if (mgr_bootstrap_needs_admin(mgr) && !osr_is_admin()) {
        sprintf(reason, "%s is not installed, and installing it needs Administrator rights.", exe);
        if (!osr_elevate_now(reason)) {
            /* Declined -- bootstrap_* below prints the manual command, and
             * the caller falls back to the row's bin: route if it has one. */
        }
    }

    switch (mgr) {
        case OSR_WINPKG_MGR_SCOOP:  bootstrap_scoop(); break;
        case OSR_WINPKG_MGR_CHOCO:  if (!bootstrap_choco()) return 0; break;
        case OSR_WINPKG_MGR_WINGET: if (!bootstrap_winget()) return 0; break;
        default: return 0;
    }

    osr_winpkg_refresh_env();
    if (osr_winpkg_have_command(exe)) return 1;

    osr_warn("%s is still not on PATH after installing it -- a new shell may be needed", exe);
    return 0;
}

/* ensure_bucket -- `scoop bucket add <b>`, tolerated when the bucket is
 * already present (scoop exits non-zero in that case, which is not an
 * error here). A genuinely failed add surfaces as the install below not
 * finding the manifest, which is the message worth showing.
 */
static void ensure_bucket(const char *bucket) {
    char cmd[300];
    char desc[200];
    sprintf(cmd, "scoop bucket add %s || exit /b 0", bucket);
    sprintf(desc, "scoop bucket %s", bucket);
    osr_run_step(desc, cmd);
}

int osr_winpkg_install(const char *map_path, const char *name, const char *test_command) {
    osr_winpkg_facets facets;
    osr_winpkg_spec spec;
    int status;
    const char *tc;
    char cmd[600];
    char desc[200];

    tc = (test_command != NULL) ? test_command : name;
    if (osr_winpkg_have_command(tc)) return 1;

    osr_winpkg_detect_facets(&facets);
    status = osr_winpkg_lookup(map_path, name, &facets, &spec);

    if (status == OSR_WINPKG_NOT_FOUND) {
        osr_warn("no windows.map row for '%s' (this machine: %s / %s / %s)",
                 name, facets.release, facets.version, facets.arch);
        return 0;
    }
    if (status != OSR_WINPKG_OK) {
        osr_warn("windows.map row '%s' is malformed: it must name exactly one provider "
                 "-- <scoop|choco|winget>:<id> or bin:<spec>, never both", name);
        return 0;
    }

    /* One provider, taken as written. A `bin:` row goes straight to the
     * vendor artifact; a manager row uses that manager, installing it first
     * if this machine does not have it yet. There is no crossover between
     * the two: which provider serves a package is the map's decision, made
     * per machine through @facet keys, not a runtime scramble. */
    if (spec.mgr == OSR_WINPKG_MGR_NONE) return osr_winbin_install(spec.bin, name, tc);

    if (!osr_winpkg_ensure_manager(spec.mgr)) {
        osr_warn("  %-14s skipped: windows.map provides it through %s, which is not "
                 "installed and could not be installed",
                 name, osr_winpkg_mgr_name(spec.mgr));
        return 0;
    }

    switch (spec.mgr) {
        case OSR_WINPKG_MGR_SCOOP:
            if (spec.bucket[0] != '\0') ensure_bucket(spec.bucket);
            sprintf(cmd, "scoop install %s", spec.id);
            break;
        case OSR_WINPKG_MGR_CHOCO:
            sprintf(cmd, "choco install %s -y", spec.id);
            break;
        default:
            /* --id ... -e: exact id, and --source winget so a user's extra
             * source cannot answer for it. Without -e winget accepts a
             * substring match across name/id/moniker, which would reopen
             * the ambiguity this map exists to close. */
            sprintf(cmd, "winget install --id %s -e --source winget "
                         "--accept-source-agreements --accept-package-agreements", spec.id);
            break;
    }

    sprintf(desc, "%s via %s (%s)", name, osr_winpkg_mgr_name(spec.mgr), spec.id);
    if (osr_run_step(desc, cmd) != 0) return 0;

    osr_winpkg_refresh_env();
    if (osr_winpkg_have_command(tc)) return 1;

    /* The manager reported success but the command is not visible yet --
     * an installer that only writes PATH for new sessions (MSIX packages
     * do this). Report the install, not a failure. */
    osr_warn("%s installed, but '%s' is not on PATH yet -- open a new shell", name, tc);
    return 1;
}

/* bin_needs_admin -- does this bin: row hand off to a system-wide installer?
 * msi and setup do; zip and exe stay inside %LOCALAPPDATA%. For a gh: source
 * the pattern still carries the extension, so this answers correctly without
 * resolving the release first.
 */
static int bin_needs_admin(const char *spec) {
    osr_winbin_kind kind;
    char args[200];
    char source[600];

    if (!osr_winbin_parse_spec(spec, &kind, args, sizeof(args), source, sizeof(source))) return 0;
    if (kind == OSR_WINBIN_KIND_AUTO) kind = osr_winbin_kind_of_file(source);

    return kind == OSR_WINBIN_KIND_MSI || kind == OSR_WINBIN_KIND_SETUP;
}

int osr_winpkg_run_needs_admin(const char *map_path, char **names, int count) {
    osr_winpkg_facets facets;
    int i;

    osr_winpkg_detect_facets(&facets);

    for (i = 0; i < count; i++) {
        osr_winpkg_spec spec;

        if (osr_winpkg_lookup(map_path, names[i], &facets, &spec) != OSR_WINPKG_OK) continue;

        if (spec.mgr == OSR_WINPKG_MGR_NONE) {
            if (bin_needs_admin(spec.bin)) return 1;
            continue;
        }

        if (osr_winpkg_have_command(osr_winpkg_mgr_name(spec.mgr))) continue;
        if (!mgr_bootstrap_needs_admin(spec.mgr)) continue;     /* scoop: per-user */

        return 1;
    }

    return 0;
}

#else /* !_WIN32 */

/* windows.map / scoop / choco / winget are Windows-only concepts. Nothing
 * to dispatch to on other platforms -- lib/pkg.sh already owns package
 * installation there. osr_winpkg_lookup above still works everywhere,
 * which is what the unit tests exercise on a Linux host.
 */

void osr_winpkg_detect_facets(osr_winpkg_facets *out) {
    memset(out, 0, sizeof(*out));
}

void osr_winpkg_refresh_env(void) {
}

int osr_winpkg_have_command(const char *name) {
    (void)name;
    return 0;
}

int osr_winpkg_ensure_manager(osr_winpkg_mgr mgr) {
    (void)mgr;
    return 0;
}

int osr_winpkg_run_needs_admin(const char *map_path, char **names, int count) {
    (void)map_path;
    (void)names;
    (void)count;
    return 0;
}

int osr_winpkg_install(const char *map_path, const char *name, const char *test_command) {
    (void)map_path;
    (void)name;
    (void)test_command;
    return 0;
}

#endif /* _WIN32 */
