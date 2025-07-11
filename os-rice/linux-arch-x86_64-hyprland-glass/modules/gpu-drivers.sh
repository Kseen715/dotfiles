info "Installing GPU drivers..."

IFS="," read -r -a vendors <<< "$GPU_VENDOR"

for vendor in "${vendors[@]}"; do
    case "$vendor" in
        NVIDIA)
            info "NVIDIA GPU detected"
            trace sudo pacman -S --needed --noconfirm nvidia-open vulkan-icd-loader lib32-vulkan-icd-loader vulkan-tools vulkan-mesa-layers lib32-vulkan-mesa-layers nvidia-settings nvidia-utils lib32-nvidia-utils nvidia-prime
            ;;
        AMD)
            info "AMD GPU detected"
            trace sudo pacman -S --needed --noconfirm mesa lib32-mesa vulkan-radeon lib32-vulkan-radeon vulkan-tools
            ;;
        Intel)
            info "Intel GPU detected"
            trace sudo pacman -S --needed --noconfirm mesa lib32-mesa vulkan-intel lib32-vulkan-intel vulkan-tools xf86-video-intel
            ;;
        VMware)
            info "VMware GPU detected"
            trace sudo pacman -S --needed --noconfirm open-vm-tools mesa lib32-vulkan-virtio vulkan-tools
            ;;
        *)
            warning "Unknown or unsopported GPU vendor: $vendor"
            ;;
    esac
done
