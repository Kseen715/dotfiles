# Vendored third-party sources

Everything in this directory is upstream code, checked in verbatim. Nothing
here is edited locally: refreshing a dependency means replacing the files and
updating the record below, so a diff against upstream stays meaningful.

## bearssl.h -- BearSSL 0.6, amalgamated

* Upstream: <https://bearssl.org/> (Thomas Pornin), MIT licence, reproduced in
  the header comment of `bearssl.h`.
* Tarball: <https://bearssl.org/bearssl-0.6.tar.gz>,
  sha256 `6705bba1714961b41a728dfc5debbe348d2966c117649392f8c8139efc83ff14`.
  Upstream publishes no checksum of its own; the hash above is of the tarball
  fetched on 2026-09-07 and is here so a later refresh is a deliberate change
  rather than a silent one.
* Taken: `inc/` and `src/`, run through `amalgamate_bearssl.py` into one
  single-header library the way `yaml.h` is (`#define BEARSSL_IMPLEMENTATION`
  in exactly one unit, `lib/bearssl.c`). The `tools/`, `test/`, `samples/`
  and build files are not used and were left behind.
* Why: Windows XP's schannel tops out at TLS 1.0, and TLS 1.1/1.2 were never
  backported to it, so WinINet cannot complete a handshake with any host worth
  fetching from today. BearSSL is the TLS 1.2 stack `lib/tls.c` speaks over a
  raw WinSock socket on that tier. Modern Windows keeps using WinINet.
* Refreshing: re-run the script (its docstring has the commands), never edit
  `bearssl.h` by hand -- every local change lives in `amalgamate_bearssl.py`,
  so a version bump stays a re-run rather than a merge. What the script does
  to upstream is listed in the comment it writes at the top of the header.
* Build note: all C89, like the rest of this tree. Upstream's only non-C90
  spelling is `inline`, and the script rewrites every `static inline` to
  `static`, so `lib/bearssl.c` and `lib/tls.c` are both built at `-std=c89`
  (see `nob.c`); the vendored unit adds `-w`, since upstream's warnings are
  upstream's to fix. The one extension left is `unsigned __int128`, which
  `src/inner.h` turns on for itself on 64-bit gcc/clang -- no more standard
  in C99 than in C89, and never reached on the 32-bit XP tier.

## bzip2.h -- bzip2 1.0.8, amalgamated

* Upstream: <https://sourceware.org/bzip2/> (Julian Seward), BSD-like licence,
  reproduced in the header comment of `bzip2.h`.
* Tarball: <https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz>,
  sha256 `ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269`,
  matching upstream's published hash.
* Taken: the seven library sources and `bzlib.h`, run through
  `amalgamate_bzip2.py` into one single-header library the way `bearssl.h` is
  (`#define BZIP2_IMPLEMENTATION` in exactly one unit, `lib/bzip2.c`). The
  `bzip2` and `bzip2recover` programs, the tests and the build files are not
  used and were left behind. The compressor is carried even though nothing
  here compresses: `bzlib.c` holds both halves of the public API, so removing
  one would mean forking upstream's file rather than configuring it -- see
  the script's docstring.
* Why: `lib/archive.c` opens `.tar.bz2` without a system `tar` or `bunzip2`,
  on the tiers that have neither.
* Refreshing: re-run the script (its docstring has the commands), never edit
  `bzip2.h` by hand -- every local change lives in `amalgamate_bzip2.py`.
* Build note: all C89, and unusually for the vendored decoders here upstream
  needed no dialect work at all -- no `//`, no `inline`. `lib/bzip2.c` builds
  at `-std=c89` like everything else (at `-w`). Verified against `gcc`,
  `i686-w64-mingw32-gcc` and `x86_64-w64-mingw32-gcc`, and end to end by
  round-tripping a 1 MB `.bz2` stream.

## cacert.pem -- Mozilla CA bundle

* Upstream: <https://curl.se/ca/cacert.pem> (curl's extract of Mozilla's
  root store), dated 2026-08-13, 121 roots.
* sha256 `f66dff1bdf8f96060b8177976f8b7d9254bc89bc4db933d769f7384d28480bc9`,
  matching <https://curl.se/ca/cacert.pem.sha256> as published.
* Why: BearSSL ships no trust store. XP's own root store is long expired, so
  the bundle travels with the binary: `nob` turns this file into a byte array
  (`build/cacert_pem.c`) and `lib/tls.c` decodes it into trust anchors at
  first use. Refreshing trust is therefore a file swap plus a rebuild.

## lzmasdk.h -- LZMA SDK 25.01 decoders, amalgamated

* Upstream: <https://www.7-zip.org/sdk.html> (Igor Pavlov), public domain,
  reproduced in the header comment of `lzmasdk.h`.
* Tarball: <https://www.7-zip.org/a/lzma2501.7z>,
  sha256 `cbc3babd589d971e45971d787ff100be8aaa5eab15b2694497ec3e447009e1f2`.
  Upstream publishes no checksum of its own; the hash above is of the archive
  fetched on 2026-09-09.
* Taken: the decode half of `C/` -- the object list of `C/Util/7z/makefile.gcc`
  (upstream's own minimal 7z extractor) plus the xz side (`Xz.c`, `XzDec.c`,
  `XzIn.c`, `XzCrc64.c`) and the two units they share -- run through
  `amalgamate_lzmasdk.py` into one single-header library the way `bearssl.h`
  is (`#define LZMASDK_IMPLEMENTATION` in exactly one unit, `lib/lzmasdk.c`).
  Nothing that compresses is carried: `LzmaEnc`, `LzFind`, `XzEnc`,
  `Lzma2Enc` and the multithreaded coders are left behind, and so is every
  `*Opt.c` (`7zCrcOpt`, `XzCrc64Opt`, `Sha256Opt`, `SwapBytes`) -- those reach
  for SSE/SHA-NI intrinsics that the 32-bit XP tier has no CPU for. The plain
  C paths they accelerate are selected by `Z7_CRC_NUM_TABLES` /
  `Z7_CRC64_NUM_TABLES`, set in the header's prologue.
* Why: XP has no `tar.exe` (Windows 10 1803 onwards) and no `7z`, and a
  minimal POSIX box need not have either. `lib/archive.c` opens 7z and xz --
  and through xz, most of the `.tar.xz` downloads `lib/build.c` fetches --
  without a tool the system was supposed to provide, the same way `lib/tls.c`
  carries BearSSL because schannel there is too old.
* Refreshing: re-run the script (its docstring has the commands), never edit
  `lzmasdk.h` by hand -- every local change lives in `amalgamate_lzmasdk.py`,
  so a version bump stays a re-run rather than a merge. What the script does
  to upstream is listed in the comment it writes at the top of the header.
* Build note: all C89. Upstream's non-C90 spellings are the `//` comment and
  one `inline` in `Z7_FORCE_INLINE`, both rewritten by the script, so
  `lib/lzmasdk.c` builds at `-std=c89` like everything else (at `-w` --
  upstream's warnings are upstream's). Verified against `gcc`,
  `i686-w64-mingw32-gcc` and `x86_64-w64-mingw32-gcc`, and end to end by
  building upstream's own `Util/7z/7zMain.c` against the amalgamation and
  round-tripping LZMA, LZMA2 and PPMd 7z archives plus crc64- and
  sha256-checked `.xz` streams. One upstream limit carried as-is: `7zDec.c`
  decodes a BCJ2 folder only in its four-coder form, so a two-coder BCJ2
  archive is refused with "decoder doesn't support this archive".

## miniz.h -- miniz 3.0.2, amalgamated

* Upstream: <https://github.com/richgel999/miniz> (Rich Geldreich et al), MIT
  licence, reproduced in `miniz.h` in upstream's own header comment.
* Tarball: <https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip>,
  sha256 `ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5`,
  fetched 2026-09-09. The release zip is upstream's own amalgamation of its
  `miniz*.c` sources, which is why `amalgamate_miniz.py` has so little to do.
* Taken: `miniz.c` and `miniz.h`, run through `amalgamate_miniz.py` into one
  single-header library the way `bearssl.h` is (`#define MINIZ_IMPLEMENTATION`
  in exactly one unit, `lib/miniz.c`). The examples and build files are not
  used and were left behind. `MINIZ_NO_DEFLATE_APIS` and
  `MINIZ_NO_ZLIB_APIS` are set in the header's prologue: the compressor and
  the zip writer go, and so does the zlib compatibility layer, which is what
  would otherwise `#define crc32` over anything else in the program.
* Why: `lib/archive.c` opens `.zip` -- Nerd Fonts, and the Windows downloads
  `lib/build.c` fetches -- where XP has no `unzip` and no PowerShell new
  enough for `Expand-Archive`.
* Refreshing: re-run the script (its docstring has the commands), never edit
  `miniz.h` by hand -- every local change lives in `amalgamate_miniz.py`.
* Build note: all C89. Upstream's non-C90 spellings are the `//` comment and
  the plain `inline` in its unknown-compiler definition of `MZ_FORCEINLINE`,
  both rewritten by the script, so `lib/miniz.c` builds at `-std=c89` like
  everything else (at `-w`). Verified against `gcc`, `i686-w64-mingw32-gcc`
  and `x86_64-w64-mingw32-gcc`. One consequence of `-std=c89`: upstream's
  large-file I/O path needs `_LARGEFILE64_SOURCE`, which strict C89 does not
  define, so the reader uses plain `fopen`/`fseeko` and says so with a
  `#pragma message` at build time. Nothing this tree downloads is a 2 GB zip.

## rar.h -- libarchive 3.8.1 RAR3 + RAR5 readers, amalgamated

* Upstream: <https://github.com/libarchive/libarchive> (Tim Kientzle et al),
  BSD-2-Clause, reproduced at the top of `rar.h`.
* Tarball: <https://github.com/libarchive/libarchive/releases/download/v3.8.1/libarchive-3.8.1.tar.gz>,
  sha256 `bde832a5e3344dc723cfe9cc37f8e54bde04565bfe6f136bc1bd31ab352e9fab`,
  fetched 2026-09-09.
* Taken: `archive_read_support_format_rar.c` and `..._rar5.c` -- the two
  readers -- plus what they call: `archive_ppmd7.c`, `archive_blake2s_ref.c`,
  `archive_blake2sp_ref.c` and `archive_time.c`, and libarchive's own internal
  headers (`archive_private.h`, `archive_read_private.h`, `archive_string.h`,
  `archive_entry_locale.h`, and the header-only `archive_endian.h` /
  `archive_crc32.h` / `archive_blake2*.h`) that those sources include. The
  internal headers come along verbatim rather than retyped, so
  `struct archive_read` and the format descriptor the readers fill in are
  upstream's own definitions and a version bump cannot drift from them.
  Nothing else of libarchive is here: no writers, no filters, no other
  formats, no `archive_entry` implementation, no locale layer.
* Why: RAR is the one format in `lib/archive.c`'s list with no public-domain
  or MIT decoder to vendor -- the reference unrar source is C++ and its
  licence forbids reimplementing the compression, which is what makes
  libarchive's clean-room readers the only option that is both C and
  redistributable. Windows XP has no extractor at all, and `unrar` is not
  installed by default on any tier this tree targets.
* The shim: the two readers expect libarchive's read stack under them, and
  `lib/rar_shim.c` is that stack -- a buffered reader over a `FILE *` behind
  `__archive_read_ahead` / `_consume` / `_seek`, a small `struct archive_entry`
  of our own, and the string-conversion object RAR3 needs to say which
  encoding a member name arrived in. It also defines the osr-facing API
  (`lib/rar_shim.h`), so no part of libarchive's own API leaks past it.
  One contract worth naming, because getting it wrong corrupts data silently:
  a pointer handed out by `__archive_read_ahead` stays valid across
  `__archive_read_consume`. The RAR3 bit reader keeps `br->next_in` pointing
  into that buffer while it consumes what it has already read, so the shim
  advances a cursor on consume and compacts only inside a read-ahead.
  Deliberately unsupported: multi-volume sets and encrypted archives.
* `archive_platform.h` is not carried -- it `#error`s without a generated
  `config.h` -- so `amalgamate_rar.py`'s prologue supplies the handful of
  things the sources take from it (the `ARCHIVE_ERRNO_*` codes, the `__LA_*`
  attribute spellings). `HAVE_ZLIB_H` stays off, which makes
  `archive_crc32.h` supply its own `crc32()`; the readers need no compression
  library at all.
* Refreshing: re-run the script (its docstring has the commands), never edit
  `rar.h` by hand -- every local change lives in `amalgamate_rar.py`, including
  the one C99 spelling upstream uses (`lldiv` in `archive_time.c`, rewritten
  to plain `int64_t` division, which truncates identically).
* Build note: all C89. `lib/rar.c` builds at `-std=c89 -pedantic` (at `-w`) and
  `lib/rar_shim.c` at `-std=c89 -pedantic -Wall` with no warnings, against
  `gcc`, `i686-w64-mingw32-gcc` and `x86_64-w64-mingw32-gcc`. Verified against
  libarchive's own 67 non-encrypted RAR fixtures: 35 read to completion, and
  every one of the other 32 is a fixture upstream's own tests document as
  invalid (fuzzer-derived), a multi-volume continuation part, or encrypted.
  Extracted bytes are identical to `7z`'s for the LZSS, PPMd, RAR3-VM-filter
  and RAR5 solid paths. The whole corpus, encrypted fixtures included, runs
  clean under `-fsanitize=address,undefined`.

## yaml.h -- see amalgamate_yaml.py

## zstd.h -- Zstandard 1.5.7 decompressor, amalgamated

* Upstream: <https://github.com/facebook/zstd> (Yann Collet, Meta), BSD-2 /
  GPL-2 dual licence, reproduced in the header comment of `zstd.h`.
* Tarball: <https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz>,
  sha256 `eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3`,
  matching upstream's published hash.
* Taken: `lib/common/` and `lib/decompress/` (ten sources) plus `zstd.h` and
  `zstd_errors.h`, run through `amalgamate_zstd.py` into one single-header
  library the way `bearssl.h` is (`#define ZSTD_IMPLEMENTATION` in exactly one
  unit, `lib/zstd.c`). `lib/compress/`, `lib/dictBuilder/`, `lib/legacy/`, the
  `zstd` and `zstdmt` programs and the build files are not used and were left
  behind. `ZSTD_DISABLE_ASM` is set in the header's prologue -- upstream's
  Huffman decoder ships a hand-written x86-64 assembly path, and one
  amalgamated C header cannot carry a `.S` file. `ZSTD_STATIC_LINKING_ONLY` is
  set there too: the decompressor's own sources are built out of types
  (`ZSTD_customMem`, `ZSTD_FrameHeader`, `ZSTD_format_e`) that `zstd.h` only
  declares under that macro, and here `zstd.h` lands once, at the top.
* Why: `.tar.zst` is what an increasing number of the releases `lib/build.c`
  fetches ship as, and no tier this targets has a `zstd` binary -- Windows
  `tar.exe` gained zstd only recently, and XP has neither.
* Refreshing: re-run the script (its docstring has the commands), never edit
  `zstd.h` by hand -- every local change lives in `amalgamate_zstd.py`.
* Build note: all C89. Upstream's non-C90 spellings are the `//` comment and
  `static inline`, both rewritten by the script, so `lib/zstd.c` builds at
  `-std=c89` like everything else (at `-w`). One structural quirk the script
  handles specially: `lib/common/zstd_deps.h` deliberately carries no include
  guard and hands out a different piece of itself at each `#include`, chosen
  by `ZSTD_DEPS_NEED_*` macros the includer sets first, so it is named in the
  script's `repeat` set and re-expanded at every include site the way the
  preprocessor would. Verified against `gcc`, `i686-w64-mingw32-gcc` and
  `x86_64-w64-mingw32-gcc`, and end to end by streaming a 1 MB `.zst` and a
  `-19 --long=27` frame through `ZSTD_decompressStream`.
