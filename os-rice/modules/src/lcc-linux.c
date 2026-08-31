/* modules/src/lcc-linux.c -- the lcc driver (etc/linux.c), patched so a 2002
 * C89 compiler works on a modern glibc/gcc host. modules/lcc.c copies this
 * over the upstream etc/linux.c before building; the whole diff from upstream
 * is the four quirks below, all verified against glibc 2.4x / gcc 15.
 *
 *   1. @LCCPREFIX@ -- the install prefix, sedded in at install time (so a
 *      bare `lcc` works with no environment). Upstream defaults to
 *      /usr/local/lib/lcc/.
 *   2. cpp is forced to `-m32 -std=c89` and the GNU-extension macros glibc
 *      emits are neutralized with -D stubs. cpp-15 defaults to gnu17, which
 *      defines __STDC_VERSION__ (a C89 tool's preprocessed output would then
 *      look C99), and `-U__GNUC__` cannot undefine a builtin, so without the
 *      stubs glibc's __attribute__/restrict/inline kill rcc's C89 parser.
 *   3. as gets `-32`: modern GNU as defaults to 64-bit, lcc emits 32-bit.
 *   4. ld links the 32-bit crt files from /usr/lib32 (lcc's x86 backend is
 *      i386 only) and, via option(), also rewrites the include-patch overlay
 *      when a -lccdir override arrives -- so the patch header ships under
 *      the same prefix as everything else.
 *
 * The include-patch dir (see include[] below) holds modules/src/
 * lcc-struct_mutex.h, a copy of glibc's <bits/struct_mutex.h> with its C11
 * anonymous union named -- the one glibc header rcc cannot parse. C89.
 */

/* x86s running Linux */

#include <string.h>

static char rcsid[] = "$Id$";

#ifndef LCCDIR
#define LCCDIR "@LCCPREFIX@"
#endif

char *suffixes[] = { ".c", ".i", ".s", ".o", ".out", 0 };
char inputs[256] = "";
char *cpp[] = { LCCDIR "gcc/cpp",
	"-m32", "-std=c89", "-U__GNUC__", "-D_POSIX_SOURCE", "-D__STDC__=1", "-D__STRICT_ANSI__",
	"-Dunix", "-Di386", "-Dlinux",
	"-D__unix__", "-D__i386__", "-D__linux__", "-D__signed__=signed",
	"-D__attribute__(x)=", "-D__attribute__=", "-D__restrict=", "-Drestrict=",
	"-Dinline=", "-D__inline=", "-D__inline__=", "-D__extension__=",
	"$1", "$2", "$3", 0 };
char *include[] = {"-I" LCCDIR "include", "-I" LCCDIR "gcc/include", "-I" LCCDIR "include-patch", "-I/usr/include", 0 };
char *com[] = {LCCDIR "rcc", "-target=x86/linux", "$1", "$2", "$3", 0 };
char *as[] = { "/usr/bin/as", "-32", "-o", "$3", "$1", "$2", 0 };
char *ld[] = {
	/*  0 */ "/usr/bin/ld", "-m", "elf_i386", "-dynamic-linker",
	/*  4 */ "/lib/ld-linux.so.2", "-o", "$3",
	/*  7 */ "/usr/lib32/crt1.o", "/usr/lib32/crti.o",
	/*  9 */ LCCDIR "/gcc/crtbegin.o",
                 "$1", "$2",
	/* 12 */ "-L" LCCDIR,
	/* 13 */ "-llcc",
	/* 14 */ "-L" LCCDIR "/gcc", "-lgcc", "-lc", "-lm",
	/* 18 */ "",
	/* 19 */ LCCDIR "/gcc/crtend.o", "/usr/lib32/crtn.o",
	0 };

extern char *concat(char *, char *);

int option(char *arg) {
  	if (strncmp(arg, "-lccdir=", 8) == 0) {
		if (strcmp(cpp[0], LCCDIR "gcc/cpp") == 0)
			cpp[0] = concat(&arg[8], "/gcc/cpp");
		include[0] = concat("-I", concat(&arg[8], "/include"));
		include[1] = concat("-I", concat(&arg[8], "/gcc/include"));
		include[2] = concat("-I", concat(&arg[8], "/include-patch"));
		ld[9]  = concat(&arg[8], "/gcc/crtbegin.o");
		ld[12] = concat("-L", &arg[8]);
		ld[14] = concat("-L", concat(&arg[8], "/gcc"));
		ld[19] = concat(&arg[8], "/gcc/crtend.o");
		com[0] = concat(&arg[8], "/rcc");
	} else if (strcmp(arg, "-p") == 0 || strcmp(arg, "-pg") == 0) {
		ld[7] = "/usr/lib/gcrt1.o";
		ld[18] = "-lgmon";
	} else if (strcmp(arg, "-b") == 0)
		;
	else if (strcmp(arg, "-g") == 0)
		;
	else if (strncmp(arg, "-ld=", 4) == 0)
		ld[0] = &arg[4];
	else if (strcmp(arg, "-static") == 0) {
		ld[3] = "-static";
		ld[4] = "";
	} else
		return 0;
	return 1;
}
