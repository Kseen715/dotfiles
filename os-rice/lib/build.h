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

/* --- version guards a module makes before calling a builder ----------------
 *
 * Two builders exist because PRESENCE IS NOT SUFFICIENCY: an old distro chafa
 * or fzf satisfies pkg_install's "is it installed" probe and would never be
 * replaced, and the feature the rice needs (chafa --probe, fzf --gutter) is
 * missing anyway. The module asks first, so the guard costs one `--version`
 * run rather than a builder invocation, exactly as the sh modules' `_chafa_ok`
 * / `_fzf_ok` did. The MIN strings are exported because the modules name them
 * in the step they print. */
#define OSR_CHAFA_MIN "1.16"
#define OSR_FZF_MIN   "0.66"
int osr_chafa_ok(void);
int osr_fzf_ok(void);

/* osr_lsd_ok -- the third face of "presence is not sufficiency", and the one
 * that is not about a version: a distro lsd links the distro libgit2, which
 * links libssh2, so lsd stops at the dynamic loader the moment anything down
 * that chain goes missing. It is still installed, `command -v` still finds it,
 * and pkg_install would never touch it again -- but 20-aliases.zsh aliases ls
 * to it, so EVERY `ls` in every shell answers with a loader error. Nothing here
 * asks how new it is; asking whether it RUNS covers a broken link and an absent
 * binary alike. */
int osr_lsd_ok(void);

/* osr_build_zig -- install Zig from ziglang.org as a whole tree, symlinked into
 * /usr/local/bin. want pins an exact version ("0.14.1"); "" or NULL takes the
 * newest stable. Exposed because it is also a PREREQUISITE: the ghostty source
 * build reads the exact Zig version ghostty pins and asks for that one (G1, a
 * source: builder with a bootstrapped toolchain under it). */
int osr_build_zig(const char *want);

#endif /* OSR_BUILD_H */
