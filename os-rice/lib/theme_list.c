/* lib/theme_list.c -- see lib/theme_list.h. C89. */
#include "theme_list.h"

#include <stdio.h>
#include <string.h>

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

static void copy_bounded(char *dst, unsigned long dst_sz, const char *src) {
    unsigned long len = (unsigned long)strlen(src);
    if (dst_sz == 0) return;
    if (len >= dst_sz) len = dst_sz - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void copy_n_bounded(char *dst, unsigned long dst_sz, const char *src, unsigned long n) {
    if (dst_sz == 0) return;
    if (n >= dst_sz) n = dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static const char *after_prefix(const char *line, const char *prefix) {
    unsigned long len = (unsigned long)strlen(prefix);
    if (strncmp(line, prefix, len) != 0) return NULL;
    return line + len;
}

static void list_append(char *list, unsigned long list_sz, const char *word) {
    unsigned long cur_len = (unsigned long)strlen(list);
    unsigned long word_len = (unsigned long)strlen(word);
    unsigned long need = cur_len + (cur_len > 0 ? 1 : 0) + word_len;

    if (need >= list_sz) return;
    if (cur_len > 0) list[cur_len++] = ' ';
    memcpy(list + cur_len, word, word_len);
    list[cur_len + word_len] = '\0';
}

/* strip_theme_comment -- port of _osr_theme_lines' three sed comment rules.
 * A palette value IS a hash (#rrggbb), so only a `#` starting the line, or
 * one with whitespace on BOTH sides, counts as a comment -- see
 * lib/theme_list.h's header comment for the full reasoning.
 */
static void strip_theme_comment(char *line) {
    char *p;
    unsigned long len;

    p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#') { line[0] = '\0'; return; }

    for (p = line; *p != '\0'; p++) {
        if ((*p == ' ' || *p == '\t') && p[1] == '#' && (p[2] == ' ' || p[2] == '\t')) {
            *p = '\0';
            return;
        }
    }

    len = (unsigned long)strlen(line);
    if (len >= 2 && line[len - 1] == '#' && (line[len - 2] == ' ' || line[len - 2] == '\t')) {
        line[len - 2] = '\0';
    }
}

/* is_meta_key -- ^[a-z][a-z0-9_]*$, the shape osr_theme_meta's own sed
 * pattern requires of a key (excludes `color:`/`config:`, handled first).
 */
static int is_meta_key(const char *key, unsigned long key_len) {
    unsigned long i;
    if (key_len == 0) return 0;
    if (key[0] < 'a' || key[0] > 'z') return 0;
    for (i = 1; i < key_len; i++) {
        char c = key[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return 0;
    }
    return 1;
}

int osr_load_theme_palette(const char *theme_list_path, const char *theme_name, osr_theme_palette *out) {
    FILE *fp;
    char line[512];

    memset(out, 0, sizeof(*out));
    copy_bounded(out->name, sizeof(out->name), theme_name);

    fp = fopen(theme_list_path, "r");
    if (fp == NULL) return 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p;
        const char *value;

        rtrim(line);
        strip_theme_comment(line);
        p = ltrim(line);
        rtrim(p);
        if (*p == '\0') continue;

        if ((value = after_prefix(p, "color:")) != NULL) {
            char *v = ltrim((char *)value);
            char *sp = v;
            while (*sp != '\0' && *sp != ' ' && *sp != '\t') sp++;
            if (*sp != '\0' && out->color_count < OSR_THEME_MAX_COLORS) {
                unsigned long role_len = (unsigned long)(sp - v);
                char *val_start = ltrim(sp);
                osr_theme_kv *kv = &out->colors[out->color_count];
                copy_n_bounded(kv->name, sizeof(kv->name), v, role_len);
                copy_bounded(kv->value, sizeof(kv->value), val_start);
                out->color_count++;
            }
        } else if ((value = after_prefix(p, "config:")) != NULL) {
            char *tok = strtok((char *)value, " \t");
            while (tok != NULL) {
                list_append(out->configs, sizeof(out->configs), tok);
                tok = strtok(NULL, " \t");
            }
        } else {
            char *colon = strchr(p, ':');
            if (colon != NULL) {
                unsigned long key_len = (unsigned long)(colon - p);
                if (is_meta_key(p, key_len) && out->meta_count < OSR_THEME_MAX_META) {
                    char *val = ltrim(colon + 1);
                    osr_theme_kv *kv = &out->meta[out->meta_count];
                    copy_n_bounded(kv->name, sizeof(kv->name), p, key_len);
                    copy_bounded(kv->value, sizeof(kv->value), val);
                    out->meta_count++;
                }
            }
        }
    }

    fclose(fp);
    return 1;
}

const char *osr_theme_color_hex(const osr_theme_palette *p, const char *role) {
    unsigned long i;
    for (i = 0; i < p->color_count; i++) {
        if (strcmp(p->colors[i].name, role) == 0) return p->colors[i].value;
    }
    return NULL;
}

const char *osr_theme_meta_get(const osr_theme_palette *p, const char *key) {
    unsigned long i;
    for (i = 0; i < p->meta_count; i++) {
        if (strcmp(p->meta[i].name, key) == 0) return p->meta[i].value;
    }
    return NULL;
}
