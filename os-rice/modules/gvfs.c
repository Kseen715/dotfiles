/* modules/gvfs.c -- mounting, removable media and trash (i3-sugg §3.3).
 * Without gvfs, "Move to Trash" errors out in every GTK app and the file-picker
 * sidebar shows no devices — the picker looks broken, and nothing points at the
 * WM as the cause.
 *
 * udisks2 is the block-device daemon; udiskie is the tray agent that actually
 * auto-mounts what you plug in (the i3 config execs `udiskie --tray`).
 * gvfs-nfs does not exist on Void — the logical name resolves to nfs-utils
 * there (see xbps.map), since NFS is a kernel mount, not a gvfs backend.
 * Filesystem drivers (ntfs-3g, exfatprogs, ...) are modules/disks.sh - mounting
 * a device and understanding what is on it are two different failures.
 *
 * Port of modules/gvfs.sh, kept as the reference at
 * test/ref/gvfs_sh_ref.sh. C89.
 */
#include "../lib/module.h"
#include "../lib/common.h"

#include <stddef.h>

int osrm_gvfs(void) {
    static const char *const gvfs[] = {
        "gvfs", "gvfs-mtp", "gvfs-gphoto2", "gvfs-smb", "gvfs-afc", "gvfs-goa",
        "gvfs-nfs", "udisks2", "udiskie", "trash-cli", NULL
    };
    static const char *const media[] = {
        "jmtpfs", "simple-mtpfs", "android-file-transfer", "cifs-utils",
        "nfs-utils", "sshfs", "rclone", NULL
    };
    int ok;

    ok = osr_pkg_install_step("Installing gvfs + udisks", gvfs);
    return osr_pkg_install_step("Installing removable/network media support", media) && ok;
}
