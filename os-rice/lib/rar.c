/* lib/rar.c -- the one translation unit that carries the vendored RAR
 * readers.
 *
 * thirdparty/rar.h is a single-header library in thirdparty/yaml.h's style:
 * including it plainly declares the API, and exactly one file in the program
 * defines the code. This is that file. lib/rar_shim.c includes the header the
 * same plain way, for the struct definitions the read stack it supplies is
 * written against.
 */
#define RAR_IMPLEMENTATION
#include "../thirdparty/rar.h"
