# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/disks.sh — filesystems, partitioning and disk health (i3-sugg §7.6).
# The mount/automount side is modules/gvfs.sh; this is what you need for the
# disks themselves: the drivers that let a foreign filesystem mount at all, and
# the tools to look at what is on them.
#
# ntfs-3g/exfatprogs/dosfstools are the ones that bite: without them plugging in
# a Windows-formatted drive fails with a bare "unknown filesystem type", and
# udisks gives no hint that a userspace helper is what is missing.

run_step "Installing filesystem tools" pkg_install \
    ntfs-3g exfatprogs dosfstools f2fs-tools btrfs-progs smartmontools

run_step "Installing disk GUIs" pkg_install gnome-disks gparted baobab
