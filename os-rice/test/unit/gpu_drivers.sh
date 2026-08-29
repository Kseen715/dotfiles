#!/bin/sh
# Proves modules/gpu-drivers.c picks the right driver stack per GPU generation:
# the chip codename from detect.sh (OSR_GPU_DEVICES) routes each vendor to a
# family, and the family to a package set. Hermetic — lspci is a PATH mock and
# `sudo` logs the install command without running it, so no GPU and no packages
# are touched.
#
# The module is C now, so it runs through the core with the facts osr_detect_gpu
# just produced handed to it in the environment; what is observed is the package
# manager command the escalation log recorded.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_PKG=pacman
NO_COLOR=1; OSR_ARCH=$(uname -m); export OSR_ARCH
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/detect.sh"
. "$HERE/../lib.sh"

BIN=$(mktemp -d); PATH="$BIN:$PATH"; export PATH
mkfake() { printf '#!/bin/sh\n%s\n' "$2" > "$BIN/$1"; chmod +x "$BIN/$1"; }
OUT=$(mktemp)

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip gpu_drivers: %s is not built\n' "$OSR_BIN"
    exit 0
fi

# The stub bin the module runs inside. `sudo` records the install command and
# stops there, which is the whole observation: which package set was chosen.
MBIN=$(mktemp -d)
for _t in sh env cat grep sed printf id rm mkdir mktemp test true false tee \
          cp chmod cut tr head sort wc dirname basename; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in /*) ln -sf "$_p" "$MBIN/$_t" ;; esac
done
cat >"$MBIN/sudo" <<'EOF'
#!/bin/sh
printf 'PKG %s\n' "$*" >>"$OUT"
exit 0
EOF
printf '#!/bin/sh\n[ "$1" = "-Q" ] && exit 1\nexit 0\n' >"$MBIN/pacman"
# The legacy NVIDIA branches are `aur:` rows, and an AUR install runs as the
# user rather than through sudo - so the helper logs for itself.
cat >"$MBIN/paru" <<'EOF'
#!/bin/sh
printf 'PKG %s\n' "$*" >>"$OUT"
exit 0
EOF
chmod +x "$MBIN/paru"
printf '#!/bin/sh\nexit 1\n' >"$MBIN/dpkg"
printf '#!/bin/sh\nexit 0\n' >"$MBIN/apt-get"
chmod +x "$MBIN/sudo" "$MBIN/pacman" "$MBIN/dpkg" "$MBIN/apt-get"

# run_module — the C module, with the facts osr_detect_gpu just produced.
run_module() {
    env -i PATH="$MBIN" OUT="$OUT" OSR_ROOT="$OSR_ROOT" OSR_LIB="$OSR_LIB" \
        OSR_PKG="${OSR_PKG:-pacman}" OSR_ARCH="$OSR_ARCH" OSR_DISTRO=arch \
        OSR_CODENAME= OSR_VERSION_ID= OSR_INIT=systemd OSR_USER=tester \
        OSR_HOME="$BIN" HOME="$BIN" NO_COLOR=1 TERM=dumb \
        OSR_GPU_VENDOR="$OSR_GPU_VENDOR" OSR_GPU_DEVICES="$OSR_GPU_DEVICES" \
        OSR_GPU_COUNT="${OSR_GPU_COUNT:-1}" \
        "$OSR_BIN" module run gpu-drivers >/dev/null 2>&1 || :
}

# gpu_case <lspci-device-field> <vendor-field> — detect, then run the module.
gpu_case() {
    : >"$OUT"
    mkfake lspci "printf '01:00.0 \"VGA compatible controller\" \"%s\" \"%s\" -ra1 \"Sub\" \"Device 1\"\n' '$2' '$1'"
    osr_detect_gpu
    run_module
}

# --- chip codename extraction (detect.sh) ------------------------------------
gpu_case 'GA104 [GeForce RTX 3070]' 'NVIDIA Corporation'
assert_eq "GA104" "$(osr_gpu_chip NVIDIA)" "codename taken from the left of the bracket"
gpu_case 'Cezanne' 'Advanced Micro Devices, Inc. [AMD/ATI]'
assert_eq "Cezanne" "$(osr_gpu_chip AMD)" "unbracketed device string is the codename"
assert_eq "" "$(osr_gpu_chip NVIDIA)" "absent vendor has no chip"

# --- NVIDIA: one card per generation, each on its own driver branch ----------
gpu_case 'AD102 [GeForce RTX 4090]' 'NVIDIA Corporation'
assert_contains "$OUT" 'nvidia-open-dkms' "Ada gets the open (Turing+) driver"

gpu_case 'GP104 [GeForce GTX 1080]' 'NVIDIA Corporation'
assert_contains "$OUT" 'nvidia-570xx-dkms' "Pascal gets the 570xx branch"
refute_contains "$OUT" 'nvidia-open-dkms' "Pascal never gets open-dkms (needs a GSP)"

gpu_case 'GM204 [GeForce GTX 970]' 'NVIDIA Corporation'
assert_contains "$OUT" 'nvidia-570xx-dkms' "Maxwell gets the 570xx branch"

gpu_case 'GK104 [GeForce GTX 680]' 'NVIDIA Corporation'
assert_contains "$OUT" 'nvidia-470xx-dkms' "Kepler gets the 470xx branch"

gpu_case 'GF114 [GeForce GTX 560 Ti]' 'NVIDIA Corporation'
assert_contains "$OUT" 'nvidia-390xx-dkms' "Fermi gets the 390xx branch"
refute_contains "$OUT" 'vulkan-icd-loader' "Fermi skips Vulkan (unsupported on 390xx)"

gpu_case 'G92 [GeForce 9800 GT]' 'NVIDIA Corporation'
assert_contains "$OUT" 'nvidia-340xx-dkms' "Tesla gets the 340xx branch"

gpu_case 'NV43 [GeForce 6600 GT]' 'NVIDIA Corporation'
assert_contains "$OUT" 'xf86-video-nouveau' "Curie falls back to nouveau"

# A chip too new for the installed lspci reads as unknown -> current driver,
# never a legacy branch.
gpu_case 'Device 2c05' 'NVIDIA Corporation'
assert_contains "$OUT" 'nvidia-open-dkms' "unnamed NVIDIA chip defaults to current"

# --- AMD: amdgpu / GCN1-2 / TeraScale / r300 / amber ------------------------
gpu_case 'Navi 22 [Radeon RX 6700 XT]' 'Advanced Micro Devices, Inc. [AMD/ATI]'
assert_contains "$OUT" 'vulkan-radeon' "Navi gets RADV"
assert_contains "$OUT" 'xf86-video-amdgpu' "Navi gets the amdgpu DDX"

gpu_case 'TAHITI [Radeon HD 7970]' 'Advanced Micro Devices, Inc. [AMD/ATI]'
assert_contains "$OUT" 'xf86-video-ati' "GCN1 also gets the radeon DDX (its kernel default)"
assert_contains "$OUT" 'vulkan-radeon' "GCN1 still gets RADV"

gpu_case 'CAYMAN [Radeon HD 6970]' 'Advanced Micro Devices, Inc. [AMD/ATI]'
assert_contains "$OUT" 'xf86-video-ati' "TeraScale gets the radeon DDX"
refute_contains "$OUT" 'vulkan-radeon' "TeraScale gets no Vulkan (RADV is GCN+)"

# lspci prints the mobile board codename, not the ASIC one, for the whole
# TeraScale mobile line - "Whistler" is Turks. Without a row for those names the
# chip classified as Unknown and fell into the GCN branch, which installs RADV
# on hardware RADV does not support.
gpu_case 'Whistler [Radeon HD 6730M/6770M/7690M XT]' 'Advanced Micro Devices, Inc. [AMD/ATI]'
assert_contains "$OUT" 'xf86-video-ati' "TeraScale mobile codename gets the radeon DDX"
refute_contains "$OUT" 'vulkan-radeon' "TeraScale mobile gets no Vulkan"

gpu_case 'Park [Mobility Radeon HD 5430]' 'Advanced Micro Devices, Inc. [AMD/ATI]'
refute_contains "$OUT" 'vulkan-radeon' "Evergreen mobile codename gets no Vulkan"

gpu_case 'RV370 [Radeon X600]' 'Advanced Micro Devices, Inc. [AMD/ATI]'
assert_contains "$OUT" 'mesa ' "r300-era gets mainline mesa"
refute_contains "$OUT" 'mesa-amber' "r300-era is not amber"

gpu_case 'RV200 [Radeon 7500]' 'Advanced Micro Devices, Inc. [AMD/ATI]'
assert_contains "$OUT" 'mesa-amber' "R100/R200 gets mesa-amber"

# --- Intel: iris / crocus / amber -------------------------------------------
gpu_case 'Alder Lake-P GT2 [Iris Xe Graphics]' 'Intel Corporation'
assert_contains "$OUT" 'vulkan-intel' "modern Intel gets ANV"
assert_contains "$OUT" 'intel-media-driver' "modern Intel gets the iHD VA-API driver"

gpu_case '3rd Gen Core processor Graphics Controller' 'Intel Corporation'
assert_contains "$OUT" 'libva-intel-driver' "Ivy Bridge gets the i965 VA-API driver"
refute_contains "$OUT" 'intel-media-driver' "Ivy Bridge does not get iHD (Broadwell+)"

gpu_case '82945G/GZ Integrated Graphics Controller' 'Intel Corporation'
assert_contains "$OUT" 'mesa-amber' "gen3 Intel gets mesa-amber"

# --- VM vendors --------------------------------------------------------------
gpu_case 'SVGA II Adapter' 'VMware'
assert_contains "$OUT" 'open-vm-tools' "VMware gets the guest tools"

gpu_case 'Virtio GPU' 'Red Hat, Inc.'
assert_contains "$OUT" 'vulkan-virtio' "QEMU/virtio gets the venus driver"

# --- multi-GPU: both vendors served in one run ------------------------------
: >"$OUT"
mkfake lspci 'cat <<EOF
00:02.0 "VGA compatible controller" "Intel Corporation" "Raptor Lake-S GT1 [UHD Graphics 770]" -r04 "Sub" "Device 1"
01:00.0 "3D controller" "NVIDIA Corporation" "AD107M [GeForce RTX 4060 Max-Q]" -ra1 "Sub" "Device 2"
EOF'
osr_detect_gpu
run_module
assert_contains "$OUT" 'vulkan-intel' "hybrid laptop: Intel iGPU served"
assert_contains "$OUT" 'nvidia-open-dkms' "hybrid laptop: NVIDIA dGPU served"

# --- non-Arch host still installs (names are Arch's, pkgmap absorbs the rest) -
# The logical names above are Arch's; on apt they resolve to the Debian ones, so
# what is asserted here is that the module RAN rather than skipped - the whole
# point of not gating it on the package manager.
gpu_case 'Navi 33 [Radeon RX 7600]' 'Advanced Micro Devices, Inc. [AMD/ATI]'
: >"$OUT"
OSR_PKG=apt
run_module
assert_contains "$OUT" 'mesa-vulkan-drivers' \
    "module still runs off Arch instead of skipping (apt's name for RADV)"
OSR_PKG=pacman

rm -rf "$BIN" "$MBIN"; rm -f "$OUT"
finish
