# lib/build.sh — source: provider build functions (§4). Each is a shell function
# in scope that installs a program a native package can't provide on some target,
# with its own idempotency owned by _via_source's `command -v <name>` probe.
#
# These are the escape hatch for §1a rows like `lsd@jammy = source:build_lsd_deb`:
# a package that is native on most targets but needs a different method on one
# release. The builder resolves version (github_latest, G4) and arch
# (OSR_ARCH_DEB, G8) itself, so the map row and rice.list stay logic-free.

# _osr_install_tarball_bin <url> <binary> — fetch a release tarball, find the
# named binary anywhere inside, and install it to /usr/local/bin. dpkg-free, so
# it works where a modern zstd .deb can't (Debian bullseye's dpkg lacks zstd).
# `tar -xf` auto-detects the compression (.gz/.xz/.bz2/.tbz).
_osr_install_tarball_bin() {
    _it_url=$1; _it_bin=$2
    _it_tmp=$(mktemp -d)
    osr_download "$_it_url" "$_it_tmp/pkg.tar" || { rm -rf "$_it_tmp"; error "failed to download $_it_url"; }
    tar -xf "$_it_tmp/pkg.tar" -C "$_it_tmp" || { rm -rf "$_it_tmp"; error "failed to extract $_it_url"; }
    _it_path=$(find "$_it_tmp" -type f -name "$_it_bin" | head -n 1)
    [ -n "$_it_path" ] || { rm -rf "$_it_tmp"; error "$_it_bin not found in $_it_url"; }
    as_root install -m 0755 "$_it_path" "/usr/local/bin/$_it_bin"
    rm -rf "$_it_tmp"
}

# build_paru — bootstrap the paru AUR helper from the AUR (source:build_paru).
# The chicken/egg package: the one AUR package that cannot come *from* an AUR
# helper. Clone its PKGBUILD and makepkg it as OSR_USER (makepkg refuses root);
# every later aur: row then dispatches through paru. Arch-only. Idempotency is
# _via_source's `command -v paru` probe, so a rerun with paru present is a no-op.
build_paru() {
    pkg_install build git                # base-devel + git: the makepkg toolchain
    _bp_repo="${TMPDIR:-/tmp}/osr-paru-build"
    as_user rm -rf "$_bp_repo"
    as_user git clone --depth 1 https://aur.archlinux.org/paru.git "$_bp_repo" \
        || error "failed to clone paru AUR repo"
    ( cd "$_bp_repo" && as_user makepkg -si --needed --noconfirm ) \
        || { as_user rm -rf "$_bp_repo"; error "paru build failed"; }
    as_user rm -rf "$_bp_repo"
}

# build_zig [version] — install Zig from ziglang.org as a whole tree (it needs
# its lib/ beside the binary), symlinked into /usr/local/bin. For distros/apt
# releases without a native zig. The exact tarball URL is resolved from
# index.json (the asset naming changed across versions: zig-<arch>-linux on
# 0.15+, zig-linux-<arch> on <=0.14 - the regex matches both). No arg = latest
# stable; an arg pins a version (Ghostty needs an exact one). Declares its own
# xz prerequisite (the tarball is .tar.xz), §1a.
build_zig() {
    _zg_want=${1:-${ZIG_VERSION:-}}
    case "$OSR_ARCH" in
        x86_64)  _zg_m=x86_64 ;;
        aarch64) _zg_m=aarch64 ;;
        *)       error "no zig tarball for arch $OSR_ARCH" ;;
    esac
    pkg_install xz
    _zg_cands=$(osr_fetch_stdout https://ziglang.org/download/index.json \
        | grep -oE 'https://ziglang\.org/download/[0-9][0-9.]+/[^"]+\.tar\.xz' \
        | grep -E "zig-($_zg_m-linux|linux-$_zg_m)-")
    if [ -n "$_zg_want" ]; then
        _zg_url=$(printf '%s\n' "$_zg_cands" | grep "/$_zg_want/" | head -n 1)
    else
        _zg_url=$(printf '%s\n' "$_zg_cands" | head -n 1)   # index lists newest first
    fi
    [ -n "$_zg_url" ] || error "no zig tarball found (version='${_zg_want:-latest}', arch=$_zg_m)"
    _zg_ver=$(printf '%s' "$_zg_url" | sed -E 's#.*/download/([0-9][0-9.]+)/.*#\1#')
    _zg_dir="/usr/local/zig-${_zg_ver}"
    if [ ! -x "$_zg_dir/zig" ]; then
        _zg_tmp=$(mktemp -d)
        osr_download "$_zg_url" "$_zg_tmp/zig.tar.xz" \
            || { rm -rf "$_zg_tmp"; error "failed to download $_zg_url"; }
        as_root mkdir -p "$_zg_dir"
        as_root tar -xf "$_zg_tmp/zig.tar.xz" -C "$_zg_dir" --strip-components=1 \
            || { rm -rf "$_zg_tmp"; error "failed to extract zig $_zg_ver"; }
        rm -rf "$_zg_tmp"
    fi
    as_root ln -sf "$_zg_dir/zig" /usr/local/bin/zig
}

# Ghostty install prefers a prebuilt community binary over a source build wherever
# one exists (https://ghostty.org/docs/install/binary). Native packages (arch/
# void/gentoo + Ubuntu 25.10) pass through pkgmap; the builders below cover the
# rest, and build_ghostty (Zig source) is the last-resort fallback.

# build_ghostty_copr — Fedora community binary via COPR (scottames/ghostty).
build_ghostty_copr() {
    pkg_install dnf-plugins-core          # provides `dnf copr`
    as_root dnf copr enable -y scottames/ghostty
    as_root dnf install -y ghostty
    check_error $? "ghostty COPR install failed"
}

# build_ghostty_deb — Debian/Ubuntu community .deb via the ghostty-ubuntu
# installer (mkasberg/ghostty-ubuntu). Covers Ubuntu 24.04/26.04 + Debian trixie;
# the script self-detects release/arch and dpkg-installs. Needs bash + root.
build_ghostty_deb() {
    command -v bash >/dev/null 2>&1 || pkg_install bash
    osr_fetch_stdout https://raw.githubusercontent.com/mkasberg/ghostty-ubuntu/HEAD/install.sh \
        | as_root bash
    check_error $? "ghostty-ubuntu install failed"
}

# build_ghostty — build the Ghostty terminal from source with Zig; the fallback
# for targets with no native package and no community binary (older Debian/
# Ubuntu, Alpine/musl). Reads the exact Zig version Ghostty pins from its source
# tree and installs it via build_zig (G1: source: with a bootstrapped toolchain
# prerequisite). Heavy (a full Zig compile) - a real-desktop concern, §9.
build_ghostty() {
    # GTK/build deps (logical names; pkgmap splits per distro where needed).
    pkg_install build gtk4-dev libadwaita-dev gettext pkg-config tar xz
    _gh_ver=$(github_latest ghostty-org/ghostty); _gh_ver=${_gh_ver#v}
    _gh_tmp=$(mktemp -d)
    osr_download "https://release.files.ghostty.org/${_gh_ver}/ghostty-${_gh_ver}.tar.gz" \
        "$_gh_tmp/ghostty.tar.gz" || { rm -rf "$_gh_tmp"; error "failed to download ghostty $_gh_ver"; }
    tar -xf "$_gh_tmp/ghostty.tar.gz" -C "$_gh_tmp" || { rm -rf "$_gh_tmp"; error "failed to extract ghostty"; }
    _gh_src="$_gh_tmp/ghostty-${_gh_ver}"
    _gh_zig=$(cat "$_gh_src/.zig-version" 2>/dev/null | tr -d '[:space:]')
    build_zig "$_gh_zig"          # exact Zig version Ghostty requires
    ( cd "$_gh_src" && as_root zig build -p /usr -Doptimize=ReleaseFast ) \
        || { rm -rf "$_gh_tmp"; error "ghostty build failed"; }
    rm -rf "$_gh_tmp"
}

# build_gh_tarball — GitHub CLI from its release tarball (single static binary),
# for apt releases without a native `gh` (Debian bullseye).
build_gh_tarball() {
    _gh_tag=$(github_latest cli/cli)          # v2.63.0
    _gh_ver=${_gh_tag#v}                        # 2.63.0
    _osr_install_tarball_bin \
        "https://github.com/cli/cli/releases/download/${_gh_tag}/gh_${_gh_ver}_linux_${OSR_ARCH_DEB}.tar.gz" gh
}

# build_btop_tarball — btop from its static release tarball, for apt releases
# without a native package (Debian bullseye). Asset arch is uname-style.
build_btop_tarball() {
    _bt_tag=$(github_latest aristocratos/btop)  # v1.4.0
    case "$OSR_ARCH" in
        x86_64)  _bt_a=x86_64 ;;
        aarch64) _bt_a=aarch64 ;;
        *)       error "no btop tarball for arch $OSR_ARCH" ;;
    esac
    _osr_install_tarball_bin \
        "https://github.com/aristocratos/btop/releases/download/${_bt_tag}/btop-${_bt_a}-unknown-linux-musl.tar.gz" btop
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
