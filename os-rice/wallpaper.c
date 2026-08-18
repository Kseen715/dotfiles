/* wallpaper.c -- C port of wallpaper.sh, a standalone program (not a mode
 * of install.exe, same separation wallpaper.sh has from install.sh: this
 * is not an install, no packages, no modules run).
 *
 *   wallpaper                 print the wallpaper in use
 *   wallpaper --list          print the library (theme images + Pictures\Wallpapers)
 *   wallpaper <path>          make <path> the wallpaper of the current theme
 *   wallpaper --next          step to the next image in the library
 *
 * Reads the current theme from state.h (osr theme/install.exe records it
 * there), same as wallpaper.sh reads it via osr_state_get theme.
 *
 * C89.
 */
#include "lib/winstate.h"
#include "lib/wallpaper.h"
#include "lib/winui.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define OSR_MAX_PATH_C 1024
#define OSR_DEFAULT_THEME "xin"

static void copy_bounded(char *dst, unsigned long dst_sz, const char *src) {
    unsigned long len = (unsigned long)strlen(src);
    if (dst_sz == 0) return;
    if (len >= dst_sz) len = dst_sz - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void path_join(char *out, unsigned long out_sz, const char *a, const char *b) {
    unsigned long len_a = (unsigned long)strlen(a);
    unsigned long len_b = (unsigned long)strlen(b);
    unsigned long need;
    int has_sep = (len_a > 0 && (a[len_a - 1] == '/' || a[len_a - 1] == '\\'));

    need = len_a + (has_sep ? 0 : 1) + len_b;
    if (out_sz == 0) return;
    if (need >= out_sz) { out[0] = '\0'; return; }

    memcpy(out, a, len_a);
    if (!has_sep) out[len_a] = '/';
    memcpy(out + len_a + (has_sep ? 0 : 1), b, len_b);
    out[need] = '\0';
}

static void dirname_of(const char *path, char *out, unsigned long out_sz) {
    const char *slash_fwd = strrchr(path, '/');
    const char *slash_back = strrchr(path, '\\');
    const char *slash = slash_fwd;
    unsigned long len;

    if (slash_back != NULL && (slash == NULL || slash_back > slash)) slash = slash_back;
    if (slash == NULL) {
        if (out_sz >= 2) { out[0] = '.'; out[1] = '\0'; }
        return;
    }
    len = (unsigned long)(slash - path);
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

static const char *basename_of(const char *path) {
    const char *slash_fwd = strrchr(path, '/');
    const char *slash_back = strrchr(path, '\\');
    const char *slash = slash_fwd;
    if (slash_back != NULL && (slash == NULL || slash_back > slash)) slash = slash_back;
    return slash != NULL ? slash + 1 : path;
}

static void usage(void) {
    printf(
        "Usage:\n"
        "  wallpaper                 print the wallpaper in use\n"
        "  wallpaper --list          print the library (theme images + Pictures\\Wallpapers)\n"
        "  wallpaper <path>          make <path> the wallpaper of the current theme\n"
        "  wallpaper --next          step to the next image in the library\n"
    );
}

int main(int argc, char **argv) {
    char exe_dir[OSR_MAX_PATH_C];
    char root[OSR_MAX_PATH_C];
    char themes_dir[OSR_MAX_PATH_C];
    char theme_dir[OSR_MAX_PATH_C];
    char theme[128];
    const char *action;
    const char *target;

    action = "show";
    target = NULL;

    if (argc >= 2) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) { usage(); return 0; }
        if (strcmp(argv[1], "--list") == 0) action = "list";
        else if (strcmp(argv[1], "--next") == 0) action = "next";
        else { action = "set"; target = argv[1]; }
    }

    /* themes/ is one level above this exe, not next to it: nob.c builds
     * every binary into <os-rice>/build/ instead of the source tree (same
     * reasoning as install.c's own root default). */
    dirname_of(argv[0], exe_dir, sizeof(exe_dir));
    dirname_of(exe_dir, root, sizeof(root));
    path_join(themes_dir, sizeof(themes_dir), root, "themes");

    osr_state_get("theme", theme, sizeof(theme));
    if (theme[0] == '\0') copy_bounded(theme, sizeof(theme), OSR_DEFAULT_THEME);
    path_join(theme_dir, sizeof(theme_dir), themes_dir, theme);

    if (strcmp(action, "show") == 0) {
        char cur[OSR_WALLPAPER_PATH_LEN];
        osr_theme_wallpaper(theme_dir, theme, cur, sizeof(cur));
        printf("%s\n", cur[0] != '\0' ? cur : "(none)");
        return 0;
    }

    if (strcmp(action, "list") == 0) {
        char library[OSR_WALLPAPER_MAX_LIBRARY][OSR_WALLPAPER_PATH_LEN];
        unsigned long count = osr_wallpaper_library(theme_dir, library, OSR_WALLPAPER_MAX_LIBRARY);
        unsigned long i;
        for (i = 0; i < count; i++) printf("%s\n", library[i]);
        return 0;
    }

    if (strcmp(action, "next") == 0) {
        char library[OSR_WALLPAPER_MAX_LIBRARY][OSR_WALLPAPER_PATH_LEN];
        unsigned long count = osr_wallpaper_library(theme_dir, library, OSR_WALLPAPER_MAX_LIBRARY);
        char cur[OSR_WALLPAPER_PATH_LEN];
        unsigned long i;
        unsigned long take = count; /* count == "not found yet" sentinel */
        const char *next = NULL;

        osr_theme_wallpaper(theme_dir, theme, cur, sizeof(cur));

        if (count == 0) { osr_error("no wallpapers found for theme '%s'", theme); }

        for (i = 0; i < count; i++) {
            if (strcmp(basename_of(library[i]), basename_of(cur)) == 0) { take = i; break; }
        }
        if (take == count) {
            next = library[0]; /* current not in library (or none set) -- wrap to first */
        } else {
            next = library[(take + 1) % count];
        }

        {
            char installed[OSR_WALLPAPER_PATH_LEN];
            if (!osr_choose_wallpaper(theme, next, installed, sizeof(installed))) {
                osr_error("could not set wallpaper: %s", next);
            }
            printf("%s\n", next);
        }
        return 0;
    }

    /* action == "set" */
    {
        char installed[OSR_WALLPAPER_PATH_LEN];
        if (!osr_is_image(target)) { osr_error("not an image: %s", target); }
        if (!osr_choose_wallpaper(theme, target, installed, sizeof(installed))) {
            osr_error("could not set wallpaper: %s", target);
        }
        osr_success("wallpaper set for theme '%s': %s", theme, target);
    }
    return 0;
}
