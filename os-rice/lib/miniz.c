/* lib/miniz.c -- the one translation unit that carries the vendored zip
 * reader.
 *
 * thirdparty/miniz.h is a single-header library in thirdparty/yaml.h's
 * style: including it plainly declares the API, and exactly one file in the
 * program defines the code. This is that file, so lib/archive.c and
 * lib/rar_shim.c can say
 *
 *     #include "../thirdparty/miniz.h"
 *
 * and get the declarations only -- rather than carrying 9k lines of upstream
 * through every one of their own rebuilds.
 */
#define MINIZ_IMPLEMENTATION
#include "../thirdparty/miniz.h"
