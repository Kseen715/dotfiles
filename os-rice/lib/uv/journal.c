/* lib/uv/journal.c -- see journal.h. The durable half is small and the
 * analysis half is pure; they are kept apart on purpose.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include "journal.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* --- record kinds --------------------------------------------------------- */

static const char *const kind_names[UV_J_KIND_MAX] = {
    "TRY", "OK", "FAIL", "CRASH", "RESET"
};

const char *uv_jkind_name(UvJKind k) {
    if (k < 0 || k >= UV_J_KIND_MAX) return "?";
    return kind_names[k];
}

UvJKind uv_jkind_parse(const char *s) {
    int i;
    for (i = 0; i < UV_J_KIND_MAX; i++) {
        if (strcmp(s, kind_names[i]) == 0) return (UvJKind)i;
    }
    return UV_J_KIND_MAX;
}

void uv_jrec_init(UvJRec *r) {
    memset(r, 0, sizeof(*r));
    r->kind = UV_J_TRY;
    r->domain = UV_CORE;
    strcpy(r->backend, "-");
    strcpy(r->phase, "-");
    strcpy(r->boot_id, "-");
}

int uv_jrec_same_step(const UvJRec *a, const UvJRec *b) {
    return a->domain == b->domain && a->idx == b->idx && a->mv == b->mv;
}

/* --- serialisation -------------------------------------------------------- */

/* add_field -- append a string field, substituting '_' for anything that would
 * break the line format. A backend or phase name is ours, not user input, so
 * this never fires in practice; it exists so that a record is guaranteed to
 * survive a round trip rather than merely expected to. */
static void add_field(Str *out, const char *s) {
    if (s == NULL || *s == '\0') {
        str_addc(out, '-');
        return;
    }
    for (; *s; s++) {
        str_addc(out, (is_space(*s) || *s == '\n') ? '_' : *s);
    }
}

void uv_jrec_format(Str *out, const UvJRec *r) {
    str_addz(out, uv_jkind_name(r->kind));
    str_addc(out, ' ');
    add_field(out, r->backend);
    str_addc(out, ' ');
    str_addz(out, uv_domain_name(r->domain));
    str_addc(out, ' ');
    str_addl(out, r->idx);
    str_addc(out, ' ');
    str_addl(out, r->mv);
    str_addc(out, ' ');
    add_field(out, r->phase);
    str_addc(out, ' ');
    add_field(out, r->boot_id);
    str_addc(out, ' ');
    str_addl(out, r->ts);
}

/* next_tok -- the next space-delimited token of a length-bounded line, copied
 * NUL-terminated into buf. Returns 0 when the line has run out, which is how a
 * half-written final record (a power cut mid-append) is rejected. */
static int next_tok(const char *line, size_t len, size_t *pos, char *buf, size_t bufsz) {
    size_t start, n;
    while (*pos < len && is_space(line[*pos])) (*pos)++;
    if (*pos >= len) return 0;
    start = *pos;
    while (*pos < len && !is_space(line[*pos])) (*pos)++;
    n = *pos - start;
    if (n == 0 || n >= bufsz) return 0;
    memcpy(buf, line + start, n);
    buf[n] = '\0';
    return 1;
}

static int tok_long(const char *line, size_t len, size_t *pos, long *out) {
    char buf[UV_J_STRMAX];
    char *endp;
    if (!next_tok(line, len, pos, buf, sizeof(buf))) return 0;
    *out = strtol(buf, &endp, 10);
    return *endp == '\0';
}

int uv_jrec_parse(const char *line, size_t len, UvJRec *out) {
    char buf[UV_J_STRMAX];
    size_t pos = 0;
    long v;

    uv_jrec_init(out);

    if (!next_tok(line, len, &pos, buf, sizeof(buf))) return 0;
    out->kind = uv_jkind_parse(buf);
    if (out->kind == UV_J_KIND_MAX) return 0;

    if (!next_tok(line, len, &pos, out->backend, sizeof(out->backend))) return 0;

    if (!next_tok(line, len, &pos, buf, sizeof(buf))) return 0;
    out->domain = uv_domain_parse(buf);
    if (out->domain == UV_DOMAIN_MAX) return 0;

    if (!tok_long(line, len, &pos, &v)) return 0;
    out->idx = (int)v;
    if (!tok_long(line, len, &pos, &v)) return 0;
    out->mv = (int)v;

    if (!next_tok(line, len, &pos, out->phase, sizeof(out->phase))) return 0;
    if (!next_tok(line, len, &pos, out->boot_id, sizeof(out->boot_id))) return 0;

    if (!tok_long(line, len, &pos, &out->ts)) return 0;
    return 1;
}

/* --- paths ---------------------------------------------------------------- */

void uv_journal_dir(Str *out) {
    str_addz(out, env_str("OSR_UV_DIR", "/var/lib/osr/undervolt"));
}

void uv_journal_path(Str *out) {
    uv_journal_dir(out);
    str_addz(out, "/journal");
}

void uv_boot_id(Str *out) {
    const char *override;
    char *buf;
    size_t len, end;

    override = env_str("OSR_UV_BOOT_ID", "");
    if (*override != '\0') {
        str_addz(out, override);
        return;
    }
    buf = slurp("/proc/sys/kernel/random/boot_id", &len);
    if (buf == NULL) {
        /* No boot id means we cannot tell a crash from an interrupted run.
         * "unknown" never equals a stored id, so every dangling TRY reads as a
         * crash -- over-cautious, which is the only safe direction here. */
        str_addz(out, "unknown");
        return;
    }
    end = len;
    while (end > 0 && is_space(buf[end - 1])) end--;
    if (end >= UV_J_STRMAX) end = UV_J_STRMAX - 1;
    str_add(out, buf, end);
    free(buf);
    if (out->len == 0) str_addz(out, "unknown");
}

/* --- durable append ------------------------------------------------------- */

/* fsync_dir -- flush the directory entry itself. Without this a journal file
 * created moments before a hard lock can vanish entirely across the reboot,
 * taking the record of what killed the machine with it. */
static int fsync_dir(const char *dir) {
    int fd = open(dir, O_RDONLY);
    int rc;
    if (fd < 0) return 0;
    rc = fsync(fd);
    close(fd);
    return rc == 0;
}

/* mkdir_parents -- mkdir -p, for the journal directory only. */
static int mkdir_parents(const char *path) {
    Str p;
    size_t i;
    int ok = 1;

    if (dir_exists(path)) return 1;
    str_init(&p);
    str_addz(&p, path);
    for (i = 1; i < p.len; i++) {
        if (p.p[i] != '/') continue;
        p.p[i] = '\0';
        if (!dir_exists(p.p) && mkdir(p.p, 0755) != 0) { /* EEXIST is fine */
            if (!dir_exists(p.p)) { ok = 0; break; }
        }
        p.p[i] = '/';
    }
    if (ok && !dir_exists(p.p) && mkdir(p.p, 0755) != 0 && !dir_exists(p.p)) ok = 0;
    str_free(&p);
    return ok;
}

int uv_journal_append(const UvJRec *r) {
    Str dir, path, line;
    int fd, ok = 0;

    str_init(&dir);
    str_init(&path);
    str_init(&line);
    uv_journal_dir(&dir);
    uv_journal_path(&path);

    if (!mkdir_parents(str_text(&dir))) goto done;

    uv_jrec_format(&line, r);
    str_addc(&line, '\n');

    fd = open(str_text(&path), O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) goto done;
    /* One write of one line. O_APPEND makes it atomic against other writers,
     * and a line is far below PIPE_BUF, so a torn record can only come from
     * the power going out -- which uv_jrec_parse already refuses to read. */
    if (write(fd, line.p, line.len) != (ssize_t)line.len) {
        close(fd);
        goto done;
    }
    if (fsync(fd) != 0) {
        close(fd);
        goto done;
    }
    close(fd);
    ok = fsync_dir(str_text(&dir));

done:
    str_free(&dir);
    str_free(&path);
    str_free(&line);
    return ok ? UV_OK : UV_ERR;
}

int uv_journal_load(UvJRec **recs, size_t *n) {
    Str path;
    char *buf;
    size_t len, pos = 0, cap = 0;
    Line ln;
    UvJRec *arr = NULL;

    *recs = NULL;
    *n = 0;

    str_init(&path);
    uv_journal_path(&path);
    buf = slurp(str_text(&path), &len);
    str_free(&path);
    if (buf == NULL) return UV_OK; /* no history yet is not an error */

    while (next_line(buf, len, &pos, &ln)) {
        UvJRec r;
        if (!uv_jrec_parse(ln.start, ln.len, &r)) continue;
        if (*n == cap) {
            UvJRec *grown;
            cap = cap ? cap * 2 : 32;
            grown = (UvJRec *)realloc(arr, cap * sizeof(UvJRec));
            if (grown == NULL) {
                free(arr);
                free(buf);
                osr_die_oom();
            }
            arr = grown;
        }
        arr[*n] = r;
        (*n)++;
    }
    free(buf);
    *recs = arr;
    return UV_OK;
}

/* --- analysis (pure) ------------------------------------------------------ */

void uv_journal_analyze(const UvJRec *recs, size_t n,
                        const char *cur_boot_id, UvRecovery *out) {
    size_t i;
    int have_open = 0;
    UvJRec open_try;

    memset(out, 0, sizeof(*out));
    uv_jrec_init(&open_try);

    for (i = 0; i < n; i++) {
        const UvJRec *r = &recs[i];
        switch (r->kind) {
        case UV_J_TRY:
            /* A TRY supersedes any earlier unresolved one: only the most
             * recent can be the thing that was applied when we went down. */
            open_try = *r;
            have_open = 1;
            break;
        case UV_J_OK:
            have_open = 0;
            out->last_good = *r;
            out->have_last_good = 1;
            break;
        case UV_J_FAIL:
            have_open = 0;
            break;
        case UV_J_CRASH:
            /* A crash already recorded on an earlier run. It closes the TRY it
             * refers to and counts toward the brakes. */
            have_open = 0;
            out->crashes_total++;
            break;
        case UV_J_RESET:
            /* Back to stock: nothing is applied, and no TRY is outstanding.
             * Crash counters deliberately survive a reset -- they are about
             * how much this machine has been hurt, not about what is applied. */
            have_open = 0;
            break;
        default:
            break;
        }
    }

    if (have_open) {
        if (strcmp(open_try.boot_id, cur_boot_id) == 0) {
            /* Same boot: the process died, the machine did not. Whatever it
             * applied may well still be live. */
            out->interrupted = 1;
            out->pending_rec = open_try;
        } else {
            out->crashed = 1;
            out->crash_rec = open_try;
            out->crashes_total++;
        }
    }

    /* Count how many times this exact step has taken the machine down, the
     * inferred crash included. Walking the whole history is fine: a journal is
     * hundreds of lines, not millions. */
    if (out->crashed) {
        out->crashes_at_step = 1;
        for (i = 0; i < n; i++) {
            if (recs[i].kind == UV_J_CRASH && uv_jrec_same_step(&recs[i], &out->crash_rec)) {
                out->crashes_at_step++;
            }
        }
    }

    if (out->crashes_at_step >= UV_BRAKE_SAME_STEP) {
        out->brake = 1;
        out->brake_reason = "this exact offset has hard-locked the machine repeatedly";
    } else if (out->crashes_total >= UV_BRAKE_TOTAL) {
        out->brake = 1;
        out->brake_reason = "too many hard lock-ups in this journal";
    }
}

int uv_journal_recover(UvRecovery *out) {
    UvJRec *recs = NULL;
    size_t n = 0;
    Str boot;
    int rc;

    if (uv_journal_load(&recs, &n) != UV_OK) {
        free(recs);
        return UV_ERR;
    }
    str_init(&boot);
    uv_boot_id(&boot);
    uv_journal_analyze(recs, n, str_text(&boot), out);
    rc = UV_OK;
    str_free(&boot);
    free(recs);
    return rc;
}

int uv_journal_note_crash(const UvRecovery *rec) {
    UvJRec r;
    Str boot;
    int ok;

    if (!rec->crashed) return UV_OK;
    r = rec->crash_rec;
    r.kind = UV_J_CRASH;
    r.ts = (long)time(NULL);
    /* The CRASH record belongs to the boot that NOTICED it, not to the one that
     * died -- otherwise a re-read would treat it as another dangling TRY. */
    str_init(&boot);
    uv_boot_id(&boot);
    strncpy(r.boot_id, str_text(&boot), sizeof(r.boot_id) - 1);
    r.boot_id[sizeof(r.boot_id) - 1] = '\0';
    str_free(&boot);

    ok = uv_journal_append(&r);
    return ok;
}
