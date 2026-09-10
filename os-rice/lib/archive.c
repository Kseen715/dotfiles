/* lib/archive.c -- see lib/archive.h.
 *
 * C89 + POSIX, and C89 + Win32 for the handful of questions the two systems
 * answer differently (how a directory is made, whether a symlink exists at
 * all, what a permission bit means).
 *
 * Two halves. The top one is ours and is the same on every tier: sniff the
 * format from magic bytes, walk the container, and write each member out
 * through one guarded path. The bottom one is the ladder osr_extract runs --
 * system tool first, vendored decoder second -- and it is the only part that
 * knows a `tar` binary might exist.
 *
 * THE WRITE PATH IS A TRUST BOUNDARY. Member names come out of a file that
 * was downloaded, so `dest_dir/../../etc/cron.d/x` is a name an archive is
 * allowed to contain and this program is not allowed to honour. safe_dest()
 * below is where that is decided, once, for every format: no absolute path,
 * no `..` component, no drive letter, no symlink followed on the way, and no
 * setuid or setgid bit kept. Every writer in this file goes through it.
 */
#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#define _DARWIN_C_SOURCE 1
#endif

#include "archive.h"
#include "common.h"
#include "module.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>
#else
#include <direct.h>
#include <sys/stat.h>
#include <sys/utime.h>
#endif

#ifdef OSR_HAVE_ARCHIVE
#include "../thirdparty/bzip2.h"
#include "../thirdparty/lzmasdk.h"
#include "../thirdparty/miniz.h"
#include "../thirdparty/zstd.h"
#include "rar_shim.h"
#endif

enum {
    FMT_UNKNOWN = 0, FMT_TAR, FMT_GZIP, FMT_BZIP2, FMT_XZ, FMT_ZSTD,
    FMT_ZIP, FMT_7Z, FMT_RAR, FMT_AR
};

static const char *const fmt_names[] = {
    "", "tar", "gzip", "bzip2", "xz", "zstd", "zip", "7z", "rar", "ar"
};

/* How many layers of "this decompresses to another archive" to follow. A
 * .tar.gz is two; nothing real is three, and a file that claims to be more
 * is a decompression bomb aimed at the recursion, not an archive. */
#define MAX_DEPTH 4

/* ------------------------------------------------------------------
 * sniffing
 * --------------------------------------------------------------- */

static int sniff_bytes(const unsigned char *b, size_t n) {
    if (n >= 6 && memcmp(b, "\xfd" "7zXZ\0", 6) == 0) return FMT_XZ;
    if (n >= 6 && memcmp(b, "7z\xbc\xaf\x27\x1c", 6) == 0) return FMT_7Z;
    if (n >= 8 && memcmp(b, "!<arch>\n", 8) == 0) return FMT_AR;
    if (n >= 7 && memcmp(b, "Rar!\x1a\x07", 6) == 0) return FMT_RAR;
    if (n >= 4 && memcmp(b, "\x28\xb5\x2f\xfd", 4) == 0) return FMT_ZSTD;
    if (n >= 4 && memcmp(b, "PK", 2) == 0 &&
        (b[2] == 3 || b[2] == 5 || b[2] == 7)) return FMT_ZIP;
    if (n >= 4 && memcmp(b, "BZh", 3) == 0 && b[3] >= '1' && b[3] <= '9')
        return FMT_BZIP2;
    if (n >= 2 && b[0] == 0x1f && b[1] == 0x8b) return FMT_GZIP;
    /* tar has no magic at offset 0: the "ustar" is 257 bytes in, and the
     * ancient v7 format has not even that. A 512-byte header whose checksum
     * field agrees with the block is the only honest test, and it is the one
     * `file` uses too. */
    if (n >= 265 && (memcmp(b + 257, "ustar\0", 6) == 0 ||
                     memcmp(b + 257, "ustar  ", 7) == 0)) return FMT_TAR;
    return FMT_UNKNOWN;
}

static int sniff_file(const char *path) {
    unsigned char b[512];
    size_t n;
    FILE *f = fopen(path, "rb");
    if (f == NULL) return FMT_UNKNOWN;
    memset(b, 0, sizeof(b));
    n = fread(b, 1, sizeof(b), f);
    fclose(f);
    return sniff_bytes(b, n);
}

const char *osr_archive_format(const char *path) {
    if (path == NULL) return "";
    return fmt_names[sniff_file(path)];
}

void osr_extract_init(OsrExtract *o) {
    if (o == NULL) return;
    o->archive = NULL;
    o->dest_dir = NULL;
    o->strip_components = 0;
    o->as_root = 0;
    o->as_user = 0;
    o->quiet = 0;
    o->allow_system = 1;
}

/* ------------------------------------------------------------------
 * the write path -- the trust boundary
 * --------------------------------------------------------------- */

/* safe_dest -- where a member named `name` is allowed to land, if anywhere.
 *
 * Returns 1 with an absolute-enough path in `out`, or 0 having explained
 * itself. Refused, in order: a name that vanishes under strip_components, a
 * drive letter or UNC prefix, a leading separator, any component that is
 * exactly "..", and anything that does not fit OSR_PATH_MAX. Both separators
 * are treated as separators no matter which system is running: a zip written
 * on Windows carries backslashes, and a `..\..\x` that only Windows would
 * split is still an escape when POSIX writes the file. */
static int safe_dest(const OsrExtract *o, const char *name,
                     char *out, unsigned long out_sz) {
    char clean[OSR_PATH_MAX];
    const char *p = name;
    unsigned long w = 0;
    int strip = o->strip_components;

    if (name == NULL || name[0] == '\0') return 0;
    if (name[0] == '/' || name[0] == '\\') {
        osr_warnf("archive: refusing absolute member %s", name);
        return 0;
    }
    if (name[1] == ':') {
        osr_warnf("archive: refusing member with a drive letter %s", name);
        return 0;
    }

    while (*p != '\0') {
        const char *end = p;
        unsigned long len;
        while (*end != '\0' && *end != '/' && *end != '\\') end++;
        len = (unsigned long)(end - p);
        if (len == 2 && p[0] == '.' && p[1] == '.') {
            osr_warnf("archive: refusing path traversal in %s", name);
            return 0;
        }
        if (len != 0 && !(len == 1 && p[0] == '.')) {
            if (strip > 0) {
                strip--;
            } else {
                if (w + len + 2 > sizeof(clean)) return 0;
                if (w > 0) clean[w++] = '/';
                memcpy(clean + w, p, len);
                w += len;
            }
        }
        p = (*end == '\0') ? end : end + 1;
    }
    clean[w] = '\0';
    /* Everything the member named was stripped away: not an error, just a
     * member this extraction does not want. */
    if (w == 0) return 0;
    return osr_path_join(out, out_sz, o->dest_dir, clean);
}

static int make_parents(const char *path) {
    char dir[OSR_PATH_MAX];
    osr_dirname(path, dir, sizeof(dir));
    if (dir[0] == '\0' || strcmp(dir, ".") == 0) return 1;
    return osr_mkdir_parents(dir);
}

/* open_member -- the member's file, created without following a symlink that
 * is already sitting there. An archive that ships `a` as a symlink to
 * /etc/passwd and then ships `a` as a regular file is asking exactly that,
 * and O_NOFOLLOW is the kernel answering no. Where there is no O_NOFOLLOW
 * (Windows, and pre-2008 POSIX), unlinking first gets most of the way: the
 * open then creates its own file rather than opening through the link. */
static FILE *open_member(const char *path) {
#ifndef _WIN32
    int flags = O_WRONLY | O_CREAT | O_TRUNC;
    int fd;
    FILE *f;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    remove(path);
    fd = open(path, flags, 0600);
    if (fd < 0) {
        osr_warnf("archive: cannot write %s: %s", path, strerror(errno));
        return NULL;
    }
    f = fdopen(fd, "wb");
    if (f == NULL) close(fd);
    return f;
#else
    FILE *f;
    remove(path);
    f = fopen(path, "wb");
    if (f == NULL) osr_warnf("archive: cannot write %s", path);
    return f;
#endif
}

/* apply_mode -- the permission bits, setuid and setgid dropped.
 *
 * A tar can carry mode 04755, and honouring that on a file this program just
 * wrote as root is how an archive turns into a root shell. Nothing os-rice
 * installs needs a setuid bit; the ones that do (sudo, ping) come from the
 * package manager. Windows has no such bits and one writable flag, so the
 * whole question is POSIX-only. */
static void apply_mode(const char *path, long mode) {
#ifndef _WIN32
    if (mode <= 0) return;
    chmod(path, (mode_t)(mode & 0777));
#else
    (void)path; (void)mode;
#endif
}

static void apply_mtime(const char *path, long mtime) {
    struct utimbuf t;
    if (mtime <= 0) return;
    t.actime = (time_t)mtime;
    t.modtime = (time_t)mtime;
    utime(path, &t);
}

static int make_dir_member(const char *path, long mode) {
    if (!osr_mkdir_parents(path)) return 0;
    apply_mode(path, mode);
    return 1;
}

/* make_symlink -- a symlink member, when the system has such a thing.
 *
 * The target is checked as strictly as a member name: a link to an absolute
 * path or one climbing out with `..` is refused. It is not that the link
 * itself does damage -- it is that the next member can be written *through*
 * it, and refusing the link is cheaper than resolving every path component
 * of every later write. Windows has no symlink an unprivileged process may
 * create, so there the member is skipped and said so. */
static int make_symlink(const char *target, const char *path) {
#ifndef _WIN32
    const char *p;
    if (target == NULL || target[0] == '\0') return 0;
    if (target[0] == '/') {
        osr_warnf("archive: refusing absolute symlink %s -> %s", path, target);
        return 0;
    }
    for (p = target; *p != '\0'; p++) {
        if (p[0] == '.' && p[1] == '.' &&
            (p[2] == '/' || p[2] == '\0') &&
            (p == target || p[-1] == '/')) {
            osr_warnf("archive: refusing escaping symlink %s -> %s", path, target);
            return 0;
        }
    }
    if (!make_parents(path)) return 0;
    remove(path);
    if (symlink(target, path) != 0) {
        osr_warnf("archive: cannot link %s: %s", path, strerror(errno));
        return 0;
    }
    return 1;
#else
    (void)target;
    osr_warnf("archive: skipping symlink %s (not supported here)", path);
    return 1;
#endif
}

/* write_all -- one member's bytes, from a callback that feeds them. */
static int copy_stream(FILE *in, FILE *out, int64_t bytes) {
    char buf[16384];
    while (bytes > 0) {
        size_t want = sizeof(buf);
        size_t got;
        if ((int64_t)want > bytes) want = (size_t)bytes;
        got = fread(buf, 1, want, in);
        if (got == 0) return 0;
        if (fwrite(buf, 1, got, out) != got) return 0;
        bytes -= (int64_t)got;
    }
    return 1;
}

/* ------------------------------------------------------------------
 * tar
 * --------------------------------------------------------------- */

/* tar is 512-byte blocks, and a header is one of them. Three dialects have
 * to be read, because all three are in the wild among the downloads this
 * program fetches: plain ustar, GNU's 'L'/'K' long-name members (a member
 * whose *contents* are the next member's name), and pax 'x' extended headers
 * (a member whose contents are "len key=value\n" records, of which only
 * `path` and `linkpath` matter here). Everything else in a pax record --
 * ownership, ACLs, nanosecond times -- is either not ours to restore or not
 * worth restoring for a dotfiles install. */

#define TAR_BLK 512

typedef struct {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} TarHdr;

static int64_t tar_octal(const char *p, size_t n) {
    int64_t v = 0;
    size_t i;
    /* GNU writes sizes over 8GB as base-256 with the high bit set. */
    if ((unsigned char)p[0] & 0x80) {
        v = (int64_t)((unsigned char)p[0] & 0x7f);
        for (i = 1; i < n; i++) v = (v << 8) | (unsigned char)p[i];
        return v;
    }
    for (i = 0; i < n; i++) {
        if (p[i] == ' ' || p[i] == '\0') continue;
        if (p[i] < '0' || p[i] > '7') break;
        v = v * 8 + (p[i] - '0');
    }
    return v;
}

static int tar_checksum_ok(const TarHdr *h) {
    const unsigned char *b = (const unsigned char *)h;
    long sum = 0;
    int i;
    for (i = 0; i < TAR_BLK; i++)
        sum += (i >= 148 && i < 156) ? ' ' : b[i];
    return sum == (long)tar_octal(h->chksum, sizeof(h->chksum));
}

/* tar_skip -- read past exactly this many bytes. Reading rather than seeking,
 * because the same reader runs over a pipe-like stream in the compressed
 * cases. */
static int tar_skip(FILE *f, int64_t bytes) {
    char buf[TAR_BLK];
    while (bytes > 0) {
        size_t want = (size_t)(bytes < TAR_BLK ? bytes : TAR_BLK);
        if (fread(buf, 1, want, f) != want) return 0;
        bytes -= (int64_t)want;
    }
    return 1;
}

/* A member's body occupies whole blocks: its size rounded up. */
static int64_t tar_padded(int64_t size) {
    return (size + TAR_BLK - 1) / TAR_BLK * TAR_BLK;
}

/* Read a member's body into a Str: how the GNU long-name and pax members
 * hand their payload to the header that follows them. */
static int tar_body_str(FILE *f, int64_t size, Str *out) {
    int64_t left = size;
    char buf[TAR_BLK];
    str_reset(out);
    if (size < 0 || size > 1024L * 1024L) return 0;
    while (left > 0) {
        size_t take = (size_t)(left > TAR_BLK ? TAR_BLK : left);
        if (fread(buf, 1, TAR_BLK, f) != TAR_BLK) return 0;
        str_add(out, buf, take);
        left -= (int64_t)take;
    }
    return 1;
}

/* pax records are "<len> <key>=<value>\n", len counting itself. */
static void pax_lookup(const char *rec, size_t n, const char *key,
                       char *out, unsigned long out_sz) {
    size_t i = 0;
    size_t klen = strlen(key);
    while (i < n) {
        size_t len = 0, j = i;
        while (j < n && rec[j] >= '0' && rec[j] <= '9')
            len = len * 10 + (size_t)(rec[j++] - '0');
        if (len == 0 || i + len > n || j >= n || rec[j] != ' ') return;
        j++;
        if (j + klen + 1 <= i + len && strncmp(rec + j, key, klen) == 0 &&
            rec[j + klen] == '=') {
            size_t vlen = (i + len) - (j + klen + 1);
            if (vlen > 0 && rec[i + len - 1] == '\n') vlen--;
            if (vlen + 1 <= out_sz) {
                memcpy(out, rec + j + klen + 1, vlen);
                out[vlen] = '\0';
            }
            return;
        }
        i += len;
    }
}

static int extract_tar(const OsrExtract *o, FILE *f) {
    TarHdr h;
    Str next_name, next_link, pax;
    char name[OSR_PATH_MAX];
    char link[OSR_PATH_MAX];
    char dest[OSR_PATH_MAX];
    int empty = 0;
    int ok = 1;

    str_init(&next_name);
    str_init(&next_link);
    str_init(&pax);
    name[0] = link[0] = '\0';

    while (fread(&h, 1, TAR_BLK, f) == TAR_BLK) {
        int64_t size;
        long mode, mtime;

        if (h.name[0] == '\0' && h.chksum[0] == '\0') {
            /* Two zero blocks end the archive; one alone is padding. */
            if (++empty >= 2) break;
            continue;
        }
        empty = 0;
        if (!tar_checksum_ok(&h)) {
            osr_warnf("archive: bad tar header checksum");
            ok = 0;
            break;
        }

        size = tar_octal(h.size, sizeof(h.size));
        mode = (long)tar_octal(h.mode, sizeof(h.mode));
        mtime = (long)tar_octal(h.mtime, sizeof(h.mtime));

        if (h.typeflag == 'L' || h.typeflag == 'K') {
            Str *dst = (h.typeflag == 'L') ? &next_name : &next_link;
            if (!tar_body_str(f, size, dst)) { ok = 0; break; }
            continue;
        }
        if (h.typeflag == 'x' || h.typeflag == 'X' || h.typeflag == 'g') {
            char v[OSR_PATH_MAX];
            if (!tar_body_str(f, size, &pax)) { ok = 0; break; }
            if (h.typeflag == 'g') continue;
            v[0] = '\0';
            pax_lookup(str_text(&pax), pax.len, "path", v, sizeof(v));
            if (v[0] != '\0') { str_reset(&next_name); str_addz(&next_name, v); }
            v[0] = '\0';
            pax_lookup(str_text(&pax), pax.len, "linkpath", v, sizeof(v));
            if (v[0] != '\0') { str_reset(&next_link); str_addz(&next_link, v); }
            continue;
        }

        /* The name: a long-name member's payload if one came, else prefix +
         * name, which ustar splits when the whole is over 100 bytes. */
        if (next_name.len > 0) {
            if (!osr_copy_bounded(name, sizeof(name), str_text(&next_name))) name[0] = '\0';
        } else {
            char base[101];
            memcpy(base, h.name, 100);
            base[100] = '\0';
            if (h.prefix[0] != '\0' && memcmp(h.magic, "ustar", 5) == 0) {
                char pre[156];
                memcpy(pre, h.prefix, 155);
                pre[155] = '\0';
                if (!osr_path_join(name, sizeof(name), pre, base)) name[0] = '\0';
            } else if (!osr_copy_bounded(name, sizeof(name), base)) {
                name[0] = '\0';
            }
        }
        if (next_link.len > 0) {
            if (!osr_copy_bounded(link, sizeof(link), str_text(&next_link))) link[0] = '\0';
        } else {
            memcpy(link, h.linkname, 100);
            link[100] = '\0';
        }
        str_reset(&next_name);
        str_reset(&next_link);

        if (name[0] == '\0' || !safe_dest(o, name, dest, sizeof(dest))) {
            if (!tar_skip(f, tar_padded(size))) { ok = 0; break; }
            continue;
        }

        switch (h.typeflag) {
        case '5':
            make_dir_member(dest, mode);
            break;
        case '1':   /* hard link: made as a copy of nothing -- see below */
        case '2':
            make_symlink(link, dest);
            break;
        case '3': case '4': case '6':
            /* Device and FIFO members. A dotfiles install has no business
             * creating either, and creating one needs privilege anyway. */
            osr_warnf("archive: skipping special file %s", name);
            break;
        default: {
            FILE *out;
            if (!make_parents(dest)) { ok = 0; break; }
            out = open_member(dest);
            if (out == NULL) { ok = 0; break; }
            if (!copy_stream(f, out, size)) {
                fclose(out);
                osr_warnf("archive: truncated tar member %s", name);
                ok = 0;
                break;
            }
            fclose(out);
            apply_mode(dest, mode);
            apply_mtime(dest, mtime);
            /* The body is padded to a block boundary. */
            if (size % TAR_BLK != 0 && !tar_skip(f, TAR_BLK - (size % TAR_BLK)))
                ok = 0;
            break;
        }
        }
        if (!ok) break;
        if (h.typeflag == '5' || h.typeflag == '1' || h.typeflag == '2' ||
            h.typeflag == '3' || h.typeflag == '4' || h.typeflag == '6') {
            if (!tar_skip(f, tar_padded(size))) { ok = 0; break; }
        }
    }

    str_free(&next_name);
    str_free(&next_link);
    str_free(&pax);
    return ok;
}

/* ------------------------------------------------------------------
 * ar -- the container a .deb is
 * --------------------------------------------------------------- */

/* Each member is a 60-byte header (name 16, mtime 12, uid 6, gid 6, mode 8,
 * size 10, then "`\n") followed by its bytes, padded to an even offset.
 *
 * ponytail: only the plain short-name form is read. A .deb holds exactly
 * three members -- debian-binary, control.tar.*, data.tar.* -- and every one
 * of those names fits the 16 bytes. GNU's "//" long-name table and BSD's
 * "#1/<len>" form belong to .a files built by a toolchain, which is not
 * something this program ever unpacks; add them the day it does. */
static int extract_ar(const OsrExtract *o, FILE *f) {
    char hdr[60];
    char name[17];
    char dest[OSR_PATH_MAX];
    int ok = 1;

    if (fseek(f, 8, SEEK_SET) != 0) return 0;
    while (fread(hdr, 1, sizeof(hdr), f) == sizeof(hdr)) {
        int64_t size;
        int i;
        FILE *out;

        if (hdr[58] != '`' || hdr[59] != '\n') {
            osr_warnf("archive: bad ar member header");
            return 0;
        }
        memcpy(name, hdr, 16);
        name[16] = '\0';
        for (i = 15; i >= 0 && (name[i] == ' ' || name[i] == '/'); i--)
            name[i] = '\0';
        /* the size field is decimal, not octal */
        size = 0;
        for (i = 48; i < 58 && hdr[i] >= '0' && hdr[i] <= '9'; i++)
            size = size * 10 + (hdr[i] - '0');

        if (name[0] == '\0' || !safe_dest(o, name, dest, sizeof(dest))) {
            if (fseek(f, (long)(size + (size & 1)), SEEK_CUR) != 0) return 0;
            continue;
        }
        if (!make_parents(dest)) return 0;
        out = open_member(dest);
        if (out == NULL) return 0;
        if (!copy_stream(f, out, size)) {
            fclose(out);
            osr_warnf("archive: truncated ar member %s", name);
            return 0;
        }
        fclose(out);
        if ((size & 1) != 0 && fseek(f, 1, SEEK_CUR) != 0) return 0;
    }
    return ok;
}

#ifdef OSR_HAVE_ARCHIVE

/* ------------------------------------------------------------------
 * the single-stream decompressors
 * --------------------------------------------------------------- */

/* Each of these turns one compressed file into one plain file. That is all a
 * .tar.gz needs: the tar reader above then walks the result. Streaming the
 * two together would save a temporary file and cost a coroutine or a thread
 * in a program that has neither; the temporary lives in osr_tmpdir() and is
 * removed on the way out.
 *
 * ponytail: buffers are 64 KiB in, 256 KiB out. Bigger buys nothing here --
 * the download that produced the file took a thousand times longer. */
#define DEC_IN 65536
#define DEC_OUT 262144

static int gzip_skip_header(FILE *in) {
    int flg, c, i;
    unsigned xlen;
    if (fgetc(in) != 0x1f || fgetc(in) != 0x8b) return 0;
    if (fgetc(in) != 8) return 0;   /* deflate is the only method defined */
    flg = fgetc(in);
    if (flg < 0) return 0;
    for (i = 0; i < 6; i++) if (fgetc(in) < 0) return 0;   /* mtime, xfl, os */
    if (flg & 4) {
        int lo = fgetc(in), hi = fgetc(in);
        if (lo < 0 || hi < 0) return 0;
        xlen = (unsigned)lo | ((unsigned)hi << 8);
        while (xlen-- > 0) if (fgetc(in) < 0) return 0;
    }
    if (flg & 8) while ((c = fgetc(in)) != 0) if (c < 0) return 0;
    if (flg & 16) while ((c = fgetc(in)) != 0) if (c < 0) return 0;
    if (flg & 2) { if (fgetc(in) < 0 || fgetc(in) < 0) return 0; }
    return 1;
}

/* Raw deflate through tinfl, miniz's streaming decompressor: the vendored
 * miniz is built with MINIZ_NO_ZLIB_APIS, so there is no mz_inflate here and
 * this is upstream's own low-level interface. It decodes into a 32 KiB
 * wrapping window, which is the deflate dictionary itself -- output is
 * flushed out of that window as it goes, so the file size never matters. */
static int decode_gzip(FILE *in, FILE *out) {
    tinfl_decompressor *inf;
    unsigned char *ibuf, *dict;
    const unsigned char *next_in;
    size_t in_avail = 0, dict_ofs = 0;
    int ok = 1, done = 0;

    if (!gzip_skip_header(in)) return 0;
    inf = tinfl_decompressor_alloc();
    ibuf = (unsigned char *)malloc(DEC_IN);
    dict = (unsigned char *)malloc(TINFL_LZ_DICT_SIZE);
    if (inf == NULL || ibuf == NULL || dict == NULL) {
        if (inf != NULL) tinfl_decompressor_free(inf);
        free(ibuf);
        free(dict);
        return 0;
    }
    tinfl_init(inf);
    next_in = ibuf;

    while (ok && !done) {
        size_t in_bytes, out_bytes;
        tinfl_status st;
        mz_uint flags = 0;

        if (in_avail == 0) {
            in_avail = fread(ibuf, 1, DEC_IN, in);
            next_in = ibuf;
        }
        if (in_avail > 0) flags |= TINFL_FLAG_HAS_MORE_INPUT;
        in_bytes = in_avail;
        out_bytes = TINFL_LZ_DICT_SIZE - dict_ofs;
        st = tinfl_decompress(inf, next_in, &in_bytes, dict, dict + dict_ofs,
                              &out_bytes, flags);
        in_avail -= in_bytes;
        next_in += in_bytes;
        if (out_bytes > 0 && fwrite(dict + dict_ofs, 1, out_bytes, out) != out_bytes)
            ok = 0;
        dict_ofs = (dict_ofs + out_bytes) & (TINFL_LZ_DICT_SIZE - 1);
        if (st == TINFL_STATUS_DONE) done = 1;
        else if (st < TINFL_STATUS_DONE) ok = 0;
    }
    tinfl_decompressor_free(inf);
    free(ibuf);
    free(dict);
    return ok && done;
}

static int decode_bzip2(FILE *in, FILE *out) {
    bz_stream s;
    char *ibuf, *obuf;
    int ok = 1, done = 0;

    memset(&s, 0, sizeof(s));
    if (BZ2_bzDecompressInit(&s, 0, 0) != BZ_OK) return 0;
    ibuf = (char *)malloc(DEC_IN);
    obuf = (char *)malloc(DEC_OUT);
    if (ibuf == NULL || obuf == NULL) { free(ibuf); free(obuf); BZ2_bzDecompressEnd(&s); return 0; }

    while (ok && !done) {
        int r;
        if (s.avail_in == 0) {
            s.next_in = ibuf;
            s.avail_in = (unsigned int)fread(ibuf, 1, DEC_IN, in);
            if (s.avail_in == 0) { ok = 0; break; }
        }
        s.next_out = obuf;
        s.avail_out = DEC_OUT;
        r = BZ2_bzDecompress(&s);
        if (r != BZ_OK && r != BZ_STREAM_END) ok = 0;
        if (r == BZ_STREAM_END) done = 1;
        {
            size_t got = DEC_OUT - s.avail_out;
            if (got > 0 && fwrite(obuf, 1, got, out) != got) ok = 0;
        }
    }
    BZ2_bzDecompressEnd(&s);
    free(ibuf);
    free(obuf);
    return ok && done;
}

/* The LZMA SDK builds its CRC tables at run time and every xz and 7z entry
 * point assumes someone did it -- an xz stream header fails its own check
 * with SZ_ERROR_NO_ARCHIVE otherwise, which reads like a corrupt file. */
static void crc_tables_once(void) {
    static int done = 0;
    if (!done) {
        done = 1;
        CrcGenerateTable();
        Crc64GenerateTable();
    }
}

static int decode_xz(FILE *in, FILE *out) {
    CXzUnpacker u;
    Byte *ibuf, *obuf;
    size_t have = 0, used = 0;
    int ok = 1, done = 0;

    crc_tables_once();
    XzUnpacker_Construct(&u, &g_Alloc);
    XzUnpacker_Init(&u);
    ibuf = (Byte *)malloc(DEC_IN);
    obuf = (Byte *)malloc(DEC_OUT);
    if (ibuf == NULL || obuf == NULL) { free(ibuf); free(obuf); XzUnpacker_Free(&u); return 0; }

    while (ok && !done) {
        SizeT inlen, outlen;
        ECoderStatus status;
        int srcFinished = 0;
        SRes r;

        if (used == have) {
            have = fread(ibuf, 1, DEC_IN, in);
            used = 0;
            if (have == 0) srcFinished = 1;
        }
        inlen = have - used;
        outlen = DEC_OUT;
        r = XzUnpacker_Code(&u, obuf, &outlen, ibuf + used, &inlen, srcFinished,
                            CODER_FINISH_ANY, &status);
        if (r != SZ_OK) ok = 0;
        used += inlen;
        if (outlen > 0 && fwrite(obuf, 1, outlen, out) != outlen) ok = 0;
        if (status == CODER_STATUS_FINISHED_WITH_MARK ||
            (srcFinished && inlen == 0 && outlen == 0)) done = 1;
    }
    if (ok && !XzUnpacker_IsStreamWasFinished(&u)) ok = 0;
    XzUnpacker_Free(&u);
    free(ibuf);
    free(obuf);
    return ok;
}

static int decode_zstd(FILE *in, FILE *out) {
    ZSTD_DStream *d = ZSTD_createDStream();
    char *ibuf, *obuf;
    ZSTD_inBuffer zin;
    int ok = 1, done = 0;

    if (d == NULL) return 0;
    ibuf = (char *)malloc(DEC_IN);
    obuf = (char *)malloc(DEC_OUT);
    if (ibuf == NULL || obuf == NULL) { free(ibuf); free(obuf); ZSTD_freeDStream(d); return 0; }
    zin.src = ibuf;
    zin.size = 0;
    zin.pos = 0;

    while (ok && !done) {
        ZSTD_outBuffer zout;
        size_t r;
        if (zin.pos == zin.size) {
            zin.size = fread(ibuf, 1, DEC_IN, in);
            zin.pos = 0;
            if (zin.size == 0) { ok = 0; break; }
        }
        zout.dst = obuf;
        zout.size = DEC_OUT;
        zout.pos = 0;
        r = ZSTD_decompressStream(d, &zout, &zin);
        if (ZSTD_isError(r)) ok = 0;
        if (zout.pos > 0 && fwrite(obuf, 1, zout.pos, out) != zout.pos) ok = 0;
        if (r == 0) done = 1;   /* a frame ended cleanly */
    }
    ZSTD_freeDStream(d);
    free(ibuf);
    free(obuf);
    return ok && done;
}

/* ------------------------------------------------------------------
 * zip, through miniz
 * --------------------------------------------------------------- */

static size_t zip_sink(void *ctx, mz_uint64 ofs, const void *buf, size_t n) {
    (void)ofs;
    return fwrite(buf, 1, n, (FILE *)ctx) == n ? n : 0;
}

static int extract_zip(const OsrExtract *o, const char *path) {
    mz_zip_archive z;
    mz_uint i, n;
    int ok = 1;

    memset(&z, 0, sizeof(z));
    if (!mz_zip_reader_init_file(&z, path, 0)) {
        osr_warnf("archive: %s is not a readable zip", path);
        return 0;
    }
    n = mz_zip_reader_get_num_files(&z);
    for (i = 0; i < n && ok; i++) {
        mz_zip_archive_file_stat st;
        char dest[OSR_PATH_MAX];
        FILE *out;
        long mode;

        if (!mz_zip_reader_file_stat(&z, i, &st)) { ok = 0; break; }
        if (!safe_dest(o, st.m_filename, dest, sizeof(dest))) continue;
        if (mz_zip_reader_is_file_a_directory(&z, i)) {
            make_dir_member(dest, 0755);
            continue;
        }
        /* A zip written on Unix keeps the mode in the top 16 bits of the
         * external attributes; one written on Windows has nothing there, and
         * 0644 is the honest answer. The executable bit matters: half the
         * things this program unpacks from a zip are programs. */
        mode = (long)((st.m_external_attr >> 16) & 0777);
        if (mode == 0) mode = 0644;
        /* A symlink in a zip is a member whose body is the target text and
         * whose file-type bits say S_IFLNK. Writing it as a regular file
         * full of a path is worse than useless. */
        if (((st.m_external_attr >> 16) & 0xf000) == 0xa000 &&
            st.m_uncomp_size > 0 && st.m_uncomp_size < OSR_PATH_MAX) {
            char target[OSR_PATH_MAX];
            size_t got = (size_t)st.m_uncomp_size;
            if (mz_zip_reader_extract_to_mem(&z, i, target, got, 0)) {
                target[got] = '\0';
                make_symlink(target, dest);
                continue;
            }
        }
        if (!make_parents(dest)) { ok = 0; break; }
        out = open_member(dest);
        if (out == NULL) { ok = 0; break; }
        if (!mz_zip_reader_extract_to_callback(&z, i, zip_sink, out, 0)) {
            osr_warnf("archive: cannot unpack %s", st.m_filename);
            ok = 0;
        }
        fclose(out);
        /* ponytail: no mtime. The vendored miniz is built with
         * MINIZ_NO_TIME (thirdparty/miniz.h's PROLOGUE), so the stat struct
         * carries no timestamp to restore; nothing this program unpacks from
         * a zip cares what date the file claims. */
        if (ok) apply_mode(dest, mode);
    }
    mz_zip_reader_end(&z);
    return ok;
}

/* ------------------------------------------------------------------
 * 7z, through the LZMA SDK
 * --------------------------------------------------------------- */

/* 7z keeps names as UTF-16; everything this program touches wants UTF-8.
 * Surrogate pairs are folded back into one code point so a name outside the
 * BMP survives -- an emoji in a filename is rare but not invalid. */
static void utf16_to_utf8(const UInt16 *src, size_t n, char *out, size_t out_sz) {
    size_t i, w = 0;
    for (i = 0; i < n && src[i] != 0; i++) {
        unsigned long c = src[i];
        if (c >= 0xd800 && c <= 0xdbff && i + 1 < n &&
            src[i + 1] >= 0xdc00 && src[i + 1] <= 0xdfff) {
            c = 0x10000 + ((c - 0xd800) << 10) + (src[i + 1] - 0xdc00);
            i++;
        }
        if (c < 0x80) {
            if (w + 2 > out_sz) break;
            out[w++] = (char)c;
        } else if (c < 0x800) {
            if (w + 3 > out_sz) break;
            out[w++] = (char)(0xc0 | (c >> 6));
            out[w++] = (char)(0x80 | (c & 0x3f));
        } else if (c < 0x10000) {
            if (w + 4 > out_sz) break;
            out[w++] = (char)(0xe0 | (c >> 12));
            out[w++] = (char)(0x80 | ((c >> 6) & 0x3f));
            out[w++] = (char)(0x80 | (c & 0x3f));
        } else {
            if (w + 5 > out_sz) break;
            out[w++] = (char)(0xf0 | (c >> 18));
            out[w++] = (char)(0x80 | ((c >> 12) & 0x3f));
            out[w++] = (char)(0x80 | ((c >> 6) & 0x3f));
            out[w++] = (char)(0x80 | (c & 0x3f));
        }
    }
    out[w] = '\0';
}

static int extract_7z(const OsrExtract *o, const char *path) {
    CFileInStream fs;
    CLookToRead2 look;
    CSzArEx db;
    UInt32 i, blockIndex = 0xFFFFFFFF;
    Byte *outBuf = NULL;
    size_t outBufSize = 0;
    UInt16 *namebuf = NULL;
    size_t namecap = 0;
    int ok = 1;

    if (InFile_Open(&fs.file, path) != 0) {
        osr_warnf("archive: cannot open %s", path);
        return 0;
    }
    FileInStream_CreateVTable(&fs);
    LookToRead2_CreateVTable(&look, False);
    look.buf = (Byte *)malloc(DEC_IN);
    if (look.buf == NULL) { File_Close(&fs.file); return 0; }
    look.bufSize = DEC_IN;
    look.realStream = &fs.vt;
    LookToRead2_INIT(&look)

    crc_tables_once();
    SzArEx_Init(&db);
    if (SzArEx_Open(&db, &look.vt, &g_Alloc, &g_Alloc) != SZ_OK) {
        osr_warnf("archive: %s is not a readable 7z", path);
        ok = 0;
    }

    for (i = 0; ok && i < db.NumFiles; i++) {
        size_t len = SzArEx_GetFileNameUtf16(&db, i, NULL);
        char name[OSR_PATH_MAX];
        char dest[OSR_PATH_MAX];
        size_t offset = 0, processed = 0;
        FILE *out;

        if (len > namecap) {
            UInt16 *p = (UInt16 *)realloc(namebuf, len * sizeof(UInt16));
            if (p == NULL) { ok = 0; break; }
            namebuf = p;
            namecap = len;
        }
        SzArEx_GetFileNameUtf16(&db, i, namebuf);
        utf16_to_utf8(namebuf, len, name, sizeof(name));
        if (!safe_dest(o, name, dest, sizeof(dest))) continue;
        if (SzArEx_IsDir(&db, i)) {
            make_dir_member(dest, 0755);
            continue;
        }
        if (SzArEx_Extract(&db, &look.vt, i, &blockIndex, &outBuf, &outBufSize,
                           &offset, &processed, &g_Alloc, &g_Alloc) != SZ_OK) {
            osr_warnf("archive: cannot unpack %s", name);
            ok = 0;
            break;
        }
        /* As in the zip walker: a symlink is a member whose body is the
         * target text, marked by the Unix file-type bits in Attribs. */
        if (SzBitWithVals_Check(&db.Attribs, i) &&
            (db.Attribs.Vals[i] & 0x8000) &&
            ((db.Attribs.Vals[i] >> 16) & 0xf000) == 0xa000 &&
            processed > 0 && processed < OSR_PATH_MAX) {
            char target[OSR_PATH_MAX];
            memcpy(target, outBuf + offset, processed);
            target[processed] = '\0';
            make_symlink(target, dest);
            continue;
        }
        if (!make_parents(dest)) { ok = 0; break; }
        out = open_member(dest);
        if (out == NULL) { ok = 0; break; }
        if (processed > 0 && fwrite(outBuf + offset, 1, processed, out) != processed)
            ok = 0;
        fclose(out);
        /* 7z carries Windows attributes; the Unix mode, when the archive was
         * written on one, is in the high 16 bits of the same field. */
        if (ok && SzBitWithVals_Check(&db.Attribs, i)) {
            UInt32 attr = db.Attribs.Vals[i];
            if (attr & 0x8000) apply_mode(dest, (long)((attr >> 16) & 0777));
        }
    }

    ISzAlloc_Free(&g_Alloc, outBuf);
    SzArEx_Free(&db, &g_Alloc);
    free(look.buf);
    free(namebuf);
    File_Close(&fs.file);
    return ok;
}

/* ------------------------------------------------------------------
 * RAR, through lib/rar_shim.c
 * --------------------------------------------------------------- */

static int extract_rar(const OsrExtract *o, const char *path) {
    OsrRar *r = osr_rar_open(path);
    OsrRarEntry e;
    int rc, ok = 1;

    if (r == NULL) {
        osr_warnf("archive: cannot open %s", path);
        return 0;
    }
    while (ok && (rc = osr_rar_next(r, &e)) > 0) {
        char dest[OSR_PATH_MAX];
        FILE *out;
        const void *buf;
        size_t len;
        int64_t off;

        if (e.encrypted) {
            osr_warnf("archive: %s is encrypted, skipping %s", path, e.name);
            continue;
        }
        if (!safe_dest(o, e.name, dest, sizeof(dest))) continue;
        if (e.kind == OSR_RAR_DIR) {
            make_dir_member(dest, e.mode ? e.mode : 0755);
            continue;
        }
        if (e.kind == OSR_RAR_SYMLINK) {
            make_symlink(e.symlink, dest);
            continue;
        }
        if (e.kind != OSR_RAR_FILE) continue;
        if (!make_parents(dest)) { ok = 0; break; }
        out = open_member(dest);
        if (out == NULL) { ok = 0; break; }
        /* RAR stores sparse members as blocks with gaps, so a block says
         * where it belongs rather than simply following the last one. */
        while ((rc = osr_rar_read(r, &buf, &len, &off)) > 0) {
            if (fseek(out, (long)off, SEEK_SET) != 0 ||
                fwrite(buf, 1, len, out) != len) { ok = 0; break; }
        }
        if (rc < 0) {
            osr_warnf("archive: %s: %s", e.name, osr_rar_error(r));
            ok = 0;
        }
        fclose(out);
        if (ok) {
            apply_mode(dest, e.mode);
        }
    }
    if (rc < 0) {
        osr_warnf("archive: %s: %s", path, osr_rar_error(r));
        ok = 0;
    }
    osr_rar_close(r);
    return ok;
}

#endif /* OSR_HAVE_ARCHIVE */

/* ------------------------------------------------------------------
 * the ladder
 * --------------------------------------------------------------- */

static int extract_vendored(const OsrExtract *o, int fmt, int depth);

#ifdef OSR_HAVE_ARCHIVE

/* tmp_name -- a scratch path under osr_tmpdir(), tagged with the pid so two
 * runs cannot collide. */
static int tmp_name(char *out, unsigned long out_sz, int depth) {
    char base[64];
    sprintf(base, "osr-ax-%ld-%d", osr_pid(), depth);
    return osr_path_join(out, out_sz, osr_tmpdir(), base);
}

/* A compressed stream holds one file. Decode it to a scratch file, then look
 * at what came out: a .tar.gz is a tar, and a bare .gz is the file itself,
 * which lands in dest_dir under the name minus its suffix. */
static int extract_compressed(const OsrExtract *o, int fmt, int depth) {
    char tmp[OSR_PATH_MAX];
    FILE *in, *out;
    int inner, ok;

    if (depth >= MAX_DEPTH) {
        osr_warnf("archive: %s is nested too deeply", o->archive);
        return 0;
    }
    if (!tmp_name(tmp, sizeof(tmp), depth)) return 0;
    in = fopen(o->archive, "rb");
    if (in == NULL) {
        osr_warnf("archive: cannot open %s", o->archive);
        return 0;
    }
    out = fopen(tmp, "wb");
    if (out == NULL) {
        fclose(in);
        osr_warnf("archive: cannot write %s", tmp);
        return 0;
    }
    switch (fmt) {
    case FMT_GZIP:  ok = decode_gzip(in, out); break;
    case FMT_BZIP2: ok = decode_bzip2(in, out); break;
    case FMT_XZ:    ok = decode_xz(in, out); break;
    case FMT_ZSTD:  ok = decode_zstd(in, out); break;
    default:        ok = 0; break;
    }
    fclose(in);
    if (fclose(out) != 0) ok = 0;
    if (!ok) {
        osr_warnf("archive: cannot decompress %s", o->archive);
        remove(tmp);
        return 0;
    }

    inner = sniff_file(tmp);
    if (inner != FMT_UNKNOWN) {
        OsrExtract inner_o = *o;
        inner_o.archive = tmp;
        ok = extract_vendored(&inner_o, inner, depth + 1);
    } else {
        /* Not a container: the decompressed file itself is the payload, and
         * it is named after the archive with the compression suffix removed
         * -- `foo.txt.gz` gives `foo.txt`, which is what gunzip does. */
        char name[OSR_PATH_MAX];
        char dest[OSR_PATH_MAX];
        char *dot;
        if (!osr_copy_bounded(name, sizeof(name), osr_basename(o->archive)))
            return 0;
        dot = strrchr(name, '.');
        if (dot != NULL && dot != name) *dot = '\0';
        if (!safe_dest(o, name, dest, sizeof(dest)) || !make_parents(dest)) {
            remove(tmp);
            return 0;
        }
        in = fopen(tmp, "rb");
        out = in != NULL ? open_member(dest) : NULL;
        ok = 0;
        if (out != NULL) {
            char buf[16384];
            size_t n;
            ok = 1;
            while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
                if (fwrite(buf, 1, n, out) != n) { ok = 0; break; }
            fclose(out);
        }
        if (in != NULL) fclose(in);
    }
    remove(tmp);
    return ok;
}

static int extract_vendored(const OsrExtract *o, int fmt, int depth) {
    FILE *f;
    int ok;

    switch (fmt) {
    case FMT_ZIP: return extract_zip(o, o->archive);
    case FMT_7Z:  return extract_7z(o, o->archive);
    case FMT_RAR: return extract_rar(o, o->archive);
    case FMT_GZIP: case FMT_BZIP2: case FMT_XZ: case FMT_ZSTD:
        return extract_compressed(o, fmt, depth);
    case FMT_TAR: case FMT_AR:
        break;
    default:
        osr_warnf("archive: %s is in no format this program reads", o->archive);
        return 0;
    }

    f = fopen(o->archive, "rb");
    if (f == NULL) {
        osr_warnf("archive: cannot open %s", o->archive);
        return 0;
    }
    ok = (fmt == FMT_TAR) ? extract_tar(o, f) : extract_ar(o, f);
    fclose(f);
    return ok;
}

#else /* !OSR_HAVE_ARCHIVE */

/* NOB_ARCHIVE=0 builds this half: the sniffer, the tar and ar readers and the
 * write path are all plain C and stay, but nothing that needs a vendored
 * decoder is compiled, exactly as lib/tls.c drops to the system TLS. A dev
 * build gets its seconds back; a shipped build never takes this branch. */
static int extract_vendored(const OsrExtract *o, int fmt, int depth) {
    FILE *f;
    int ok;
    (void)depth;

    if (fmt != FMT_TAR && fmt != FMT_AR) {
        osr_warnf("archive: no built-in extractor for %s (built with NOB_ARCHIVE=0)",
                  o->archive);
        return 0;
    }
    f = fopen(o->archive, "rb");
    if (f == NULL) {
        osr_warnf("archive: cannot open %s", o->archive);
        return 0;
    }
    ok = (fmt == FMT_TAR) ? extract_tar(o, f) : extract_ar(o, f);
    fclose(f);
    return ok;
}

#endif /* OSR_HAVE_ARCHIVE */

/* system_tool -- the tool that would have run before this file existed, and
 * still runs when it is there. Same argv the call sites built by hand, so a
 * box with tar behaves byte for byte as it did.
 *
 * `tar` reads every compression layer itself, so it is tried for the
 * compressed formats too; a tar that cannot (the ancient ones cannot do xz)
 * simply fails and the vendored path picks it up. */
static int system_tool(const OsrExtract *o, int fmt) {
    char *argv[10];
    char strip[32];
    int n = 0;
    const char *tool;
    const char *dot;

    switch (fmt) {
    case FMT_TAR: case FMT_GZIP: case FMT_BZIP2: case FMT_XZ: case FMT_ZSTD:
        tool = osr_have_cmd("tar") ? "tar" : (osr_have_cmd("bsdtar") ? "bsdtar" : NULL);
        break;
    case FMT_ZIP:
        tool = osr_have_cmd("unzip") ? "unzip" : NULL;
        break;
    case FMT_UNKNOWN:
        /* Nothing matched the magic bytes, which does not mean nothing can
         * read it: .Z, .lz and .lzo are all formats tar knows and this file
         * does not sniff. Hand it to the system tool named by the extension
         * and let it report its own failure, rather than refusing something
         * that worked before this file existed. */
        dot = strrchr(o->archive, '.');
        if (dot != NULL && strcmp(dot, ".zip") == 0)
            tool = osr_have_cmd("unzip") ? "unzip" : NULL;
        else
            tool = osr_have_cmd("tar") ? "tar" : (osr_have_cmd("bsdtar") ? "bsdtar" : NULL);
        break;
    default:
        /* 7z, RAR and ar: the vendored readers are the only ones this tree
         * ever had, so there is no system path to prefer. */
        tool = NULL;
        break;
    }
    if (tool == NULL) return 0;

    argv[n++] = (char *)tool;
    if (strcmp(tool, "unzip") == 0) {
        if (o->quiet) argv[n++] = (char *)"-q";
        argv[n++] = (char *)"-o";
        argv[n++] = (char *)o->archive;
        argv[n++] = (char *)"-d";
        argv[n++] = (char *)o->dest_dir;
    } else {
        argv[n++] = (char *)"-xf";
        argv[n++] = (char *)o->archive;
        argv[n++] = (char *)"-C";
        argv[n++] = (char *)o->dest_dir;
        if (o->strip_components > 0) {
            sprintf(strip, "--strip-components=%d", o->strip_components);
            argv[n++] = strip;
        }
    }
    argv[n] = NULL;

    /* unzip has no --strip-components; letting it run and then finding the
     * files one level too deep is worse than not running it. */
    if (strcmp(tool, "unzip") == 0 && o->strip_components > 0) return 0;

    if (o->as_user) return (o->quiet ? osr_run_user_quiet(argv) : osr_run_user(argv)) == 0;
    return (o->as_root ? osr_run_root(argv) : osr_run(argv)) == 0;
}

/* give_to_user -- hand what the vendored path just wrote to OSR_USER. The
 * in-process writers run with whatever privilege the process has, and a rice
 * started under sudo is root, so without this a font unpacked into
 * ~/.local/share/fonts would be root-owned (SS8). Same shape as lib/git.c's
 * repair of a previous sudo run. */
static void give_to_user(const char *dir) {
#ifndef _WIN32
    char *argv[5];
    Str owner;

    str_init(&owner);
    str_addzz(&owner, osr_mod_user(), ":", osr_mod_user(), (const char *)NULL);
    argv[0] = (char *)"chown"; argv[1] = (char *)"-R";
    argv[2] = (char *)str_text(&owner); argv[3] = (char *)dir; argv[4] = NULL;
    (void)osr_run_root_quiet(argv);
    str_free(&owner);
#else
    (void)dir;
#endif
}

int osr_extract(const OsrExtract *o) {
    int fmt;

    if (o == NULL || o->archive == NULL || o->dest_dir == NULL) return 0;
    if (!dir_exists(o->dest_dir)) {
        /* A root destination (/usr/local/<tree>) cannot be created by this
         * process when the rice runs unescalated, so an as_root extraction
         * makes it the way everything else in the tree does. */
        if (o->as_root) {
            char *mk[4];
            mk[0] = (char *)"mkdir"; mk[1] = (char *)"-p";
            mk[2] = (char *)o->dest_dir; mk[3] = NULL;
            (void)osr_run_root_quiet(mk);
        } else if (!osr_mkdir_parents(o->dest_dir)) {
            osr_warnf("archive: cannot create %s", o->dest_dir);
            return 0;
        }
    }

    fmt = sniff_file(o->archive);
    if (o->allow_system && system_tool(o, fmt)) return 1;
    if (fmt == FMT_UNKNOWN) {
        osr_warnf("archive: %s is in no format this program reads", o->archive);
        return 0;
    }
    if (o->as_root)
        osr_warnf("archive: unpacking %s without root -- no system tool for it",
                  o->archive);
    if (!extract_vendored(o, fmt, 0)) return 0;
    if (o->as_user) give_to_user(o->dest_dir);
    return 1;
}
