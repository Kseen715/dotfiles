/* lib/config_copy.c -- see lib/config_copy.h. C89. */
#include "config_copy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy_bounded(char *dst, unsigned long dst_sz, const char *src) {
    unsigned long len = (unsigned long)strlen(src);
    if (dst_sz == 0) return;
    if (len >= dst_sz) len = dst_sz - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void append_bounded(char *dst, unsigned long dst_sz, const char *suffix) {
    unsigned long cur = (unsigned long)strlen(dst);
    unsigned long add = (unsigned long)strlen(suffix);
    if (cur + add >= dst_sz) return;
    memcpy(dst + cur, suffix, add);
    dst[cur + add] = '\0';
}

static void dirname_of(const char *path, char *out, unsigned long out_sz) {
    const char *slash_fwd = strrchr(path, '/');
    const char *slash_back = strrchr(path, '\\');
    const char *slash = slash_fwd;
    unsigned long len;

    if (slash_back != NULL && (slash == NULL || slash_back > slash)) slash = slash_back;
    if (slash == NULL) { copy_bounded(out, out_sz, "."); return; }
    len = (unsigned long)(slash - path);
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, path, len);
    out[len] = '\0';
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

#else /* !_WIN32 */
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

static int get_home(char *out, unsigned long out_sz) {
    const char *h = getenv("HOME");
    if (h == NULL) { if (out_sz > 0) out[0] = '\0'; return 0; }
    copy_bounded(out, out_sz, h);
    return 1;
}

static int make_one_dir(const char *path) {
    return mkdir(path, 0755) == 0 || errno == EEXIST;
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

void osr_expand_home(const char *path, char *out, unsigned long out_sz) {
    char home[512];

    if (path[0] == '~' && (path[1] == '\0' || path[1] == '/' || path[1] == '\\')) {
        if (get_home(home, sizeof(home))) {
            copy_bounded(out, out_sz, home);
            append_bounded(out, out_sz, path + 1);
            return;
        }
    }
    copy_bounded(out, out_sz, path);
}

/* osr_copy_file -- plain fopen/fread/fwrite loop, so this works on any
 * platform; only directory creation (mkdir_p above) is genuinely
 * OS-specific.
 */
int osr_copy_file(const char *src, const char *dst) {
    char parent[600];
    FILE *in;
    FILE *out;
    char buf[8192];
    size_t n;
    int ok;

    dirname_of(dst, parent, sizeof(parent));
    mkdir_p(parent);

    in = fopen(src, "rb");
    if (in == NULL) return 0;
    out = fopen(dst, "wb");
    if (out == NULL) { fclose(in); return 0; }

    ok = 1;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = 0; break; }
    }
    if (ferror(in)) ok = 0;

    fclose(in);
    fclose(out);
    return ok;
}
