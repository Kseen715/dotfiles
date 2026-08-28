/* lib/build.h -- the C port of lib/build.sh: the `source:` provider builders.
 *
 * A builder installs one program a native package cannot provide on some
 * target (§4), and is named by a pkgmap row: `lsd@jammy = source:provide_lsd_deb`.
 * In sh those names were shell functions in scope, so lib/pkg.c had to shell
 * out for the whole row; here they are a table, and lib/pkg.c looks the name up
 * first. A name that is not in the table yet is still lib/build.sh's, so the
 * port can land one builder at a time without a flag day.
 *
 * A builder returns 1 for success. The failure paths that lib/build.sh spelled
 * `error ...` are osr_die here, same as there: a half-installed program is not
 * something to limp on from.
 *
 * C89 + POSIX.
 */
#ifndef OSR_BUILD_H
#define OSR_BUILD_H

#include "common.h"

/* osr_build_has -- is this builder name ported to C yet? */
int osr_build_has(const char *fn);

/* osr_build_run -- run it. Undefined for a name osr_build_has rejects. */
int osr_build_run(const char *fn);

/* --- the shared primitives, exposed because a module may want them --------- */

/* osr_install_tarball_bin -- fetch a release tarball, find the named binary
 * anywhere inside it, install it 0755 into /usr/local/bin. dpkg-free, so it
 * works where a modern zstd .deb cannot (bullseye's dpkg lacks zstd). */
int osr_install_tarball_bin(const char *url, const char *bin);

#endif /* OSR_BUILD_H */
