"""amalgamate_rar.py -- rebuild rar.h from an unpacked libarchive release.

    curl -L -o libarchive-3.8.1.tar.gz \
        https://github.com/libarchive/libarchive/releases/download/v3.8.1/libarchive-3.8.1.tar.gz
    tar -xzf libarchive-3.8.1.tar.gz
    python3 amalgamate_rar.py libarchive-3.8.1 rar.h

Takes libarchive's two RAR readers -- `archive_read_support_format_rar.c`
(RAR 1.5 through 3.x) and `archive_read_support_format_rar5.c` (RAR 5) -- and
the three units they need that nothing else in this tree carries in a
compatible form: `archive_ppmd7.c` (libarchive's own API-renamed PPMd7, kept
rather than adapting the readers to the LZMA SDK's), the two BLAKE2s reference
files RAR5 checksums with, and `archive_time.c` for the NTFS/DOS timestamp
conversions -- the last only because writing four date functions by hand when
upstream ships them is worse than carrying 163 lines.

Nothing else of libarchive comes along. The readers are welded to libarchive's
read stack, but the surface they touch is small -- a buffered reader, an error
stash, an entry struct and a handful of string helpers -- and that surface is
implemented in lib/rar_shim.c against the real upstream headers, which this
amalgamation carries verbatim. So the structs the readers reach into are
upstream's, not a hand-maintained copy that drifts at the next release.

The one upstream header that cannot come along is `archive_platform.h`: it
starts with `#error` unless a configure-generated `config.h` exists. PROLOGUE
below is that configuration, written out rather than generated.

Keep local changes in this script rather than in rar.h, so a version bump
stays a re-run instead of a merge.
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import amalgamate_lib as A

PROLOGUE = r'''/*
 * rar.h -- libarchive 3.8.1's RAR readers, amalgamated into one
 * single-header library for os-rice.
 *
 * Upstream (https://libarchive.org/, Tim Kientzle et al) is BSD-2-Clause;
 * the licence is reproduced in the file comments below, where upstream put
 * it. This carries the decode side of RAR only: the RAR 1.5-3.x reader, the
 * RAR 5 reader, libarchive's PPMd7 and the BLAKE2s pair RAR5 checksums with.
 *
 * Vendored the way thirdparty/yaml.h and thirdparty/bearssl.h are: one file
 * in the source tree, no submodule, no package to install, and the same
 * stb-style split -- including it plainly gets the declarations, and exactly
 * one translation unit defines the code:
 *
 *   #define RAR_IMPLEMENTATION
 *   #include "../thirdparty/rar.h"
 *
 * That unit is lib/rar.c. Everywhere else just includes it.
 *
 * The readers are not standalone: they call into libarchive's read stack
 * (__archive_read_ahead, archive_set_error, the archive_entry setters, a
 * few archive_string helpers). lib/rar_shim.c implements that surface --
 * around 40 functions -- against the upstream headers this file carries
 * verbatim, so nothing here is a retyped copy of an upstream struct.
 *
 * All C89, like the rest of this tree, with one borrowed extension: the
 * fixed-width types in <stdint.h>, which every compiler this builds with
 * provides in C89 mode.
 *
 * Regenerating: see amalgamate_rar.py's docstring.
 *
 * ---- upstream licence ----
 *
 * Copyright (c) 2003-2007 Tim Kientzle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE DAMAGE.
 */

/* This is what archive_platform.h would have read out of a configure-built
 * config.h, written out here because that header #errors without one. Only
 * what the RAR readers and the headers under them look at is listed; the
 * rest of libarchive's several hundred HAVE_* macros belong to files this
 * amalgamation does not carry.
 *
 * HAVE_ZLIB_H stays off on purpose: archive_crc32.h then supplies its own
 * crc32(), so the RAR readers need no compression library at all. */
#define __LIBARCHIVE_BUILD 1
#define HAVE_LIMITS_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_ERRNO_H 1
#define HAVE_STDINT_H 1
#define HAVE_WCHAR_H 1

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

/* ssize_t and mode_t: POSIX puts them in <sys/types.h>, and the RAR readers
 * use both spellings raw. MSVC's ucrt declares neither -- not mode_t, and not
 * the underscored _mode_t either -- so spell both out here. unsigned short is
 * the width archive_entry.h's __LA_MODE_T already uses on _WIN32. */
#include <sys/types.h>
#include <sys/stat.h>
#if defined(_MSC_VER)
#if !defined(_SSIZE_T_DEFINED)
#define _SSIZE_T_DEFINED
#ifdef _WIN64
typedef __int64 ssize_t;
#else
typedef int ssize_t;
#endif
#endif
#if !defined(_MODE_T_DEFINED)
#define _MODE_T_DEFINED
typedef unsigned short mode_t;
#endif
/* The RAR 1.5-3.x reader spells the POSIX permission bits raw when it
 * translates a DOS/Windows attribute word into a mode. MSVC's <sys/stat.h>
 * carries only the S_IREAD/S_IWRITE/S_IEXEC trio, so define the rest at
 * their POSIX values -- what libarchive's own archive_windows.h does. */
#if !defined(S_IRUSR)
#define S_IRUSR 0000400
#define S_IWUSR 0000200
#define S_IXUSR 0000100
#define S_IRGRP 0000040
#define S_IXGRP 0000010
#define S_IROTH 0000004
#define S_IXOTH 0000001
#endif
/* The RAR 5 reader's debug printfs use PRIx32, and archive.h deliberately
 * skips <inttypes.h> on _MSC_VER for MSVC versions that predate it. Every
 * MSVC that can build this tree (2015 and up) ships the header. */
#include <inttypes.h>
#endif

/* archive_platform.h's tail: the error codes archive.h only lists as
 * comments, and the attribute spellings the sources use. EILSEQ over
 * upstream's BSD-only EFTYPE, which is the fallback it picks anyway. */
#define ARCHIVE_ERRNO_FILE_FORMAT EILSEQ
#define ARCHIVE_ERRNO_PROGRAMMER EINVAL
#define ARCHIVE_ERRNO_MISC (-1)
#if defined(__GNUC__) && (__GNUC__ >= 7)
#define __LA_FALLTHROUGH __attribute__((fallthrough))
#else
#define __LA_FALLTHROUGH
#endif
#define __LA_LIBC_CC
#define la_stat(path, stref) stat(path, stref)
'''

SOURCES = [
    'archive_read_support_format_rar.c',
    'archive_read_support_format_rar5.c',
    'archive_ppmd7.c',
    'archive_blake2s_ref.c',
    'archive_blake2sp_ref.c',
    'archive_time.c',
]

# Everything lib/rar_shim.c and lib/archive.c need to see. The private ones
# are here because the shim implements against them: struct archive_read and
# struct archive_entry are upstream's definitions, not copies.
HEADERS = [
    'archive.h', 'archive_entry.h', 'archive_private.h',
    'archive_read_private.h', 'archive_entry_locale.h', 'archive_string.h',
]

# archive_platform.h is replaced by PROLOGUE (it #errors without a
# configure-built config.h). archive_entry_private.h is dropped so that
# lib/rar_shim.c can define struct archive_entry itself: rar5.c includes the
# header but reaches into none of its fields, and upstream's definition is
# built on the archive_mstring/iconv machinery, which is far more of
# libarchive than the readers need. The rest belong to parts of libarchive
# this does not carry -- archive_windows.h and archive_xxhash.h to other
# formats, android_lf.h to upstream's Android large-file shim, and the two
# blake2 headers to the self-test vectors and the installed-blake2 build.
SKIP = ('archive_platform.h', 'config.h', 'archive_entry_private.h',
        'archive_windows.h', 'archive_xxhash.h', 'android_lf.h',
        'blake2-kat.h', 'blake2.h')


LLDIV = re.compile(
    r'\t\tlldiv_t tdiv;\n'
    r'(\t\tint64_t value = [^\n]*\n)'
    r'\n'
    r'\t\ttdiv = lldiv\(value, NTFS_TICKS\);\n'
    r'\t\t\*secs = tdiv\.quot;\n'
    r'\t\t\*nsecs = \(uint32_t\)\(tdiv\.rem \* 100\);\n')


PACKED_FALLBACK = re.compile(
    r'^#define BLAKE2_PACKED\(x\) _Pragma\("pack 1"\) x _Pragma\("pack 0"\)$',
    re.M)


# archive_crc32.h's crc32() guards its lazy table init with a `static
# volatile int`, which cproc -- one of the compilers nob.c drives -- rejects
# outright ("volatile store is not yet supported"). The qualifier buys
# nothing here even where it compiles: the table is filled with the same 256
# constants by whoever gets there first, and libarchive reads the flag with
# an ordinary load either way.
CRC_TBL_VOLATILE = re.compile(r'^\tstatic volatile int crc_tbl_inited = 0;$', re.M)


def clean(text, path):
    """Two upstream edits: C99's lldiv(), and BLAKE2_PACKED's _Pragma.

    Plain int64_t division is the same operation -- C89 leaves the sign of a
    negative quotient implementation-defined where C99 fixed it at truncation
    toward zero, but every compiler this builds with (gcc, both mingw
    targets) truncates, and lldiv is specified to truncate too. The branch is
    reached only for NTFS timestamps before 1970 in the first place.
    """
    if os.path.basename(path) == 'archive_blake2.h':
        text, n = PACKED_FALLBACK.subn(
            '/* amalgamated: the non-MSVC, non-GNU fallback spelled itself with\n'
            ' * _Pragma, which tcc 0.9.27 -- one of the compilers nob.c drives\n'
            ' * -- does not parse at all. Dropping the packing costs nothing:\n'
            ' * both blake2 parameter blocks are already naturally packed (every\n'
            ' * field lands on its own alignment, 32 and 64 bytes exactly), which\n'
            " * is why the MSVC and GNU branches above agree with it. */\n"
            '#define BLAKE2_PACKED(x) x', text)
        assert n == 1, 'archive_blake2.h no longer has the _Pragma BLAKE2_PACKED'
        return text
    if os.path.basename(path) == 'archive_crc32.h':
        text, n = CRC_TBL_VOLATILE.subn(
            '\t/* amalgamated: `volatile` dropped -- cproc, one of the compilers\n'
            '\t * nob.c drives, has no volatile store. The flag guards a lazy\n'
            '\t * table of 256 constants that any racing thread would fill\n'
            '\t * identically, and upstream reads it with a plain load. */\n'
            '\tstatic int crc_tbl_inited = 0;', text)
        assert n == 1, 'archive_crc32.h no longer guards its table with a volatile int'
        return text
    if os.path.basename(path) != 'archive_time.c':
        return text
    text, n = LLDIV.subn(
        r'\1\n'
        '\t\t/* amalgamated: lldiv() is C99; plain division truncates the same */\n'
        '\t\t*secs = value / NTFS_TICKS;\n'
        '\t\t*nsecs = (uint32_t)((value % NTFS_TICKS) * 100);\n', text)
    assert n == 1, 'archive_time.c no longer converts NTFS time with lldiv()'
    return text


def main():
    src = os.path.join(sys.argv[1], 'libarchive')
    out = sys.argv[2]
    assert os.path.isdir(src), '%s: not an unpacked libarchive' % sys.argv[1]

    text, nh, ns, renames = A.build(
        prologue=PROLOGUE,
        macro='RAR',
        headers=[os.path.join(src, h) for h in HEADERS],
        sources=[os.path.join(src, s) for s in SOURCES],
        roots=[src],
        skip=SKIP,
        clean=clean,
        prefix='rar_amalg',
    )
    open(out, 'w', encoding='utf-8').write(text)
    print('%s: %d headers, %d sources, %d renamed statics, %d lines'
          % (out, nh, ns, len(set(n for v in renames.values() for n in v)),
             text.count('\n')))


if __name__ == '__main__':
    main()
