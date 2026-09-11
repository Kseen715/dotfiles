"""amalgamate_miniz.py -- rebuild miniz.h from an unpacked miniz release.

    curl -L -o miniz.zip https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
    unzip -d miniz302 miniz.zip
    python3 amalgamate_miniz.py miniz302 miniz.h

Upstream already ships one .c and one .h, so there is far less to do here than
in the other scripts: fold the two into the stb-style single header this tree
uses, rewrite the `//` comments C90 has no word for, and set the configuration
macros that leave the compressor behind.

Keep local changes in this script rather than in miniz.h, so a version bump
stays a re-run instead of a merge.
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import amalgamate_lib as A

PROLOGUE = r'''/*
 * miniz.h -- miniz 3.0.2, the zip reader and inflate, amalgamated into one
 * single-header library for os-rice.
 *
 * Upstream (https://github.com/richgel999/miniz, Rich Geldreich et al) is
 * MIT-licensed; the licence is reproduced further down, in upstream's own
 * header comment. This carries the decode side only: the zip central
 * directory reader, inflate, and the CRC-32 that both need -- which is what
 * lib/archive.c opens a .zip with when the system has no unzip, and the
 * checksum lib/rar_shim.c hands libarchive's RAR readers.
 *
 * Vendored the way thirdparty/yaml.h and thirdparty/bearssl.h are: one file
 * in the source tree, no submodule, no package to install, and the same
 * stb-style split -- including it plainly gets the declarations, and exactly
 * one translation unit defines the code:
 *
 *   #define MINIZ_IMPLEMENTATION
 *   #include "../thirdparty/miniz.h"
 *
 * That unit is lib/miniz.c. Everywhere else just includes it.
 *
 * All C89, like the rest of this tree. Upstream's only non-C90 spelling is
 * the `//` comment, and the script that generated this file rewrote every
 * one of them to a block comment.
 *
 * Reading an archive, in short (see lib/archive.c for the whole thing):
 *
 *   mz_zip_archive z;
 *   memset(&z, 0, sizeof(z));
 *   mz_zip_reader_init_file(&z, path, 0);
 *   n = mz_zip_reader_get_num_files(&z);
 *   mz_zip_reader_file_stat(&z, i, &st);
 *   mz_zip_reader_extract_to_callback(&z, i, sink, ctx, 0);
 *   mz_zip_reader_end(&z);
 *
 * Regenerating:
 *
 *   curl -L -o miniz.zip https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
 *   unzip -d miniz302 miniz.zip
 *   python3 amalgamate_miniz.py miniz302 miniz.h
 *
 * What the script does to upstream, beyond concatenating the two files:
 * rewrites `//` comments, empties the unknown-compiler fallback definition
 * of MZ_FORCEINLINE (C90 has no `inline`; the gcc and MSVC spellings beside
 * it are extensions and are kept), comments out upstream's #pragma message
 * about the file-I/O path, and sets the two macros below. System #includes
 * are left where upstream put them.
 */

/* Extract only. MINIZ_NO_DEFLATE_APIS turns off MINIZ_NO_ARCHIVE_WRITING_APIS
 * as well (miniz.h:159), so tdefl and the zip writer both go; inflate, the
 * zip reader and mz_crc32 stay. MINIZ_NO_ZLIB_APIS drops the compatibility
 * layer, which is the half that would #define plain `crc32`, `uncompress`
 * and friends over anything else in the program that owns those names.
 * stdio and time stay on: the zip reader opens files and reports mtimes. */
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_ZLIB_APIS
'''


FORCEINLINE = re.compile(r'^#define MZ_FORCEINLINE inline\s*$', re.M)
PRAGMA_MESSAGE = re.compile(r'^#pragma message\(.*\)\s*$', re.M)
PRAGMAS = {}


def clean(text, path):
    """Two upstream edits: no `inline`, and no #pragma message.

    miniz.c announces its file-I/O path with a #pragma message, which no
    compiler flag can quiet; commented out, it stops being printed on every
    build. main() checks that exactly one was found.

    miniz.h picks __forceinline for MSVC and __inline__ for gcc/clang, both
    extensions this tree already compiles with, and falls back to plain
    `inline` for anything else -- a word C90 does not have, so a strict C89
    compiler reads it as a type name and the declaration after it stops
    parsing. MZ_FORCEINLINE is a hint; empty is a correct definition of it.
    """
    # The file-I/O note miniz.c prints on every build of lib/miniz.c. No -W
    # flag turns a #pragma message off, so the line itself goes.
    text, pragmas = PRAGMA_MESSAGE.subn(
        lambda m: '/* amalgamated: silenced */\n/* %s */' % m.group(0), text)
    PRAGMAS[os.path.basename(path)] = pragmas

    if os.path.basename(path) != 'miniz.h':
        return text
    text, n = FORCEINLINE.subn(
        '/* amalgamated: C90 has no `inline` */\n#define MZ_FORCEINLINE', text)
    assert n == 1, 'miniz.h no longer falls back to plain `inline`'
    return text


def main():
    src = sys.argv[1]
    out = sys.argv[2]
    for base in (src, os.path.join(src, 'miniz')):
        if os.path.isfile(os.path.join(base, 'miniz.c')):
            src = base
            break
    else:
        raise SystemExit('%s: not an unpacked miniz release (no miniz.c)' % sys.argv[1])

    text, nh, ns, renames = A.build(
        prologue=PROLOGUE,
        macro='MINIZ',
        headers=[os.path.join(src, 'miniz.h')],
        sources=[os.path.join(src, 'miniz.c')],
        roots=[src],
        clean=clean,
        prefix='mz_amalg',
    )
    assert sum(PRAGMAS.values()) == 1, (
        'expected exactly one #pragma message upstream, found %r' % PRAGMAS)
    open(out, 'w', encoding='utf-8').write(text)
    print('%s: %d headers, %d sources, %d renamed statics, %d lines'
          % (out, nh, ns, len(set(n for v in renames.values() for n in v)),
             text.count('\n')))


if __name__ == '__main__':
    main()
