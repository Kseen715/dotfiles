info "Installing GPU drivers..."
if [ "$GPU_VENDOR" == "NVIDIA" ]; then
    info "NVIDIA GPU detected"
    trace sudo pacman -S --needed --noconfirm nvidia nvidia-utils lib32-nvidia-utils vulkan-icd-loader lib32-vulkan-icd-loader nvidia-settings
fi
if [ "$GPU_VENDOR" == "AMD" ]; then
    info "AMD GPU detected"
    trace sudo pacman -S --needed --noconfirm mesa lib32-mesa vulkan-radeon lib32-vulkan-radeon
fi
if [ "$GPU_VENDOR" == "Intel" ]; then
    info "Intel GPU detected"
    trace sudo pacman -S --needed --noconfirm mesa lib32-mesa vulkan-intel lib32-vulkan-intel
fi
if [ "$GPU_VENDOR" == "VMware" ]; then
    info "VMware GPU detected"
    trace sudo pacman -S --needed --noconfirm open-vm-tools mesa lib32-vulkan-virtio
fi