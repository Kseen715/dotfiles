/* ccver -- print the version of every C compiler found on PATH, one line,
 * for the starship prompt's [custom.c] module.
 *
 * Why this exists: starship runs a custom module's `command` through `sh -c`
 * on unix but through `cmd /C` on Windows, so a POSIX one-liner (our previous
 * `for c in gcc clang tcc; do ... sed ...; done`) silently produced nothing on
 * Windows -- the prompt showed a bare symbol with no version. The built-in [c]
 * module is portable but reports only the first compiler it finds. A compiled
 * helper sidesteps both problems: the same command string, `ccver`, works in
 * every shell on every OS, and it lists all the compilers.
 *
 * Output (stdout, single line, empty if nothing was found):
 *
 *     v15.2.0-gcc v20.1.0-clang
 *
 * Usage: ccver [name ...]   default list: gcc clang tcc
 *
 * Portability: C89 plus POSIX `access`, with a Windows branch for the `_popen`
 * spelling, the PATH separator and executable suffixes. Always exits 0 so the
 * prompt never sees a failed command.
 *
 * Build:
 *     cc  -O2 -std=c89 -Wall -Wextra -pedantic -o ~/.local/bin/ccver     ccver.c
 *     gcc -O2 -std=c89 -Wall -Wextra -pedantic -o ~/.local/bin/ccver.exe ccver.c
 */

#ifndef _WIN32
/* popen/pclose are POSIX.2, access/X_OK POSIX.1; ask for them explicitly so
 * the file also builds under a strict -std=c89. */
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <io.h>
#define CC_POPEN  _popen
#define CC_PCLOSE _pclose
#define CC_ACCESS _access
#define CC_ACCESS_MODE 0        /* _access has no X_OK on Windows */
#define CC_PATH_SEP ';'
#define CC_DIR_SEP  '\\'
#else
#include <unistd.h>
#define CC_POPEN  popen
#define CC_PCLOSE pclose
#define CC_ACCESS access
#define CC_ACCESS_MODE X_OK
#define CC_PATH_SEP ':'
#define CC_DIR_SEP  '/'
#endif

#define CC_MAX_PATH  1024
#define CC_MAX_OUT   8192
#define CC_MAX_LINE  4096

/* Compilers probed when no names are given on the command line, in the order
 * they are reported. */
static const char *cc_default_names[] = { "gcc", "clang", "tcc" };

/* Suffixes tried when looking a name up in PATH. The empty string must stay
 * last so an extension-less file still matches. */
#ifdef _WIN32
static const char *cc_exts[] = { ".exe", ".cmd", ".bat", "" };
#else
static const char *cc_exts[] = { "" };
#endif

/* cc_on_path -- true when `name` resolves to an executable in PATH. Probing
 * this first keeps us from spawning a shell for a compiler that is not
 * installed, which is the expensive part on Windows. */
static int cc_on_path(const char *name) {
    const char *path;
    const char *dir;
    char candidate[CC_MAX_PATH];
    size_t dir_len, name_len, ext_len, i;

    path = getenv("PATH");
    if (path == NULL) return 0;
    name_len = strlen(name);

    for (dir = path; *dir != '\0';) {
        const char *end = strchr(dir, CC_PATH_SEP);
        dir_len = (end != NULL) ? (size_t)(end - dir) : strlen(dir);

        for (i = 0; i < sizeof cc_exts / sizeof cc_exts[0]; i++) {
            ext_len = strlen(cc_exts[i]);
            /* +2: the directory separator and the terminator. */
            if (dir_len > 0 && dir_len + name_len + ext_len + 2 <= sizeof candidate) {
                memcpy(candidate, dir, dir_len);
                candidate[dir_len] = CC_DIR_SEP;
                memcpy(candidate + dir_len + 1, name, name_len);
                memcpy(candidate + dir_len + 1 + name_len, cc_exts[i], ext_len);
                candidate[dir_len + 1 + name_len + ext_len] = '\0';
                if (CC_ACCESS(candidate, CC_ACCESS_MODE) == 0) return 1;
            }
        }

        if (end == NULL) break;
        dir = end + 1;
    }
    return 0;
}

/* cc_run -- capture `<name> -v` (stdout and stderr both; gcc and clang print
 * their banner on stderr) into `out`. Returns the number of bytes stored. */
static size_t cc_run(const char *name, char *out, size_t out_size) {
    char cmd[CC_MAX_PATH];
    FILE *pipe;
    size_t used = 0;

    if (strlen(name) + sizeof(" -v 2>&1") > sizeof cmd) return 0;
    strcpy(cmd, name);
    strcat(cmd, " -v 2>&1");

    pipe = CC_POPEN(cmd, "r");
    if (pipe == NULL) return 0;

    while (used + 1 < out_size) {
        size_t got = fread(out + used, 1, out_size - used - 1, pipe);
        if (got == 0) break;
        used += got;
    }
    out[used] = '\0';
    CC_PCLOSE(pipe);
    return used;
}

/* cc_copy_version -- copy the leading [0-9.] run at `src` into `dst`. Returns
 * 0 when there is no digit to copy or it does not fit. */
static int cc_copy_version(const char *src, char *dst, size_t dst_size) {
    size_t n = 0;

    if (!isdigit((unsigned char)*src)) return 0;
    while ((isdigit((unsigned char)src[n]) || src[n] == '.') && n + 1 < dst_size) {
        dst[n] = src[n];
        n++;
    }
    /* A trailing dot belongs to the surrounding prose, not the version. */
    while (n > 0 && dst[n - 1] == '.') n--;
    dst[n] = '\0';
    return n > 0;
}

/* cc_parse_version -- pull the version out of a compiler banner. Preferred
 * form is "<name> version X.Y.Z", which gcc, clang and tcc all print (possibly
 * behind a vendor prefix, as in "Ubuntu clang version 18.1.3"). Anything else
 * falls back to the first version-looking token on the first line. */
static int cc_parse_version(const char *banner, const char *name,
                            char *dst, size_t dst_size) {
    char needle[64];
    const char *hit;
    const char *p;

    if (strlen(name) + sizeof(" version ") <= sizeof needle) {
        strcpy(needle, name);
        strcat(needle, " version ");
        hit = strstr(banner, needle);
        if (hit != NULL && cc_copy_version(hit + strlen(needle), dst, dst_size)) return 1;
    }

    for (p = banner; *p != '\0' && *p != '\n'; p++) {
        if (isdigit((unsigned char)*p) && (p == banner || !isalnum((unsigned char)p[-1])) &&
            cc_copy_version(p, dst, dst_size) && strchr(dst, '.') != NULL) {
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    const char **names;
    char banner[CC_MAX_OUT];
    char version[CC_MAX_LINE];
    int count, i, printed = 0;

    if (argc > 1) {
        names = (const char **)(argv + 1);
        count = argc - 1;
    } else {
        names = cc_default_names;
        count = (int)(sizeof cc_default_names / sizeof cc_default_names[0]);
    }

    for (i = 0; i < count; i++) {
        if (!cc_on_path(names[i])) continue;
        if (cc_run(names[i], banner, sizeof banner) == 0) continue;
        if (!cc_parse_version(banner, names[i], version, sizeof version)) continue;
        printf("%sv%s-%s", printed ? " " : "", version, names[i]);
        printed = 1;
    }
    if (printed) putchar('\n');

    return 0;
}
