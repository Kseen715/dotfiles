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

    # System config base dir — varies by distro *family*, not per-package (G7).
    # Modules write to "$OSR_ETC_DEFAULT/<name>", never a literal path.
    case "$OSR_INIT" in
        openrc) OSR_ETC_DEFAULT=/etc/conf.d ;;
        *)      if [ -d /etc/sysconfig ]; then OSR_ETC_DEFAULT=/etc/sysconfig
                else OSR_ETC_DEFAULT=/etc/default; fi ;;
    esac

    export OSR_DISTRO OSR_PKG OSR_INIT OSR_CODENAME OSR_VERSION_ID \
           OSR_ARCH OSR_ARCH_DEB OSR_ETC_DEFAULT

    [ -n "$OSR_PKG" ] || warn "could not detect a package manager (OSR_DISTRO='$OSR_DISTRO')"
}
