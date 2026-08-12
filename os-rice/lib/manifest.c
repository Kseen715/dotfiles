/* lib/manifest.c -- see lib/manifest.h. C89. */
#include "manifest.h"

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

static const char *after_prefix(const char *line, const char *prefix) {
    unsigned long len = (unsigned long)strlen(prefix);
    if (strncmp(line, prefix, len) != 0) return NULL;
    return line + len;
}

/* copy_bounded -- dst = src, truncated to dst_sz - 1 bytes, always
 * null-terminated. Used instead of strncpy(), whose "pad or don't
 * null-terminate" behavior GCC's -Wstringop-truncation (rightly) flags as a
 * likely bug at every call site that reuses the size as a copy length.
 */
static void copy_bounded(char *dst, unsigned long dst_sz, const char *src) {
    unsigned long len = (unsigned long)strlen(src);
    if (len >= dst_sz) len = dst_sz - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* list_append -- append `word` to a space-joined list, growing in place.
 * A word that would overflow the list is dropped rather than truncating the
 * buffer mid-word -- same "advisory, never corrupt" spirit as lib/net.sh's
 * progress meter degrading to silence rather than printing garbage.
 */
static void list_append(char *list, unsigned long list_sz, const char *word) {
    unsigned long cur_len = (unsigned long)strlen(list);
    unsigned long word_len = (unsigned long)strlen(word);
    unsigned long need = cur_len + (cur_len > 0 ? 1 : 0) + word_len;

    if (need >= list_sz) return;
    if (cur_len > 0) list[cur_len++] = ' ';
    memcpy(list + cur_len, word, word_len);
    list[cur_len + word_len] = '\0';
}

int osr_parse_rice_list(const char *path, osr_manifest *out) {
    FILE *fp;
    char line[512];

    memset(out, 0, sizeof(*out));

    fp = fopen(path, "r");
    if (fp == NULL) return 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p;
        char *hash;
        const char *value;

        hash = strchr(line, '#');
        if (hash != NULL) *hash = '\0';

        rtrim(line);
        p = ltrim(line);
        if (*p == '\0') continue;

        if ((value = after_prefix(p, "themes:")) != NULL) {
            char *tok = strtok((char *)value, " \t");
            while (tok != NULL) {
                list_append(out->themes, sizeof(out->themes), tok);
                tok = strtok(NULL, " \t");
            }
        } else if ((value = after_prefix(p, "theme:")) != NULL) {
            value = ltrim((char *)value);
            copy_bounded(out->theme, sizeof(out->theme), value);
        } else if ((value = after_prefix(p, "require:")) != NULL) {
            char *tok = strtok((char *)value, " \t");
            while (tok != NULL) {
                list_append(out->requires_list, sizeof(out->requires_list), tok);
                tok = strtok(NULL, " \t");
            }
        } else if (out->module_count < OSR_MANIFEST_MAX_MODULES) {
            copy_bounded(out->modules[out->module_count], OSR_MANIFEST_MAX_NAME, p);
            out->module_count++;
        }
    }

    fclose(fp);
    return 1;
}
