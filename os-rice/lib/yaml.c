/* lib/yaml.c -- the one translation unit that carries the vendored parser.
 *
 * thirdparty/yaml.h is a single-header library in nob.h's style: including
 * it plainly declares the API, and exactly one file in the program defines
 * the code. This is that file, so every other unit can say
 *
 *     #include "../thirdparty/yaml.h"
 *
 * and get the declarations only. It exists as its own .c rather than a
 * #define at the top of the first unit that happens to parse YAML because
 * that unit would then carry 13k lines of upstream code through every one
 * of its own rebuilds -- here the object is compiled once and cached like
 * any other, and the file that owns the parsing stays small.
 */
#define YAML_IMPLEMENTATION
#include "../thirdparty/yaml.h"
