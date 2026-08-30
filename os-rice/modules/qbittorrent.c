/* modules/qbittorrent.c -- qBittorrent. ONE copy, POSIX (was .../apps/qbittorrent.sh).
 *
 * Was modules/qbittorrent.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
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
