/* lib/migrate.c -- C port of lib/migrate.sh. See lib/migrate.h.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include "module.h"

#include "cmds.h"
#include "migrate.h"

/* base_name -- the basename the info lines print, sh's ${file##...}. */
static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

/* file_matches -- `grep -qE <ere> <file> 2>/dev/null`. grep itself rather than
 * an in-process matcher: these patterns are written as EREs in the module
 * scripts, and a second engine with its own idea of what [[:space:]] or an
 * unescaped brace means would diverge from the shell tier on exactly the edge
 * cases that decide whether a box gets migrated. */
static int file_matches(const char *file, const char *ere) {
    char *argv[5];
    argv[0] = (char *)"grep";
    argv[1] = (char *)"-qE";
    argv[2] = (char *)ere;
    argv[3] = (char *)file;
    argv[4] = NULL;
    return osr_run_quiet(argv) == 0;
}

/* backup_once -- one .pre-migrate copy per file, first touch only. Mirrors
 * backup_copy's once-only .bak: a second migration in the same run must not
 * overwrite the copy that still shows the pre-migration state. */
static void backup_once(const char *file) {
    Str bak;
    char *argv[4];

    str_init(&bak);
    str_addz(&bak, file);
    str_addz(&bak, ".pre-migrate");
    if (!file_exists(str_text(&bak))) {
        argv[0] = (char *)"cp";
        argv[1] = (char *)file;
        argv[2] = (char *)str_text(&bak);
        argv[3] = NULL;
        (void)osr_run_user(argv);
    }
    str_free(&bak);
}

int osr_migrate_append(const char *file, const char *detect_ere,
                       const char *label, const char *text) {
    char *argv[4];

    /* `[ ! -f "$file" ] || grep -qE ...` -- a missing file and an already
     * migrated one are both a quiet no-op. */
    if (!file_exists(file) || file_matches(file, detect_ere)) return 1;

    backup_once(file);

    /* `{ printf '\n'; cat; } | as_user tee -a "$file"`. tee rather than a
     * plain append redirect because the append has to happen as the riced
     * user, and the shell's own > runs before the privilege change. */
    {
        Str tmp;
        int fd;
        str_init(&tmp);
        str_addz(&tmp, env_str("TMPDIR", "/tmp"));
        str_trim_trailing(&tmp, '/');
        str_addz(&tmp, "/osr-mig-app-");
        {
            char pid[32];
            sprintf(pid, "%ld", (long)getpid());
            str_addz(&tmp, pid);
        }
        /* An unlinked scratch file rather than a pipe: nothing reads the pipe
         * until the child exists, so a text larger than the pipe buffer would
         * wedge this process against a child it has not spawned yet. */
        fd = open(str_text(&tmp), O_RDWR | O_CREAT | O_TRUNC, 0600);
        remove(str_text(&tmp));
        str_free(&tmp);
        if (fd < 0) return 1;
        {
            char nl = '\n';
            size_t n = strlen(text);
            if (write(fd, &nl, 1) != 1 ||
                (n > 0 && write(fd, text, n) != (ssize_t)n)) {
                close(fd);
                return 1;
            }
        }
        lseek(fd, 0, SEEK_SET);

        argv[0] = (char *)"tee";
        argv[1] = (char *)"-a";
        argv[2] = (char *)file;
        argv[3] = NULL;
        (void)osr_run_user_quiet_in(argv, fd);
        close(fd);
    }

    osr_infof("migrated %s: %s", base_name(file), label);
    return 1;
}

/* norm -- the body awk built with `{ body = body $0 "\n" }`: every line
 * terminated, so a file (or a region) without a trailing newline still ends in
 * one and the two sides compare on equal footing. */
static void norm(Str *out, const char *buf, size_t len) {
    size_t pos = 0;
    Line line;
    while (next_line(buf, len, &pos, &line)) {
        str_add(out, line.start, line.len);
        str_addc(out, '\n');
    }
}

/* find_sub -- index() over embedded NULs' worth of bytes. The regions are text,
 * but the file is read as bytes and a stray NUL must not truncate the search. */
static long find_sub(const Str *hay, const Str *needle) {
    size_t i;
    if (needle->len == 0 || needle->len > hay->len) return -1;
    for (i = 0; i + needle->len <= hay->len; i++) {
        if (memcmp(str_text(hay) + i, str_text(needle), needle->len) == 0)
            return (long)i;
    }
    return -1;
}

int osr_migrate_replace(const char *file, const char *label, const char *old,
                        const char *new_text) {
    char *buf;
    size_t len = 0;
    Str body;
    Str needle;
    Str repl;
    Str out;
    long at;
    int ok = 0;

    buf = slurp(file, &len);
    if (buf == NULL) return 0;

    str_init(&body); str_init(&needle); str_init(&repl); str_init(&out);
    norm(&body, buf, len);
    norm(&needle, old, strlen(old));
    norm(&repl, new_text, strlen(new_text));
    free(buf);

    /* An empty needle would "match" at offset 0 and splice the replacement in
     * at the top of the file. */
    at = needle.len == 0 ? -1 : find_sub(&body, &needle);
    if (at >= 0) {
        Str tmp;
        str_add(&out, str_text(&body), (size_t)at);
        str_add(&out, str_text(&repl), repl.len);
        str_add(&out, str_text(&body) + (size_t)at + needle.len,
                body.len - (size_t)at - needle.len);

        /* Written through a scratch file and copied as the user, so a failure
         * partway through leaves the original intact rather than truncated. */
        str_init(&tmp);
        str_addz(&tmp, env_str("TMPDIR", "/tmp"));
        str_trim_trailing(&tmp, '/');
        str_addz(&tmp, "/osr-mig-res-");
        {
            char pid[32];
            sprintf(pid, "%ld", (long)getpid());
            str_addz(&tmp, pid);
        }
        {
            FILE *f = fopen(str_text(&tmp), "wb");
            if (f != NULL) {
                if (fwrite(str_text(&out), 1, out.len, f) == out.len &&
                    fclose(f) == 0) {
                    char *argv[4];
                    backup_once(file);
                    argv[0] = (char *)"cp";
                    argv[1] = (char *)str_text(&tmp);
                    argv[2] = (char *)file;
                    argv[3] = NULL;
                    (void)osr_run_user(argv);
                    osr_infof("migrated %s: %s", base_name(file), label);
                    ok = 1;
                } else {
                    (void)fclose(f);
                }
                remove(str_text(&tmp));
            }
        }
        str_free(&tmp);
    }

    str_free(&body); str_free(&needle); str_free(&repl); str_free(&out);
    return ok;
}

int osr_migrate_stale(const char *file, const char *detect_ere,
                      const char *what) {
    Str pat;
    int hit;

    if (!file_exists(file)) return 1;

    /* '^[[:space:]]*[^#[:space:]].*(<ere>)' -- code lines only. */
    str_init(&pat);
    str_addz(&pat, "^[[:space:]]*[^#[:space:]].*(");
    str_addz(&pat, detect_ere);
    str_addz(&pat, ")");
    hit = file_matches(file, str_text(&pat));
    str_free(&pat);

    if (hit)
        osr_warnf("%s still has %s, edited so it cannot be patched automatically"
                  " - see zsh/rc.d/ in the dotfiles repo for the current version",
                  file, what);
    return 1;
}

/* read_all -- a whole stream, for the append text arriving on stdin. */
static void read_all(Str *out, FILE *f) {
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) str_add(out, chunk, n);
}

static int migrate_usage(void) {
    fputs("usage: osr migrate <subcommand> [args]\n\n", stderr);
    fputs("  append <file> <ere> <label>          append stdin unless <ere> matches\n", stderr);
    fputs("  replace <file> <label> <old> <new>   swap an exact region\n", stderr);
    fputs("  stale <file> <ere> <what>            warn about an unpatchable legacy\n", stderr);
    return 2;
}

int osr_migrate_main(int argc, char **argv) {
    if (argc < 2) return migrate_usage();

    if (strcmp(argv[1], "append") == 0 && argc == 5) {
        Str text;
        str_init(&text);
        read_all(&text, stdin);
        (void)osr_migrate_append(argv[2], argv[3], argv[4], str_text(&text));
        str_free(&text);
        return 0;
    }
    if (strcmp(argv[1], "replace") == 0 && argc == 6)
        return osr_migrate_replace(argv[2], argv[3], argv[4], argv[5]) ? 0 : 1;
    if (strcmp(argv[1], "stale") == 0 && argc == 5)
        return osr_migrate_stale(argv[2], argv[3], argv[4]) ? 0 : 1;

    return migrate_usage();
}
