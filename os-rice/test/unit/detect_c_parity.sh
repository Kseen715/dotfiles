#!/bin/sh
# Proves lib/detect.sh (now a shim over `osr detect` in the harness core) finds
# exactly what the pure-sh implementation found, frozen at
# test/ref/detect_sh_ref.sh.
#
# Two passes. First against THIS machine, unstubbed: whatever lscpu/lspci/
# dmidecode/systemd-detect-virt really say here, both implementations must
# agree on every one of the 27 variables. Then against fixtures: the probes are
# PATH commands and the sysfs/proc paths are overridable (OSR_MEMINFO, OSR_DRM,
# OSR_ACCEL) — the sh version made them so precisely to be testable — so a
# stub bin/ turns the whole detector into a pure function of its inputs. Those
# fixtures cover an Arch box with an NVIDIA card, an Alpine container with no
# lspci at all, a multi-GPU laptop, a VM, and DMI output with empty slots.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
OSR_REF="$OSR_ROOT/test/ref/detect_sh_ref.sh"; export OSR_REF
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

hex() { od -An -tx1 | tr -d ' \n'; }

VARS='OSR_DISTRO OSR_PKG OSR_INIT OSR_CODENAME OSR_VERSION_ID OSR_VERSION
      OSR_ID_LIKE OSR_ARCH OSR_ARCH_DEB OSR_ETC_DEFAULT
      OSR_CPU_VENDOR OSR_CPU_MODEL OSR_CPU_ARCH OSR_CPU_CORES OSR_CPU_THREADS
      OSR_RAM_TOTAL OSR_RAM_TYPE OSR_RAM_SPEED OSR_RAM_STICKS OSR_RAM_CHANNELS
      OSR_GPU_VENDOR OSR_GPU_MODEL OSR_GPU_COUNT OSR_GPU_DEVICES
      OSR_NPU_VENDOR OSR_NPU_COUNT OSR_VIRT'
# One line: a newline inside a `for x in ...` list ends the list, and $VARS is
# written across several for readability.
VARS_1LINE=$(printf '%s' "$VARS" | tr '\n' ' ')
DUMP='osr_detect; for v in '"$VARS_1LINE"'; do eval "printf \"%s=[%s]\n\" \"\$v\" \"\${$v:-}\""; done'
REF_PRE='. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_REF"'
NEW_PRE='. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/detect.sh"'

# compare <label> [env...] — run the whole detector both ways and diff every
# variable, plus whatever went to stderr (the "no package manager" warning).
compare() {
    _label=$1
    shift
    env "$@" sh -c "$REF_PRE; $DUMP" >"$TMP/ref.out" 2>"$TMP/ref.err" </dev/null
    env "$@" sh -c "$NEW_PRE; $DUMP" >"$TMP/c.out" 2>"$TMP/c.err" </dev/null
    if [ "$(hex <"$TMP/ref.out")" = "$(hex <"$TMP/c.out")" ]; then
        ok "$_label: all 27 facts identical"
    else
        fail "$_label: facts differ"
        diff -u "$TMP/ref.out" "$TMP/c.out" | head -20 >&2 || :
    fi
    assert_eq "$(hex <"$TMP/ref.err")" "$(hex <"$TMP/c.err")" "$_label: stderr identical"
}

# --- 1. this machine, exactly as it is ---------------------------------------
compare "real host" PATH="$PATH"

# --- 2. fixtures -------------------------------------------------------------
# stub <name> <<'EOF' body EOF — a fake probe on PATH.
BIN="$TMP/bin"
mkdir -p "$BIN"
stub() { { printf '#!/bin/sh\n'; cat; } >"$BIN/$1"; chmod +x "$BIN/$1"; }
nostub() { rm -f "$BIN/$1"; }

make_meminfo() { printf 'MemTotal:       %s kB\nMemFree:        1000 kB\n' "$1" >"$TMP/meminfo"; }
make_drm() {
    rm -rf "$TMP/drm"; mkdir -p "$TMP/drm"
    for _id in $1; do
        mkdir -p "$TMP/drm/card$_n/device" 2>/dev/null || :
        _n=$((${_n:-0} + 1))
        mkdir -p "$TMP/drm/card$_n/device"
        printf '%s\n' "$_id" >"$TMP/drm/card$_n/device/vendor"
    done
}

# The stub set every fixture starts from: nothing found.
reset_stubs() {
    for _c in lscpu lspci dmidecode systemd-detect-virt sudo apt-get dnf pacman apk \
              xbps-install emerge systemctl rc-service sv rc-update; do
        nostub "$_c"
    done
    rm -rf "$TMP/drm" "$TMP/accel"
    mkdir -p "$TMP/drm" "$TMP/accel"
    make_meminfo 16323096
}
FIX_ENV_BASE="PATH=$BIN:/usr/bin:/bin OSR_MEMINFO=$TMP/meminfo OSR_DRM=$TMP/drm OSR_ACCEL=$TMP/accel"

# (a) an Arch box with an NVIDIA card and real DMI
reset_stubs
stub pacman <<'EOF'
exit 0
EOF
stub systemctl <<'EOF'
exit 0
EOF
stub lscpu <<'EOF'
cat <<'OUT'
Architecture:                       x86_64
CPU(s):                             16
Vendor ID:                          AuthenticAMD
Model name:                         AMD Ryzen 7 5800X 8-Core Processor
Core(s) per socket:                 8
Socket(s):                          1
Hypervisor vendor:                  KVM
OUT
EOF
stub lspci <<'EOF'
cat <<'OUT'
01:00.0 "VGA compatible controller" "NVIDIA Corporation" "GA104 [GeForce RTX 3070]" -r a1 "NVIDIA Corporation" "Device 3903"
00:02.0 "Audio device" "Intel Corporation" "Sound" "" ""
OUT
EOF
stub dmidecode <<'EOF'
cat <<'OUT'
# dmidecode 3.6
Memory Device
	Size: 16384 MB
	Locator: DIMM 0
	Bank Locator: P0 CHANNEL A
	Type: DDR4
	Speed: 3200 MT/s
	Configured Memory Speed: 3200 MT/s

Memory Device
	Size: No Module Installed
	Locator: DIMM 1
	Bank Locator: P0 CHANNEL B
	Type: Unknown
	Speed: Unknown

Memory Device
	Size: 16384 MB
	Locator: DIMM 0
	Bank Locator: P0 CHANNEL B
	Type: DDR4
	Speed: 3600 MT/s
OUT
EOF
compare "fixture: arch + nvidia + DMI" $FIX_ENV_BASE

# (b) an Alpine container: no lspci, no lscpu, no dmidecode, sysfs only
reset_stubs
stub apk <<'EOF'
exit 0
EOF
stub rc-service <<'EOF'
exit 0
EOF
mkdir -p "$TMP/drm/card0/device" "$TMP/drm/card1/device"
printf '0x1002\n' >"$TMP/drm/card0/device/vendor"
printf '0x8086\n' >"$TMP/drm/card1/device/vendor"
mkdir -p "$TMP/accel/accel0/device"
printf '0x8086\n' >"$TMP/accel/accel0/device/vendor"
make_meminfo 4020000
compare "fixture: alpine, sysfs only" $FIX_ENV_BASE

# (c) nothing at all: no package manager (the warn line), no init evidence
reset_stubs
compare "fixture: bare box, no pkg manager" $FIX_ENV_BASE

# (d) a VM with two GPUs and systemd-detect-virt
reset_stubs
stub apt-get <<'EOF'
exit 0
EOF
stub systemd-detect-virt <<'EOF'
echo vmware
EOF
stub lspci <<'EOF'
cat <<'OUT'
00:0f.0 "VGA compatible controller" "VMware" "SVGA II Adapter" "" ""
02:00.0 "3D controller" "NVIDIA Corporation" "GP104GLM [Quadro P3000 Mobile]" -ra1 "Dell" "Device 07be"
00:1e.0 "Processing accelerators" "Intel Corporation" "Neural Processor" "" ""
OUT
EOF
compare "fixture: VM, two GPUs, an NPU" $FIX_ENV_BASE

# (e) dmidecode only works through sudo -n (the unprivileged retry)
reset_stubs
stub dnf <<'EOF'
exit 0
EOF
stub dmidecode <<'EOF'
printf '# dmidecode 3.6\nScanning /dev/mem for entry point.\n'
printf '/dev/mem: Permission denied\n' >&2
exit 1
EOF
stub sudo <<'EOF'
# only `sudo -n <dmidecode> -t 17` works here
[ "$1" = "-n" ] || exit 1
cat <<'OUT'
Memory Device
	Size: 8192 MB
	Locator: ChannelA-DIMM0
	Type: LPDDR4X
	Speed: 4267 MT/s
OUT
EOF
compare "fixture: DMI only via sudo -n" $FIX_ENV_BASE

# (f) an Intel brand string with its padding intact. Intel's is a fixed-width
# field, so the runs of spaces inside the name are what lscpu really prints on
# those parts -- and the model is the one field where both implementations
# squeeze rather than merely trim. Nothing else here is interesting, which is
# the point: this fixture exists for OSR_CPU_MODEL.
reset_stubs
stub lscpu <<'EOF'
cat <<'OUT'
Architecture:                       x86_64
CPU(s):                             8
Vendor ID:                          GenuineIntel
Model name:                         Intel(R) Core(TM) i7-8550U  CPU @ 1.80GHz
Core(s) per socket:                 4
Socket(s):                          1
OUT
EOF
compare "fixture: Intel brand string with interior padding" $FIX_ENV_BASE
_m=$(env $FIX_ENV_BASE sh -c '. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/detect.sh"; osr_detect >/dev/null 2>&1; printf "%s" "$OSR_CPU_MODEL"')
assert_eq "Intel(R) Core(TM) i7-8550U CPU @ 1.80GHz" "$_m" "the padding is squeezed, not just trimmed"

# --- 3. osr_gpu_chip ---------------------------------------------------------
# modules/gpu-drivers.sh reads this to pick a driver branch.
DEVICES='NVIDIA|GA104
AMD|Navi 22
Intel|'
for _v in NVIDIA AMD Intel VMware; do
    _r=$(printf '%s\n' "$DEVICES" | awk -F'|' -v v="$_v" '$1 == v { print $2; exit }' | hex)
    _c=$(OSR_GPU_DEVICES="$DEVICES" "$OSR_BIN" detect gpu-chip "$_v" | hex)
    assert_eq "$_r" "$_c" "osr_gpu_chip $_v"
done

finish
