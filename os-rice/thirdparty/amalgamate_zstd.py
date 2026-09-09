"""amalgamate_zstd.py -- rebuild zstd.h from an unpacked zstd release.

    curl -L -o zstd-1.5.7.tar.gz https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
    tar -xf zstd-1.5.7.tar.gz
    python3 amalgamate_zstd.py zstd-1.5.7 zstd.h

Takes lib/decompress plus the parts of lib/common it needs. Nothing that
compresses is carried, and neither is the multithreaded pool (pool.c,
threading.c) nor the dictionary builder -- decompression is single-threaded
here whatever the archive was compressed with.

huf_decompress_amd64.S is an assembly fast path for x86-64, and is left
behind: it cannot be amalgamated into a C header, and the 32-bit Windows tier
this exists for could not use it anyway. ZSTD_DISABLE_ASM in PROLOGUE selects
the C fallback that upstream keeps beside it.

Keep local changes in this script rather than in zstd.h, so a version bump
stays a re-run instead of a merge.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import amalgamate_lib as A

PROLOGUE = r'''/*
 * zstd.h -- the zstd 1.5.7 decompressor, amalgamated into one single-header
 * library for os-rice.
 *
 * Upstream (https://github.com/facebook/zstd, Meta) is BSD-3 / GPL-2 dual
 * licensed; the BSD notice is reproduced further down, at the top of each of
 * upstream's own files. This carries the decode side only, which is what
 * lib/archive.c needs to open a .tar.zst -- Debian packages have shipped
 * zstd-compressed data members since bookworm, so the Yandex Browser .deb
 * lib/build.c fetches is one.
 *
 * Vendored the way thirdparty/yaml.h and thirdparty/bearssl.h are: one file
 * in the source tree, no submodule, no package to install, and the same
 * stb-style split -- including it plainly gets the declarations, and exactly
 * one translation unit defines the code:
 *
 *   #define ZSTD_IMPLEMENTATION
 *   #include "../thirdparty/zstd.h"
 *
 * That unit is lib/zstd.c. Everywhere else just includes it.
 *
 * All C89, like the rest of this tree. Upstream's only non-C90 spelling is
 * the `//` comment, and the script that generated this file rewrote every
 * one of them to a block comment.
 *
 * Decompressing a stream, in short (see lib/archive.c for the whole thing):
 *
 *   ZSTD_DCtx *d = ZSTD_createDCtx();
 *   ZSTD_inBuffer in = { buf, n, 0 };
 *   ZSTD_outBuffer out = { obuf, m, 0 };
 *   r = ZSTD_decompressStream(d, &out, &in);   // 0 when a frame ended
 *   ZSTD_freeDCtx(d);
 *
 * Regenerating:
 *
 *   curl -L -o zstd-1.5.7.tar.gz https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
 *   tar -xf zstd-1.5.7.tar.gz
 *   python3 amalgamate_zstd.py zstd-1.5.7 zstd.h
 *
 * What the script does to upstream, beyond concatenating: expands each local
 * #include once (so the file order is upstream's own, taken from its include
 * lines rather than hardcoded), rewrites `//` comments, renames the statics
 * two files both claim, and gives each source's file-scope macros an #undef
 * after its body so they do not leak into the next file. System #includes
 * are left where upstream put them.
 */

/* No assembly: huf_decompress_amd64.S is not carried (see the script's
 * docstring), and this selects the C huffman decoder beside it. */
#define ZSTD_DISABLE_ASM

/* The decompressor's own sources set this before including zstd.h, for the
 * types (ZSTD_customMem, ZSTD_FrameHeader, ZSTD_format_e ...) they are built
 * out of. Here zstd.h lands once, at the top, so it has to be set before
 * that -- otherwise the half of the API the internals need is inside a dead
 * #if by the time they arrive. It also puts the experimental API in the
 * declarations half, which is where ZSTD_getFrameHeader lives. */
#define ZSTD_STATIC_LINKING_ONLY
'''

SOURCES = [
    'common/debug.c', 'common/entropy_common.c', 'common/error_private.c',
    'common/fse_decompress.c', 'common/xxhash.c', 'common/zstd_common.c',
    'decompress/huf_decompress.c', 'decompress/zstd_ddict.c',
    'decompress/zstd_decompress.c', 'decompress/zstd_decompress_block.c',
]

HEADERS = ['zstd.h', 'zstd_errors.h']

# zstd_deps.h has no include guard on purpose: it is included over and over,
# and each includer sets ZSTD_DEPS_NEED_MALLOC / _ASSERT / _MATH64 / _STDINT
# first to say which piece of it it wants. Expanded once it would land
# whichever piece the first includer asked for -- and that first includer is
# debug.h, inside `#if (DEBUGLEVEL>=1)`, so the piece would land in a dead
# branch and ZSTD_memcpy would not exist at all.
REPEAT = ('zstd_deps.h',)


def main():
    src = os.path.join(sys.argv[1], 'lib')
    out = sys.argv[2]
    assert os.path.isdir(os.path.join(src, 'decompress')), \
        '%s: not an unpacked zstd release (no lib/decompress/)' % sys.argv[1]

    text, nh, ns, renames = A.build(
        prologue=PROLOGUE,
        macro='ZSTD',
        headers=[os.path.join(src, h) for h in HEADERS],
        sources=[os.path.join(src, s) for s in SOURCES],
        roots=[src, os.path.join(src, 'common'), os.path.join(src, 'decompress')],
        repeat=REPEAT,
        prefix='zstd_amalg',
    )
    open(out, 'w', encoding='utf-8').write(text)
    print('%s: %d headers, %d sources, %d renamed statics, %d lines'
          % (out, nh, ns, len(set(n for v in renames.values() for n in v)),
             text.count('\n')))


if __name__ == '__main__':
    main()
