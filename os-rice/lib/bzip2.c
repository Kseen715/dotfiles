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
#define BZIP2_IMPLEMENTATION
#include "../thirdparty/bzip2.h"
