/* modules/disks.c -- filesystems, partitioning and disk health (i3-sugg §7.6).
 * The mount/automount side is modules/gvfs.sh; this is what you need for the
 * disks themselves: the drivers that let a foreign filesystem mount at all, and
 * the tools to look at what is on them.
 *
 * ntfs-3g/exfatprogs/dosfstools are the ones that bite: without them plugging in
 * a Windows-formatted drive fails with a bare "unknown filesystem type", and
 * udisks gives no hint that a userspace helper is what is missing.
 *
 * Was modules/disks.sh; what it must do is stated in the C tests
 * under test/unit_c/ rather than diffed against a recording. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_disks(void) {
    static const char *const fs[] = {
        "ntfs-3g", "exfatprogs", "dosfstools", "f2fs-tools", "btrfs-progs",
        "smartmontools", NULL
    };
    static const char *const guis[] = { "gnome-disks", "gparted", "baobab", NULL };
    int ok;

    ok = osr_pkg_install_step("Installing filesystem tools", fs);
    return osr_pkg_install_step("Installing disk GUIs", guis) && ok;
}
