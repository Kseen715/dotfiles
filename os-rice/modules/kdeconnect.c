/* modules/kdeconnect.c -- phone integration: clipboard sync, notifications,
 * file send, remote input (i3-sugg §7.1).
 *
 * Two things it needs that are easy to miss under a bare WM: mDNS to discover
 * the phone (modules/avahi.sh) and open UDP+TCP 1714-1764. If modules/ufw.sh ran
 * with its default deny-inbound policy, pairing will fail until those ports are
 * opened — the rule is commented in that module rather than opened silently.
 *
 * `kdeconnect-indicator` is the tray icon that makes it usable without a KDE
 * panel; on Void it ships inside the kdeconnect package.
 *
 * Port of modules/kdeconnect.sh, kept as the reference at
 * test/ref/kdeconnect_sh_ref.sh. C89.
 */
#include "../lib/module.h"

#include <stddef.h>

int osrm_kdeconnect(void) {
    static const char *const pkgs[] = { "kdeconnect", NULL };
    return osr_pkg_install_step("Installing KDE Connect", pkgs);
}
