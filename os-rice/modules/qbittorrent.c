/* modules/qbittorrent.c -- qBittorrent. ONE copy, POSIX (was .../apps/qbittorrent.sh).
 *
 * Port of modules/qbittorrent.sh, kept as the reference at
 * test/ref/qbittorrent_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_qbittorrent(void) {
    static const char *const pkgs[] = { "qbittorrent", NULL };
    Str dir;
    int ok;

    ok = osr_pkg_install_step("Installing qBittorrent", pkgs);
    str_init(&dir);
    str_addz(&dir, osr_mod_home());
    str_addz(&dir, "/.config/qBittorrent");
    ok = osr_mkdir_p(str_text(&dir)) && ok;
    str_free(&dir);
    return ok;
}
