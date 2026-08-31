/* modules/src/lcc-byteswap.h -- <bits/byteswap.h> overlay for lcc's rcc on a
 * modern glibc host. Installed as <lccdir>/include-patch/bits/byteswap.h
 * (include-patch sits on the driver's include path ahead of /usr/include, so
 * this shadows glibc's).
 *
 * WHY: glibc's __bswap_64 takes the #else branch here (the driver undefines
 * __GNUC__, so __GNUC_PREREQ is false) and expands __bswap_constant_64, whose
 * `0x...ull` constants rcc's C89 lexer rejects ("preprocessing number but an
 * invalid integer constant"). The 16/32-bit parts are C89-fine (plain `u`
 * suffix); only the 64-bit one needs rewriting. __uint64_t is `unsigned long
 * long` and rcc accepts that type and shifts on it -- the `ull` SUFFIX is the
 * only thing it cannot lex -- so splitting into two __bswap_32 halves avoids
 * the constants entirely and is behavior-identical. C89.
 */
#if !defined _BYTESWAP_H && !defined _NETINET_IN_H && !defined _ENDIAN_H
# error "Never use <bits/byteswap.h> directly; include <byteswap.h> instead."
#endif

#ifndef _BITS_BYTESWAP_H
#define _BITS_BYTESWAP_H 1

#include <features.h>
#include <bits/types.h>

/* Swap bytes in 16-bit value.  */
#define __bswap_constant_16(x)                                  \
  ((__uint16_t) ((((x) >> 8) & 0xff) | (((x) & 0xff) << 8)))

static __inline __uint16_t
__bswap_16 (__uint16_t __bsx)
{
  return __bswap_constant_16 (__bsx);
}

/* Swap bytes in 32-bit value.  */
#define __bswap_constant_32(x)                                  \
  ((((x) & 0xff000000u) >> 24) | (((x) & 0x00ff0000u) >> 8)     \
   | (((x) & 0x0000ff00u) << 8) | (((x) & 0x000000ffu) << 24))

static __inline __uint32_t
__bswap_32 (__uint32_t __bsx)
{
  return __bswap_constant_32 (__bsx);
}

/* Swap bytes in 64-bit value.  glibc writes this with `0x...ull` constants;
 * rcc cannot lex the `ull` suffix. Split into two 32-bit halves instead:
 * __bswap_32 is defined above and shifts on the (unsigned long long) halves
 * are fine for rcc. */
#define __bswap_constant_64(x) \
  (((__uint64_t)__bswap_32 (x) << 32) | __bswap_32 ((x) >> 32))

static __inline __uint64_t
__bswap_64 (__uint64_t __bsx)
{
  return __bswap_constant_64 (__bsx);
}

#endif /* _BITS_BYTESWAP_H */
