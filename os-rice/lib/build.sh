# lib/build.sh — source: provider build functions (§4). Each is a shell function
# in scope that installs a program a native package can't provide on some target,
# with its own idempotency owned by _via_source's `command -v <name>` probe.
#
# These are the escape hatch for §1a rows like `lsd@jammy = source:build_lsd_deb`:
# a package that is native on most targets but needs a different method on one
# release. The builder resolves version (github_latest, G4) and arch
# (OSR_ARCH_DEB, G8) itself, so the map row and rice.list stay logic-free.

# _osr_install_tarball_bin <url> <binary> — fetch a .tar.gz release, find the
# named binary anywhere inside, and install it to /usr/local/bin. dpkg-free, so
# it works where a modern zstd .deb can't (Debian bullseye's dpkg lacks zstd).
_osr_install_tarball_bin() {
    _it_url=$1; _it_bin=$2
    _it_tmp=$(mktemp -d)
    osr_download "$_it_url" "$_it_tmp/pkg.tar.gz" || { rm -rf "$_it_tmp"; error "failed to download $_it_url"; }
    tar -xzf "$_it_tmp/pkg.tar.gz" -C "$_it_tmp" || { rm -rf "$_it_tmp"; error "failed to extract $_it_url"; }
    _it_path=$(find "$_it_tmp" -type f -name "$_it_bin" | head -n 1)
    [ -n "$_it_path" ] || { rm -rf "$_it_tmp"; error "$_it_bin not found in $_it_url"; }
    as_root install -m 0755 "$_it_path" "/usr/local/bin/$_it_bin"
    rm -rf "$_it_tmp"
}

# build_lsd_tarball — lsd binary from the release .tar.gz (for old dpkg, §bullseye).
build_lsd_tarball() {
    _lt_tag=$(github_latest lsd-rs/lsd)                  # v1.2.0
    case "$OSR_ARCH" in
        x86_64)  _lt_a=x86_64-unknown-linux-gnu ;;
        aarch64) _lt_a=aarch64-unknown-linux-gnu ;;
        *)       error "no lsd tarball for arch $OSR_ARCH" ;;
    esac
    _osr_install_tarball_bin \
        "https://github.com/lsd-rs/lsd/releases/download/${_lt_tag}/lsd-${_lt_tag}-${_lt_a}.tar.gz" lsd
}

# build_fastfetch_tarball — fastfetch binary from the release .tar.gz (old dpkg).
build_fastfetch_tarball() {
    _ft_tag=$(github_latest fastfetch-cli/fastfetch)    # 2.66.0
    case "$OSR_ARCH" in
        x86_64)  _ft_a=amd64 ;;
        aarch64) _ft_a=aarch64 ;;
        *)       error "no fastfetch tarball for arch $OSR_ARCH" ;;
    esac
    _osr_install_tarball_bin \
        "https://github.com/fastfetch-cli/fastfetch/releases/download/${_ft_tag}/fastfetch-linux-${_ft_a}.tar.gz" fastfetch
}

# build_fastfetch_deb — install fastfetch from its official prebuilt .deb
# (fastfetch-cli/fastfetch releases). The "easiest method" on Debian/Ubuntu
# releases that don't package it natively; native distros (arch/fedora/void/
# alpine/gentoo) install it straight from their repos, no builder. fastfetch's
# asset arch naming is mixed (amd64 for x86, aarch64 for arm) — resolve inline.
build_fastfetch_deb() {
    _bf_tag=$(github_latest fastfetch-cli/fastfetch)   # e.g. 2.66.0 (no v prefix)
    case "$OSR_ARCH" in
        x86_64)  _bf_a=amd64 ;;
        aarch64) _bf_a=aarch64 ;;
        armv7l)  _bf_a=armv7l ;;
        *)       _bf_a=$OSR_ARCH_DEB ;;
    esac
    _bf_deb="fastfetch-linux-${_bf_a}.deb"
    _bf_url="https://github.com/fastfetch-cli/fastfetch/releases/download/${_bf_tag}/${_bf_deb}"
    _bf_tmp="${TMPDIR:-/tmp}/${_bf_deb}"
    osr_download "$_bf_url" "$_bf_tmp" || error "failed to download $_bf_url"
    as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y "$_bf_tmp"
    _bf_rc=$?
    rm -f "$_bf_tmp"
    check_error "$_bf_rc" "failed to install fastfetch from $_bf_deb"
}

# build_lsd_deb — install lsd from its official prebuilt .deb (lsd-rs/lsd
# releases). For Debian/Ubuntu releases too old to ship lsd natively (jammy).
# apt-get install of a local .deb pulls any deps; glibc build (not -musl).
build_lsd_deb() {
    _bl_tag=$(github_latest lsd-rs/lsd)          # e.g. v1.2.0
    _bl_ver=${_bl_tag#v}                          # 1.2.0
    _bl_deb="lsd_${_bl_ver}_${OSR_ARCH_DEB}.deb"  # lsd_1.2.0_amd64.deb
    _bl_url="https://github.com/lsd-rs/lsd/releases/download/${_bl_tag}/${_bl_deb}"
    _bl_tmp="${TMPDIR:-/tmp}/${_bl_deb}"
    osr_download "$_bl_url" "$_bl_tmp" || error "failed to download $_bl_url"
    as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y "$_bl_tmp"
    _bl_rc=$?
    rm -f "$_bl_tmp"
    check_error "$_bl_rc" "failed to install lsd from $_bl_deb"
}
