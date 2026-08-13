/* modules/src/common.c -- see common.h. C89. */
#include "common.h"

#include <stdio.h>
#include <string.h>

void osrm_path_join(char *out, unsigned long out_sz, const char *a, const char *b) {
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

void osrm_copy_bounded(char *dst, unsigned long dst_sz, const char *src) {
    unsigned long len = (unsigned long)strlen(src);
    if (dst_sz == 0) return;
    if (len >= dst_sz) len = dst_sz - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dirent.h>

#include "../../lib/winpkg.h"

int osrm_capture_command_output(const char *cmd, char *out, unsigned long out_sz) {
    FILE *fp;
    unsigned long len;

    if (out_sz > 0) out[0] = '\0';
    fp = _popen(cmd, "r");
    if (fp == NULL) return 0;

    len = 0;
    if (out_sz > 0) {
        len = (unsigned long)fread(out, 1, out_sz - 1, fp);
        out[len] = '\0';
    }
    _pclose(fp);

    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) { out[--len] = '\0'; }
    return len > 0;
}

int osrm_resolve_pwsh_profile_path(char *out, unsigned long out_sz) {
    if (!osr_winpkg_have_command("pwsh")) return 0;
    return osrm_capture_command_output(
        "pwsh -NoLogo -NoProfile -Command \"$PROFILE.CurrentUserCurrentHost\"", out, out_sz);
}

static int find_on_path(const char *name, char *out, unsigned long out_sz) {
    char buf[MAX_PATH];
    DWORD len = SearchPathA(NULL, name, NULL, (DWORD)sizeof(buf), buf, NULL);
    if (len == 0 || len >= sizeof(buf)) return 0;
    osrm_copy_bounded(out, out_sz, buf);
    return 1;
}

static void dirname_of(const char *path, char *out, unsigned long out_sz) {
    const char *slash_fwd = strrchr(path, '/');
    const char *slash_back = strrchr(path, '\\');
    const char *slash = slash_fwd;
    unsigned long len;

    if (slash_back != NULL && (slash == NULL || slash_back > slash)) slash = slash_back;
    if (slash == NULL) { osrm_copy_bounded(out, out_sz, "."); return; }
    len = (unsigned long)(slash - path);
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

static int dir_exists(const char *path) {
    DIR *d = opendir(path);
    if (d == NULL) return 0;
    closedir(d);
    return 1;
}

int osrm_resolve_posh_themes_path(char *out, unsigned long out_sz) {
    char buf[600];
    DWORD n;

    if (out_sz > 0) out[0] = '\0';

    n = GetEnvironmentVariableA("POSH_THEMES_PATH", buf, (DWORD)sizeof(buf));
    if (n > 0 && n < sizeof(buf)) {
        osrm_copy_bounded(out, out_sz, buf);
    }

    if (out[0] == '\0' && osr_winpkg_have_command("scoop")) {
        char prefix[600];
        if (osrm_capture_command_output("scoop prefix oh-my-posh", prefix, sizeof(prefix)) && prefix[0] != '\0') {
            osrm_path_join(out, out_sz, prefix, "themes");
        }
    }

    if (out[0] == '\0') {
        char exe_path[600];
        char dir[600];
        if (find_on_path("oh-my-posh.exe", exe_path, sizeof(exe_path))
            || find_on_path("oh-my-posh", exe_path, sizeof(exe_path))) {
            dirname_of(exe_path, dir, sizeof(dir));
            osrm_path_join(out, out_sz, dir, "themes");
        }
    }

    return out[0] != '\0' && dir_exists(out);
}

#endif /* _WIN32 */
