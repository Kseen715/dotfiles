# modules/gpu-drivers.sh — GPU drivers + Vulkan/OpenCL/VA-API stack for every
# detected GPU, across every generation the Arch repos and AUR still carry:
# NVIDIA Blackwell..Curie, AMD Navi..R100, Intel Xe..gen3, plus the VM vendors.
# POSIX port of the legacy hyprland-glass module, generation matrix included.
#
# Vendor alone can't pick a driver — the same "NVIDIA" needs nvidia-open-dkms on
# Turing+, a frozen AUR branch on Maxwell/Kepler/Fermi/Tesla, and nouveau below
# that. So each vendor's chip codename (OSR_GPU_DEVICES, §7) goes through a
# family classifier, and the family picks the package set.
#
# Package names below are Arch's (and the legacy NVIDIA branches are AUR rows),
# but the module runs everywhere: the family matrix is distro-independent, so on
# a non-pacman host it fails loudly on the first missing package instead of
# silently installing no driver at all. Fix a break by adding the distro's rows
# to lib/pkgmap/<mgr>.map — the logical names here stay put.
#
# dkms + kernel headers come from the dkms module and paru from the paru module:
# list both BEFORE this one (manifest order is the dependency graph, §4). No
# explicit `dkms install` — Arch's dkms package ships the alpm hooks that build
# every module on install and on kernel upgrade.
#
# Hardware-dependent and NOT container-testable (§9); the classifiers are pure
# functions, so test/unit/gpu_drivers.sh covers the matrix without a GPU.
# shellcheck disable=SC2086  # $_gpu_vk intentionally word-splits into a pkg list
[ "$OSR_PKG" = pacman ] || warn "gpu-drivers package names are Arch's - untested on $OSR_PKG, add pkgmap rows when it breaks"

# Vulkan loader + tools: every branch that has a Vulkan driver at all wants these.
_gpu_vk="vulkan-icd-loader lib32-vulkan-icd-loader vulkan-tools"

# _gpu_family_nvidia <chip> — codename -> family. Both naming schemes appear in
# the wild: modern lspci prints "AD102"/"GA104", older/quirky tables print the
# internal "NV190"/"NVE0". Unknown (incl. empty) falls through to current: a chip
# too new for this lspci is far likelier than one too old.
_gpu_family_nvidia() {
    case "$1" in
        GB*|NV1[AB]0)       echo Blackwell ;;
        AD*|NV190)          echo "Ada Lovelace" ;;
        GA*|NV170)          echo Ampere ;;
        TU*|NV160)          echo Turing ;;
        GV*|NV140)          echo Volta ;;
        GP*|NV130)          echo Pascal ;;
        GM*|NV110)          echo Maxwell ;;
        GK*|NVE*)           echo Kepler ;;
        GF*|NVC*)           echo Fermi ;;
        G8*|G9*|GT2*|NV5*)  echo Tesla ;;
        G7*|NV4*)           echo Curie ;;
        NV3*)               echo Rankine ;;
        NV2*)               echo Kelvin ;;
        NV1*)               echo Celsius ;;
        NV0*)               echo Fahrenheit ;;
        *)                  echo Unknown ;;
    esac
}

# _gpu_family_amd <chip> — codename -> family. Marketing generation names are
# useless here (an "RX 6700" and an "RX 580" are both "Radeon RX"), the ASIC
# codename is what maps to a driver: amdgpu for GCN3+, radeon/r600/r300 gallium
# below that, and mesa-amber for the pre-shader R100/R200.
_gpu_family_amd() {
    case "$1" in
        Navi*|Strix*|Phoenix*|Rembrandt*|"Van Gogh"*|Raphael*|Granite*) echo Navi ;;
        Vega*|Raven*|Picasso*|Renoir*|Cezanne*|Barcelo*)                echo Vega ;;
        POLARIS*|Polaris*|Ellesmere*|Baffin*|Lexa*|Neo*|Scorpio*)       echo Polaris ;;
        TONGA*|ICELAND*|TOPAZ*|CARRIZO*|FIJI*|STONEY*|VEGAM*)           echo "Volcanic Islands" ;;
        BONAIRE*|KABINI*|MULLINS*|KAVERI*|HAWAII*)                      echo "Sea Islands" ;;
        VERDE*|PITCAIRN*|TAHITI*|OLAND*|HAINAN*)                        echo "Southern Islands" ;;
        ARUBA*|Trinity*|Richland*|BARTS*|TURKS*|CAICOS*|CAYMAN*)        echo "Northern Islands" ;;
        CEDAR*|REDWOOD*|JUNIPER*|CYPRESS*|PALM*|Wrestler*|Ontario*|SUMO*|Llano*) echo Evergreen ;;
        RV7*)                                                           echo R700 ;;
        R600*|RV6*|RS78*|RS88*)                                         echo R600 ;;
        RV51*|R52*|RV53*|RV56*|RV57*|R58*)                              echo R500 ;;
        R4[0-9][0-9]*|RV41*|RS6*|RS7*)                                  echo R400 ;;
        R3[0-9][0-9]*|RV3*|RS4*)                                        echo R300 ;;
        R1[0-9][0-9]*|R2[0-9][0-9]*|RV1*|RV2*|RS1*|RS2*|RS3*)           echo R100 ;;
        *)                                                              echo Unknown ;;
    esac
}

# _gpu_family_intel <chip> — lspci names Intel iGPUs by CPU generation, not by
# graphics gen, so the split is by driver era: iris/ANV (Broadwell+), crocus
# (gen6-7.5, Vulkan only via hasvk), i915 gallium (gen3-5, mesa-amber only).
# Order matters: "2nd Generation Core Processor" must be tested before the
# gen5 "Core Processor" it contains.
_gpu_family_intel() {
    case "$1" in
        *"2nd Generation"*|*"3rd Gen"*|*"4th Gen"*|Haswell*|"Bay Trail"*|*"Atom Processor Z3"*) echo crocus ;;
        8*|"4 Series"*|"Mobile 4"*|G3[0-9]*|Q3[0-9]*|Pineview*|*"Core Processor Integrated"*)   echo amber ;;
        *)                                                                                      echo modern ;;
    esac
}

for _v in ${OSR_GPU_VENDOR:-}; do
    _chip=$(osr_gpu_chip "$_v")
    case "$_v" in
        NVIDIA)
            _fam=$(_gpu_family_nvidia "$_chip")
            info "NVIDIA chip='${_chip:-unknown}' family=$_fam"
            case "$_fam" in
                Blackwell|"Ada Lovelace"|Ampere|Turing|Unknown)
                    # Open kernel modules need a GSP, i.e. Turing and newer.
                    run_step "Installing NVIDIA drivers ($_fam)" pkg_install \
                        nvidia-open-dkms nvidia-utils lib32-nvidia-utils nvidia-settings \
                        nvidia-prime opencl-nvidia lib32-opencl-nvidia ocl-icd nvtop $_gpu_vk ;;
                Volta|Pascal|Maxwell)
                    # Dropped by the 580 branch; 570xx is their last driver.
                    run_step "Installing NVIDIA 570xx drivers ($_fam)" pkg_install \
                        nvidia-570xx-dkms nvidia-570xx-utils lib32-nvidia-570xx-utils \
                        nvidia-570xx-settings opencl-nvidia-570xx ocl-icd nvtop $_gpu_vk ;;
                Kepler)
                    run_step "Installing NVIDIA 470xx drivers (Kepler)" pkg_install \
                        nvidia-470xx-dkms nvidia-470xx-utils lib32-nvidia-470xx-utils \
                        nvidia-470xx-settings opencl-nvidia-470xx ocl-icd nvtop $_gpu_vk ;;
                Fermi)
                    # 390xx predates NVIDIA Vulkan on Fermi — VDPAU is the accel path.
                    warn "Fermi: no Vulkan support on the 390xx branch"
                    run_step "Installing NVIDIA 390xx drivers (Fermi)" pkg_install \
                        nvidia-390xx-dkms nvidia-390xx-utils lib32-nvidia-390xx-utils \
                        nvidia-390xx-settings opencl-nvidia-390xx ocl-icd libvdpau vdpauinfo ;;
                Tesla)
                    warn "Tesla: no Vulkan support on the 340xx branch"
                    run_step "Installing NVIDIA 340xx drivers (Tesla)" pkg_install \
                        nvidia-340xx-dkms nvidia-340xx-utils lib32-nvidia-340xx-utils \
                        nvidia-340xx-settings opencl-nvidia-340xx ocl-icd libvdpau vdpauinfo ;;
                *)
                    # Curie and older: no proprietary branch survives, nouveau is it.
                    warn "$_fam ($_chip) has no maintained NVIDIA driver - using nouveau, no Vulkan/OpenCL"
                    run_step "Installing nouveau ($_fam)" pkg_install \
                        mesa lib32-mesa xf86-video-nouveau libvdpau-va-gl libva-utils ;;
            esac ;;
        AMD)
            _fam=$(_gpu_family_amd "$_chip")
            info "AMD chip='${_chip:-unknown}' family=$_fam"
            case "$_fam" in
                Navi|Vega|Polaris|"Volcanic Islands"|Unknown)
                    run_step "Installing AMD drivers ($_fam)" pkg_install \
                        mesa lib32-mesa vulkan-radeon lib32-vulkan-radeon xf86-video-amdgpu \
                        opencl-mesa ocl-icd libva-utils vdpauinfo libvdpau-va-gl nvtop $_gpu_vk ;;
                "Sea Islands"|"Southern Islands")
                    # GCN 1/2 boot on the radeon DDX by default (amdgpu needs
                    # amdgpu.si_support=1 / cik_support=1 + radeon.*_support=0);
                    # RADV works on either KMS driver, so ship both DDX paths.
                    run_step "Installing AMD GCN1/2 drivers ($_fam)" pkg_install \
                        mesa lib32-mesa vulkan-radeon lib32-vulkan-radeon \
                        xf86-video-ati xf86-video-amdgpu opencl-mesa ocl-icd \
                        libva-utils vdpauinfo libvdpau-va-gl $_gpu_vk ;;
                Evergreen|"Northern Islands"|R700|R600)
                    # TeraScale: r600 gallium, no Vulkan (RADV is GCN+).
                    warn "$_fam is pre-GCN - no Vulkan, OpenCL is unsupported"
                    run_step "Installing AMD TeraScale drivers ($_fam)" pkg_install \
                        mesa lib32-mesa xf86-video-ati libvdpau lib32-libvdpau \
                        libvdpau-va-gl libva-utils vdpauinfo ;;
                R500|R400|R300)
                    warn "$_fam is pre-GCN - no Vulkan, OpenCL is unsupported"
                    run_step "Installing ATI r300 drivers ($_fam)" pkg_install \
                        mesa lib32-mesa xf86-video-ati libvdpau-va-gl libva-utils ;;
                R100)
                    # Fixed-function era: dropped from mainline mesa, amber only.
                    warn "$_fam predates programmable shaders - mesa-amber, no Vulkan/OpenCL"
                    run_step "Installing ATI amber drivers ($_fam)" pkg_install \
                        mesa-amber lib32-mesa-amber xf86-video-ati ;;
            esac ;;
        Intel)
            _fam=$(_gpu_family_intel "$_chip")
            info "Intel chip='${_chip:-unknown}' family=$_fam"
            case "$_fam" in
                modern)
                    run_step "Installing Intel drivers" pkg_install \
                        mesa lib32-mesa vulkan-intel lib32-vulkan-intel \
                        intel-media-driver libva-utils intel-gpu-tools $_gpu_vk ;;
                crocus)
                    # gen6-7.5: crocus for GL, hasvk (shipped in vulkan-intel)
                    # for gen7.5 only, i965 VA-API for video.
                    warn "pre-Broadwell Intel: Vulkan is hasvk-only (gen7.5) and partial"
                    run_step "Installing Intel crocus drivers" pkg_install \
                        mesa lib32-mesa vulkan-intel lib32-vulkan-intel \
                        libva-intel-driver lib32-libva-intel-driver libva-utils $_gpu_vk ;;
                amber)
                    warn "gen3-5 Intel is mesa-amber only - no Vulkan"
                    run_step "Installing Intel amber drivers" pkg_install \
                        mesa-amber lib32-mesa-amber xf86-video-intel libva-utils ;;
            esac ;;
        VMware)
            run_step "Installing VMware GPU drivers" pkg_install \
                open-vm-tools mesa lib32-mesa xf86-video-vmware \
                vulkan-virtio lib32-vulkan-virtio $_gpu_vk ;;
        VirtualBox)
            run_step "Installing VirtualBox GPU drivers" pkg_install \
                virtualbox-guest-utils mesa lib32-mesa vulkan-swrast $_gpu_vk ;;
        QEMU)
            # virtio-gpu with venus/virgl passthrough, or plain software GL.
            run_step "Installing QEMU/virtio GPU drivers" pkg_install \
                mesa lib32-mesa vulkan-virtio lib32-vulkan-virtio vulkan-swrast $_gpu_vk ;;
        Microsoft)
            # Hyper-V / WSLg: hyperv_drm is in-kernel, only userspace is needed.
            run_step "Installing Hyper-V GPU drivers" pkg_install \
                mesa lib32-mesa vulkan-swrast $_gpu_vk ;;
        Cirrus|Unknown|"")
            warn "no vendor-specific driver for GPU '$_v' - installing software rendering only"
            run_step "Installing software rendering fallback" pkg_install \
                mesa lib32-mesa vulkan-swrast $_gpu_vk ;;
        *)
            warn "unknown/unsupported GPU vendor '$_v' - skipping driver install" ;;
    esac
done
