# modules/gpu-drivers.sh — GPU drivers + Vulkan/OpenCL stack for the detected
# vendor(s). POSIX port of .../modules/gpu-drivers.sh, using OSR_GPU_VENDOR from
# detect.sh. Hardware-dependent and NOT container-testable (§9).
#
# Scope note: this port targets the mainstream *current* families (NVIDIA
# Turing+/open-dkms, AMD GCN+/amdgpu, Intel, VMware) that cover essentially all
# live desktops. The exhaustive legacy NVIDIA/AMD generation matrix (Kepler/
# Fermi/Tesla 340xx-470xx, pre-GCN ATI) is intentionally left to the retained
# legacy module for those rare old cards, per the "keep what CI can't verify"
# salvage rule. dkms is provided by the dkms module (list it earlier).
[ "$OSR_PKG" = pacman ] || { info "gpu-drivers module is Arch-specific - skipping"; return 0; }

for _v in ${OSR_GPU_VENDOR:-}; do
    case "$_v" in
        NVIDIA)
            run_step "Installing NVIDIA drivers" pkg_install \
                nvidia-open-dkms nvidia-settings nvidia-utils lib32-nvidia-utils \
                nvidia-prime ocl-icd \
                vulkan-icd-loader lib32-vulkan-icd-loader vulkan-tools ;;
        AMD)
            run_step "Installing AMD drivers" pkg_install \
                mesa lib32-mesa vulkan-radeon lib32-vulkan-radeon xf86-video-amdgpu \
                opencl-mesa ocl-icd libva-utils libvdpau-va-gl \
                vulkan-icd-loader lib32-vulkan-icd-loader vulkan-tools ;;
        Intel)
            run_step "Installing Intel drivers" pkg_install \
                mesa lib32-mesa vulkan-intel lib32-vulkan-intel \
                vulkan-icd-loader lib32-vulkan-icd-loader vulkan-tools ;;
        VMware)
            run_step "Installing VMware GPU drivers" pkg_install \
                open-vm-tools mesa lib32-vulkan-virtio \
                vulkan-icd-loader lib32-vulkan-icd-loader vulkan-tools ;;
        *)
            warn "unknown/unsupported GPU vendor '$_v' - skipping driver install" ;;
    esac
done
