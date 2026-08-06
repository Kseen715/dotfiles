# session: x11+wayland
# modules/gvfs.sh — mounting, removable media and trash (i3-sugg §3.3).
# Without gvfs, "Move to Trash" errors out in every GTK app and the file-picker
# sidebar shows no devices — the picker looks broken, and nothing points at the
# WM as the cause.
#
# udisks2 is the block-device daemon; udiskie is the tray agent that actually
# auto-mounts what you plug in (the i3 config execs `udiskie --tray`).
# gvfs-nfs does not exist on Void — the logical name resolves to nfs-utils
# there (see xbps.map), since NFS is a kernel mount, not a gvfs backend.

run_step "Installing gvfs + udisks" pkg_install \
    gvfs gvfs-mtp gvfs-gphoto2 gvfs-smb gvfs-afc gvfs-goa gvfs-nfs \
    udisks2 udiskie trash-cli

# Filesystem drivers (ntfs-3g, exfatprogs, ...) are modules/disks.sh - mounting
# a device and understanding what is on it are two different failures.
run_step "Installing removable/network media support" pkg_install \
    jmtpfs simple-mtpfs android-file-transfer \
    cifs-utils nfs-utils sshfs rclone
