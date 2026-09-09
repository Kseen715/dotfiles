/* lib/rar_shim.h -- reading a RAR archive with libarchive's readers alone.
 *
 * thirdparty/rar.h carries libarchive 3.8.1's two RAR readers and nothing
 * else of libarchive. The readers call into a read stack that is not there,
 * so lib/rar_shim.c supplies it -- a buffered reader over a FILE *, an error
 * stash, an entry struct and a few string helpers, around 40 functions. The
 * whole of libarchive that os-rice depends on is that file.
 *
 * What lib/archive.c uses is below: open, walk, read, close. Nothing of
 * libarchive's own API leaks through it, so nothing outside rar_shim.c has
 * to include thirdparty/rar.h.
 *
 * Not supported, deliberately: multi-volume sets (only the named file is
 * read) and encrypted archives, which are refused with an error rather than
 * prompting -- osr never has a passphrase to give.
 */
#ifndef OSR_RAR_SHIM_H
#define OSR_RAR_SHIM_H

#include <stddef.h>
/* Sizes and offsets in an archive do not fit a 32-bit long, which is what
 * `long` is on the Windows tier this exists for. <stdint.h> is not C89, but
 * every compiler nob.c drives provides it in C89 mode, and thirdparty/rar.h
 * already depends on that. */
#include <stdint.h>

typedef struct OsrRar OsrRar;

/* One member, as much of it as lib/archive.c acts on. `name` and `symlink`
 * point into the reader and are valid until the next osr_rar_next(). */
typedef struct {
    const char *name;
    const char *symlink;  /* NULL unless the member is a symlink */
    long mode;            /* permission bits; the type bits are in `kind` */
    int kind;             /* OSR_RAR_FILE / _DIR / _SYMLINK / _OTHER */
    int encrypted;
    int64_t size;         /* -1 when the reader does not know it yet */
} OsrRarEntry;

enum { OSR_RAR_FILE = 0, OSR_RAR_DIR, OSR_RAR_SYMLINK, OSR_RAR_OTHER };

/* NULL if the file is not a RAR archive at all, or cannot be opened. */
OsrRar *osr_rar_open(const char *path);

/* 1: `e` filled. 0: end of archive. -1: error, see osr_rar_error(). */
int osr_rar_next(OsrRar *r, OsrRarEntry *e);

/* One block of the current member's contents. 1: `*buf`/`*len` filled and
 * `*offset` is where the block belongs in the member (RAR stores sparse
 * files, so blocks can skip). 0: member finished. -1: error. */
int osr_rar_read(OsrRar *r, const void **buf, size_t *len, int64_t *offset);

const char *osr_rar_error(OsrRar *r);
void osr_rar_close(OsrRar *r);

#endif /* OSR_RAR_SHIM_H */
