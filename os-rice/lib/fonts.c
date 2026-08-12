/* lib/fonts.c -- see lib/fonts.h. C89. */
#include "fonts.h"
#include "winpkg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ci_char_eq(char a, char b) {
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    return a == b;
}

/* ci_substr -- 1 if needle occurs anywhere in haystack, case-insensitive. */
static int ci_substr(const char *haystack, const char *needle) {
    unsigned long h_len = (unsigned long)strlen(haystack);
    unsigned long n_len = (unsigned long)strlen(needle);
    unsigned long i, j;

    if (n_len == 0) return 1;
    if (n_len > h_len) return 0;

    for (i = 0; i + n_len <= h_len; i++) {
        int match = 1;
        for (j = 0; j < n_len; j++) {
            if (!ci_char_eq(haystack[i + j], needle[j])) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

static void to_lower_bounded(char *dst, unsigned long dst_sz, const char *src) {
    unsigned long i;
    unsigned long len = (unsigned long)strlen(src);
    if (len >= dst_sz) len = dst_sz - 1;
    for (i = 0; i < len; i++) {
        char c = src[i];
        dst[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    dst[len] = '\0';
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* registry_has_font_substring -- scan every value name under the given
 * root's installed-fonts key for a case-insensitive match. Both
 * HKLM (machine-wide) and HKCU (per-user, Win10 1809+) are checked --
 * see osr_font_installed.
 */
static int registry_has_font_substring(HKEY root, const char *name) {
    HKEY hKey;
    DWORD index;
    int found;

    if (RegOpenKeyExA(root, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
                       0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return 0;
    }

    found = 0;
    for (index = 0; ; index++) {
        char value_name[256];
        DWORD value_name_len = sizeof(value_name);
        if (RegEnumValueA(hKey, index, value_name, &value_name_len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) {
            break;
        }
        if (ci_substr(value_name, name)) { found = 1; break; }
    }

    RegCloseKey(hKey);
    return found;
}

int osr_font_installed(const char *name) {
    return registry_has_font_substring(HKEY_LOCAL_MACHINE, name)
        || registry_has_font_substring(HKEY_CURRENT_USER, name);
}

int osr_install_nerd_font(const char *name) {
    char cmd[300];
    char lower[128];

    if (osr_font_installed(name)) return 1;

    if (osr_winpkg_have_command("scoop")) {
        system("scoop bucket add nerd-fonts");
        sprintf(cmd, "scoop install nerd-fonts/%s-NF", name);
        if (system(cmd) == 0) return 1;
    }

    if (osr_winpkg_have_command("choco")) {
        to_lower_bounded(lower, sizeof(lower), name);
        sprintf(cmd, "choco install nerd-fonts-%s -y", lower);
        if (system(cmd) == 0) return 1;
    }

    return 0;
}

#else /* !_WIN32 */

int osr_font_installed(const char *name) {
    (void)name;
    return 0;
}

int osr_install_nerd_font(const char *name) {
    (void)name;
    return 0;
}

#endif /* _WIN32 */
