# Install video drivers based on virtualization type and hardware if not virtualized
if [ "$VIRT" = "vmware" ]; then
    info "Detected VMware, installing VMware specific GPU drivers..."
    trace pacman -S --needed --noconfirm open-vm-tools mesa
    sudo -u "$DELEVATED_USER" $AUR_HELPER -S --needed --noconfirm xf86-video-vmware-git
    info "Activating VMware tools..."
    trace systemctl enable vmtoolsd.service --force
    trace systemctl enable vmware-vmblock-fuse.service --force
fi