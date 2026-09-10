/* lib/zstd.c -- the one translation unit that carries the vendored zstd
 * decoder.
 *
 * thirdparty/zstd.h is a single-header library in thirdparty/yaml.h's style:
 * including it plainly declares the API, and exactly one file in the program
 * defines the code. This is that file, so lib/archive.c can say
 *
 *     #include "../thirdparty/zstd.h"
 *
 * and get the declarations only -- rather than carrying 25k lines of upstream
 * through every one of its own rebuilds. Here the object is compiled once and
 * cached like any other.
 */
/* musl's <limits.h> keeps PATH_MAX behind the POSIX feature test, and the
 * tree builds -std=c89; Alpine's fortify-headers <stdlib.h> -- which upstream
 * includes from its implementation half -- refuses to fortify realpath()
 * without it. Same opt-in lib/archive.c makes, for the same reason. */
#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

/* tcc defines neither __GNUC__ nor an ARM inline assembler, so upstream's
 * prefetch falls through to the aarch64 `prfm` asm it cannot compile. The
 * hint is an optimisation; dropping it only costs speed. */
#if defined(__TINYC__)
#define NO_PREFETCH 1
#endif

#include <limits.h>   /* PATH_MAX, before upstream reaches <stdlib.h> */

#define ZSTD_IMPLEMENTATION
#include "../thirdparty/zstd.h"
