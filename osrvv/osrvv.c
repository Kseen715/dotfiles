/* osrvv -- os-rice version viewer: print the version of a toolchain, in the
 * one form the starship prompt wants to render.
 *
 *     $ osrvv c
 *     v15.2.0-gcc v20.1.0-clang
 *     $ osrvv c3
 *     0.7.4
 *     $ osrvv make
 *     v4.4.1
 *
 * WHY THIS EXISTS. starship runs a custom module's `command` through `sh -c`
 * on unix but through `cmd /C` on Windows, so every `command = "... | sed ..."`
 * one-liner in starship.toml silently produced nothing on Windows: the prompt
 * showed a bare symbol and no version, from the same starship.toml that worked
 * on Linux. There is no one-liner both shells understand -- `sed` is not on a
 * stock Windows box at all -- so the probe is a program instead. One command
 * string, `osrvv <key>`, runs in every shell on every OS.
 *
 * It also does what a shell probe cannot do cheaply: a key may name SEVERAL
 * programs and all of the installed ones are reported ("v15.2.0-gcc
 * v20.1.0-clang"), which is why starship's built-in [c] module is disabled --
 * that one reports only the first compiler it finds.
 *
 * WHAT A KEY IS. One row per program in `probes` below, keyed by the starship
 * module that consumes it ([custom.c3] runs `osrvv c3`). Adding a language is
 * a row, not code: name the program, the flag that makes it print its banner,
 * the text the version follows, and any literal that wraps it. The rows for
 * one key are probed and printed in table order, space separated.
 *
 * Output is one line on stdout, or nothing at all when no program for that key
 * is installed -- starship renders `($output )` and so shows nothing. The exit
 * status is ALWAYS 0: a non-zero exit makes starship drop the module, which
 * would hide an installed compiler over a missing one.
 *
 * `osrvv --list` prints the keys. `osrvv --selftest` runs the parser over
 * canned banners and is the check that this file still works after an edit.
 *
 * Portability: C89 plus POSIX `access`, with a Windows branch for the `_popen`
 * spelling, the PATH separator and the executable suffixes. No dependencies,
 * no build system:
 *
 *     cc  -O2 -std=c89 -Wall -Wextra -pedantic -o ~/.local/bin/osrvv     osrvv.c
 *     gcc -O2 -std=c89 -Wall -Wextra -pedantic -o ~/.local/bin/osrvv.exe osrvv.c
 *
 * os-rice/modules/osrvv.c is that build, wired into the installer.
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
#define VV_POPEN  _popen
#define VV_PCLOSE _pclose
#define VV_ACCESS _access
#define VV_ACCESS_MODE 0        /* _access has no X_OK on Windows */
#define VV_PATH_SEP ';'
#define VV_DIR_SEP  '\\'
#else
#include <unistd.h>
#define VV_POPEN  popen
#define VV_PCLOSE pclose
#define VV_ACCESS access
#define VV_ACCESS_MODE X_OK
#define VV_PATH_SEP ':'
#define VV_DIR_SEP  '/'
#endif

#define VV_MAX_PATH 1024
#define VV_MAX_OUT  8192
#define VV_MAX_VER  128

/* Probe -- one program's version lookup.
 *
 *   key     the starship module that runs it; several rows may share one.
 *   cmd     the program, looked up on PATH before it is ever spawned.
 *   arg     the flag that makes it print its banner (--version, -v).
 *   needle  literal text the version follows in that banner, e.g. "GNU Make".
 *           NULL means "the first version-looking token on the first line",
 *           which is right for a banner shaped like "cpp (GCC) 15.2.0".
 *   prefix  printed before the version ("v", "beta-v").
 *   suffix  printed after it ("-gcc", "-gnat"), which is how one key's several
 *           programs stay tellable apart in the prompt.
 */
typedef struct {
    const char *key;
    const char *cmd;
    const char *arg;
    const char *needle;
    const char *prefix;
    const char *suffix;
} Probe;

/* The table. Ordered by key, and within a key by the order the prompt should
 * list them. Every row here replaces a `command = "... | sed ..."` line in
 * starship/starship.toml. */
static const Probe probes[] = {
    /* key      cmd         arg          needle                   prefix    suffix   */
    { "ada",   "gnatmake", "--version", "GNATMAKE",              "v",       "-gnat"  },

    /* C: all of them, not the first one. gcc and clang print their banner on
     * stderr, which is why every row is run with stderr folded in. */
    { "c",     "gcc",      "-v",        "gcc version",           "v",      "-gcc"   },
    { "c",     "clang",    "-v",        "clang version",         "v",      "-clang" },
    { "c",     "tcc",      "-v",        "tcc version",           "v",      "-tcc"   },

    { "c3",    "c3c",      "--version", "C3 Compiler Version:",  "v",       ""       },
    { "cpp",   "cpp",      "--version", NULL,                    "v",       ""       },
    { "cuda",  "nvcc",     "--version", "release",               "v",      ""       },
    /* hcc prints "hcc beta-v0.0.1": the version sits behind a letter, so the
     * literal is the needle and the prefix puts it back. */
    { "holyc", "hcc",      "--version", "beta-v",                "beta-v", ""       },
    { "make",  "make",     "--version", "GNU Make",              "v",      ""       }
};
#define PROBE_COUNT ((int)(sizeof probes / sizeof probes[0]))

/* Suffixes tried when looking a name up in PATH. The empty string must stay
 * last so an extension-less file still matches. */
#ifdef _WIN32
static const char *vv_exts[] = { ".exe", ".cmd", ".bat", "" };
#else
static const char *vv_exts[] = { "" };
#endif

/* vv_on_path -- true when `name` resolves to an executable in PATH. Probing
 * this first keeps us from spawning a shell for a compiler that is not
 * installed, which is the expensive part on Windows -- and the prompt pays it
 * on every keystroke. */
static int vv_on_path(const char *name) {
    const char *path;
    const char *dir;
    char candidate[VV_MAX_PATH];
    size_t dir_len, name_len, ext_len, i;

    path = getenv("PATH");
    if (path == NULL) return 0;
    name_len = strlen(name);

    for (dir = path; *dir != '\0';) {
        const char *end = strchr(dir, VV_PATH_SEP);
        dir_len = (end != NULL) ? (size_t)(end - dir) : strlen(dir);

        for (i = 0; i < sizeof vv_exts / sizeof vv_exts[0]; i++) {
            ext_len = strlen(vv_exts[i]);
            /* +2: the directory separator and the terminator. */
            if (dir_len > 0 && dir_len + name_len + ext_len + 2 <= sizeof candidate) {
                memcpy(candidate, dir, dir_len);
                candidate[dir_len] = VV_DIR_SEP;
                memcpy(candidate + dir_len + 1, name, name_len);
                memcpy(candidate + dir_len + 1 + name_len, vv_exts[i], ext_len);
                candidate[dir_len + 1 + name_len + ext_len] = '\0';
                if (VV_ACCESS(candidate, VV_ACCESS_MODE) == 0) return 1;
            }
        }

        if (end == NULL) break;
        dir = end + 1;
    }
    return 0;
}

/* vv_run -- capture `<cmd> <arg>` with stdout and stderr both (compilers are
 * split on which one they print the banner to) into `out`. Returns the number
 * of bytes stored. */
static size_t vv_run(const char *cmd, const char *arg, char *out, size_t out_size) {
    char line[VV_MAX_PATH];
    FILE *pipe;
    size_t used = 0;

    if (strlen(cmd) + strlen(arg) + sizeof("  2>&1") > sizeof line) return 0;
    strcpy(line, cmd);
    strcat(line, " ");
    strcat(line, arg);
    strcat(line, " 2>&1");

    pipe = VV_POPEN(line, "r");
    if (pipe == NULL) return 0;

    while (used + 1 < out_size) {
        size_t got = fread(out + used, 1, out_size - used - 1, pipe);
        if (got == 0) break;
        used += got;
    }
    out[used] = '\0';
    VV_PCLOSE(pipe);
    return used;
}

/* vv_copy_version -- copy the leading [0-9.] run at `src` into `dst`. Returns
 * 0 when there is no digit to copy. */
static int vv_copy_version(const char *src, char *dst, size_t dst_size) {
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

/* vv_parse -- pull the version out of a banner, the needle first and the
 * first-token scan as the fallback. The fallback runs even when a needle was
 * given, because an upstream that reworded its banner should degrade to a
 * roughly-right version rather than to nothing at all. */
static int vv_parse(const char *banner, const char *needle,
                    char *dst, size_t dst_size) {
    const char *p;

    if (needle != NULL) {
        const char *hit = strstr(banner, needle);
        if (hit != NULL) {
            p = hit + strlen(needle);
            while (*p == ' ' || *p == '\t') p++;
            if (vv_copy_version(p, dst, dst_size)) return 1;
        }
    }

    for (p = banner; *p != '\0' && *p != '\n'; p++) {
        if (isdigit((unsigned char)*p) && (p == banner || !isalnum((unsigned char)p[-1])) &&
            vv_copy_version(p, dst, dst_size) && strchr(dst, '.') != NULL) {
            return 1;
        }
    }
    return 0;
}

/* vv_report -- every installed program under `key`, space separated, one line.
 * Returns how many were printed. */
static int vv_report(const char *key) {
    char banner[VV_MAX_OUT];
    char version[VV_MAX_VER];
    int i, printed = 0;

    for (i = 0; i < PROBE_COUNT; i++) {
        const Probe *p = &probes[i];
        if (strcmp(p->key, key) != 0) continue;
        if (!vv_on_path(p->cmd)) continue;
        if (vv_run(p->cmd, p->arg, banner, sizeof banner) == 0) continue;
        if (!vv_parse(banner, p->needle, version, sizeof version)) continue;
        printf("%s%s%s%s", printed ? " " : "", p->prefix, version, p->suffix);
        printed++;
    }
    if (printed) putchar('\n');
    return printed;
}

/* vv_list -- the keys, one per line, each once. The table is grouped by key,
 * so "same as the previous row" is the whole deduplication. */
static void vv_list(void) {
    int i;
    for (i = 0; i < PROBE_COUNT; i++) {
        if (i == 0 || strcmp(probes[i].key, probes[i - 1].key) != 0)
            printf("%s\n", probes[i].key);
    }
}

/* vv_selftest -- the parser against banners taken from the real tools. This is
 * the whole risk surface of the file that does not need a compiler installed
 * to exercise, and every table row is one line here. Prints the failures and
 * returns how many there were. */
static int vv_selftest(void) {
    static const struct {
        const char *banner;
        const char *needle;
        const char *want;
    } cases[] = {
        { "GNATMAKE 13.2.0\nCopyright (C) 1995-2023\n",        "GNATMAKE",             "13.2.0" },
        { "gcc version 15.2.0 (GCC)\n",                        "gcc version",          "15.2.0" },
        { "Ubuntu clang version 18.1.3 (1ubuntu1)\n",          "clang version",        "18.1.3" },
        { "tcc version 0.9.27 (x86_64 Linux)\n",               "tcc version",          "0.9.27" },
        { "C3 Compiler Version:  0.7.4\nInstalled directory\n","C3 Compiler Version:", "0.7.4"  },
        { "cpp (Ubuntu 11.4.0-1ubuntu1~22.04) 11.4.0\n",       NULL,                   "11.4.0" },
        { "cpp (GCC) 15.2.0\n",                                NULL,                   "15.2.0" },
        { "Cuda compilation tools, release 13.0, V13.0.88\n",  "release",              "13.0"   },
        { "hcc beta-v0.0.1\n",                                 "beta-v",               "0.0.1"  },
        { "GNU Make 4.4.1\nBuilt for x86_64\n",                "GNU Make",             "4.4.1"  },
        /* The banner was reworded: fall back rather than print nothing. */
        { "GNU Gmake 4.4.1\n",                                 "GNU Make",             "4.4.1"  },
        /* Nothing version-shaped in there at all. */
        { "command not found\n",                               NULL,                   ""       }
    };
    int i, bad = 0;
    char got[VV_MAX_VER];

    for (i = 0; i < (int)(sizeof cases / sizeof cases[0]); i++) {
        if (!vv_parse(cases[i].banner, cases[i].needle, got, sizeof got)) got[0] = '\0';
        if (strcmp(got, cases[i].want) != 0) {
            printf("FAIL [%d] want \"%s\" got \"%s\"\n", i, cases[i].want, got);
            bad++;
        }
    }
    printf("%s: %d cases, %d failed\n", bad ? "FAIL" : "ok", i, bad);
    return bad;
}

int main(int argc, char **argv) {
    int i;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) return vv_selftest() == 0 ? 0 : 1;
    if (argc > 1 && strcmp(argv[1], "--list") == 0) { vv_list(); return 0; }
    if (argc < 2) {
        fprintf(stderr, "usage: osrvv <key>...   (--list for the keys)\n");
        return 0;       /* never non-zero: starship drops a module that fails */
    }

    for (i = 1; i < argc; i++) vv_report(argv[i]);
    return 0;
}
