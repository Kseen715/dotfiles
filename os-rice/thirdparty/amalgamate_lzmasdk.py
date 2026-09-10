"""amalgamate_lzmasdk.py -- rebuild lzmasdk.h from an unpacked LZMA SDK.

    curl -L -o lzma2501.7z https://www.7-zip.org/a/lzma2501.7z
    7z x -olzma2501 lzma2501.7z
    python3 amalgamate_lzmasdk.py lzma2501 lzmasdk.h

Takes the decoder half of the SDK's C/ directory -- the object list in
C/Util/7z/makefile.gcc, which is upstream's own minimal 7z extractor, plus the
xz decoder (Xz.c, XzDec.c, XzIn.c, XzCrc64.c) and the two allocator/sort units
they share -- and folds it into one single-header library in the style of
thirdparty/yaml.h and thirdparty/bearssl.h.

Nothing here compresses: LzmaEnc, LzFind, XzEnc, Lzma2Enc and the multithreaded
coders are left behind, and so is every *Opt.c (7zCrcOpt, XzCrc64Opt,
Sha256Opt, SwapBytes) -- those reach for SSE/SHA-NI intrinsics that the 32-bit
Windows XP tier this exists for has no CPU for and that mingw's older headers
do not always carry. The plain C paths they accelerate are selected by the two
NUM_TABLES macros in PROLOGUE below.

Keep local changes in this script rather than in lzmasdk.h, so a version bump
stays a re-run instead of a merge.
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import amalgamate_lib as A

PROLOGUE = r'''/*
 * lzmasdk.h -- the LZMA SDK 25.01 decoders, amalgamated into one
 * single-header library for os-rice.
 *
 * Upstream (https://www.7-zip.org/sdk.html, Igor Pavlov) is in the public
 * domain. This carries the decode side only: the 7z container reader, and
 * the LZMA, LZMA2, PPMd7, BCJ/BCJ2, delta and xz decoders under it. It is
 * two of the formats lib/archive.c has to open without help from the system
 * -- 7z and xz -- and, through xz, the compression layer under most of the
 * .tar.xz downloads lib/build.c fetches.
 *
 * Vendored the way thirdparty/yaml.h and thirdparty/bearssl.h are: one file
 * in the source tree, no submodule, no package to install, and the same
 * stb-style split -- including it plainly gets the declarations, and exactly
 * one translation unit defines the code:
 *
 *   #define LZMASDK_IMPLEMENTATION
 *   #include "../thirdparty/lzmasdk.h"
 *
 * That unit is lib/lzmasdk.c. Everywhere else just includes it.
 *
 * All C89, like the rest of this tree. Upstream's only non-C90 spelling is
 * the `//` comment, and the script that generated this file rewrote every
 * one of them to a block comment, so nob.c builds lib/lzmasdk.c at -std=c89
 * alongside everything else (at -w -- upstream's warnings are upstream's).
 *
 * Reading an archive, in short (see lib/archive.c for the whole thing):
 *
 *   CSzArEx db;    CFileInStream fs;   CLookToRead2 look;
 *   InFile_Open(&fs.file, path);
 *   FileInStream_CreateVTable(&fs);
 *   LookToRead2_CreateVTable(&look, False);
 *   SzArEx_Init(&db);
 *   SzArEx_Open(&db, &look.vt, &g_Alloc, &g_Alloc);
 *   SzArEx_Extract(&db, &look.vt, i, &blockIndex, &buf, &bufSize, ...);
 *
 * Regenerating:
 *
 *   curl -L -o lzma2501.7z https://www.7-zip.org/a/lzma2501.7z
 *   7z x -olzma2501 lzma2501.7z
 *   python3 amalgamate_lzmasdk.py lzma2501 lzmasdk.h
 *
 * What the script does to upstream, beyond concatenating: expands each local
 * #include once (so the file order below is upstream's own, taken from its
 * include lines rather than hardcoded here), rewrites `//` comments, drops
 * the `inline` keyword from Z7_FORCE_INLINE (C90 has no such word; the
 * always_inline attribute beside it stays), renames the statics two files
 * both claim, gives each source's file-scope macros an #undef after its body
 * so they do not leak into the next file, and turns off the one line in
 * Sha256.c that enables the SHA-NI code path, because the hand-written
 * assembly implementing it (Sha256Opt.c) is not carried -- see the module
 * docstring for why. System #includes are left where upstream put them:
 * hoisting <windows.h> out of its #ifdef _WIN32 is how that goes wrong.
 *
 * ---- upstream licence ----
 *
 * LZMA SDK is written and placed in the public domain by Igor Pavlov.
 *
 * Some code in LZMA SDK is based on public domain code from another
 * developers:
 *   1) PPMd var.H (2001): Dmitry Shkarin
 *   2) SHA-256: Wei Dai (Crypto++)
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * the original LZMA SDK code, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any means.
 */

/* Single-threaded, and the plain byte-at-a-time CRC tables. The wide-table
 * CRC paths live in 7zCrcOpt.c / XzCrc64Opt.c, which this amalgamation does
 * not carry; a checksum is never what makes an install slow. */
#define Z7_ST
#define Z7_CRC_NUM_TABLES 1
#define Z7_CRC64_NUM_TABLES 1

/* 7zDec.c ships the PPMd branch of the 7z decoder switched off, and this
 * turns it on. Not optional here: Ppmd7.c and Ppmd7Dec.c are carried, and
 * the only #include of Ppmd7.h in the whole set is the one inside that
 * branch -- left off, the declarations the two sources need are inside a
 * dead #ifdef and the amalgamation does not compile. 7-Zip writes PPMd
 * blocks for text often enough that a decoder refusing them is a bug. */
#define Z7_PPMD_SUPPORT
'''

# Upstream's own minimal extractor (C/Util/7z/makefile.gcc) plus the xz side.
# 7zMain.c, the command-line front end around them, is not taken:
# lib/archive.c is this tree's front end.
SOURCES = [
    '7zAlloc.c', '7zArcIn.c', '7zBuf.c', '7zBuf2.c', '7zCrc.c', '7zDec.c',
    '7zFile.c', '7zStream.c', 'Bcj2.c', 'Bra.c', 'Bra86.c', 'BraIA64.c',
    'CpuArch.c', 'Delta.c', 'Lzma2Dec.c', 'LzmaDec.c', 'Ppmd7.c', 'Ppmd7Dec.c',
    'Alloc.c', 'Sort.c', 'Sha256.c',
    'Xz.c', 'XzDec.c', 'XzIn.c', 'XzCrc64.c',
]

# What lib/archive.c calls. Expanded first so the declarations half is
# complete on its own; every other header arrives through these.
HEADERS = [
    '7zTypes.h', 'CpuArch.h', 'Alloc.h', '7zCrc.h', '7zBuf.h', '7zFile.h',
    'LzmaDec.h', '7z.h', 'Xz.h', 'XzCrc64.h',
]

# Not in the release at all -- these are includes inside
# branches upstream ships turned off (`#ifdef Z7_CRC_HW_USE` in 7zCrc.c,
# `#ifdef USE_SUBBLOCK` in XzDec.c, both debug-only). The expander is textual,
# not a preprocessor, so it cannot see that the branch is dead; naming them
# here is how it is told. If a release ever turns one of those on, the file
# will be missing and the build will say so.
SKIP = ('7zCrcEmu.h', 'Bcj3Dec.c', 'SbDec.h')

SHA_HW = re.compile(r'^(\s*)#(\s*)define(\s+)Z7_COMPILER_SHA256_SUPPORTED\s*$', re.M)

FORCE_INLINE = re.compile(
    r'^(#\s*define\s+Z7_FORCE_INLINE\s+__attribute__\(\(always_inline\)\))\s+inline\s*$',
    re.M)


NO_CPUID = re.compile(
    r'^/\* for unsupported cpuid: \*/\n'
    r'void Z7_FASTCALL z7_x86_cpuid\(UInt32 p\[4\], UInt32 func\)\n'
    r'\{\n[^}]*\}\n'
    r'UInt32 Z7_FASTCALL z7_x86_cpuid_GetMaxFunc\(void\)\n'
    r'\{\n  return 0;\n\}\n', re.M)


# Alloc.c's MY_uintptr_t: upstream picks uintptr_t through a hardcoded
# `#elif 1` and keeps its own C89 fallback (ptrdiff_t) in the dead #else
# below it. uintptr_t is C99 and optional at that, so a C89 front end with
# honest headers (lacc, cproc -- both driven by nob.c) does not declare it
# and the typedef loses its type specifier. Flipping the branch selects the
# fallback upstream already wrote.
UINTPTR = re.compile(r'^  #elif 1\n    uintptr_t\n', re.M)

PACK_PUSH = re.compile(r'^MY_CPU_pragma_pack_push_1$', re.M)
PACK_POP = re.compile(r'^MY_CPU_pragma_pop$', re.M)


def clean(text, path):
    """Four upstream edits: `inline`, SHA-NI without Sha256Opt.c, _Pragma,
    and the cpuid stub CpuArch.c's no-cpuid branch is missing.

    7zTypes.h spells the gcc/clang half of Z7_FORCE_INLINE as
    `__attribute__((always_inline)) inline`, and C90 has no `inline`. Every
    use of the macro is already `static Z7_FORCE_INLINE`, so dropping the
    keyword is the same rewrite amalgamate_bearssl.py makes (`static inline`
    -> `static`) and leaves the attribute, which is the half that matters.
    """
    if os.path.basename(path) == 'Ppmd.h':
        text, n = PACK_PUSH.subn(
            '/* amalgamated: MY_CPU_pragma_pack_push_1 spelled out. It expands\n'
            ' * to _Pragma("pack(push, 1)"), which tcc 0.9.27 -- one of the\n'
            ' * compilers nob.c drives -- does not parse at all, while the\n'
            ' * #pragma spelling below it accepts. Nothing is lost either way:\n'
            ' * every struct between here and the pop is already naturally\n'
            ' * packed on the ABIs this tree builds for, which is what\n'
            " * upstream's own comment below says. */\n"
            '#pragma pack(push, 1)', text)
        assert n == 1, 'Ppmd.h no longer opens with MY_CPU_pragma_pack_push_1'
        text, n = PACK_POP.subn('#pragma pack(pop)', text)
        assert n == 1, 'Ppmd.h no longer closes with MY_CPU_pragma_pop'
        return text
    if os.path.basename(path) == 'Alloc.c':
        text, n = UINTPTR.subn(
            '  /* amalgamated: uintptr_t is C99 and optional; the ptrdiff_t\n'
            '   * branch below is upstream\'s own C89 fallback. */\n'
            '  #elif 0\n    uintptr_t\n', text)
        assert n == 1, 'Alloc.c no longer picks uintptr_t with `#elif 1`'
        return _clean_sha(text, path)
    if os.path.basename(path) == 'CpuArch.c':
        text, n = NO_CPUID.subn(
            lambda m: m.group(0) +
            '/* amalgamated: upstream\'s no-cpuid branch forgets\n'
            ' * z7_x86_cpuid_subFunc, which CPU_IsSupported_SHA512 below calls\n'
            ' * unconditionally -- so on a compiler that is neither GNU nor\n'
            ' * MSVC (tcc 0.9.27, which nob.c drives) every other unit links\n'
            ' * and this one symbol does not. Zeros match the branch above it,\n'
            ' * and GetMaxFunc returning 0 means SHA512 answers False before\n'
            ' * this is ever reached; it exists to be defined, not called. */\n'
            'static\n'
            'void Z7_FASTCALL z7_x86_cpuid_subFunc(UInt32 p[4], UInt32 func, UInt32 subFunc)\n'
            '{\n'
            '  UNUSED_VAR(func)\n'
            '  UNUSED_VAR(subFunc)\n'
            '  p[0] = p[1] = p[2] = p[3] = 0;\n'
            '}\n', text)
        assert n == 1, "CpuArch.c no longer has the no-cpuid branch"
        return _clean_sha(text, path)
    if os.path.basename(path) == '7zTypes.h':
        text, n = FORCE_INLINE.subn(r'\1', text)
        assert n == 1, '7zTypes.h no longer spells Z7_FORCE_INLINE with `inline`'
        return text
    return _clean_sha(text, path)


def _clean_sha(text, path):
    """No SHA-NI dispatch without Sha256Opt.c.

    Sha256.c picks between a software block function and Sha256_UpdateBlocks_HW
    at run time, and declares the latter whenever the compiler *could* emit
    SHA-NI. The definition lives in Sha256Opt.c, which this amalgamation does
    not carry (intrinsics, and no CPU on the XP tier can run them), so the
    reference would not link. Turning the macro off collapses the dispatch to
    the software path, which is the only path the tier ever took.

    xz reaches SHA-256 only for the rare SHA-256 integrity check; CRC32 and
    CRC64 are what real archives use.
    """
    if os.path.basename(path) != 'Sha256.c':
        return text
    text, n = SHA_HW.subn(
        r'\1/* amalgamated: SHA-NI path dropped with Sha256Opt.c */'
        r'\n\1#define Z7_COMPILER_SHA256_SUPPORTED_UNUSED', text)
    assert n >= 1, 'Sha256.c no longer defines Z7_COMPILER_SHA256_SUPPORTED'
    return text


def main():
    src = os.path.join(sys.argv[1], 'C')
    out = sys.argv[2]
    assert os.path.isdir(src), '%s: not an unpacked LZMA SDK (no C/)' % sys.argv[1]

    roots = [src]
    text, nh, ns, renames = A.build(
        prologue=PROLOGUE,
        macro='LZMASDK',
        headers=[os.path.join(src, h) for h in HEADERS],
        sources=[os.path.join(src, s) for s in SOURCES],
        roots=roots,
        skip=SKIP,
        clean=clean,
        prefix='z7_amalg',
    )
    open(out, 'w', encoding='utf-8').write(text)
    print('%s: %d headers, %d sources, %d renamed statics, %d lines'
          % (out, nh, ns, len(set(n for v in renames.values() for n in v)),
             text.count('\n')))


if __name__ == '__main__':
    main()
