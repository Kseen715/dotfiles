# session: x11
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/vmware-init.sh — VMware guest tools + driver, only under a VMware
# hypervisor. POSIX port of .../modules/vmware-init.sh, keyed on OSR_VIRT from
# detect.sh. Hardware/VM-dependent (§9). Arch-only.
[ "$OSR_PKG" = pacman ] || return 0
[ "${OSR_VIRT:-none}" = vmware ] || { info "not a VMware guest (virt=${OSR_VIRT:-none}) - skipping"; return 0; }

run_step "Installing VMware guest tools" pkg_install open-vm-tools mesa
run_step "Installing VMware Xorg driver (AUR)" pkg_install xf86-video-vmware
enable_service vmtoolsd
enable_service vmware-vmblock-fuse
