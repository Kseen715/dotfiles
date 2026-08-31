/* modules/src/lcc-stddef.h -- <stddef.h> overlay for lcc's rcc on a modern
 * glibc host. Installed as <lccdir>/include/stddef.h -- the FIRST directory on
 * the driver's include path -- so gcc's own stddef.h is never reached for
 * `#include <stddef.h>` and its `typedef __SIZE_TYPE__ size_t;` cannot clash.
 *
 * WHY: with _XOPEN_SOURCE set (os-rice's lib/config.c, lib/preflight.c do, for
 * realpath etc.), glibc's glob.h typedefs size_t itself, and gcc's stddef.h
 * typedefs it again a few headers later -- a redeclaration C89 forbids. Both
 * glob.h and this header key off the SAME `__size_t` macro guard, so whichever
 * runs first defines size_t and the other skips it, and include order stops
 * mattering. __SIZE_TYPE__ is `unsigned int` on this 32-bit i386 target, so the
 * typedefs are type-identical. C89.
 */
#ifndef _STDDEF_H
#define _STDDEF_H

/* size_t -- guarded with glibc's own __size_t marker (glob.h uses the same
 * #ifndef __size_t / #define __size_t idiom), so the two headers agree on who
 * defines it. __size_t stays a real typedef too, matching glob.h's shape. */
#ifndef __size_t
typedef unsigned int __size_t;
typedef unsigned int size_t;
#define __size_t
#endif

#ifndef __ptrdiff_t
typedef long int ptrdiff_t;
#define __ptrdiff_t
#endif

#ifndef __wchar_t
typedef int wchar_t;
#define __wchar_t
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#define offsetof(type, member) ((size_t)&(((type *)0)->member))

#endif /* _STDDEF_H */
