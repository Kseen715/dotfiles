/* lib/state.c -- see lib/state.h. C89. */
#include "state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OSR_STATE_BODY_CAP 8192

static void copy_bounded(char *dst, unsigned long dst_sz, const char *src) {
    unsigned long len = (unsigned long)strlen(src);
    if (dst_sz == 0) return;
    if (len >= dst_sz) len = dst_sz - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static int get_home(char *out, unsigned long out_sz) {
    DWORD n = GetEnvironmentVariableA("USERPROFILE", out, (DWORD)out_sz);
    return n > 0 && n < out_sz;
}

/* ensure_state_dir -- mkdir -p equivalent for home\.config\osr. Windows has
 * no single "create with parents" call, so this does it one level at a
 * time; ERROR_ALREADY_EXISTS from either level is not a failure.
 */
static int ensure_state_dir(const char *home) {
    char dot_config[600];
    char osr_dir[620];
    unsigned long home_len = (unsigned long)strlen(home);

    if (home_len + 9 >= sizeof(dot_config)) return 0;
    memcpy(dot_config, home, home_len);
    memcpy(dot_config + home_len, "\\.config", 9);
    if (!CreateDirectoryA(dot_config, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return 0;

    if (strlen(dot_config) + 5 >= sizeof(osr_dir)) return 0;
    sprintf(osr_dir, "%s\\osr", dot_config);
    if (!CreateDirectoryA(osr_dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return 0;

    return 1;
}

static int state_file_path(char *out, unsigned long out_sz) {
    char home[512];
    if (!get_home(home, sizeof(home))) { out[0] = '\0'; return 0; }
    if (strlen(home) + 20 >= out_sz) { out[0] = '\0'; return 0; }
    sprintf(out, "%s\\.config\\osr\\state", home);
    return 1;
}

#else /* !_WIN32 */

static int get_home(char *out, unsigned long out_sz) { (void)out_sz; out[0] = '\0'; return 0; }
static int ensure_state_dir(const char *home) { (void)home; return 0; }
static int state_file_path(char *out, unsigned long out_sz) { (void)out_sz; out[0] = '\0'; return 0; }

#endif /* _WIN32 */

void osr_state_get(const char *key, char *out, unsigned long out_sz) {
    char path[600];
    FILE *fp;
    char line[512];
    unsigned long key_len;

    if (out_sz > 0) out[0] = '\0';
    if (!state_file_path(path, sizeof(path))) return;

    fp = fopen(path, "r");
    if (fp == NULL) return;

    key_len = (unsigned long)strlen(key);
    while (fgets(line, sizeof(line), fp) != NULL) {
        unsigned long len = (unsigned long)strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) { line[--len] = '\0'; }
        if (len > key_len && line[key_len] == '=' && strncmp(line, key, key_len) == 0) {
            copy_bounded(out, out_sz, line + key_len + 1);
        }
    }
    fclose(fp);
}

int osr_state_set(const char *key, const char *value) {
    char home[512];
    char path[600];
    char body[OSR_STATE_BODY_CAP];
    unsigned long body_len;
    unsigned long key_len;
    FILE *fp;
    char line[512];
    int rc;

    if (!get_home(home, sizeof(home))) return 0;
    if (!ensure_state_dir(home)) return 0;
    if (!state_file_path(path, sizeof(path))) return 0;

    body_len = 0;
    body[0] = '\0';
    key_len = (unsigned long)strlen(key);

    fp = fopen(path, "r");
    if (fp != NULL) {
        while (fgets(line, sizeof(line), fp) != NULL) {
            unsigned long len = (unsigned long)strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) { line[--len] = '\0'; }
            if (len > key_len && line[key_len] == '=' && strncmp(line, key, key_len) == 0) continue;
            if (len == 0) continue;
            if (body_len + len + 1 >= OSR_STATE_BODY_CAP) continue; /* drop, never overflow */
            memcpy(body + body_len, line, len);
            body_len += len;
            body[body_len++] = '\n';
        }
        fclose(fp);
    }

    {
        unsigned long add_len = key_len + 1 + (unsigned long)strlen(value) + 1;
        if (body_len + add_len < OSR_STATE_BODY_CAP) {
            memcpy(body + body_len, key, key_len);
            body_len += key_len;
            body[body_len++] = '=';
            memcpy(body + body_len, value, strlen(value));
            body_len += (unsigned long)strlen(value);
            body[body_len++] = '\n';
        }
    }
    body[body_len] = '\0';

    fp = fopen(path, "w");
    if (fp == NULL) return 0;
    rc = fputs(body, fp) >= 0;
    fclose(fp);
    return rc;
}
