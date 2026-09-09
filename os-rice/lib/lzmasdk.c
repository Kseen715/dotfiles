/* lib/lzmasdk.c -- the one translation unit that carries the vendored 7z/xz
 * decoders.
 *
 * thirdparty/lzmasdk.h is a single-header library in thirdparty/yaml.h's
 * style: including it plainly declares the API, and exactly one file in the
 * program defines the code. This is that file, so lib/archive.c can say
 *
 *     #include "../thirdparty/lzmasdk.h"
 *
 * and get the declarations only -- rather than carrying 19k lines of upstream
 * through every one of its own rebuilds. Here the object is compiled once and
 * cached like any other.
 */
#define LZMASDK_IMPLEMENTATION
#include "../thirdparty/lzmasdk.h"
