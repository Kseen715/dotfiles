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
                    install_pkg_pacman nvidia-open-dkms nvidia-settings nvidia-utils lib32-nvidia-utils nvtop
                    trace pacman -Qi nvidia-open-dkms | grep Version | awk '{print $3}' | sed 's/-.*//g' > /tmp/nvidia_version
                    nvidia_version=$(cat /tmp/nvidia_version)
                    info "NVIDIA version: $nvidia_version"
                    trace dkms install --no-depmod nvidia/$nvidia_version
                    ;;
                "Volta"|"Pascal"|"Maxwell")
                    install_pkg_pacman nvidia-dkms nvidia-settings nvidia-utils lib32-nvidia-utils nvtop
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
                    install_pkg_aur opencl-nvidia-470xx
                    install_pkg_pacman ocl-icd nvtop
                    ;;
                "Fermi")
                    install_pkg_aur nvidia-390xx-dkms
                    trace pacman -Qi nvidia-390xx-dkms | grep Version | awk '{print $3}' | sed 's/-.*//g' > /tmp/nvidia_version
                    nvidia_version=$(cat /tmp/nvidia_version)
                    info "NVIDIA version: $nvidia_version"
                    trace dkms install --no-depmod nvidia/$nvidia_version
                    install_pkg_aur opencl-nvidia-390xx
                    install_pkg_pacman ocl-icd nvtop libvdpau
                    ;;
                "Tesla")
                    install_pkg_aur nvidia-340xx-dkms
                    trace pacman -Qi nvidia-340xx-dkms | grep Version | awk '{print $3}' | sed 's/-.*//g' > /tmp/nvidia_version
                    nvidia_version=$(cat /tmp/nvidia_version)
                    info "NVIDIA version: $nvidia_version"
                    trace dkms install --no-depmod nvidia/$nvidia_version
                    install_pkg_aur opencl-nvidia-340xx
                    install_pkg_pacman ocl-icd nvtop
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
            install_pkg_pacman vulkan-icd-loader lib32-vulkan-icd-loader vulkan-tools vulkan-mesa-layers lib32-vulkan-mesa-layers nvidia-prime ocl-icd
            ;;
        AMD)
            info "AMD GPU detected"

            amd_chip=$(lspci -k -d ::03xx | grep -oP '\[AMD/ATI\]\s+\K[^\s\[]+')
            info "AMD chip: $amd_chip"

            amd_family=
            if [[ $amd_chip =~ ^(R100|RV100|RV200|RS100|RS200) ]]; then
                amd_family="R100" # 7xxx, 320-345
            elif [[ $amd_chip =~ ^(R200|RV250|RV280|RS300) ]]; then
                amd_family="R200" # 8xxx - 9250
            elif [[ $amd_chip =~ ^(R300|R350|RV350|RV380|RS400|RS480) ]]; then
                amd_family="R300" # 9500 - 9800, X300 - X600, X1050 - X1150, 200M
            elif [[ $amd_chip =~ ^(R420|R423|RV410|RS600|RS690|RS740) ]]; then
                amd_family="R400" # X700 - X850, X12xx, 2100
            elif [[ $amd_chip =~ ^(RV515|R520|RV530|RV560|RV570|R580) ]]; then
                amd_family="R500" # X1300 - X2300, HD2300
            elif [[ $amd_chip =~ ^(R600|RV610|RV630|RV620|RV635|RV670|RS780|RS880) ]]; then
                amd_family="R600" # HD2400 - HD4290
            elif [[ $amd_chip =~ ^(RV770|RV730|RV710|RV740) ]]; then
                amd_family="R700" # HD4330 - HD5165, HD5xxV
            elif [[ $amd_chip =~ ^(CEDAR|REDWOOD|JUNIPER|CYPRESS|PALM|Wrestler|Ontario|SUMO|Llano|SUMO2) ]]; then
                amd_family="Evergreen" # HD5430 - HD5970, all HD6000 not listed under Northern Islands, HD7350
            elif [[ $amd_chip =~ ^(ARUBA|Trinity|Richland|BARTS|TURKS|CAICOS|CAYMAN) ]]; then
                amd_family="Northern Islands" # HD6450, HD6570, HD6670, HD6790 - HD6990, HD64xxM, HD67xxM, HD69xxM, HD7450 - HD7670
            elif [[ $amd_chip =~ ^(VERDE|PITCAIRN|TAHITI|OLAND|HAINAN) ]]; then
                amd_family="Southern Islands" # HD7750 - HD7970, R9 270, R9-280, R7 240, R7 250
            elif [[ $amd_chip =~ ^(BONAIRE|KABINI|MULLINS|KAVERI|HAWAII) ]]; then
                amd_family="Sea Islands" # HD7790, R7 260, R9 290
            elif [[ $amd_chip =~ ^(TONGA|ICELAND/TOPAZ|CARRIZO|FIJI|STONEY|POLARIS10|POLARIS11|POLARIS12|VEGAM) ]]; then
                amd_family="Volcanic Islands" # R9 285
            elif [[ $amd_chip =~ ^(Polaris|Ellesmere|Baffin|Lexa|Neo|Scorpio) ]]; then
                amd_family="Arctic Islands/Polaris" # RX480, 520/530, RX530/550/570/580
            elif [[ $amd_chip =~ ^(Vega 10|Vega 12|Vega 20|Raven Ridge|Picasso|Renoir|Cezanne) ]]; then
                amd_family="Vega" # Vega Frontier Edition
            elif [[ $amd_chip =~ ^(Navi 1) ]]; then
                amd_family="Navi 1x" # RX 5xx0
            elif [[ $amd_chip =~ ^(Navi 2) ]]; then
                amd_family="Navi 2x" # RX 6xx0
            elif [[ $amd_chip =~ ^(Navi 3) ]]; then
                amd_family="Navi 3x" # RX 7xx0
            elif [[ $amd_chip =~ ^(Navi 4) ]]; then
                amd_family="Navi 4x" # RX 90x0
            else 
                amd_family="Unknown"
            fi
            info "AMD family: $amd_family"

            case "$amd_family" in
                "R100"|"R200")
                    # ATI Ember
                    warning "OpenCL is NOT supported on $amd_family"
                    install_pkg_pacman mesa-amber lib32-mesa-amber xf86-video-ati
                    install_pkg_pacman libva-utils libvdpau-va-gl
                    ;;
                "R300"|"R400"|"R500")
                    # ATI + VA-API
                    warning "OpenCL is NOT supported on $amd_family"
                    install_pkg_pacman mesa mesa-utils lib32-mesa xf86-video-ati
                    install_pkg_pacman libva-utils libvdpau-va-gl
                    ;;
                "R600")
                    # ATI + VA-API + VDPAU
                    warning "OpenCL is NOT supported on $amd_family"
                    install_pkg_pacman mesa mesa-utils lib32-mesa xf86-video-ati
                    install_pkg_pacman libva-utils libvdpau-va-gl
                    install_pkg_pacman libvdpau lib32-libvdpau vdpauinfo
                    # install_pkg_aur opencl-legacy-amdgpu-pro
                    ;;
                "R700"|"Evergreen"|"Northern Islands"|"Sea Islands"|"Southern Islands")
                    # ATI
                    warning "OpenCL has limited support on R700"
                    install_pkg_pacman mesa mesa-utils xf86-video-ati libva-utils vdpauinfo libvdpau-va-gl libvdpau lib32-mesa lib32-libvdpau
                    ;;
                "Volcanic Islands"|"Unknown")
                    # AMDGPU
                    install_pkg_pacman mesa opencl-mesa ocl-icd libva-utils vdpauinfo libvdpau-va-gl libvdpau lib32-mesa lib32-opencl-mesa lib32-libvdpau
                    install_pkg_pacman vulkan-radeon vulkan-tools lib32-vulkan-radeon xf86-video-amdgpu
                    ;;
                *)
                    warning "AMD $amd_chip is NOT IMPLEMENTED YET"
                    ;;
            esac

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
