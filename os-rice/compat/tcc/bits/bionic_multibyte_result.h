/* compat/tcc/bits/bionic_multibyte_result.h -- a C99-parseable stand-in for
 * Termux/bionic's header of the same name.
 *
 * bionic spells those constants as a C23 enum with a fixed underlying type:
 *
 *     enum : size_t { BIONIC_MULTIBYTE_RESULT_ILLEGAL_SEQUENCE = -1UL, ... };
 *
 * unconditionally -- no __STDC_VERSION__ guard -- and <wchar.h> includes it,
 * so under tcc (which reports C99 and parses C99) every TU that reaches
 * <wchar.h> dies with "struct/union/enum name expected". thirdparty/rar.h
 * includes <wchar.h>, which is how lib/rar.c and lib/rar_shim.c hit it.
 *
 * bionic #defines each enumerator to itself right after declaring it, so
 * every user of these names already goes through a macro: defining the macros
 * and skipping the enum keeps the interface and drops the syntax tcc cannot
 * read. Nothing in this tree names them at all -- they are here so that a
 * libc header that does still compiles. Reached only for tcc, via the
 * -Icompat/tcc in nob.c; gcc and clang read bionic's own file as usual.
 */
#ifndef OSR_TCC_BIONIC_MULTIBYTE_RESULT_H
#define OSR_TCC_BIONIC_MULTIBYTE_RESULT_H

#include <stddef.h>

/* An encoding error: the bytes read are not a valid character, nor a
 * partially valid one. */
#define BIONIC_MULTIBYTE_RESULT_ILLEGAL_SEQUENCE ((size_t)-1)
/* The bytes read may yet produce a valid character; the sequence is
 * incomplete and a future call may complete it. */
#define BIONIC_MULTIBYTE_RESULT_INCOMPLETE_SEQUENCE ((size_t)-2)
/* The output was the result of a previous successful decoding -- no new
 * bytes were consumed. mbrtoc16 returning the low surrogate of a pair is the
 * common case. */
#define BIONIC_MULTIBYTE_RESULT_NO_BYTES_CONSUMED ((size_t)-3)

#endif /* OSR_TCC_BIONIC_MULTIBYTE_RESULT_H */
