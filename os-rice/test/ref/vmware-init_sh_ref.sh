# test/ref/vmware-init_sh_ref.sh — the sh implementation of modules/vmware-init.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/vmware-init.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11
# modules/vmware-init.sh — VMware guest tools + driver, only under a VMware
# hypervisor. POSIX port of .../modules/vmware-init.sh, keyed on OSR_VIRT from
# detect.sh. Hardware/VM-dependent (§9). Arch-only.
[ "$OSR_PKG" = pacman ] || return 0
[ "${OSR_VIRT:-none}" = vmware ] || { info "not a VMware guest (virt=${OSR_VIRT:-none}) - skipping"; return 0; }

run_step "Installing VMware guest tools" pkg_install open-vm-tools mesa
run_step "Installing VMware Xorg driver (AUR)" pkg_install xf86-video-vmware
enable_service vmtoolsd
enable_service vmware-vmblock-fuse
