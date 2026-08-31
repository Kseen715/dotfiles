/* lib/nerdfont.c -- C port of lib/fonts.sh. See lib/fonts.h for the contract
 * and for why this file is not called fonts.c: that name is taken by the
 * Windows core's font installer.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "module.h"

#include "cmds.h"
#include "fetch.h"
#include "nerdfont.h"

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
