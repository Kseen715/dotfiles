#!/bin/sh
# Proves §7 hardware detection: osr_detect_cpu/gpu/virt parse synthetic hardware.
# lscpu/lspci/systemd-detect-virt are PATH mocks; the sysfs fallback reads a fake
# DRM tree via OSR_DRM. No real hardware touched.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB
NO_COLOR=1; OSR_ARCH=$(uname -m); export OSR_ARCH
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/detect.sh"
. "$HERE/../lib.sh"

BIN=$(mktemp -d); PATH="$BIN:$PATH"; export PATH
mkfake() { printf '#!/bin/sh\n%s\n' "$2" > "$BIN/$1"; chmod +x "$BIN/$1"; }

# --- CPU: fake lscpu (Intel, 8 cores) ----------------------------------------
mkfake lscpu 'cat <<EOF
Architecture:            x86_64
CPU(s):                  8
Vendor ID:               GenuineIntel
Model name:              Intel(R) Core(TM) i7-9700K CPU @ 3.60GHz
EOF'
osr_detect_cpu
assert_eq "GenuineIntel" "$OSR_CPU_VENDOR" "cpu vendor parsed from lscpu"
assert_eq "8" "$OSR_CPU_CORES" "cpu core count parsed"
assert_eq "x86_64" "$OSR_CPU_ARCH" "cpu arch parsed"
assert_contains_str() { case "$1" in *"$2"*) ok "$3" ;; *) fail "$3 (got '$1')" ;; esac; }
assert_contains_str "$OSR_CPU_MODEL" "i7-9700K" "cpu model parsed"

# --- GPU via lspci: NVIDIA + Intel (two devices) -----------------------------
mkfake lspci 'cat <<EOF
00:02.0 "VGA compatible controller" "Intel Corporation" "UHD Graphics 630" -r02 "Dell" "Device 0704"
01:00.0 "3D controller" "NVIDIA Corporation" "GeForce RTX 3080" -ra1 "Foo" "Device 1"
EOF'
osr_detect_gpu
assert_eq "2" "$OSR_GPU_COUNT" "two GPU devices counted via lspci"
assert_contains_str "$OSR_GPU_VENDOR" "Intel" "Intel GPU normalized"
assert_contains_str "$OSR_GPU_VENDOR" "NVIDIA" "NVIDIA GPU normalized"

# --- GPU sysfs fallback: no lspci, fake DRM tree (AMD) -----------------------
rm -f "$BIN/lspci"                       # force the sysfs path
DRM=$(mktemp -d); OSR_DRM="$DRM"; export OSR_DRM
mkdir -p "$DRM/card0/device"; printf '0x1002\n' > "$DRM/card0/device/vendor"   # AMD
osr_detect_gpu
assert_eq "AMD" "$OSR_GPU_VENDOR" "AMD GPU detected from sysfs DRM PCI id (no lspci)"
assert_eq "1" "$OSR_GPU_COUNT" "one GPU counted from sysfs"
unset OSR_DRM

# --- virt: systemd-detect-virt says vmware -----------------------------------
mkfake systemd-detect-virt 'echo vmware'
osr_detect_virt
assert_eq "vmware" "$OSR_VIRT" "virt from systemd-detect-virt"

# --- virt fallback: systemd-detect-virt reports none, lscpu shows KVM --------
# (mock stays on PATH shadowing the host's real one; "none" triggers the lscpu
# fallback exactly as bare-metal systemd-detect-virt would.)
mkfake systemd-detect-virt 'echo none; exit 1'
mkfake lscpu 'echo "Hypervisor vendor:      KVM"'
osr_detect_virt
assert_eq "kvm" "$OSR_VIRT" "virt from lscpu hypervisor line (fallback)"

# --- bare metal: no virt tools, lscpu shows no hypervisor --------------------
mkfake lscpu 'echo "Architecture:  x86_64"'
osr_detect_virt
assert_eq "none" "$OSR_VIRT" "virt=none on bare metal"

rm -rf "$BIN" "$DRM"
finish
