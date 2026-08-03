# lib/detect.sh — detect the host once (POSIX sh)
#
# Sets OSR_DISTRO, OSR_PKG, OSR_INIT, plus release/arch/config-path facets used
# by the map @qualifier resolver (§1) and preconditions (§10). CPU/GPU/virt
# detection folds in here later (§7).

osr_detect() {
    OSR_DISTRO=""
    OSR_PKG=""
    OSR_INIT=""
    OSR_CODENAME=""
    OSR_VERSION_ID=""

    # Distro id + release from /etc/os-release. CODENAME (jammy/noble) and
    # VERSION_ID (24.04 / RHEL's 9) drive the `name@release` map qualifier (G6).
    _osr_like=""
    if [ -r /etc/os-release ]; then
        # ID_LIKE is absent on Debian/Arch — default-expand so `set -u` is happy.
        OSR_DISTRO=$(. /etc/os-release 2>/dev/null && printf '%s' "${ID:-}")
        _osr_like=$(. /etc/os-release 2>/dev/null && printf '%s' "${ID_LIKE:-}")
        OSR_CODENAME=$(. /etc/os-release 2>/dev/null && printf '%s' "${VERSION_CODENAME:-}")
        OSR_VERSION_ID=$(. /etc/os-release 2>/dev/null && printf '%s' "${VERSION_ID:-}")
    fi

    # CPU arch — only artifact-fetching providers (tarball/source/github) need it;
    # native pkg managers resolve arch themselves. Two dominant naming schemes
    # (G8): raw `uname -m` and the Debian/Go/GitHub-release form.
    OSR_ARCH=$(uname -m)
    case "$OSR_ARCH" in
        x86_64)  OSR_ARCH_DEB=amd64 ;;
        aarch64) OSR_ARCH_DEB=arm64 ;;
        armv7l)  OSR_ARCH_DEB=armhf ;;
        *)       OSR_ARCH_DEB=$OSR_ARCH ;;   # unknown -> pass through
    esac

    # Package manager — probe binaries, then fall back to distro/id_like. The
    # binary probe is authoritative (a Debian derivative still has apt-get).
    if command -v apt-get >/dev/null 2>&1; then
        OSR_PKG=apt
    elif command -v dnf >/dev/null 2>&1; then
        OSR_PKG=dnf
    elif command -v pacman >/dev/null 2>&1; then
        OSR_PKG=pacman
    elif command -v apk >/dev/null 2>&1; then
        OSR_PKG=apk
    elif command -v xbps-install >/dev/null 2>&1; then
        OSR_PKG=xbps
    elif command -v emerge >/dev/null 2>&1; then
        OSR_PKG=portage
    else
        case " $OSR_DISTRO $_osr_like " in
            *" debian "*|*" ubuntu "*)   OSR_PKG=apt ;;
            *" fedora "*|*" rhel "*)     OSR_PKG=dnf ;;
            *" arch "*)                  OSR_PKG=pacman ;;
            *" alpine "*)                OSR_PKG=apk ;;
            *" void "*)                  OSR_PKG=xbps ;;
            *" gentoo "*)                OSR_PKG=portage ;;
        esac
    fi

    # Init system — drives service.sh (§8). Probe by evidence, not by PID 1 name
    # (works inside containers where PID 1 is a shell).
    if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then
        OSR_INIT=systemd
    elif command -v rc-service >/dev/null 2>&1; then
        OSR_INIT=openrc
    elif command -v sv >/dev/null 2>&1 && [ -d /var/service ]; then
        OSR_INIT=runit
    elif command -v systemctl >/dev/null 2>&1; then
        OSR_INIT=systemd
    elif command -v rc-update >/dev/null 2>&1; then
        OSR_INIT=openrc
    else
        OSR_INIT=sysvinit
    fi

    # System config base dir — varies by distro *family*, not per-package (G7).
    # Modules write to "$OSR_ETC_DEFAULT/<name>", never a literal path.
    case "$OSR_INIT" in
        openrc) OSR_ETC_DEFAULT=/etc/conf.d ;;
        *)      if [ -d /etc/sysconfig ]; then OSR_ETC_DEFAULT=/etc/sysconfig
                else OSR_ETC_DEFAULT=/etc/default; fi ;;
    esac

    export OSR_DISTRO OSR_PKG OSR_INIT OSR_CODENAME OSR_VERSION_ID \
           OSR_ARCH OSR_ARCH_DEB OSR_ETC_DEFAULT

    # Hardware facets (§7): absorb the legacy detect-cpu/virt/hwaccel bash into
    # bounded POSIX probes. Silent + command-guarded so a minimal box never errors.
    osr_detect_cpu
    osr_detect_ram
    osr_detect_gpu
    osr_detect_npu
    osr_detect_virt

    [ -n "$OSR_PKG" ] || warn "could not detect a package manager (OSR_DISTRO='$OSR_DISTRO')"
}

# --- hardware detection (POSIX port of linux-debian/src/detect-*.sh) ----------
# Sets OSR_CPU_*, OSR_RAM_*, OSR_GPU_*, OSR_NPU_*, OSR_VIRT. Bounded to what
# rices consume (CPU id, RAM, GPU/NPU vendor, virtualization); legacy Mali SoC probing is
# intentionally not ported (YAGNI — add a probe when a rice needs it). Primary paths use
# mockable commands (lscpu/lspci/systemd-detect-virt) so detection is testable;
# the sysfs DRM dir is overridable via OSR_DRM for the same reason.

# _osr_uniq_add <list> <item> — echo <list> with <item> appended iff absent.
_osr_uniq_add() {
    case " $1 " in
        *" $2 "*) printf '%s' "$1" ;;
        *)        printf '%s' "${1:+$1 }$2" ;;
    esac
}

# _osr_norm_gpu <text> — map a vendor/model string to a canonical vendor tag.
_osr_norm_gpu() {
    case "$1" in
        *NVIDIA*|*nVidia*|*GeForce*|*Quadro*|*Tesla*|*RTX*|*GTX*) echo NVIDIA ;;
        *AMD*|*ATI*|*Radeon*)                                     echo AMD ;;
        *Intel*|*"HD Graphics"*|*Iris*|*Arc*)                     echo Intel ;;
        *VMware*|*VMWARE*)                                        echo VMware ;;
        *VirtualBox*|*VBOX*)                                      echo VirtualBox ;;
        *QEMU*|*virtio*|*"Red Hat"*|*QXL*)                        echo QEMU ;;
        *Microsoft*|*Hyper-V*)                                    echo Microsoft ;;
        *Cirrus*)                                                 echo Cirrus ;;
        *)                                                        echo Unknown ;;
    esac
}

# osr_detect_cpu — vendor / model / arch / core count via lscpu (POSIX).
osr_detect_cpu() {
    OSR_CPU_VENDOR=""; OSR_CPU_MODEL=""; OSR_CPU_ARCH="$OSR_ARCH"
    OSR_CPU_CORES=0; OSR_CPU_THREADS=0
    if command -v lscpu >/dev/null 2>&1; then
        _cpu=$(lscpu 2>/dev/null)
        OSR_CPU_VENDOR=$(printf '%s\n' "$_cpu" | awk -F: '/^Vendor ID:/{gsub(/^[ \t]+/,"",$2);print $2;exit}')
        OSR_CPU_MODEL=$(printf '%s\n' "$_cpu"  | awk -F: '/^Model name:/{gsub(/^[ \t]+/,"",$2);print $2;exit}')
        _ca=$(printf '%s\n' "$_cpu"            | awk -F: '/^Architecture:/{gsub(/^[ \t]+/,"",$2);print $2;exit}')
        [ -n "$_ca" ] && OSR_CPU_ARCH="$_ca"
        # `CPU(s)` is the logical count (threads). Physical cores = sockets ×
        # cores-per-socket; falls back to threads on a CPU without SMT info.
        _ct=$(printf '%s\n' "$_cpu"            | awk -F: '/^CPU\(s\):/{gsub(/^[ \t]+/,"",$2);print $2;exit}')
        [ -n "$_ct" ] && OSR_CPU_THREADS="$_ct"
        _cps=$(printf '%s\n' "$_cpu"           | awk -F: '/^Core\(s\) per socket:/{gsub(/^[ \t]+/,"",$2);print $2;exit}')
        _sock=$(printf '%s\n' "$_cpu"          | awk -F: '/^Socket\(s\):/{gsub(/^[ \t]+/,"",$2);print $2;exit}')
        if [ -n "$_cps" ]; then OSR_CPU_CORES=$(( _cps * ${_sock:-1} )); fi
    fi
    if [ "$OSR_CPU_CORES" -eq 0 ]; then OSR_CPU_CORES=$OSR_CPU_THREADS; fi
    export OSR_CPU_VENDOR OSR_CPU_MODEL OSR_CPU_ARCH OSR_CPU_CORES OSR_CPU_THREADS
}

# osr_detect_gpu — GPU vendor list + count via lspci, falling back to sysfs DRM
# PCI vendor IDs (works with no lspci, e.g. minimal containers/headless).
osr_detect_gpu() {
    OSR_GPU_VENDOR=""; OSR_GPU_MODEL=""; OSR_GPU_COUNT=0
    _gv=""; _gm=""
    if command -v lspci >/dev/null 2>&1; then
        _lines=$(lspci -mm 2>/dev/null | grep -E "VGA compatible controller|3D controller" || true)
        _oldifs=$IFS; IFS='
'
        for _l in $_lines; do
            [ -n "$_l" ] || continue
            # lspci -mm quoting: "class" "vendor" "device" "subsys-vendor" ...
            _vendor=$(printf '%s' "$_l" | cut -d'"' -f4)
            _dev=$(printf '%s' "$_l" | cut -d'"' -f6)
            _vt=$(_osr_norm_gpu "$_vendor $_dev")
            _gv=$(_osr_uniq_add "$_gv" "$_vt")
            # lspci names a chip by codename with the marketing name bracketed
            # ("Kaby Lake-S GT2 [HD Graphics 630]") — the bracket is the part
            # anyone recognizes, so prefer it and fall back to the whole string.
            _model=$_dev
            case "$_model" in *"["*"]"*) _model=${_model#*[}; _model=${_model%%]*} ;; esac
            # Don't stutter "Intel Intel Arc A770".
            case "$_model" in "$_vt"*) ;; *) _model="$_vt $_model" ;; esac
            _gm="${_gm:+$_gm, }$_model"
            OSR_GPU_COUNT=$((OSR_GPU_COUNT + 1))
        done
        IFS=$_oldifs
    fi
    if [ -z "$_gv" ]; then
        for _vf in "${OSR_DRM:-/sys/class/drm}"/card*/device/vendor; do
            [ -f "$_vf" ] || continue
            read -r _id < "$_vf" 2>/dev/null || continue
            case "$_id" in
                0x10de) _n=NVIDIA ;; 0x1002) _n=AMD ;;   0x8086) _n=Intel ;;
                0x15ad) _n=VMware ;; 0x1af4) _n=QEMU ;;   0x1414) _n=Microsoft ;;
                *)      _n="" ;;
            esac
            [ -n "$_n" ] || continue
            _gv=$(_osr_uniq_add "$_gv" "$_n")
            OSR_GPU_COUNT=$((OSR_GPU_COUNT + 1))
        done
    fi
    OSR_GPU_VENDOR=$_gv
    OSR_GPU_MODEL=$_gm
    export OSR_GPU_VENDOR OSR_GPU_MODEL OSR_GPU_COUNT
}

# _osr_dmi17 <cmd...> — echo type-17 DMI records, or nothing. Unprivileged
# dmidecode still prints its banner ("# dmidecode 3.6", "Scanning /dev/mem") on
# stdout while failing, so emptiness is not a usable signal: demand a real
# record instead, otherwise the sudo retry would never fire.
_osr_dmi17() {
    _o=$("$@" -t 17 2>/dev/null || true)
    case "$_o" in *"Memory Device"*) printf '%s\n' "$_o" ;; esac
}

# osr_detect_ram — total size from /proc/meminfo (always readable), plus
# type/speed/stick/channel counts from DMI type 17 when dmidecode can run
# without prompting (root, or an already-cached sudo). Every field stays empty
# when unavailable; callers omit what they didn't get. Paths are overridable
# (OSR_MEMINFO) and dmidecode is a PATH command, so both halves are mockable.
osr_detect_ram() {
    OSR_RAM_TOTAL=""; OSR_RAM_TYPE=""; OSR_RAM_SPEED=""
    OSR_RAM_STICKS=0; OSR_RAM_CHANNELS=0

    _mi=${OSR_MEMINFO:-/proc/meminfo}
    if [ -r "$_mi" ]; then
        _kb=$(awk '/^MemTotal:/{print $2; exit}' "$_mi" 2>/dev/null || true)
        # Two decimals, integer math only: MemTotal is always a bit under the
        # installed size (firmware/kernel reservations), so 8GiB reads 7.19 —
        # showing the fraction makes that visible instead of surprising.
        if [ -n "$_kb" ]; then
            OSR_RAM_TOTAL=$(printf '%d.%02dGiB' \
                "$((_kb / 1048576))" "$(( (_kb % 1048576) * 100 / 1048576 ))")
        fi
    fi

    _dmi=""
    if command -v dmidecode >/dev/null 2>&1; then
        _dmi=$(_osr_dmi17 dmidecode)
        # Unprivileged: retry through a cached sudo ticket (-n never prompts).
        if [ -z "$_dmi" ] && command -v sudo >/dev/null 2>&1; then
            _dmi=$(_osr_dmi17 sudo -n dmidecode)
        fi
    fi
    if [ -n "$_dmi" ]; then
        # One pass over the type-17 records: a slot counts only once its Size
        # line shows a number ("No Module Installed" = empty slot), and the
        # Type/Speed/Locator lines that follow belong to that populated slot.
        _r=$(printf '%s\n' "$_dmi" | awk '
            /^Memory Device/ { pop = 0 }
            $1 == "Size:" { pop = ($2 ~ /^[0-9]+$/); if (pop) sticks++ }
            pop && $1 == "Type:" && type == "" && $2 ~ /^[A-Za-z]/ && $2 != "Unknown" { type = $2 }
            pop && /Speed:/ { for (i = 1; i < NF; i++)
                                  if ($i == "Speed:" && $(i+1) ~ /^[0-9]+$/ && $(i+1)+0 > speed) speed = $(i+1)+0 }
            pop && /Locator:/ && match($0, /Channel ?[A-Za-z0-9]/) { ch[substr($0, RSTART + 7)] = 1 }
            END { n = 0; for (k in ch) n++
                  printf "%d %s %d %d\n", sticks+0, (type == "" ? "-" : type), speed+0, n }')
        IFS=' ' read -r OSR_RAM_STICKS OSR_RAM_TYPE OSR_RAM_SPEED OSR_RAM_CHANNELS <<EOF
$_r
EOF
        if [ "$OSR_RAM_TYPE" = "-" ]; then OSR_RAM_TYPE=""; fi
        if [ "$OSR_RAM_SPEED" = 0 ]; then OSR_RAM_SPEED=""; else OSR_RAM_SPEED="${OSR_RAM_SPEED}MT/s"; fi
    fi
    export OSR_RAM_TOTAL OSR_RAM_TYPE OSR_RAM_SPEED OSR_RAM_STICKS OSR_RAM_CHANNELS
}

# osr_detect_npu — NPUs / dedicated accelerators. Kernel accel subsystem first
# (/sys/class/accel, driven by intel_vpu / amdxdna), then lspci's "Processing
# accelerators" class. Dir overridable via OSR_ACCEL for testability.
osr_detect_npu() {
    OSR_NPU_VENDOR=""; OSR_NPU_COUNT=0
    for _af in "${OSR_ACCEL:-/sys/class/accel}"/accel*/device/vendor; do
        [ -f "$_af" ] || continue
        read -r _id < "$_af" 2>/dev/null || continue
        case "$_id" in
            0x8086) _n=Intel ;; 0x1022|0x1002) _n=AMD ;; 0x10de) _n=NVIDIA ;;
            *)      _n=Unknown ;;
        esac
        OSR_NPU_VENDOR=$(_osr_uniq_add "$OSR_NPU_VENDOR" "$_n")
        OSR_NPU_COUNT=$((OSR_NPU_COUNT + 1))
    done
    if [ -z "$OSR_NPU_VENDOR" ] && command -v lspci >/dev/null 2>&1; then
        _lines=$(lspci -mm 2>/dev/null | grep -E "Processing accelerators" || true)
        _oldifs=$IFS; IFS='
'
        for _l in $_lines; do
            [ -n "$_l" ] || continue
            _vendor=$(printf '%s' "$_l" | cut -d'"' -f4)
            _dev=$(printf '%s' "$_l" | cut -d'"' -f6)
            OSR_NPU_VENDOR=$(_osr_uniq_add "$OSR_NPU_VENDOR" "$(_osr_norm_gpu "$_vendor $_dev")")
            OSR_NPU_COUNT=$((OSR_NPU_COUNT + 1))
        done
        IFS=$_oldifs
    fi
    export OSR_NPU_VENDOR OSR_NPU_COUNT
}

# osr_detect_virt — virtualization/container tech; prefers systemd-detect-virt,
# falls back to lscpu's hypervisor line. "none" on bare metal.
osr_detect_virt() {
    OSR_VIRT=none
    if command -v systemd-detect-virt >/dev/null 2>&1; then
        _v=$(systemd-detect-virt 2>/dev/null || true)
        [ -n "$_v" ] && OSR_VIRT=$_v
    fi
    if [ "$OSR_VIRT" = none ] && command -v lscpu >/dev/null 2>&1; then
        case "$(lscpu 2>/dev/null | grep -iE 'hypervisor vendor|vmware|virtualbox|kvm|qemu' || true)" in
            *VMware*)     OSR_VIRT=vmware ;;
            *VirtualBox*) OSR_VIRT=virtualbox ;;
            *KVM*)        OSR_VIRT=kvm ;;
            *QEMU*)       OSR_VIRT=qemu ;;
        esac
    fi
    export OSR_VIRT
}
