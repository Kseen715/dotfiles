info "Installing GPU drivers..."

IFS="," read -r -a vendors <<< "$GPU_VENDOR"

for vendor in "${vendors[@]}"; do
    case "$vendor" in
        NVIDIA)
            info "NVIDIA GPU detected"

            nvidia_chip=$(lspci -k -d ::03xx | grep -oP '(?<=NVIDIA Corporation )[^ ]+')
            info "NVIDIA chip: $nvidia_chip"

            # match patterns to get turing/maxwell/etc text
            nvidia_family=
            if [[ $nvidia_chip =~ ^(AD|NV190) ]]; then
                nvidia_family="Ada Lovelace" # GeForce RTX 40xx
            elif [[ $nvidia_chip =~ ^(GA|NV170) ]]; then
                nvidia_family="Ampere" # GeForce RTX 30xx
            elif [[ $nvidia_chip =~ ^(TU|NV160) ]]; then
                nvidia_family="Turing" # GeForce RTX 20xx
            elif [[ $nvidia_chip =~ ^(NV140) ]]; then
                nvidia_family="Volta" # NVIDIA Titan V
            elif [[ $nvidia_chip =~ ^(NV130) ]]; then
                nvidia_family="Pascal" # GeForce 10xx
            elif [[ $nvidia_chip =~ ^(GM|NV110) ]]; then
                nvidia_family="Maxwell" # GeForce 750-9xx
            elif [[ $nvidia_chip =~ ^(GK|NVE0) ]]; then
                nvidia_family="Kepler" # GeForce 600-7xx, Titan
            elif [[ $nvidia_chip =~ ^(GF|NVC0) ]]; then
                nvidia_family="Fermi" # GeForce 4xx-5xx
            elif [[ $nvidia_chip =~ ^(G[89]|GT2|NV50) ]]; then
                nvidia_family="Tesla" # GeForce 8-9-1xx-2xx-3xx
            elif [[ $nvidia_chip =~ ^(G70|NV40) ]]; then
                nvidia_family="Curie" # GeForce 6-7
            elif [[ $nvidia_chip =~ ^(NV30) ]]; then
                nvidia_family="Rankine" # GeForce 5, FX
            elif [[ $nvidia_chip =~ ^(NV20) ]]; then
                nvidia_family="Kelvin" # GeForce 3-4
            elif [[ $nvidia_chip =~ ^(NV10) ]]; then
                nvidia_family="Celsius" # GeForce 256, 2, 4MX
            elif [[ $nvidia_chip =~ ^(NV05|NV04|NV0A) ]]; then
                nvidia_family="Fahrenheit" # Riva TNT, TNT2
            elif [[ $nvidia_chip =~ ^(NV03|NV02|NV01) ]]; then
                nvidia_family="Ancient" # Riva 128, Diamond Edge 3D
            else
                nvidia_family="Unknown"
            fi
            info "NVIDIA family: $nvidia_family"

            install_pkg_pacman dkms
            case "$nvidia_family" in
                "Ada Lovelace"|"Ampere"|"Turing"|"Unknown")
                    install_pkg_pacman nvidia-open-dkms nvidia-settings nvidia-utils lib32-nvidia-utils
                    trace pacman -Qi nvidia-open-dkms | grep Version | awk '{print $3}' | sed 's/-.*//g' > /tmp/nvidia_version
                    nvidia_version=$(cat /tmp/nvidia_version)
                    info "NVIDIA version: $nvidia_version"
                    trace dkms install --no-depmod nvidia/$nvidia_version
                    ;;
                "Volta"|"Pascal"|"Maxwell")
                    install_pkg_pacman nvidia-dkms nvidia-settings nvidia-utils lib32-nvidia-utils
                    trace pacman -Qi nvidia-dkms | grep Version | awk '{print $3}' | sed 's/-.*//g' > /tmp/nvidia_version
                    nvidia_version=$(cat /tmp/nvidia_version)
                    info "NVIDIA version: $nvidia_version"
                    trace dkms install --no-depmod nvidia/$nvidia_version
                    ;;
                "Kepler")
                    install_pkg_aur nvidia-470xx-dkms
                    trace pacman -Qi nvidia-470xx-dkms | grep Version | awk '{print $3}' | sed 's/-.*//g' > /tmp/nvidia_version
                    nvidia_version=$(cat /tmp/nvidia_version)
                    info "NVIDIA version: $nvidia_version"
                    trace dkms install --no-depmod nvidia/$nvidia_version
                    ;;
                "Fermi")
                    install_pkg_aur nvidia-390xx-dkms
                    trace pacman -Qi nvidia-390xx-dkms | grep Version | awk '{print $3}' | sed 's/-.*//g' > /tmp/nvidia_version
                    nvidia_version=$(cat /tmp/nvidia_version)
                    info "NVIDIA version: $nvidia_version"
                    trace dkms install --no-depmod nvidia/$nvidia_version
                    ;;
                "Tesla")
                    install_pkg_aur nvidia-340xx-dkms
                    trace pacman -Qi nvidia-340xx-dkms | grep Version | awk '{print $3}' | sed 's/-.*//g' > /tmp/nvidia_version
                    nvidia_version=$(cat /tmp/nvidia_version)
                    info "NVIDIA version: $nvidia_version"
                    trace dkms install --no-depmod nvidia/$nvidia_version
                    ;;
                "Curie"|"Rankine"|"Kelvin"|"Celsius"|"Fahrenheit")
                    error "NVIDIA $nvidia_chip is NOT IMPLEMENTED YET"
                    ;;
                "Ancient")
                    if [[ $nvidia_chip =~ ^(NV03) ]]; then
                        error "NVIDIA $nvidia_chip is NOT IMPLEMENTED YET"
                        # Supported by vesa driver
                        # http://www.x.org/wiki/vesa
                    elif [[ $nvidia_chip =~ ^(NV02) ]]; then
                        error "NVIDIA $nvidia_chip is NOT IMPLEMENTED YET"
                    elif [[ $nvidia_chip =~ ^(NV01) ]]; then
                        error "NVIDIA $nvidia_chip is NOT IMPLEMENTED YET"
                        # Supported by nv driver
                        # http://www.x.org/wiki/nv
                    fi
                    ;;
            esac
            install_pkg_pacman vulkan-icd-loader lib32-vulkan-icd-loader vulkan-tools vulkan-mesa-layers lib32-vulkan-mesa-layers nvidia-prime
            ;;
        AMD)
            info "AMD GPU detected"
            install_pkg_pacman mesa lib32-mesa vulkan-radeon lib32-vulkan-radeon vulkan-tools
            ;;
        Intel)
            info "Intel GPU detected"
            install_pkg_pacman mesa lib32-mesa vulkan-intel lib32-vulkan-intel vulkan-tools xf86-video-intel
            ;;
        VMware)
            info "VMware GPU detected"
            install_pkg_pacman open-vm-tools mesa lib32-vulkan-virtio vulkan-tools
            ;;
        *)
            warning "Unknown or unsupported GPU vendor: $vendor"
            ;;
    esac
done
