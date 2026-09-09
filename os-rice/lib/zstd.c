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
#define ZSTD_IMPLEMENTATION
#include "../thirdparty/zstd.h"
