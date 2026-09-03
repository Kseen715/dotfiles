/* lib/ccsrc.h -- installing a C compiler from its upstream source tree.
 *
 * Every compiler this repo tracks besides tcc and zig is unpackaged: no
 * distro ships lacc, cproc, xcc, SmallerC, shecc, arocc or Cuik, and the
 * only route to them is a clone and a build. Those seven modules differ in
 * exactly two things -- the repository and the shell recipe -- so the parts
 * they share (the presence probe that makes a rerun free, the build
 * toolchain, a clean checkout, a hello world, the symlink onto PATH) live
 * here once rather than seven times.
 *
 * modules/lcc.c is deliberately NOT written against this: lcc needs a
 * patched driver, a 32-bit multilib probe and four glibc header overlays,
 * which is a recipe rather than a variation on one.
 *
 * Everything is installed under the riced account's own
 * ~/.local/share/<name>, so `osr module <cc>` needs no elevation.
 *
 * C89 + POSIX.
 */
#ifndef OSR_CCSRC_H
#define OSR_CCSRC_H

/* OsrCcSource -- one compiler's recipe.
 *
 *   name    the program, the directory under ~/.local/share, and the name
 *           symlinked into ~/.local/bin.
 *   repo    git URL, cloned shallow into <prefix>/src.
 *   pkgs    logical package names the build needs, NULL-terminated. Always
 *           starts with "build" and "git" -- there is no implicit list.
 *   script  sh, run in the checkout with PREFIX, SRC and MODROOT exported
 *           and `set -e` in force. It must leave the driver at $PREFIX/bin/
 *           <name>; upstreams whose `make install` does that are one line.
 *   hosted  1 when the compiler emits binaries that run on THIS box, which
 *           is then verified with a hello world before the symlink. 0 for a
 *           cross compiler (shecc targets ARM/RISC-V), whose output cannot
 *           be executed here.
 */
typedef struct {
    const char *name;
    const char *repo;
    const char *const *pkgs;
    const char *script;
    int hosted;
} OsrCcSource;

/* osr_cc_from_source -- install one. Returns 1 on success, and 1 without
 * doing anything when the compiler is already in place (SS2). */
int osr_cc_from_source(const OsrCcSource *cc);

#endif /* OSR_CCSRC_H */
