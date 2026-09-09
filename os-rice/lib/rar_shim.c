/* lib/rar_shim.c -- see lib/rar_shim.h.
 *
 * libarchive's two RAR readers are the only RAR decoders worth carrying, and
 * they are welded to libarchive's read stack. Rather than vendor that stack
 * -- the filter chain, the entry object built on iconv, the write-to-disk
 * half, none of which os-rice wants -- this file is the stack, cut down to
 * what the two readers actually call. It was measured, not guessed: build
 * thirdparty/rar.h on its own and `nm -u` lists exactly the names below.
 *
 * Three pieces:
 *
 *   1. A buffered reader over a FILE * behind __archive_read_ahead /
 *      _consume / _seek, with the same contract libarchive's has: ahead()
 *      returns a pointer to at least `min` contiguous bytes or NULL, and the
 *      pointer stays valid until the next call.
 *   2. struct archive_entry, ours. Upstream's is built on archive_mstring
 *      and pulls in libarchive's whole locale layer; the readers only ever
 *      set fields, and lib/archive.c only ever reads a handful back.
 *   3. The string-conversion object the RAR3 reader needs to say which
 *      encoding a member name arrived in. Three singletons -- UTF-8,
 *      UTF-16BE, and the locale default -- and one real conversion,
 *      UTF-16BE to UTF-8, because that is the form RAR3 packs names in.
 *
 * C89, like the rest of this tree.
 */
#include "rar_shim.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../thirdparty/rar.h"

/* ------------------------------------------------------------------ *
 * struct archive_entry
 * ------------------------------------------------------------------ */

/* Upstream keeps every name in three encodings at once and converts lazily.
 * Here every name is UTF-8 by the time it is stored -- the RAR3 reader hands
 * over UTF-16BE and the conversion below folds it, RAR5 stores UTF-8 -- so
 * one string per field is the whole of it. */
struct archive_entry {
    char *pathname;
    char *symlink;
    char *hardlink;
    char *uname;
    char *gname;
    int64_t size;
    int64_t uid, gid;
    time_t mtime, atime, ctime;
    long mtime_ns, atime_ns, ctime_ns;
    mode_t mode;            /* permission bits and type bits together */
    int symlink_type;
    char data_encrypted;
    char metadata_encrypted;
};

static void set_str(char **slot, const char *s, size_t len) {
    char *n = NULL;
    if (s != NULL) {
        n = (char *)malloc(len + 1);
        if (n == NULL) return;   /* out of memory: keep the old value */
        memcpy(n, s, len);
        n[len] = '\0';
    }
    free(*slot);
    *slot = n;
}

static void set_cstr(char **slot, const char *s) {
    set_str(slot, s, s == NULL ? 0 : strlen(s));
}

struct archive_entry *archive_entry_new(void) {
    return (struct archive_entry *)calloc(1, sizeof(struct archive_entry));
}

struct archive_entry *archive_entry_clear(struct archive_entry *e) {
    if (e == NULL) return NULL;
    free(e->pathname); free(e->symlink); free(e->hardlink);
    free(e->uname); free(e->gname);
    memset(e, 0, sizeof(*e));
    return e;
}

void archive_entry_free(struct archive_entry *e) {
    if (e == NULL) return;
    archive_entry_clear(e);
    free(e);
}

void archive_entry_set_size(struct archive_entry *e, la_int64_t v) { e->size = v; }
void archive_entry_set_uid(struct archive_entry *e, la_int64_t v) { e->uid = v; }
void archive_entry_set_gid(struct archive_entry *e, la_int64_t v) { e->gid = v; }
void archive_entry_set_uname(struct archive_entry *e, const char *s) { set_cstr(&e->uname, s); }
void archive_entry_set_gname(struct archive_entry *e, const char *s) { set_cstr(&e->gname, s); }
void archive_entry_set_symlink_type(struct archive_entry *e, int t) { e->symlink_type = t; }
void archive_entry_set_is_data_encrypted(struct archive_entry *e, char c) { e->data_encrypted = c; }
void archive_entry_set_is_metadata_encrypted(struct archive_entry *e, char c) { e->metadata_encrypted = c; }

void archive_entry_set_mode(struct archive_entry *e, __LA_MODE_T m) { e->mode = m; }

/* set_filetype replaces only the type bits, as upstream's does. */
void archive_entry_set_filetype(struct archive_entry *e, unsigned int t) {
    e->mode = (mode_t)((e->mode & ~AE_IFMT) | (t & AE_IFMT));
}

void archive_entry_set_mtime(struct archive_entry *e, __LA_TIME_T t, long ns) {
    e->mtime = (time_t)t; e->mtime_ns = ns;
}
void archive_entry_set_atime(struct archive_entry *e, __LA_TIME_T t, long ns) {
    e->atime = (time_t)t; e->atime_ns = ns;
}
void archive_entry_set_ctime(struct archive_entry *e, __LA_TIME_T t, long ns) {
    e->ctime = (time_t)t; e->ctime_ns = ns;
}

const char *archive_entry_pathname_utf8(struct archive_entry *e) { return e->pathname; }

int archive_entry_update_pathname_utf8(struct archive_entry *e, const char *s) {
    set_cstr(&e->pathname, s);
    return 1;
}
int archive_entry_update_symlink_utf8(struct archive_entry *e, const char *s) {
    set_cstr(&e->symlink, s);
    return 1;
}
int archive_entry_update_hardlink_utf8(struct archive_entry *e, const char *s) {
    set_cstr(&e->hardlink, s);
    return 1;
}

/* BSD file flags. RAR carries none that matter to an extract, and honouring
 * them would mean chflags/FILE_ATTRIBUTE juggling this tree does not do.
 * Returning NULL is upstream's "parsed it all" answer. */
const char *archive_entry_copy_fflags_text(struct archive_entry *e, const char *s) {
    (void)e; (void)s;
    return NULL;
}

/* ------------------------------------------------------------------ *
 * string conversion
 * ------------------------------------------------------------------ */

/* The RAR3 reader hands member names over with a conversion object saying
 * what encoding they are in. Only one of the three needs real work: RAR3
 * packs unicode names as UTF-16BE, and everything downstream of here is
 * UTF-8. The other two are byte copies -- a UTF-8 name is already right, and
 * a name with no unicode field at all is whatever bytes the archiver wrote,
 * which is the same thing every other reader in lib/archive.c does with a
 * name it cannot attribute an encoding to. */
struct archive_string_conv {
    const char *name;
    int utf16be;
};

static struct archive_string_conv sconv_utf8 = { "UTF-8", 0 };
static struct archive_string_conv sconv_utf16be = { "UTF-16BE", 1 };

struct archive_string_conv *
archive_string_conversion_from_charset(struct archive *a, const char *charset, int best_effort) {
    (void)a; (void)best_effort;
    if (charset != NULL && strcmp(charset, "UTF-16BE") == 0) return &sconv_utf16be;
    return &sconv_utf8;
}

/* Upstream returns NULL here on every platform but Windows, meaning "no
 * conversion needed"; the readers then pass NULL along and the name is
 * copied as bytes. Same answer everywhere is the honest one for a tree with
 * no locale layer. */
struct archive_string_conv *archive_string_default_conversion_for_read(struct archive *a) {
    (void)a;
    return NULL;
}

const char *archive_string_conversion_charset_name(struct archive_string_conv *sc) {
    return sc == NULL ? "current locale" : sc->name;
}

/* UTF-16BE to UTF-8. Returns 0, or -1 on a malformed sequence -- which the
 * caller reports as a name it could not convert, not as a fatal error. */
static int utf16be_to_utf8(struct archive_string *as, const unsigned char *p, size_t len) {
    size_t i;
    for (i = 0; i + 1 < len; i += 2) {
        unsigned long c = ((unsigned long)p[i] << 8) | p[i + 1];
        if (c >= 0xD800 && c < 0xDC00) {          /* high surrogate */
            unsigned long lo;
            if (i + 3 >= len) return -1;
            lo = ((unsigned long)p[i + 2] << 8) | p[i + 3];
            if (lo < 0xDC00 || lo >= 0xE000) return -1;
            c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
            i += 2;
        } else if (c >= 0xDC00 && c < 0xE000) {   /* stray low surrogate */
            return -1;
        }
        if (c < 0x80) {
            archive_strappend_char(as, (char)c);
        } else if (c < 0x800) {
            archive_strappend_char(as, (char)(0xC0 | (c >> 6)));
            archive_strappend_char(as, (char)(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            archive_strappend_char(as, (char)(0xE0 | (c >> 12)));
            archive_strappend_char(as, (char)(0x80 | ((c >> 6) & 0x3F)));
            archive_strappend_char(as, (char)(0x80 | (c & 0x3F)));
        } else {
            archive_strappend_char(as, (char)(0xF0 | (c >> 18)));
            archive_strappend_char(as, (char)(0x80 | ((c >> 12) & 0x3F)));
            archive_strappend_char(as, (char)(0x80 | ((c >> 6) & 0x3F)));
            archive_strappend_char(as, (char)(0x80 | (c & 0x3F)));
        }
    }
    return 0;
}

static int copy_l(char **slot, const void *src, size_t len, struct archive_string_conv *sc) {
    if (sc != NULL && sc->utf16be) {
        struct archive_string as;
        int r;
        archive_string_init(&as);
        r = utf16be_to_utf8(&as, (const unsigned char *)src, len);
        if (r == 0) set_str(slot, as.s == NULL ? "" : as.s, as.length);
        archive_string_free(&as);
        return r;
    }
    set_str(slot, (const char *)src, len);
    return 0;
}

int _archive_entry_copy_pathname_l(struct archive_entry *e, const char *s, size_t len,
                                   struct archive_string_conv *sc) {
    return copy_l(&e->pathname, s, len, sc);
}

int _archive_entry_copy_symlink_l(struct archive_entry *e, const char *s, size_t len,
                                  struct archive_string_conv *sc) {
    return copy_l(&e->symlink, s, len, sc);
}

/* ------------------------------------------------------------------ *
 * struct archive_string
 * ------------------------------------------------------------------ */

struct archive_string *archive_string_ensure(struct archive_string *as, size_t want) {
    char *p;
    size_t n;
    if (as->s != NULL && as->buffer_length >= want) return as;
    n = as->buffer_length ? as->buffer_length : 32;
    while (n < want) n *= 2;
    p = (char *)realloc(as->s, n);
    if (p == NULL) return NULL;
    as->s = p;
    as->buffer_length = n;
    return as;
}

struct archive_string *archive_array_append(struct archive_string *as, const char *p, size_t len) {
    if (archive_string_ensure(as, as->length + len + 1) == NULL) return NULL;
    memcpy(as->s + as->length, p, len);
    as->length += len;
    as->s[as->length] = '\0';
    return as;
}

struct archive_string *archive_strappend_char(struct archive_string *as, char c) {
    return archive_array_append(as, &c, 1);
}

struct archive_string *archive_strncat(struct archive_string *as, const void *p, size_t len) {
    const char *s = (const char *)p;
    size_t n = 0;
    if (s == NULL) return as;
    while (n < len && s[n] != '\0') n++;   /* upstream stops at a NUL too */
    return archive_array_append(as, s, n);
}

struct archive_string *archive_strcat(struct archive_string *as, const void *p) {
    if (p == NULL) return as;
    return archive_array_append(as, (const char *)p, strlen((const char *)p));
}

void archive_string_free(struct archive_string *as) {
    free(as->s);
    as->s = NULL;
    as->length = 0;
    as->buffer_length = 0;
}

/* Upstream's own note on its version applies here too: this implements only
 * the sliver of printf its callers use. RAR5 uses exactly one directive,
 * ";%zu" for the version suffix on a versioned member name. */
void archive_string_vsprintf(struct archive_string *as, const char *fmt, va_list ap) {
    char buf[64];
    for (; *fmt != '\0'; fmt++) {
        if (*fmt != '%') {
            archive_strappend_char(as, *fmt);
            continue;
        }
        fmt++;
        while (*fmt == 'z' || *fmt == 'l') fmt++;   /* size modifiers */
        switch (*fmt) {
        case 'u':
            sprintf(buf, "%lu", (unsigned long)va_arg(ap, size_t));
            archive_strcat(as, buf);
            break;
        case 'd':
            sprintf(buf, "%ld", (long)va_arg(ap, int));
            archive_strcat(as, buf);
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            archive_strcat(as, s == NULL ? "(null)" : s);
            break;
        }
        case '\0':
            return;
        default:
            archive_strappend_char(as, *fmt);
            break;
        }
    }
}

void archive_string_sprintf(struct archive_string *as, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    archive_string_vsprintf(as, fmt, ap);
    va_end(ap);
}

/* ------------------------------------------------------------------ *
 * the read stack
 * ------------------------------------------------------------------ */

#define RAR_BUF_MIN (64 * 1024)

/* One archive being read. struct archive_read is upstream's -- carried
 * verbatim in thirdparty/rar.h -- so the readers reach into the fields they
 * expect, and a libarchive release that adds one does not silently corrupt
 * anything here. What is ours is the buffer under it. */
struct OsrRar {
    struct archive_read a;
    struct archive_read_filter filter;   /* the readers read a->filter->position */
    struct archive_entry *entry;

    FILE *f;
    char *buf;
    size_t cap;        /* bytes allocated in buf */
    size_t start;      /* offset in buf where the live bytes begin */
    size_t avail;      /* live bytes, from buf[start] */
    int64_t pos;       /* file offset of buf[start] */
    int64_t fsize;     /* the file's size, so a header cannot ask for more */
    int eof;

    char err[512];
};

static struct OsrRar *self(struct archive_read *a) {
    /* struct archive_read is the first member, so the cast is the offset. */
    return (struct OsrRar *)a;
}

void archive_set_error(struct archive *a, int number, const char *fmt, ...) {
    struct OsrRar *r = (struct OsrRar *)a;   /* struct archive is first in both */
    va_list ap;
    a->archive_error_number = number;
    va_start(ap, fmt);
    vsprintf(r->err, fmt, ap);
    va_end(ap);
    a->error = r->err;
}

int __archive_check_magic(struct archive *a, unsigned int magic, unsigned int state,
                          const char *func) {
    (void)a; (void)magic; (void)state; (void)func;
    return ARCHIVE_OK;   /* one caller, one call order, checked by the compiler */
}

void __archive_reset_read_data(struct archive *a) {
    a->read_data_block = NULL;
    a->read_data_offset = 0;
    a->read_data_output_offset = 0;
    a->read_data_remaining = 0;
}

int __archive_read_register_format(struct archive_read *a, void *format_data,
        const char *name,
        int (*bid)(struct archive_read *, int),
        int (*options)(struct archive_read *, const char *, const char *),
        int (*read_header)(struct archive_read *, struct archive_entry *),
        int (*read_data)(struct archive_read *, const void **, size_t *, int64_t *),
        int (*read_data_skip)(struct archive_read *),
        int64_t (*seek_data)(struct archive_read *, int64_t, int),
        int (*cleanup)(struct archive_read *),
        int (*format_capabilities)(struct archive_read *),
        int (*has_encrypted_entries)(struct archive_read *)) {
    size_t i;
    for (i = 0; i < sizeof(a->formats) / sizeof(a->formats[0]); i++) {
        if (a->formats[i].name != NULL) continue;
        a->formats[i].data = format_data;
        a->formats[i].name = name;
        a->formats[i].bid = bid;
        a->formats[i].options = options;
        a->formats[i].read_header = read_header;
        a->formats[i].read_data = read_data;
        a->formats[i].read_data_skip = read_data_skip;
        a->formats[i].seek_data = seek_data;
        a->formats[i].cleanup = cleanup;
        a->formats[i].format_capabilties = format_capabilities;
        a->formats[i].has_encrypted_entries = has_encrypted_entries;
        return ARCHIVE_OK;
    }
    return ARCHIVE_FATAL;
}

/* Fill the buffer until it holds at least `want` bytes, or the file ends.
 *
 * Compaction happens here and nowhere else. That is the whole point of the
 * `start` cursor: libarchive's readers hold the pointer a read-ahead handed
 * back across calls to __archive_read_consume -- the RAR3 bit reader keeps
 * `br->next_in` pointing into it while it consumes what it already read --
 * so consuming must not move the bytes. Only a read-ahead may, and a
 * read-ahead is exactly when the caller asks for a fresh pointer. */
static int fill_to(struct OsrRar *r, size_t want) {
    while (r->avail < want && !r->eof) {
        size_t got;
        if (r->start > 0) {
            memmove(r->buf, r->buf + r->start, r->avail);
            r->start = 0;
        }
        if (r->cap < want || r->cap - r->avail < RAR_BUF_MIN / 4) {
            size_t cap = r->cap ? r->cap : RAR_BUF_MIN;
            char *p;
            while (cap < want + RAR_BUF_MIN) cap *= 2;
            p = (char *)realloc(r->buf, cap);
            if (p == NULL) return -1;
            r->buf = p;
            r->cap = cap;
        }
        got = fread(r->buf + r->avail, 1, r->cap - r->avail, r->f);
        if (got == 0) {
            r->eof = 1;
            break;
        }
        r->avail += got;
    }
    return 0;
}

const void *__archive_read_ahead(struct archive_read *a, size_t min, ssize_t *avail) {
    struct OsrRar *r = self(a);
    /* `min` comes from lengths the archive declares about itself, so a
     * corrupt header can ask for 2^63 bytes. Anything past the end of the
     * file is end of file, not an allocation -- which is what keeps fill_to's
     * doubling from overflowing as well. */
    if ((int64_t)min > r->fsize - r->pos) {
        if (avail != NULL) *avail = 0;
        return NULL;
    }
    if (fill_to(r, min) < 0) {
        if (avail != NULL) *avail = ARCHIVE_FATAL;
        return NULL;
    }
    if (r->avail < min || (min == 0 && r->avail == 0)) {
        /* Short of `min` means end of file, which is not an error here: the
         * readers use a failed read-ahead to detect the end of a header. */
        if (avail != NULL) *avail = 0;
        return NULL;
    }
    if (avail != NULL) *avail = (ssize_t)r->avail;
    return r->buf + r->start;
}

int64_t __archive_read_consume(struct archive_read *a, int64_t request) {
    struct OsrRar *r = self(a);
    int64_t done = 0;
    while (done < request) {
        int64_t n = request - done;
        if (r->avail > 0) {
            if ((int64_t)r->avail < n) n = (int64_t)r->avail;
            r->start += (size_t)n;
            r->avail -= (size_t)n;
        } else {
            /* Past the buffer: seek rather than read the skipped bytes.
             * fseek takes a long, which is 32 bits on the Windows tier this
             * exists for, so a big skip goes in several steps. */
            if (n > 0x7fffffffL) n = 0x7fffffffL;
            if (fseek(r->f, (long)n, SEEK_CUR) != 0) break;
            r->eof = 0;
        }
        r->pos += n;
        done += n;
    }
    r->filter.position = r->pos;
    return done;
}

int64_t __archive_read_seek(struct archive_read *a, int64_t offset, int whence) {
    struct OsrRar *r = self(a);
    int64_t target;
    switch (whence) {
    case SEEK_SET: target = offset; break;
    case SEEK_CUR: target = r->pos + offset; break;
    default: {
        if (fseek(r->f, 0, SEEK_END) != 0) return ARCHIVE_FATAL;
        target = (int64_t)ftell(r->f) + offset;
        break;
    }
    }
    /* Same 32-bit fseek limit as above, and here there is no way to step. */
    if (target < 0 || target > 0x7fffffffL) return ARCHIVE_FATAL;
    /* Inside what is already buffered, move the window rather than re-read. */
    if (target >= r->pos && target <= r->pos + (int64_t)r->avail) {
        size_t skip = (size_t)(target - r->pos);
        r->start += skip;
        r->avail -= skip;
    } else {
        if (fseek(r->f, (long)target, SEEK_SET) != 0) return ARCHIVE_FATAL;
        r->start = 0;
        r->avail = 0;
        r->eof = 0;
    }
    r->pos = target;
    r->filter.position = target;
    return target;
}

/* ------------------------------------------------------------------ *
 * what lib/archive.c calls
 * ------------------------------------------------------------------ */

OsrRar *osr_rar_open(const char *path) {
    struct OsrRar *r = (struct OsrRar *)calloc(1, sizeof(struct OsrRar));
    struct archive_format_descriptor *winner = NULL;
    int best = 0, i;

    if (r == NULL) return NULL;
    r->f = fopen(path, "rb");
    if (r->f == NULL) { free(r); return NULL; }
    if (fseek(r->f, 0, SEEK_END) != 0 || (r->fsize = (int64_t)ftell(r->f)) < 0 ||
        fseek(r->f, 0, SEEK_SET) != 0) {
        fclose(r->f); free(r); return NULL;
    }

    r->a.archive.magic = ARCHIVE_READ_MAGIC;
    r->a.archive.state = ARCHIVE_STATE_HEADER;
    r->a.filter = &r->filter;
    r->filter.archive = &r->a;
    r->entry = archive_entry_new();
    if (r->entry == NULL) { osr_rar_close((OsrRar *)r); return NULL; }

    /* Both readers register themselves, then bid on the stream; the higher
     * bid wins, which is what libarchive's own format detection does. */
    if (archive_read_support_format_rar(&r->a.archive) != ARCHIVE_OK ||
        archive_read_support_format_rar5(&r->a.archive) != ARCHIVE_OK) {
        osr_rar_close((OsrRar *)r);
        return NULL;
    }
    for (i = 0; i < (int)(sizeof(r->a.formats) / sizeof(r->a.formats[0])); i++) {
        int bid;
        if (r->a.formats[i].bid == NULL) continue;
        r->a.format = &r->a.formats[i];        /* the bidder reads format->data */
        bid = r->a.format->bid(&r->a, best);
        __archive_read_seek(&r->a, 0, SEEK_SET);
        if (bid > best) { best = bid; winner = &r->a.formats[i]; }
    }
    r->a.format = winner;
    if (winner == NULL) { osr_rar_close((OsrRar *)r); return NULL; }
    return (OsrRar *)r;
}

int osr_rar_next(OsrRar *r, OsrRarEntry *e) {
    int ret;

    /* Whatever of the previous member was not read has to be stepped over
     * before the next header is where the reader expects it. */
    if (r->a.archive.state == ARCHIVE_STATE_DATA && r->a.format->read_data_skip != NULL)
        r->a.format->read_data_skip(&r->a);

    archive_entry_clear(r->entry);
    ret = r->a.format->read_header(&r->a, r->entry);
    if (ret == ARCHIVE_EOF) return 0;
    if (ret < ARCHIVE_WARN) return -1;

    r->a.archive.state = ARCHIVE_STATE_DATA;
    memset(e, 0, sizeof(*e));
    e->name = r->entry->pathname;
    e->symlink = r->entry->symlink;
    e->mode = (long)(r->entry->mode & 07777);
    e->size = r->entry->size;
    e->encrypted = r->entry->data_encrypted || r->entry->metadata_encrypted;
    switch (r->entry->mode & AE_IFMT) {
    case AE_IFDIR: e->kind = OSR_RAR_DIR; break;
    case AE_IFLNK: e->kind = OSR_RAR_SYMLINK; break;
    case AE_IFREG: e->kind = OSR_RAR_FILE; break;
    default:       e->kind = r->entry->symlink != NULL ? OSR_RAR_SYMLINK : OSR_RAR_OTHER;
    }
    if (e->name == NULL) {
        strcpy(r->err, "RAR member has no name");
        r->a.archive.error = r->err;
        return -1;
    }
    return 1;
}

int osr_rar_read(OsrRar *r, const void **buf, size_t *len, int64_t *offset) {
    int ret = r->a.format->read_data(&r->a, buf, len, offset);
    if (ret == ARCHIVE_EOF) return 0;
    if (ret < ARCHIVE_WARN) return -1;
    return 1;
}

const char *osr_rar_error(OsrRar *r) {
    if (r == NULL) return "out of memory";
    return r->a.archive.error != NULL ? r->a.archive.error : "RAR read failed";
}

void osr_rar_close(OsrRar *r) {
    int i;
    if (r == NULL) return;
    for (i = 0; i < (int)(sizeof(r->a.formats) / sizeof(r->a.formats[0])); i++)
        if (r->a.formats[i].cleanup != NULL) {
            r->a.format = &r->a.formats[i];
            r->a.formats[i].cleanup(&r->a);
        }
    archive_entry_free(r->entry);
    archive_string_free(&r->a.archive.error_string);
    if (r->f != NULL) fclose(r->f);
    free(r->buf);
    free(r);
}
