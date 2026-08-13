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

/* -------------------------------------------------------------------------
 * pure parsing -- plain fopen/fgets, no OS dependency, unit-tested against
 * the real windows-rice/windows.map fixture without a Windows build.
 * ---------------------------------------------------------------------- */

int osr_winpkg_lookup(const char *map_path, const char *name, osr_winpkg_spec *out) {
    FILE *fp;
    char line[512];
    int found;
    unsigned long name_reqlen;

    memset(out, 0, sizeof(*out));
    found = 0;
    name_reqlen = (unsigned long)strlen(name);

    fp = fopen(map_path, "r");
    if (fp == NULL) return 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p;
        char *eq;
        char *name_part;
        unsigned long name_len;
        char *rhs;
        char *tok;

        rtrim(line);
        p = ltrim(line);
        if (*p == '\0' || *p == '#') continue;

        eq = strchr(p, '=');
        if (eq == NULL) continue;

        name_part = p;
        name_len = (unsigned long)(eq - p);
        while (name_len > 0 && (name_part[name_len - 1] == ' ' || name_part[name_len - 1] == '\t')) name_len--;
        if (name_len != name_reqlen || strncmp(name_part, name, name_len) != 0) continue;

        found = 1;
        rhs = ltrim(eq + 1);

        tok = strtok(rhs, " \t");
        while (tok != NULL) {
            char *colon = strchr(tok, ':');
            if (colon != NULL) {
                unsigned long mgr_len = (unsigned long)(colon - tok);
                const char *id = colon + 1;
                unsigned long id_len = (unsigned long)strlen(id);

                if (mgr_len == 5 && strncmp(tok, "scoop", 5) == 0) {
                    copy_bounded(out->scoop, sizeof(out->scoop), id, id_len);
                    out->has_scoop = 1;
                } else if (mgr_len == 5 && strncmp(tok, "choco", 5) == 0) {
                    copy_bounded(out->choco, sizeof(out->choco), id, id_len);
                    out->has_choco = 1;
                } else if (mgr_len == 6 && strncmp(tok, "winget", 6) == 0) {
                    copy_bounded(out->winget, sizeof(out->winget), id, id_len);
                    out->has_winget = 1;
                }
            }
            tok = strtok(NULL, " \t");
        }
    }

    fclose(fp);
    return found;
}

/* -------------------------------------------------------------------------
 * package manager dispatch -- per platform.
 * ---------------------------------------------------------------------- */

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "ui.h"

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

static void refresh_path(void) {
    char machine[4096];
    char user[4096];
    char joined[8192];
    HKEY key;
    DWORD len;

    machine[0] = '\0';
    user[0] = '\0';

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
            "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
            0, KEY_READ, &key) == ERROR_SUCCESS) {
        len = (DWORD)sizeof(machine) - 1;
        if (RegQueryValueExA(key, "Path", NULL, NULL, (BYTE *)machine, &len) != ERROR_SUCCESS) len = 0;
        machine[len] = '\0';
        RegCloseKey(key);
    }

    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        len = (DWORD)sizeof(user) - 1;
        if (RegQueryValueExA(key, "Path", NULL, NULL, (BYTE *)user, &len) != ERROR_SUCCESS) len = 0;
        user[len] = '\0';
        RegCloseKey(key);
    }

    sprintf(joined, "%s;%s", machine, user);
    SetEnvironmentVariableA("Path", joined);
}

/* osr_winpkg_refresh_env -- re-read Machine + User environment variables
 * (including PATH) from the registry into this process. Called after
 * every install attempt below, same point Update-SessionEnvironment is
 * called from pkg.ps1.
 */
static void osr_winpkg_refresh_env(void) {
    refresh_one_scope(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment");
    refresh_one_scope(HKEY_CURRENT_USER, "Environment");
    refresh_path();
}

/* osr_winpkg_bootstrap_scoop -- install scoop itself (no admin required),
 * the one no-admin fallback pkg.ps1's Install-RicePackage reaches for when
 * NONE of scoop/choco/winget are present at all. Same command scoop's own
 * docs give (`irm get.scoop.sh | iex`), run through cmd.exe -> powershell
 * so it works even from an old cmd.exe-only environment.
 */
static void osr_winpkg_bootstrap_scoop(void) {
    if (osr_winpkg_have_command("scoop")) return;
    osr_run_step("installing scoop (no admin required)",
        "powershell -NoProfile -ExecutionPolicy Bypass -Command "
        "\"iwr -useb get.scoop.sh | iex\"");
    osr_winpkg_refresh_env();
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

void osr_winpkg_available_managers(int *has_scoop, int *has_choco, int *has_winget) {
    *has_scoop = osr_winpkg_have_command("scoop");
    *has_choco = osr_winpkg_have_command("choco");
    *has_winget = osr_winpkg_have_command("winget");
}

/* Same scoop -> choco -> winget preference order as pkg.ps1's
 * Install-RicePackage: scoop needs no admin and is fastest for CLI tools,
 * choco covers what scoop's buckets miss, winget is the built-in fallback.
 * If none of the three are present at all, bootstraps scoop first (the
 * one no-admin option), same as Install-RicePackage's own fallback.
 */
int osr_winpkg_install(const char *map_path, const char *name, const char *test_command) {
    osr_winpkg_spec spec;
    int has_scoop, has_choco, has_winget;
    const char *tc;
    char cmd[600];
    char desc[200];

    tc = (test_command != NULL) ? test_command : name;
    if (osr_winpkg_have_command(tc)) return 1;

    if (!osr_winpkg_lookup(map_path, name, &spec)) return 0;

    osr_winpkg_available_managers(&has_scoop, &has_choco, &has_winget);
    if (!has_scoop && !has_choco && !has_winget) {
        osr_winpkg_bootstrap_scoop();
        osr_winpkg_available_managers(&has_scoop, &has_choco, &has_winget);
    }

    if (has_scoop && spec.has_scoop) {
        sprintf(cmd, "scoop install %s", spec.scoop);
        sprintf(desc, "%s via scoop (%s)", name, spec.scoop);
        if (osr_run_step(desc, cmd) == 0) {
            osr_winpkg_refresh_env();
            if (osr_winpkg_have_command(tc)) return 1;
        }
    }
    if (has_choco && spec.has_choco) {
        sprintf(cmd, "choco install %s -y", spec.choco);
        sprintf(desc, "%s via choco (%s)", name, spec.choco);
        if (osr_run_step(desc, cmd) == 0) {
            osr_winpkg_refresh_env();
            if (osr_winpkg_have_command(tc)) return 1;
        }
    }
    if (has_winget && spec.has_winget) {
        sprintf(cmd, "winget install --id %s -e --accept-source-agreements --accept-package-agreements", spec.winget);
        sprintf(desc, "%s via winget (%s)", name, spec.winget);
        if (osr_run_step(desc, cmd) == 0) {
            osr_winpkg_refresh_env();
            if (osr_winpkg_have_command(tc)) return 1;
        }
    }

    return 0;
}

#else /* !_WIN32 */

/* windows.map / scoop / choco / winget are Windows-only concepts. Nothing
 * to dispatch to on other platforms -- lib/pkg.sh already owns package
 * installation there.
 */

int osr_winpkg_have_command(const char *name) {
    (void)name;
    return 0;
}

void osr_winpkg_available_managers(int *has_scoop, int *has_choco, int *has_winget) {
    *has_scoop = 0;
    *has_choco = 0;
    *has_winget = 0;
}

int osr_winpkg_install(const char *map_path, const char *name, const char *test_command) {
    (void)map_path;
    (void)name;
    (void)test_command;
    return 0;
}

#endif /* _WIN32 */
