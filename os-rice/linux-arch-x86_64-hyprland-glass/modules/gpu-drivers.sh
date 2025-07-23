info "Installing GPU drivers..."

IFS="," read -r -a vendors <<< "$GPU_VENDOR"

for vendor in "${vendors[@]}"; do
    case "$vendor" in
        NVIDIA)
            info "NVIDIA GPU detected"
            # check if nvidia-open-dkms is in ignore list
            to_install=""
            if grep -q "nvidia-open-dkms" /etc/pacman.conf; then
                warning "nvidia-open-dkms is in the ignore list -- skipping"
            else
                to_install="$to_install nvidia-open-dkms"
            fi
            if grep -q "nvidia-settings" /etc/pacman.conf; then
                warning "nvidia-settings is in the ignore list -- skipping"
            else
                to_install="$to_install nvidia-settings"
            fi
            if grep -q "nvidia-utils" /etc/pacman.conf; then
                warning "nvidia-utils is in the ignore list -- skipping"
            else
                to_install="$to_install nvidia-utils"
            fi
            if grep -q "lib32-nvidia-utils" /etc/pacman.conf; then
                warning "lib32-nvidia-utils is in the ignore list -- skipping"
            else
                to_install="$to_install lib32-nvidia-utils"
            fi
            if [ -z "$to_install" ]; then
                warning "No NVIDIA packages to install, skipping"
            else
                info "Installing NVIDIA packages: $to_install"
                trace sudo pacman -S --needed --noconfirm $to_install
            fi
            trace sudo pacman -S --needed --noconfirm vulkan-icd-loader lib32-vulkan-icd-loader vulkan-tools vulkan-mesa-layers lib32-vulkan-mesa-layers nvidia-prime
            trace pacman -Qi nvidia-open-dkms | grep Version | awk '{print $3}' | sed 's/-2//g' > /tmp/nvidia_version
            nvidia_version=$(cat /tmp/nvidia_version)
            info "NVIDIA version: $nvidia_version"
            trace dkms install --no-depmod nvidia/$nvidia_version
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
