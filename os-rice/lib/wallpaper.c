/* lib/wallpaper.c -- see lib/wallpaper.h. C89. */
#include "wallpaper.h"
#include "winstate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

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

static const char *basename_of(const char *path) {
    const char *slash_fwd = strrchr(path, '/');
    const char *slash_back = strrchr(path, '\\');
    const char *slash = slash_fwd;
    if (slash_back != NULL && (slash == NULL || slash_back > slash)) slash = slash_back;
    return slash != NULL ? slash + 1 : path;
}

static int ci_char_eq(char a, char b) {
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    return a == b;
}

static int ci_str_eq(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (!ci_char_eq(*a, *b)) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

int osr_is_image(const char *path) {
    static const char *exts[] = { ".jpg", ".jpeg", ".png", ".webp", ".bmp", ".gif" };
    const char *dot = strrchr(path, '.');
    unsigned long i;

    if (dot == NULL) return 0;
    for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        if (ci_str_eq(dot, exts[i])) return 1;
    }
    return 0;
}

static void sort_paths(char paths[][OSR_WALLPAPER_PATH_LEN], unsigned long count) {
    unsigned long i, j;
    for (i = 1; i < count; i++) {
        char tmp[OSR_WALLPAPER_PATH_LEN];
        j = i;
        copy_bounded(tmp, sizeof(tmp), paths[i]);
        while (j > 0 && strcmp(paths[j - 1], tmp) > 0) {
            copy_bounded(paths[j], sizeof(paths[j]), paths[j - 1]);
            j--;
        }
        copy_bounded(paths[j], sizeof(paths[j]), tmp);
    }
}

unsigned long osr_theme_wallpapers(const char *theme_dir, char out[][OSR_WALLPAPER_PATH_LEN], unsigned long out_max) {
    char wallpapers_dir[512];
    DIR *d;
    struct dirent *ent;
    unsigned long n = 0;

    if (theme_dir == NULL || theme_dir[0] == '\0') return 0;
    path_join(wallpapers_dir, sizeof(wallpapers_dir), theme_dir, "wallpapers");

    d = opendir(wallpapers_dir);
    if (d == NULL) return 0;

    while (n < out_max && (ent = readdir(d)) != NULL) {
        char full[OSR_WALLPAPER_PATH_LEN];
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        path_join(full, sizeof(full), wallpapers_dir, ent->d_name);
        if (!osr_is_image(full)) continue;
        copy_bounded(out[n], OSR_WALLPAPER_PATH_LEN, full);
        n++;
    }
    closedir(d);

    sort_paths(out, n);
    return n;
}

void osr_theme_wallpaper(const char *theme_dir, const char *theme, char *out, unsigned long out_sz) {
    char key[128];
    char recorded[OSR_WALLPAPER_PATH_LEN];
    char library[OSR_WALLPAPER_MAX_LIBRARY][OSR_WALLPAPER_PATH_LEN];
    unsigned long count;

    if (out_sz > 0) out[0] = '\0';
    if (theme == NULL || theme[0] == '\0') return;

    sprintf(key, "wallpaper.%.100s", theme);
    osr_state_get(key, recorded, sizeof(recorded));
    if (recorded[0] != '\0' && osr_is_image(recorded)) {
        copy_bounded(out, out_sz, recorded);
        return;
    }

    count = osr_theme_wallpapers(theme_dir, library, OSR_WALLPAPER_MAX_LIBRARY);
    if (count > 0) copy_bounded(out, out_sz, library[0]);
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static int get_home(char *out, unsigned long out_sz) {
    DWORD n = GetEnvironmentVariableA("USERPROFILE", out, (DWORD)out_sz);
    return n > 0 && n < out_sz;
}

static int make_one_dir(const char *path) {
    return CreateDirectoryA(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

int osr_wallpaper_set_live(const char *path) {
    /* SPI_SETDESKWALLPAPER wants an absolute path; both callers below
     * already give it one (osr_install_wallpaper_file's own return, or
     * the caller-resolved path passed into osr_choose_wallpaper). JPG/PNG
     * work from Vista on; XP wants BMP -- a known format gap, see
     * lib/wallpaper.h.
     */
    return SystemParametersInfoA(SPI_SETDESKWALLPAPER, 0, (PVOID)path,
                                  SPIF_UPDATEINIFILE | SPIF_SENDCHANGE) != 0;
}

#else /* !_WIN32 */

static int get_home(char *out, unsigned long out_sz) {
    const char *h = getenv("HOME");
    if (h == NULL) { if (out_sz > 0) out[0] = '\0'; return 0; }
    copy_bounded(out, out_sz, h);
    return 1;
}

static int make_one_dir(const char *path) {
    (void)path;
    return 0;
}

int osr_wallpaper_set_live(const char *path) {
    (void)path;
    return 0;
}

#endif /* _WIN32 */

static int mkdir_p(const char *dir) {
    char buf[600];
    char *p;

    if (dir[0] == '\0' || strcmp(dir, ".") == 0) return 1;
    copy_bounded(buf, sizeof(buf), dir);

    for (p = buf + 1; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = '\0';
            make_one_dir(buf);
            *p = saved;
        }
    }
    return make_one_dir(buf);
}

static int files_identical(const char *a, const char *b) {
    FILE *fa, *fb;
    int same;

    fa = fopen(a, "rb");
    if (fa == NULL) return 0;
    fb = fopen(b, "rb");
    if (fb == NULL) { fclose(fa); return 0; }

    same = 1;
    for (;;) {
        unsigned char bufa[4096], bufb[4096];
        size_t na = fread(bufa, 1, sizeof(bufa), fa);
        size_t nb = fread(bufb, 1, sizeof(bufb), fb);
        if (na != nb || memcmp(bufa, bufb, na) != 0) { same = 0; break; }
        if (na == 0) break;
    }

    fclose(fa);
    fclose(fb);
    return same;
}

static int copy_bytes(const char *src, const char *dst) {
    FILE *in, *out;
    char buf[8192];
    size_t n;
    int ok = 1;

    in = fopen(src, "rb");
    if (in == NULL) return 0;
    out = fopen(dst, "wb");
    if (out == NULL) { fclose(in); return 0; }

    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = 0; break; }
    }
    if (ferror(in)) ok = 0;

    fclose(in);
    fclose(out);
    return ok;
}

int osr_install_wallpaper_file(const char *src, char *out, unsigned long out_sz) {
    char home[512];
    char dir[560];
    char dst[600];

    if (out_sz > 0) out[0] = '\0';
    if (src == NULL || src[0] == '\0') return 0;
    if (!get_home(home, sizeof(home))) return 0;

    path_join(dir, sizeof(dir), home, "Pictures");
    path_join(dir, sizeof(dir), dir, "Wallpapers");
    path_join(dst, sizeof(dst), dir, basename_of(src));

    if (files_identical(src, dst)) {
        copy_bounded(out, out_sz, dst);
        return 1;
    }

    mkdir_p(dir);
    if (!copy_bytes(src, dst)) return 0;

    copy_bounded(out, out_sz, dst);
    return 1;
}

int osr_choose_wallpaper(const char *theme, const char *path, char *out, unsigned long out_sz) {
    char key[128];
    char installed[OSR_WALLPAPER_PATH_LEN];

    if (out_sz > 0) out[0] = '\0';
    if (!osr_is_image(path)) return 0;

    if (theme != NULL && theme[0] != '\0') {
        sprintf(key, "wallpaper.%.100s", theme);
        osr_state_set(key, path);
    }

    if (!osr_install_wallpaper_file(path, installed, sizeof(installed))) return 0;
    osr_wallpaper_set_live(installed); /* best-effort: a failure here is not fatal */
    copy_bounded(out, out_sz, installed);
    return 1;
}

int osr_apply_theme_wallpaper(const char *theme_dir, const char *theme) {
    char wanted[OSR_WALLPAPER_PATH_LEN];
    char installed[OSR_WALLPAPER_PATH_LEN];

    osr_theme_wallpaper(theme_dir, theme, wanted, sizeof(wanted));
    if (wanted[0] == '\0') return 1; /* nothing to do -- not a failure */

    if (!osr_install_wallpaper_file(wanted, installed, sizeof(installed))) return 0;
    osr_state_set("wallpaper", installed);
    osr_wallpaper_set_live(installed); /* best-effort */
    return 1;
}

unsigned long osr_wallpaper_library(const char *theme_dir, char out[][OSR_WALLPAPER_PATH_LEN], unsigned long out_max) {
    char home[512];
    char wallpapers_dir[560];
    unsigned long n;
    DIR *d;
    struct dirent *ent;

    n = osr_theme_wallpapers(theme_dir, out, out_max);

    if (!get_home(home, sizeof(home))) return n;
    path_join(wallpapers_dir, sizeof(wallpapers_dir), home, "Pictures");
    path_join(wallpapers_dir, sizeof(wallpapers_dir), wallpapers_dir, "Wallpapers");

    d = opendir(wallpapers_dir);
    if (d == NULL) return n;

    while (n < out_max && (ent = readdir(d)) != NULL) {
        char full[OSR_WALLPAPER_PATH_LEN];
        unsigned long i;
        int seen = 0;

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        path_join(full, sizeof(full), wallpapers_dir, ent->d_name);
        if (!osr_is_image(full)) continue;

        for (i = 0; i < n; i++) {
            if (ci_str_eq(basename_of(out[i]), ent->d_name)) { seen = 1; break; }
        }
        if (seen) continue;

        copy_bounded(out[n], OSR_WALLPAPER_PATH_LEN, full);
        n++;
    }
    closedir(d);

    return n;
}
