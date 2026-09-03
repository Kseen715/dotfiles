/* lib/fonts.c -- lib/fonts.sh: install a Nerd Font.
 *
 * Icons and glyphs are a shared cosmetic asset several modules need (foot,
 * starship, wezterm, oh-my-posh), so the logic lives here once rather than
 * being pasted per module. Best-effort by contract on both systems: a font is
 * cosmetic, so a failure warns and the module carries on rather than aborting
 * a rice or breaking the rerun contract (section 2).
 *
 * ONE FILE, TWO BODIES, because "install a font" means two entirely different
 * acts. On POSIX it is: fetch the release zip from ryanoasis/nerd-fonts,
 * unpack it into the user's font directory, and re-run fc-cache -- the font
 * system is a directory of files plus an index. On Windows a font is a
 * REGISTERED object, not a file in a directory, so the work is handed to a
 * package manager (scoop's nerd-fonts bucket, else choco) that knows how to
 * register one.
 *
 * The idempotency probe differs with it: fontconfig on one side, the registry
 * on the other. Both answer the same question -- is a family whose name
 * contains this already here -- and both count "already installed" as success.
 *
 * NOT ported on the Windows side: fonts.ps1's manual
 * download-the-zip-and-register-each-ttf fallback for a machine with neither
 * scoop nor choco (it registers each .ttf through the Shell.Application COM
 * copy). That is a real feature on its own, and it has no data source yet for
 * "which asset for which font" beyond scraping the release JSON -- a known,
 * documented gap rather than a silent omission. scoop and choco cover the
 * realistic case.
 *
 * C89 + POSIX, and C89 + Win32.
 */
#ifdef _WIN32

#include "fonts.h"
#include "module.h"
#include "ui.h"

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

    if (name == NULL || *name == '\0') name = "JetBrainsMono";
    if (osr_font_installed(name)) return 1;

    if (osr_have_cmd("scoop")) {
        system("scoop bucket add nerd-fonts");
        sprintf(cmd, "scoop install nerd-fonts/%s-NF", name);
        if (system(cmd) == 0) return 1;
    }

    if (osr_have_cmd("choco")) {
        to_lower_bounded(lower, sizeof(lower), name);
        sprintf(cmd, "choco install nerd-fonts-%s -y", lower);
        if (system(cmd) == 0) return 1;
    }

    return 0;
}

int osr_fonts_main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "install") == 0) {
        return osr_install_nerd_font(NULL) ? 0 : 1;
    }
    if (argc == 3 && strcmp(argv[1], "install") == 0) {
        return osr_install_nerd_font(argv[2]) ? 0 : 1;
    }
    if (argc == 3 && strcmp(argv[1], "installed") == 0) {
        return osr_font_installed(argv[2]) ? 0 : 1;
    }
    fputs("usage: osr fonts <subcommand> [name]\n\n", stderr);
    fputs("  install [name]     install a Nerd Font (default: JetBrainsMono)\n", stderr);
    fputs("  installed <name>   exit 0 when a matching family is registered\n", stderr);
    return 2;
}

#else /* !_WIN32 */

#define _POSIX_C_SOURCE 200809L

#include "fonts.h"
#include "module.h"
#include "cmds.h"
#include "fetch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NERD_FONT_RELEASES "https://github.com/ryanoasis/nerd-fonts/releases/download/"

/* ci_find -- strstr, case-insensitively, over exactly n bytes of haystack. */
static const char *ci_find(const char *hay, size_t n, const char *needle) {
    size_t need = strlen(needle);
    size_t i;
    size_t j;

    if (need == 0) return hay;
    if (n < need) return NULL;
    for (i = 0; i + need <= n; i++) {
        for (j = 0; j < need; j++) {
            char a = hay[i + j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
        }
        if (j == need) return hay + i;
    }
    return NULL;
}

/* font_registered -- fontconfig already knows this family. The sh probe was
 * `fc-list | grep -qi "<name>.*Nerd"`: the name and then "Nerd" later ON THE
 * SAME LINE, both case-insensitive, because `.` never matches a newline. */
static int font_registered(const char *name) {
    char *argv[2];
    Str listing;
    size_t pos = 0;
    Line line;
    int hit = 0;

    if (!osr_have_cmd("fc-list")) return 0;

    argv[0] = (char *)"fc-list";
    argv[1] = NULL;
    str_init(&listing);
    (void)osr_run_user_capture(argv, &listing);

    while (!hit && next_line(str_text(&listing), listing.len, &pos, &line)) {
        const char *at = ci_find(line.start, line.len, name);
        if (at == NULL) continue;
        at += strlen(name);
        hit = ci_find(at, (size_t)(line.start + line.len - at), "Nerd") != NULL;
    }
    str_free(&listing);
    return hit;
}

int osr_install_nerd_font(const char *name) {
    Str url;
    Str dir;
    Str zip;
    char *argv[7];

    if (osr_theme_only()) return osr_theme_only_skip("install_nerd_font");

    if (name == NULL || *name == '\0') name = "JetBrainsMono";

    if (font_registered(name)) {
        osr_infof("%s Nerd Font already installed - skipping", name);
        return 1;
    }
    if (!osr_have_cmd("unzip")) {
        osr_warnf("unzip not available - skipping %s Nerd Font install", name);
        return 1;
    }

    str_init(&url);
    str_addz(&url, NERD_FONT_RELEASES);
    str_addz(&url, env_str("OSR_NERD_FONT_VERSION", "v3.4.0"));
    str_addc(&url, '/');
    str_addz(&url, name);
    str_addz(&url, ".zip");

    str_init(&dir);
    str_addz(&dir, osr_mod_home());
    str_addz(&dir, "/.local/share/fonts");

    str_init(&zip);
    str_addz(&zip, env_str("TMPDIR", "/tmp"));
    str_addc(&zip, '/');
    str_addz(&zip, name);
    str_addc(&zip, '-');
    str_addl(&zip, (long)getpid());
    str_addz(&zip, ".zip");

    (void)osr_mkdir_p(str_text(&dir));

    if (!osr_fetch_download(str_text(&url), str_text(&zip), 0)) {
        osr_warnf("failed to download %s Nerd Font (%s) - skipping", name, str_text(&url));
        remove(str_text(&zip));
    } else {
        argv[0] = (char *)"unzip"; argv[1] = (char *)"-o";
        argv[2] = (char *)str_text(&zip); argv[3] = (char *)"-d";
        argv[4] = (char *)str_text(&dir); argv[5] = NULL;
        if (osr_run_user_quiet(argv) != 0) {
            osr_warnf("failed to unzip %s Nerd Font - skipping", name);
            remove(str_text(&zip));
        } else {
            /* The zip goes before the cache refresh, the order sh wrote it
             * in: fc-cache is the slow part and there is no reason to hold a
             * scratch file across it. */
            remove(str_text(&zip));
            if (osr_have_cmd("fc-cache")) {
                argv[0] = (char *)"fc-cache"; argv[1] = (char *)"-f";
                argv[2] = (char *)str_text(&dir); argv[3] = NULL;
                (void)osr_run_user_quiet(argv);
            }
        }
    }

    str_free(&url);
    str_free(&dir);
    str_free(&zip);
    return 1;
}

int osr_fonts_main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "nerd") == 0)
        return osr_install_nerd_font(NULL) ? 0 : 1;
    if (argc == 3 && strcmp(argv[1], "nerd") == 0)
        return osr_install_nerd_font(argv[2]) ? 0 : 1;

    fputs("usage: osr fonts nerd [family]\n", stderr);
    return 2;
}

#endif /* _WIN32 */
