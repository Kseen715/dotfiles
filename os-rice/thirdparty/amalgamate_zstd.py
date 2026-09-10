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
import re

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


# cpu.h's cpuid: the MSVC and i386-PIC branches above it are guarded by the
# compiler that supports them, but the general x86 one is selected on
# architecture alone and then uses GNU inline asm. cproc rejects it ("inline
# assembly is not yet supported") and lacc's x86_64 backend asserts on it, so
# it is gated on __GNUC__ like its neighbours. Non-GNU compilers fall out of
# the chain with the feature words left at zero, which is what ZSTD_cpuid
# already returns everywhere that is not x86.
# zstd_deps.h routes ZSTD_memcpy/memmove/memset through the __builtin_
# spellings on any __GNUC__ >= 4. pcc -- another front end nob.c drives --
# claims __GNUC__ 4 so glibc's headers work, and implements the memcpy and
# memset builtins but not memmove, which then goes undefined at link time.
# The #else branch upstream already wrote calls libc directly.
MEM_BUILTINS = re.compile(
    r'^#if defined\(__GNUC__\) && __GNUC__ >= 4$\n'
    r'(?=# define ZSTD_memcpy)', re.M)


# zstd_trace.h turns on its weak-symbol hooks for any __GNUC__ on ELF. pcc
# claims both and then ignores __attribute__((__weak__)), leaving the four
# ZSTD_trace_* calls undefined at link time; with the guard off, ZSTD_TRACE
# is 0 and they are never declared.
WEAK_SYMBOLS = re.compile(
    r'^#if !defined\(ZSTD_HAVE_WEAK_SYMBOLS\) && \\$', re.M)


WEAK_SYMBOLS_REPL = (
    "/* amalgamated: !__PCC__ added to upstream's guard. pcc defines __GNUC__ 4\n"
    ' * and __ELF__ but drops __attribute__((__weak__)) on the floor, so the four\n'
    ' * ZSTD_trace_* hooks below become undefined references at link time instead\n'
    ' * of weak no-ops. With this 0, ZSTD_TRACE is 0 and the hooks are never\n'
    ' * declared or called at all -- which is what every non-ELF target does. */\n'
    '#if !defined(ZSTD_HAVE_WEAK_SYMBOLS) && !defined(__PCC__) && \\')


CPUID_ASM = re.compile(
    r'^#elif defined\(__x86_64__\) \|\| defined\(_M_X64\) \|\| defined\(__i386__\)$',
    re.M)


def clean(text, path):
    if os.path.basename(path) == 'cpu.h':
        text, n = CPUID_ASM.subn(
            '#elif (defined(__x86_64__) || defined(_M_X64) '
            '|| defined(__i386__)) && defined(__GNUC__)', text)
        assert n == 1, 'cpu.h no longer selects its asm cpuid on arch alone'
    if os.path.basename(path) == 'zstd_trace.h':
        # lambda, not a template: the replacement ends in the guard's line
        # continuation and re would read that backslash as an escape.
        text, n = WEAK_SYMBOLS.subn(lambda m: WEAK_SYMBOLS_REPL, text)
        assert n == 1, 'zstd_trace.h no longer opens its guard on ZSTD_HAVE_WEAK_SYMBOLS'
    if os.path.basename(path) == 'zstd_deps.h':
        text, n = MEM_BUILTINS.subn(
            "/* amalgamated: !__PCC__ -- pcc defines __GNUC__ 4 for glibc's headers\n"
            ' * but has no __builtin_memmove, so every use below is an undefined\n'
            ' * reference at link time. The #else spells the same three calls. */\n'
            '#if defined(__GNUC__) && __GNUC__ >= 4 && !defined(__PCC__)\n', text)
        assert n == 1, 'zstd_deps.h no longer picks the mem builtins on __GNUC__ >= 4'
    return text


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
        clean=clean,
        prefix='zstd_amalg',
    )
    open(out, 'w', encoding='utf-8').write(text)
    print('%s: %d headers, %d sources, %d renamed statics, %d lines'
          % (out, nh, ns, len(set(n for v in renames.values() for n in v)),
             text.count('\n')))


if __name__ == '__main__':
    main()
