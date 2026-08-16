/* lib/theme_render.c -- see lib/theme_render.h. C89. */
#include "theme_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OSR_MAX_REPLACEMENTS 320
#define OSR_REPL_NAME_LEN    80
#define OSR_REPL_VALUE_LEN   300

typedef struct {
    char name[OSR_REPL_NAME_LEN];
    char value[OSR_REPL_VALUE_LEN];
} osr_replacement;

static void copy_bounded(char *dst, unsigned long dst_sz, const char *src) {
    unsigned long len = (unsigned long)strlen(src);
    if (dst_sz == 0) return;
    if (len >= dst_sz) len = dst_sz - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* path_join -- out = a + "/" + b, bounded. Same shape as install.c's
 * helper of the same name (kept local on purpose -- see that file's
 * comment on why these tiny utilities are duplicated per file here).
 */
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

static void append_bounded(char *dst, unsigned long dst_sz, const char *suffix) {
    unsigned long cur = (unsigned long)strlen(dst);
    unsigned long add = (unsigned long)strlen(suffix);
    if (cur + add >= dst_sz) return;
    memcpy(dst + cur, suffix, add);
    dst[cur + add] = '\0';
}

static int file_exists(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) return 0;
    fclose(fp);
    return 1;
}

static int is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int is_hex6(const char *v) {
    unsigned long i;
    if (strlen(v) != 7 || v[0] != '#') return 0;
    for (i = 1; i < 7; i++) {
        if (!is_hex_digit(v[i])) return 0;
    }
    return 1;
}

static unsigned int hex2(const char *p) {
    unsigned int val = 0;
    int i;
    for (i = 0; i < 2; i++) {
        char c = p[i];
        unsigned int d;
        if (c >= '0' && c <= '9') d = (unsigned int)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned int)(c - 'a' + 10);
        else d = (unsigned int)(c - 'A' + 10);
        val = val * 16 + d;
    }
    return val;
}

/* build_replacements -- flatten a palette into a {{name}} -> value table.
 * Port of _osr_theme_sed's rule set (see this file's header comment). */
static unsigned long build_replacements(const osr_theme_palette *p, osr_replacement *out) {
    unsigned long n = 0;
    unsigned long i;

    copy_bounded(out[n].name, sizeof(out[n].name), "THEME");
    copy_bounded(out[n].value, sizeof(out[n].value), p->name);
    n++;

    for (i = 0; i < p->color_count && n + 4 <= OSR_MAX_REPLACEMENTS; i++) {
        const char *role = p->colors[i].name;
        const char *val = p->colors[i].value;

        copy_bounded(out[n].name, sizeof(out[n].name), role);
        copy_bounded(out[n].value, sizeof(out[n].value), val);
        n++;

        if (is_hex6(val)) {
            unsigned int r = hex2(val + 1);
            unsigned int g = hex2(val + 3);
            unsigned int b = hex2(val + 5);
            char dec[32];
            char sgr[32];
            sprintf(dec, "%u,%u,%u", r, g, b);
            sprintf(sgr, "%u;%u;%u", r, g, b);

            copy_bounded(out[n].name, sizeof(out[n].name), role);
            append_bounded(out[n].name, sizeof(out[n].name), "_rgb");
            copy_bounded(out[n].value, sizeof(out[n].value), val + 1);
            n++;

            copy_bounded(out[n].name, sizeof(out[n].name), role);
            append_bounded(out[n].name, sizeof(out[n].name), "_dec");
            copy_bounded(out[n].value, sizeof(out[n].value), dec);
            n++;

            copy_bounded(out[n].name, sizeof(out[n].name), role);
            append_bounded(out[n].name, sizeof(out[n].name), "_sgr");
            copy_bounded(out[n].value, sizeof(out[n].value), sgr);
            n++;
        }
    }

    for (i = 0; i < p->meta_count && n < OSR_MAX_REPLACEMENTS; i++) {
        copy_bounded(out[n].name, sizeof(out[n].name), p->meta[i].name);
        copy_bounded(out[n].value, sizeof(out[n].value), p->meta[i].value);
        n++;
    }

    return n;
}

int osr_render_template(const char *tmpl_path, const osr_theme_palette *palette, const char *out_path) {
    FILE *fp;
    long size;
    char *text;
    char *out_buf;
    unsigned long out_cap;
    unsigned long out_len;
    osr_replacement reps[OSR_MAX_REPLACEMENTS];
    unsigned long rep_count;
    const char *p;
    FILE *outfp;
    size_t written;

    fp = fopen(tmpl_path, "rb");
    if (fp == NULL) return 0;
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size < 0) { fclose(fp); return 0; }

    text = (char *)malloc((size_t)size + 1);
    if (text == NULL) { fclose(fp); return 0; }
    if (size > 0 && fread(text, 1, (size_t)size, fp) != (size_t)size) {
        fclose(fp);
        free(text);
        return 0;
    }
    text[size] = '\0';
    fclose(fp);

    rep_count = build_replacements(palette, reps);

    out_cap = (unsigned long)size + 4096;
    out_buf = (char *)malloc(out_cap);
    if (out_buf == NULL) { free(text); return 0; }
    out_len = 0;

    for (p = text; *p != '\0'; ) {
        int matched = 0;

        if (p[0] == '{' && p[1] == '{') {
            const char *end = strstr(p + 2, "}}");
            if (end != NULL) {
                unsigned long name_len = (unsigned long)(end - (p + 2));
                unsigned long i;
                for (i = 0; i < rep_count; i++) {
                    if (strlen(reps[i].name) == name_len && strncmp(reps[i].name, p + 2, name_len) == 0) {
                        unsigned long val_len = (unsigned long)strlen(reps[i].value);
                        if (out_len + val_len + 1 > out_cap) {
                            unsigned long new_cap = (out_len + val_len + 4096) * 2;
                            char *nb = (char *)realloc(out_buf, new_cap);
                            if (nb == NULL) { free(out_buf); free(text); return 0; }
                            out_buf = nb;
                            out_cap = new_cap;
                        }
                        memcpy(out_buf + out_len, reps[i].value, val_len);
                        out_len += val_len;
                        matched = 1;
                        break;
                    }
                }
                if (matched) p = end + 2;
            }
        }

        if (!matched) {
            if (out_len + 1 > out_cap) {
                unsigned long new_cap = out_cap * 2 + 16;
                char *nb = (char *)realloc(out_buf, new_cap);
                if (nb == NULL) { free(out_buf); free(text); return 0; }
                out_buf = nb;
                out_cap = new_cap;
            }
            out_buf[out_len++] = *p;
            p++;
        }
    }

    free(text);

    outfp = fopen(out_path, "wb");
    if (outfp == NULL) { free(out_buf); return 0; }
    written = fwrite(out_buf, 1, out_len, outfp);
    fclose(outfp);
    free(out_buf);
    return written == out_len;
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static void temp_dir(char *out, unsigned long out_sz) {
    DWORD n = GetTempPathA((DWORD)out_sz, out);
    if (n == 0 || n >= out_sz) copy_bounded(out, out_sz, ".");
}
static unsigned long unique_id(void) {
    return (unsigned long)GetCurrentProcessId() * 100000UL + (unsigned long)GetTickCount() % 100000UL;
}
#else
static void temp_dir(char *out, unsigned long out_sz) { copy_bounded(out, out_sz, "/tmp"); }
static unsigned long unique_id(void) { return 0; }
#endif

int osr_theme_layer_source(const char *themes_root, const char *repo_root,
                            const char *app, const char *file, const char *theme,
                            char *out_path, unsigned long out_path_sz, int *is_temp) {
    char theme_dir[512], config_dir[512], app_dir[512], literal[600];
    char app_repo_dir[512], tmpl_path[700];
    char theme_list_path[600];
    char tmp_dir[300], tmp_name[400];
    osr_theme_palette palette;

    *is_temp = 0;
    if (out_path_sz > 0) out_path[0] = '\0';

    /* 1. literal override: <themes_root>/<theme>/config/<app>/<file> */
    path_join(theme_dir, sizeof(theme_dir), themes_root, theme);
    path_join(config_dir, sizeof(config_dir), theme_dir, "config");
    path_join(app_dir, sizeof(app_dir), config_dir, app);
    path_join(literal, sizeof(literal), app_dir, file);
    if (file_exists(literal)) {
        copy_bounded(out_path, out_path_sz, literal);
        *is_temp = 0;
        return 1;
    }

    /* 2. render <repo_root>/<app>/<file>.tmpl against <themes_root>/<theme>/theme.list */
    path_join(app_repo_dir, sizeof(app_repo_dir), repo_root, app);
    copy_bounded(tmpl_path, sizeof(tmpl_path), app_repo_dir);
    append_bounded(tmpl_path, sizeof(tmpl_path), "/");
    append_bounded(tmpl_path, sizeof(tmpl_path), file);
    append_bounded(tmpl_path, sizeof(tmpl_path), ".tmpl");
    if (!file_exists(tmpl_path)) return 0;

    path_join(theme_list_path, sizeof(theme_list_path), theme_dir, "theme.list");
    if (!osr_load_theme_palette(theme_list_path, theme, &palette)) return 0;

    temp_dir(tmp_dir, sizeof(tmp_dir));
    sprintf(tmp_name, "osr-theme-%s-%lu-%s", app, unique_id(), file);
    {
        char full_tmp[700];
        path_join(full_tmp, sizeof(full_tmp), tmp_dir, tmp_name);
        if (!osr_render_template(tmpl_path, &palette, full_tmp)) return 0;
        copy_bounded(out_path, out_path_sz, full_tmp);
    }
    *is_temp = 1;
    return 1;
}

void osr_theme_layer_cleanup(const char *path, int is_temp) {
    if (is_temp) remove(path);
}
