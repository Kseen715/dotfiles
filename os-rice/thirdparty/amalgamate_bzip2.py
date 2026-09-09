"""amalgamate_bzip2.py -- rebuild bzip2.h from an unpacked bzip2 release.

    curl -L -o bzip2-1.0.8.tar.gz https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz
    tar -xf bzip2-1.0.8.tar.gz
    python3 amalgamate_bzip2.py bzip2-1.0.8 bzip2.h

Takes the library, which is upstream's seven library sources -- not the bzip2
and bzip2recover command-line programs, nor the test files beside them.

The compressor comes along even though this tree only ever decompresses:
bzlib.c holds both halves of the public API in one file, so BZ2_bzCompress and
its callees are defined whether or not anything calls them, and pulling them
out would mean editing upstream's source rather than configuring it. That
trade is the wrong way round -- 2.5k lines of unreachable code is cheaper than
a local fork of the file that owns the API.

Keep local changes in this script rather than in bzip2.h, so a version bump
stays a re-run instead of a merge.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import amalgamate_lib as A

PROLOGUE = r'''/*
 * bzip2.h -- the bzip2 1.0.8 library, amalgamated into one single-header
 * library for os-rice.
 *
 * Upstream (https://sourceware.org/bzip2/, Julian Seward) is BSD-like; the
 * licence is reproduced further down, at the top of upstream's own bzlib.h.
 * This carries the whole library, but the only half this tree calls is the
 * decompressor: lib/archive.c opens the .tar.bz2 downloads lib/build.c
 * fetches when the system has no tar and no bunzip2 to do it.
 *
 * Vendored the way thirdparty/yaml.h and thirdparty/bearssl.h are: one file
 * in the source tree, no submodule, no package to install, and the same
 * stb-style split -- including it plainly gets the declarations, and exactly
 * one translation unit defines the code:
 *
 *   #define BZIP2_IMPLEMENTATION
 *   #include "../thirdparty/bzip2.h"
 *
 * That unit is lib/bzip2.c. Everywhere else just includes it.
 *
 * All C89, like the rest of this tree -- upstream is already C90 throughout,
 * so unlike the other vendored decoders here nothing had to be rewritten.
 *
 * Decompressing a stream, in short (see lib/archive.c for the whole thing):
 *
 *   bz_stream s;
 *   s.bzalloc = NULL; s.bzfree = NULL; s.opaque = NULL;
 *   BZ2_bzDecompressInit(&s, 0, 0);
 *   s.next_in = in; s.avail_in = n; s.next_out = out; s.avail_out = m;
 *   r = BZ2_bzDecompress(&s);        // BZ_OK until BZ_STREAM_END
 *   BZ2_bzDecompressEnd(&s);
 *
 * Regenerating:
 *
 *   curl -L -o bzip2-1.0.8.tar.gz https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz
 *   tar -xf bzip2-1.0.8.tar.gz
 *   python3 amalgamate_bzip2.py bzip2-1.0.8 bzip2.h
 *
 * What the script does to upstream: expands each local #include once and
 * gives each source's file-scope macros an #undef after its body so they do
 * not leak into the next file. That is all -- there was no dialect work to
 * do. System #includes are left where upstream put them.
 */
'''

SOURCES = ['blocksort.c', 'huffman.c', 'crctable.c', 'randtable.c',
           'compress.c', 'decompress.c', 'bzlib.c']

HEADERS = ['bzlib.h']


def main():
    src = sys.argv[1]
    out = sys.argv[2]
    assert os.path.isfile(os.path.join(src, 'bzlib.c')), \
        '%s: not an unpacked bzip2 release (no bzlib.c)' % src

    text, nh, ns, renames = A.build(
        prologue=PROLOGUE,
        macro='BZIP2',
        headers=[os.path.join(src, h) for h in HEADERS],
        sources=[os.path.join(src, s) for s in SOURCES],
        roots=[src],
        prefix='bz_amalg',
    )
    open(out, 'w', encoding='utf-8').write(text)
    print('%s: %d headers, %d sources, %d renamed statics, %d lines'
          % (out, nh, ns, len(set(n for v in renames.values() for n in v)),
             text.count('\n')))


if __name__ == '__main__':
    main()
