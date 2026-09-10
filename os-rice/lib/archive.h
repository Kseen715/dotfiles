/* lib/archive.h -- opening an archive without the system's help.
 *
 * C89 + POSIX, like the rest of lib/. The Windows half is behind the same
 * platform branches the rest of the tree uses.
 *
 * Every archive this program opened used to be opened by a tool it did not
 * bring: `tar -xf` at seventeen call sites, `unzip` at two, PowerShell's
 * Expand-Archive on Windows. On the tiers this repo targets -- Windows XP, a
 * minimal POSIX box -- none of those exist, and the install stopped there.
 * So the binary carries its own decoders (thirdparty/lzmasdk.h, miniz.h,
 * bzip2.h, zstd.h, rar.h) and this is the one door to them.
 *
 * osr_extract still prefers the system tool when there is one: same argv the
 * call sites built by hand, so a normal box behaves exactly as before, and
 * the vendored path is what happens when that tool is missing or fails.
 *
 * Formats read: tar (ustar, GNU long names, pax), gzip, bzip2, xz, zstd,
 * zip, 7z, RAR 3 and 5, and the `!<arch>` container a .deb is. Decode only --
 * nothing here writes an archive.
 */
#ifndef OSR_ARCHIVE_H
#define OSR_ARCHIVE_H

/* One extraction. Fill it through osr_extract_init, then set what differs:
 * C89 has no named arguments, and CODE_STYLE.md's "Structures and
 * initialization" is why this is a named type rather than a long argument
 * list that grows a parameter every time a call site needs one more thing. */
typedef struct {
    const char *archive;    /* the file to open; required */
    const char *dest_dir;   /* where members land; required, created if absent */
    int strip_components;   /* drop this many leading path components */
    int as_root;            /* write with root privilege */
    int as_user;            /* write as OSR_USER: members land under the riced
                             * user's home, so root-owned files there would be
                             * files that user cannot later replace (SS8) */
    int quiet;              /* no per-archive progress line */
    int allow_system;       /* 0 forces the vendored path; the tests use it */
} OsrExtract;

void osr_extract_init(OsrExtract *o);

/* 1 on success, 0 on failure (already reported through osr_warnf). */
int osr_extract(const OsrExtract *o);

/* The format of `path`, sniffed from its magic bytes rather than its name:
 * "tar", "gzip", "bzip2", "xz", "zstd", "zip", "7z", "rar", "ar", or "" when
 * nothing matched. A compressed stream reads as its compression ("gzip"),
 * not as what is inside it -- osr_extract is what unwraps the layers. */
const char *osr_archive_format(const char *path);

#endif /* OSR_ARCHIVE_H */
