/* lib/bearssl.c -- the one translation unit that carries the vendored TLS stack.
 *
 * thirdparty/bearssl.h is a single-header library in thirdparty/yaml.h's
 * style: including it plainly declares the API, and exactly one file in the
 * program defines the code. This is that file, so lib/tls.c can say
 *
 *     #include "../thirdparty/bearssl.h"
 *
 * and get the declarations only. Windows-only (see nob.c's win_srcs): on
 * POSIX the fetch path goes through curl or wget, which brought their own
 * TLS, and 63k lines of it would be compiled for nothing.
 *
 * Its own file rather than a #define at the top of lib/tls.c because that
 * unit would then carry the whole of BearSSL through every one of its own
 * rebuilds -- here the object is compiled once and cached like any other,
 * and the file that owns the HTTPS client stays small.
 */
#define BEARSSL_IMPLEMENTATION
#include "../thirdparty/bearssl.h"
