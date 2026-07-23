# lib/detect.sh — detect the host once (POSIX sh)
#
# Sets OSR_DISTRO, OSR_PKG, OSR_INIT. CPU/GPU/virt detection folds in here later
# (§7); the MVP needs distro + package manager + init.

osr_detect() {
    OSR_DISTRO=""
    OSR_PKG=""
    OSR_INIT=""

    # Distro id from /etc/os-release (ID and ID_LIKE).
    _osr_like=""
    if [ -r /etc/os-release ]; then
        # ID_LIKE is absent on Debian/Arch — default-expand so `set -u` is happy.
        OSR_DISTRO=$(. /etc/os-release 2>/dev/null && printf '%s' "${ID:-}")
        _osr_like=$(. /etc/os-release 2>/dev/null && printf '%s' "${ID_LIKE:-}")
    fi

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
    else
        case " $OSR_DISTRO $_osr_like " in
            *" debian "*|*" ubuntu "*)   OSR_PKG=apt ;;
            *" fedora "*|*" rhel "*)     OSR_PKG=dnf ;;
            *" arch "*)                  OSR_PKG=pacman ;;
            *" alpine "*)                OSR_PKG=apk ;;
            *" void "*)                  OSR_PKG=xbps ;;
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

    export OSR_DISTRO OSR_PKG OSR_INIT

    [ -n "$OSR_PKG" ] || warn "could not detect a package manager (OSR_DISTRO='$OSR_DISTRO')"
}
