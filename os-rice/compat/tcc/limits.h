/* compat/tcc/limits.h -- the freestanding <limits.h> tcc does not ship.
 *
 * A hosted libc is allowed to assume the compiler brings the width macros
 * itself: Termux/bionic's $PREFIX/include/limits.h defines PATH_MAX and the
 * POSIX limits, then `#include_next <limits.h>` for CHAR_BIT, INT_MAX and the
 * rest. gcc and clang ship that header; tcc does not, so bionic's is found
 * first, its include_next finds nothing, and every TU that wants INT_MAX --
 * thirdparty/yaml.h among them -- fails to compile.
 *
 * So this dir goes on tcc's include path ahead of the system one (see
 * is_tcc in nob.c) and this file is the missing half: delegate to the libc's
 * header first, then define whatever it left to the compiler. Every define is
 * guarded, so on a libc that does spell them out (glibc) this file adds
 * nothing at all and the two never disagree.
 */
#ifndef OSR_TCC_LIMITS_H
#define OSR_TCC_LIMITS_H

#include_next <limits.h>

#ifndef CHAR_BIT
#define CHAR_BIT 8
#endif
#ifndef MB_LEN_MAX
#define MB_LEN_MAX 4
#endif

#ifndef SCHAR_MIN
#define SCHAR_MIN (-128)
#endif
#ifndef SCHAR_MAX
#define SCHAR_MAX 127
#endif
#ifndef UCHAR_MAX
#define UCHAR_MAX 255
#endif

/* Whether a bare `char` is the signed or the unsigned one is the target's
 * choice -- ARM and PowerPC say unsigned where x86 says signed -- and tcc
 * predefines __CHAR_UNSIGNED__ on the targets where it is. */
#ifndef CHAR_MIN
#ifdef __CHAR_UNSIGNED__
#define CHAR_MIN 0
#else
#define CHAR_MIN SCHAR_MIN
#endif
#endif
#ifndef CHAR_MAX
#ifdef __CHAR_UNSIGNED__
#define CHAR_MAX UCHAR_MAX
#else
#define CHAR_MAX SCHAR_MAX
#endif
#endif

#ifndef SHRT_MIN
#define SHRT_MIN (-32768)
#endif
#ifndef SHRT_MAX
#define SHRT_MAX 32767
#endif
#ifndef USHRT_MAX
#define USHRT_MAX 65535
#endif

#ifndef INT_MAX
#define INT_MAX 2147483647
#endif
#ifndef INT_MIN
#define INT_MIN (-INT_MAX - 1)
#endif
#ifndef UINT_MAX
#define UINT_MAX 4294967295U
#endif

/* long is the one width that is not the same everywhere tcc runs: 64-bit on
 * every LP64 Unix, 32-bit on i386 and on Windows. */
#ifndef LONG_MAX
#ifdef __LP64__
#define LONG_MAX 9223372036854775807L
#else
#define LONG_MAX 2147483647L
#endif
#endif
#ifndef LONG_MIN
#define LONG_MIN (-LONG_MAX - 1L)
#endif
#ifndef ULONG_MAX
#ifdef __LP64__
#define ULONG_MAX 18446744073709551615UL
#else
#define ULONG_MAX 4294967295UL
#endif
#endif

#ifndef LLONG_MAX
#define LLONG_MAX 9223372036854775807LL
#endif
#ifndef LLONG_MIN
#define LLONG_MIN (-LLONG_MAX - 1LL)
#endif
#ifndef ULLONG_MAX
#define ULLONG_MAX 18446744073709551615ULL
#endif

#endif /* OSR_TCC_LIMITS_H */
