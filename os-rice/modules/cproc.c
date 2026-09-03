/* modules/cproc.c -- cproc, a C11 front end over the QBE backend
 * (michaelforney/cproc).
 *
 * Builds the whole os-rice tree (README.md's compiler table): it accepts
 * every flag nob.c emits, -std=c89 included, and links against the host
 * glibc through the system linker.
 *
 * cproc is a front end only, so QBE is built first, into the same prefix,
 * and baked into the driver with --with-qbe -- an absolute path rather than
 * a PATH lookup, so `cproc` works whatever PATH the caller has.
 *
 * Idempotent (SS2), user-local (~/.local/share/cproc), no elevation. C89.
 */
#include "../lib/ccsrc.h"

#include <stddef.h>

int osrm_cproc(void) {
    static const char *const pkgs[] = { "build", "git", NULL };
    static const OsrCcSource cproc = {
        "cproc",
        "https://github.com/michaelforney/cproc",
        pkgs,
        /* git:// and not https://: c9x.me serves git over DUMB http,
         * where --depth fails outright ("dumb http transport does not
         * support shallow capabilities") and a full clone dies fetching
         * blobs. The git protocol is unauthenticated and unencrypted, so
         * this trusts the network for the backend's source -- upstream
         * offers no other transport, and QBE has no release tarball. */
        "git clone --depth 1 git://c9x.me/qbe.git qbe\n"
        "make -C qbe PREFIX=\"$PREFIX\" install\n"
        "./configure --prefix=\"$PREFIX\" --with-qbe=\"$PREFIX/bin/qbe\"\n"
        "make\n"
        "make install\n",
        1
    };
    return osr_cc_from_source(&cproc);
}
