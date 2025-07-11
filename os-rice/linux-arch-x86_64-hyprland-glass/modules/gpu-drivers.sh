info "Installing GPU drivers..."

IFS="," read -r -a vendors <<< "$GPU_VENDOR"

for vendor in "${vendors[@]}"; do
    case "$vendor" in
        NVIDIA)
            info "NVIDIA GPU detected"
            trace sudo pacman -S --needed --noconfirm mesa nvidia nvidia-utils lib32-nvidia-utils vulkan-icd-loader lib32-vulkan-icd-loader nvidia-settings lib32-vulkan-nouveau xf86-video-nouveau
            ;;
        AMD)
            info "AMD GPU detected"
            trace sudo pacman -S --needed --noconfirm mesa lib32-mesa vulkan-radeon lib32-vulkan-radeon
            ;;
        Intel)
            info "Intel GPU detected"
            trace sudo pacman -S --needed --noconfirm mesa lib32-mesa vulkan-intel lib32-vulkan-intel
            ;;
        VMware)
            info "VMware GPU detected"
            trace sudo pacman -S --needed --noconfirm open-vm-tools mesa lib32-vulkan-virtio
            ;;
        *)
            warning "Unknown or unsopported GPU vendor: $vendor"
            ;;
    esac
done
