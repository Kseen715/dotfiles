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

# --- CPU: fake lscpu (Intel, 2 sockets × 4 cores, SMT -> 16 threads) ---------
mkfake lscpu 'cat <<EOF
Architecture:            x86_64
CPU(s):                  16
Vendor ID:               GenuineIntel
Model name:              Intel(R) Core(TM) i7-9700K CPU @ 3.60GHz
Thread(s) per core:      2
Core(s) per socket:      4
Socket(s):               2
EOF'
osr_detect_cpu
assert_eq "GenuineIntel" "$OSR_CPU_VENDOR" "cpu vendor parsed from lscpu"
assert_eq "8" "$OSR_CPU_CORES" "physical cores = sockets x cores-per-socket"
assert_eq "16" "$OSR_CPU_THREADS" "logical threads from CPU(s)"
assert_eq "x86_64" "$OSR_CPU_ARCH" "cpu arch parsed"
assert_contains_str() { case "$1" in *"$2"*) ok "$3" ;; *) fail "$3 (got '$1')" ;; esac; }
assert_contains_str "$OSR_CPU_MODEL" "i7-9700K" "cpu model parsed"

# No socket/core topology in lscpu (some VMs, some ARM) -> cores fall back to threads.
mkfake lscpu 'printf "Architecture: x86_64\nCPU(s):                  4\n"'
osr_detect_cpu
assert_eq "4" "$OSR_CPU_CORES" "cores fall back to thread count without topology"

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
mkfake lspci 'exit 1'                    # no usable lspci (host's real one must not leak in)
DRM=$(mktemp -d); OSR_DRM="$DRM"; export OSR_DRM
mkdir -p "$DRM/card0/device"; printf '0x1002\n' > "$DRM/card0/device/vendor"   # AMD
osr_detect_gpu
assert_eq "AMD" "$OSR_GPU_VENDOR" "AMD GPU detected from sysfs DRM PCI id (no lspci)"
assert_eq "1" "$OSR_GPU_COUNT" "one GPU counted from sysfs"
unset OSR_DRM

# --- RAM: /proc/meminfo size, DMI type 17 for type/speed/sticks/channels -----
MI=$(mktemp); printf 'MemTotal:       32756432 kB\nMemFree: 100 kB\n' > "$MI"
OSR_MEMINFO="$MI"; export OSR_MEMINFO
mkfake dmidecode 'cat <<EOF
Memory Device
	Size: 16384 MB
	Locator: Controller0-ChannelA-DIMM0
	Type: DDR4
	Speed: 3200 MT/s
	Configured Memory Speed: 3200 MT/s

Memory Device
	Size: 16384 MB
	Locator: Controller0-ChannelB-DIMM0
	Type: DDR4
	Speed: 3200 MT/s

Memory Device
	Size: No Module Installed
	Locator: Controller0-ChannelC-DIMM0
	Type: Unknown
EOF'
osr_detect_ram
assert_eq "31.23GiB" "$OSR_RAM_TOTAL" "ram total with fraction from meminfo"
assert_eq "DDR4" "$OSR_RAM_TYPE" "ram type from DMI"
assert_eq "3200MT/s" "$OSR_RAM_SPEED" "ram speed from DMI"
assert_eq "2" "$OSR_RAM_STICKS" "empty slot not counted as a stick"
assert_eq "2" "$OSR_RAM_CHANNELS" "channels counted from populated slots only"

# Unprivileged dmidecode: banner on stdout, no records, exit 1 -> size only.
mkfake dmidecode 'echo "# dmidecode 3.6"; echo "Scanning /dev/mem for entry point."; exit 1'
osr_detect_ram
assert_eq "31.23GiB" "$OSR_RAM_TOTAL" "ram total still detected without DMI"
assert_eq "" "$OSR_RAM_TYPE" "ram type empty without DMI"
assert_eq "0" "$OSR_RAM_STICKS" "stick count zero without DMI"
unset OSR_MEMINFO

# --- NPU: kernel accel subsystem (Intel VPU), then lspci class ---------------
ACC=$(mktemp -d); OSR_ACCEL="$ACC"; export OSR_ACCEL
mkdir -p "$ACC/accel0/device"; printf '0x8086\n' > "$ACC/accel0/device/vendor"
osr_detect_npu
assert_eq "Intel" "$OSR_NPU_VENDOR" "Intel NPU from /sys/class/accel"
assert_eq "1" "$OSR_NPU_COUNT" "one NPU counted from sysfs"

OSR_ACCEL="$ACC/empty"                       # no accel subsystem -> lspci path
mkfake lspci 'cat <<EOF
c5:00.0 "Processing accelerators" "Advanced Micro Devices, Inc. [AMD]" "AMD IPU Device" -r10 "AMD" "Device 1"
EOF'
osr_detect_npu
assert_eq "AMD" "$OSR_NPU_VENDOR" "AMD NPU from lspci processing accelerators"

mkfake lspci 'exit 1'
osr_detect_npu
assert_eq "" "$OSR_NPU_VENDOR" "no NPU -> empty"
unset OSR_ACCEL

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

rm -rf "$BIN" "$DRM" "$ACC" "$MI"
finish
