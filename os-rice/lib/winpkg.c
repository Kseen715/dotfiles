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
 */
int osr_winpkg_install(const char *map_path, const char *name, const char *test_command) {
    osr_winpkg_spec spec;
    int has_scoop, has_choco, has_winget;
    const char *tc;
    char cmd[600];

    tc = (test_command != NULL) ? test_command : name;
    if (osr_winpkg_have_command(tc)) return 1;

    if (!osr_winpkg_lookup(map_path, name, &spec)) return 0;

    osr_winpkg_available_managers(&has_scoop, &has_choco, &has_winget);

    if (has_scoop && spec.has_scoop) {
        sprintf(cmd, "scoop install %s", spec.scoop);
        if (system(cmd) == 0 && osr_winpkg_have_command(tc)) return 1;
    }
    if (has_choco && spec.has_choco) {
        sprintf(cmd, "choco install %s -y", spec.choco);
        if (system(cmd) == 0 && osr_winpkg_have_command(tc)) return 1;
    }
    if (has_winget && spec.has_winget) {
        sprintf(cmd, "winget install --id %s -e --accept-source-agreements --accept-package-agreements", spec.winget);
        if (system(cmd) == 0 && osr_winpkg_have_command(tc)) return 1;
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
