# lib/detect.sh — the shell-callable surface of host detection (POSIX sh)
#
# Sets OSR_DISTRO, OSR_PKG, OSR_INIT, plus the release/arch/config-path facets
# the map @qualifier resolver (§1) and the preconditions (§10) read, and the
# hardware facets (§7): CPU id, RAM, GPU/NPU vendor, virtualization.
#
# The probing is `osr detect` in the harness core (lib/detect.c) — same
# commands, same order, same fallbacks. This file exists because the answers
# have to become SHELL VARIABLES: every module branches on $OSR_PKG or
# $OSR_DISTRO, so the core prints assignments and these functions eval them.
#
# Byte-for-byte the sh original, frozen at test/ref/detect_sh_ref.sh and diffed
# by test/unit/detect_c_parity.sh.

if [ -z "${OSR_BIN:-}" ]; then
    . "${OSR_LIB:?detect.sh: source lib/ui.sh first, or export OSR_LIB}/ui.sh"
fi

# _osr_detect_eval <what> <vars...> — run one probe and export what it found.
_osr_detect_eval() {
    _de_what=$1
    shift
    eval "$("$OSR_BIN" detect "$_de_what")"
    # shellcheck disable=SC2048,SC2086  # the caller's list of names, on purpose
    export $*
}

osr_detect() {
    _osr_detect_eval all \
        OSR_DISTRO OSR_PKG OSR_INIT OSR_CODENAME OSR_VERSION_ID OSR_VERSION \
        OSR_ID_LIKE OSR_ARCH OSR_ARCH_DEB OSR_ETC_DEFAULT \
        OSR_CPU_VENDOR OSR_CPU_MODEL OSR_CPU_ARCH OSR_CPU_CORES OSR_CPU_THREADS \
        OSR_RAM_TOTAL OSR_RAM_TYPE OSR_RAM_SPEED OSR_RAM_STICKS OSR_RAM_CHANNELS \
        OSR_GPU_VENDOR OSR_GPU_MODEL OSR_GPU_COUNT OSR_GPU_DEVICES \
        OSR_NPU_VENDOR OSR_NPU_COUNT OSR_VIRT
}

# The individual probes, for the callers that re-run one of them: install.sh
# retries the RAM probe after warming a sudo ticket, because the DMI half of it
# needs root.
osr_detect_cpu() {
    _osr_detect_eval cpu OSR_CPU_VENDOR OSR_CPU_MODEL OSR_CPU_ARCH OSR_CPU_CORES OSR_CPU_THREADS
}
osr_detect_ram() {
    _osr_detect_eval ram OSR_RAM_TOTAL OSR_RAM_TYPE OSR_RAM_SPEED OSR_RAM_STICKS OSR_RAM_CHANNELS
}
osr_detect_gpu() {
    _osr_detect_eval gpu OSR_GPU_VENDOR OSR_GPU_MODEL OSR_GPU_COUNT OSR_GPU_DEVICES
}
osr_detect_npu() { _osr_detect_eval npu OSR_NPU_VENDOR OSR_NPU_COUNT; }
osr_detect_virt() { _osr_detect_eval virt OSR_VIRT; }

# osr_gpu_chip <vendor> — echo the chip codename of the first <vendor> GPU, or
# "" when unknown (no lspci, or a device lspci couldn't name). The vendor alone
# can't pick a driver: an NVIDIA card needs open-dkms, one of the legacy
# branches or nouveau depending on generation, and the codename is the only
# thing lspci gives us to tell them apart.
osr_gpu_chip() { "$OSR_BIN" detect gpu-chip "$1"; }
