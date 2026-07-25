# modules/cpu-microcodes.sh — CPU microcode package for the detected vendor. ONE
# copy, POSIX (was .../modules/cpu-microcodes.sh). Uses OSR_CPU_VENDOR from
# detect.sh. Hardware-dependent: correct only on the machine it runs on (§9).
case "${OSR_CPU_VENDOR:-}" in
    GenuineIntel) run_step "Installing Intel microcode" pkg_install intel-ucode ;;
    AuthenticAMD) run_step "Installing AMD microcode"   pkg_install amd-ucode ;;
    *)            warn "unknown CPU vendor '${OSR_CPU_VENDOR:-}' - no microcode installed" ;;
esac
