/* lib/bzip2.c -- the one translation unit that carries the vendored bzip2.
 *
 * thirdparty/bzip2.h is a single-header library in thirdparty/yaml.h's
 * style: including it plainly declares the API, and exactly one file in the
 * program defines the code. This is that file, so lib/archive.c can say
 *
 *     #include "../thirdparty/bzip2.h"
 *
 * and get the declarations only.
 */
/* BZ_STRICT_ANSI drops upstream's one call to fdopen(), which BZ2_bzdopen
 * needs and nothing here does: fdopen is POSIX, not C89, so under -std=c89
 * it is an undeclared function, and clang makes that an error rather than a
 * warning. Nothing in this tree opens a bz2 stream by descriptor. */
#define BZ_STRICT_ANSI
#define BZIP2_IMPLEMENTATION
#include "../thirdparty/bzip2.h"
