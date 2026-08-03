# lib/build.sh — source: provider functions (§4). Each is a shell function
# in scope that installs a program a native package can't provide on some target,
# with its own idempotency owned by _via_source's `command -v <name>` probe.
#
# These are the escape hatch for §1a rows like `lsd@jammy = source:provide_lsd_deb`:
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

# _osr_install_zip_bins <url> <binary>... — the .zip counterpart of
# _osr_install_tarball_bin, for upstreams that ship no tarball (yazi). Fetches
# the archive, finds each named binary anywhere inside, and installs them to
# /usr/local/bin. Unlike the tarball helper this RETURNS non-zero instead of
# calling error(), so a caller with a fallback path can catch the failure.
_osr_install_zip_bins() {
    _iz_url=$1; shift
    command -v unzip >/dev/null 2>&1 || pkg_install unzip
    command -v unzip >/dev/null 2>&1 || { warn "unzip not available for $_iz_url"; return 1; }
    _iz_tmp=$(mktemp -d)
    osr_download "$_iz_url" "$_iz_tmp/pkg.zip" \
        || { rm -rf "$_iz_tmp"; warn "failed to download $_iz_url"; return 1; }
    unzip -q -o "$_iz_tmp/pkg.zip" -d "$_iz_tmp" \
        || { rm -rf "$_iz_tmp"; warn "failed to extract $_iz_url"; return 1; }
    for _iz_bin in "$@"; do
        _iz_path=$(find "$_iz_tmp" -type f -name "$_iz_bin" | head -n 1)
        [ -n "$_iz_path" ] || { rm -rf "$_iz_tmp"; warn "$_iz_bin not found in $_iz_url"; return 1; }
        as_root install -m 0755 "$_iz_path" "/usr/local/bin/$_iz_bin" \
            || { rm -rf "$_iz_tmp"; warn "failed to install $_iz_bin"; return 1; }
    done
    rm -rf "$_iz_tmp"
}

# _osr_pkgconfig_path — echo PKG_CONFIG_PATH with the distro's standard .pc dirs
# appended. A pkg-config from Homebrew/conda/nix shadows the system one on PATH
# and searches ONLY its own prefix (`pkg-config --variable pc_path pkg-config`
# shows just the brew dirs), so a source build fails on system libs that ARE
# installed - wayland-client for wezterm, gtk4 for ghostty. Appending the standard
# dirs costs nothing when the system pkg-config is in use: they are already its
# defaults, and dirs that do not exist are ignored.
_osr_pkgconfig_path() {
    printf '%s' "${PKG_CONFIG_PATH:+$PKG_CONFIG_PATH:}/usr/lib/$(uname -m)-linux-gnu/pkgconfig:/usr/lib64/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig"
}

# provide_yazi_bin — Yazi from its official prebuilt release archive
# (github.com/sxyazi/yazi/releases), falling back to `cargo install` (the crates
# route upstream documents: https://yazi-rs.github.io/docs/installation/#crates).
# Upstream ships one .zip per target holding BOTH binaries — `yazi` (the TUI) and
# `ya` (its package/plugin CLI, which modules/yazi.sh's package.toml layer needs)
# — so install the pair from either route. The gnu asset is the glibc build;
# arches with no asset (or a release/download that fails) take the cargo path.
#
# The fallback is inside one builder, not a chain across map rows (DESIGN rejects
# those): the row still resolves to exactly one method, and cargo only runs when
# the binary route is genuinely unavailable on this target.
provide_yazi_bin() {
    case "$OSR_ARCH" in
        x86_64)  _yz_a=x86_64-unknown-linux-gnu ;;
        aarch64) _yz_a=aarch64-unknown-linux-gnu ;;
        *)       _yz_a="" ;;
    esac
    if [ -n "$_yz_a" ]; then
        # Subshell: github_latest calls error() (a hard exit) when the API is
        # unreachable — catching it here keeps that a fallback, not a dead run.
        _yz_tag=$(github_latest sxyazi/yazi 2>/dev/null) || _yz_tag=""   # e.g. v26.5.6
        if [ -n "$_yz_tag" ]; then
            info "installing yazi $_yz_tag from the upstream release binary"
            _osr_install_zip_bins \
                "https://github.com/sxyazi/yazi/releases/download/${_yz_tag}/yazi-${_yz_a}.zip" \
                yazi ya && return 0
        fi
        warn "yazi release binary unavailable ($_yz_a) - falling back to cargo"
    else
        warn "no yazi release binary for arch $OSR_ARCH - falling back to cargo"
    fi
    # Needs the toolchain: list `rust` before `yazi` in the rice when the target
    # can land here (manifest order is the dependency graph, §4). _via_cargo
    # carries its own §2 probe and the "install 'rust' first" error.
    _via_cargo yazi yazi-fm      # the TUI
    _via_cargo ya   yazi-cli     # the `ya` plugin/package CLI
}

# provide_paru — bootstrap the paru AUR helper from the AUR (source:provide_paru).
# The chicken/egg package: the one AUR package that cannot come *from* an AUR
# helper. Clone its PKGBUILD and makepkg it as OSR_USER (makepkg refuses root);
# every later aur: row then dispatches through paru. Arch-only. Idempotency is
# _via_source's `command -v paru` probe, so a rerun with paru present is a no-op.
provide_paru() {
    pkg_install build git                # base-devel + git: the makepkg toolchain
    _bp_repo="${TMPDIR:-/tmp}/osr-paru-build"
    as_user rm -rf "$_bp_repo"
    as_user git clone --depth 1 https://aur.archlinux.org/paru.git "$_bp_repo" \
        || error "failed to clone paru AUR repo"
    ( cd "$_bp_repo" && as_user makepkg -si --needed --noconfirm ) \
        || { as_user rm -rf "$_bp_repo"; error "paru build failed"; }
    as_user rm -rf "$_bp_repo"
}

# provide_zig [version] — install Zig from ziglang.org as a whole tree (it needs
# its lib/ beside the binary), symlinked into /usr/local/bin. For distros/apt
# releases without a native zig. The exact tarball URL is resolved from
# index.json (the asset naming changed across versions: zig-<arch>-linux on
# 0.15+, zig-linux-<arch> on <=0.14 - the regex matches both). No arg = latest
# stable; an arg pins a version (Ghostty needs an exact one). Declares its own
# xz prerequisite (the tarball is .tar.xz), §1a.
provide_zig() {
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
# rest, and provide_ghostty (Zig source) is the last-resort fallback.

# provide_ghostty_copr — Fedora community binary via COPR (scottames/ghostty).
provide_ghostty_copr() {
    pkg_install dnf-plugins-core          # provides `dnf copr`
    as_root dnf copr enable -y scottames/ghostty
    as_root dnf install -y ghostty
    check_error $? "ghostty COPR install failed"
}

# provide_ghostty_deb — Debian/Ubuntu community .deb via the ghostty-ubuntu
# installer (mkasberg/ghostty-ubuntu). Covers Ubuntu 24.04/26.04 + Debian trixie;
# the script self-detects release/arch and dpkg-installs. Needs bash + root.
provide_ghostty_deb() {
    command -v bash >/dev/null 2>&1 || pkg_install bash
    osr_fetch_stdout https://raw.githubusercontent.com/mkasberg/ghostty-ubuntu/HEAD/install.sh \
        | as_root bash
    check_error $? "ghostty-ubuntu install failed"
}

# provide_ghostty — build the Ghostty terminal from source with Zig; the fallback
# for targets with no native package and no community binary (older Debian/
# Ubuntu, Alpine/musl). Reads the exact Zig version Ghostty pins from its source
# tree and installs it via provide_zig (G1: source: with a bootstrapped toolchain
# prerequisite). Heavy (a full Zig compile) - a real-desktop concern, §9.
provide_ghostty() {
    # GTK/build deps (logical names; pkgmap splits per distro where needed).
    pkg_install build gtk4-dev libadwaita-dev gettext pkg-config tar xz
    _gh_ver=$(github_latest ghostty-org/ghostty); _gh_ver=${_gh_ver#v}
    _gh_tmp=$(mktemp -d)
    osr_download "https://release.files.ghostty.org/${_gh_ver}/ghostty-${_gh_ver}.tar.gz" \
        "$_gh_tmp/ghostty.tar.gz" || { rm -rf "$_gh_tmp"; error "failed to download ghostty $_gh_ver"; }
    tar -xf "$_gh_tmp/ghostty.tar.gz" -C "$_gh_tmp" || { rm -rf "$_gh_tmp"; error "failed to extract ghostty"; }
    _gh_src="$_gh_tmp/ghostty-${_gh_ver}"
    _gh_zig=$(cat "$_gh_src/.zig-version" 2>/dev/null | tr -d '[:space:]')
    provide_zig "$_gh_zig"          # exact Zig version Ghostty requires
    ( cd "$_gh_src" && as_root env PKG_CONFIG_PATH="$(_osr_pkgconfig_path)" \
        zig build -p /usr -Doptimize=ReleaseFast ) \
        || { rm -rf "$_gh_tmp"; error "ghostty build failed"; }
    rm -rf "$_gh_tmp"
}

# provide_wezterm — build WezTerm from source, the route upstream documents
# (https://wezterm.org/install/source.html). No AppImage/flatpak: the source
# build is the ONLY install method here, so every distro gets the same binary
# from the same recipe. Heavy (a full Rust workspace compile) - a real-desktop
# concern, §9, not part of the container matrix.
#
# Needs a toolchain: list `rust` BEFORE `wezterm` in the rice (manifest order is
# the dependency graph, §4). Upstream's own build deps come from the repo's
# ./get-deps, which detects the distro and installs them - one script instead of
# a per-distro dep list duplicated into every pkgmap.
# Idempotency is _via_source's `command -v wezterm` probe (§2).
provide_wezterm() {
    pkg_install build git
    _wt_cargo="$OSR_HOME/.cargo/bin/cargo"
    as_user test -x "$_wt_cargo" \
        || error "cargo not found - install 'rust' before wezterm (manifest order, section 4)"

    # A failed build KEEPS the checkout (see below), so a retry reuses it: no
    # second 15-minute compile from scratch after a transient registry/network
    # blip. Only a successful install cleans it up.
    _wt_src="${TMPDIR:-/tmp}/osr-wezterm-src"
    if [ -f "$_wt_src/Cargo.toml" ]; then
        info "reusing the existing wezterm checkout ($_wt_src) - rebuild is incremental"
    else
        as_user rm -rf "$_wt_src"
        # --branch=main is what upstream documents for a source build; the
        # submodules (freetype/harfbuzz/... vendored deps) are not optional.
        as_user git clone --depth=1 --branch=main --recursive \
            https://github.com/wezterm/wezterm.git "$_wt_src" || error "failed to clone wezterm"
    fi
    ( cd "$_wt_src" && as_user git submodule update --init --recursive ) \
        || error "wezterm submodule checkout failed"
    # get-deps needs root to install, but ends with a `rustc --version` check -
    # and the toolchain is OSR_USER's, not root's (§8 user-for-user). sudo resets
    # PATH, so root does not see ~/.cargo/bin; and the cargo/rustc SHIMS resolve
    # the actual toolchain through RUSTUP_HOME, which for root is /root/.rustup
    # (empty -> "could not choose a version of rustc to run"). Point all three at
    # OSR_USER's toolchain, or get-deps exits 1 on a box where the deps installed
    # fine. Root-for-root installs are already correct; this is the delta.
    ( cd "$_wt_src" && as_root env \
        PATH="$OSR_HOME/.cargo/bin:$PATH" \
        RUSTUP_HOME="$OSR_HOME/.rustup" \
        CARGO_HOME="$OSR_HOME/.cargo" \
        ./get-deps ) \
        || error "wezterm get-deps failed"
    # PKG_CONFIG_PATH: see _osr_pkgconfig_path - a shadowing pkg-config (brew et
    # al) otherwise fails the wayland-sys build script on a box that HAS the libs.
    ( cd "$_wt_src" && as_user env PKG_CONFIG_PATH="$(_osr_pkgconfig_path)" \
        "$_wt_cargo" build --release ) \
        || error "wezterm build failed (checkout kept at $_wt_src - rerun to resume)"

    for _wt_b in wezterm wezterm-gui wezterm-mux-server; do
        as_root install -m 0755 "$_wt_src/target/release/$_wt_b" "/usr/local/bin/$_wt_b" \
            || error "failed to install $_wt_b"
    done
    # Desktop entry + icon so a DE launcher finds it. Cosmetic: warn, never fail.
    if [ -f "$_wt_src/assets/wezterm.desktop" ]; then
        as_root install -Dm 0644 "$_wt_src/assets/wezterm.desktop" \
            /usr/local/share/applications/org.wezfurlong.wezterm.desktop \
            || warn "failed to install the wezterm desktop entry"
        as_root install -Dm 0644 "$_wt_src/assets/icon/terminal.png" \
            /usr/local/share/icons/hicolor/128x128/apps/org.wezfurlong.wezterm.png \
            || warn "failed to install the wezterm icon"
    fi
    as_user rm -rf "$_wt_src"
}

# provide_gh_tarball — GitHub CLI from its release tarball (single static binary),
# for apt releases without a native `gh` (Debian bullseye).
provide_gh_tarball() {
    _gh_tag=$(github_latest cli/cli)          # v2.63.0
    _gh_ver=${_gh_tag#v}                        # 2.63.0
    _osr_install_tarball_bin \
        "https://github.com/cli/cli/releases/download/${_gh_tag}/gh_${_gh_ver}_linux_${OSR_ARCH_DEB}.tar.gz" gh
}

# provide_btop_tarball — btop from its static release tarball, for apt releases
# without a native package (Debian bullseye). Asset arch is uname-style.
provide_btop_tarball() {
    _bt_tag=$(github_latest aristocratos/btop)  # v1.4.0
    case "$OSR_ARCH" in
        x86_64)  _bt_a=x86_64 ;;
        aarch64) _bt_a=aarch64 ;;
        *)       error "no btop tarball for arch $OSR_ARCH" ;;
    esac
    _osr_install_tarball_bin \
        "https://github.com/aristocratos/btop/releases/download/${_bt_tag}/btop-${_bt_a}-unknown-linux-musl.tar.gz" btop
}

# provide_lsd_tarball — lsd binary from the release .tar.gz (for old dpkg, §bullseye).
provide_lsd_tarball() {
    _lt_tag=$(github_latest lsd-rs/lsd)                  # v1.2.0
    case "$OSR_ARCH" in
        x86_64)  _lt_a=x86_64-unknown-linux-gnu ;;
        aarch64) _lt_a=aarch64-unknown-linux-gnu ;;
        *)       error "no lsd tarball for arch $OSR_ARCH" ;;
    esac
    _osr_install_tarball_bin \
        "https://github.com/lsd-rs/lsd/releases/download/${_lt_tag}/lsd-${_lt_tag}-${_lt_a}.tar.gz" lsd
}

# provide_fastfetch_tarball — fastfetch binary from the release .tar.gz (old dpkg).
provide_fastfetch_tarball() {
    _ft_tag=$(github_latest fastfetch-cli/fastfetch)    # 2.66.0
    case "$OSR_ARCH" in
        x86_64)  _ft_a=amd64 ;;
        aarch64) _ft_a=aarch64 ;;
        *)       error "no fastfetch tarball for arch $OSR_ARCH" ;;
    esac
    _osr_install_tarball_bin \
        "https://github.com/fastfetch-cli/fastfetch/releases/download/${_ft_tag}/fastfetch-linux-${_ft_a}.tar.gz" fastfetch
}

# provide_fastfetch_deb — install fastfetch from its official prebuilt .deb
# (fastfetch-cli/fastfetch releases). The "easiest method" on Debian/Ubuntu
# releases that don't package it natively; native distros (arch/fedora/void/
# alpine/gentoo) install it straight from their repos, no builder. fastfetch's
# asset arch naming is mixed (amd64 for x86, aarch64 for arm) — resolve inline.
provide_fastfetch_deb() {
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

# provide_lsd_deb — install lsd from its official prebuilt .deb (lsd-rs/lsd
# releases). For Debian/Ubuntu releases too old to ship lsd natively (jammy).
# apt-get install of a local .deb pulls any deps; glibc build (not -musl).
provide_lsd_deb() {
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

# provide_amneziavpn — AmneziaVPN client from the upstream release (amnezia-vpn/
# amnezia-client). For targets with no native/AUR package (Debian/Ubuntu): the
# Linux asset is a Qt Installer Framework (QtIFW) self-extracting installer that
# needs root to place a privileged helper. Driven headless (-p minimal, no X;
# --accept-* + -c skip every prompt) into /opt/AmneziaVPN, then symlinked onto
# PATH as `amneziavpn` so _via_source's `command -v amneziavpn` probe (§4) sees
# it and a rerun is a no-op. x86_64 only — upstream ships no other Linux arch.
provide_amneziavpn() {
    [ "$OSR_ARCH" = x86_64 ] || error "no AmneziaVPN Linux build for arch $OSR_ARCH"
    _av_tag=$(github_latest amnezia-vpn/amnezia-client)   # e.g. 4.8.21.0 (no v)
    _av_dir=/opt/AmneziaVPN
    _av_tmp=$(mktemp -d)
    osr_download \
        "https://github.com/amnezia-vpn/amnezia-client/releases/download/${_av_tag}/AmneziaVPN_${_av_tag}_linux_x64.tar" \
        "$_av_tmp/amnezia.tar" || { rm -rf "$_av_tmp"; error "failed to download AmneziaVPN $_av_tag"; }
    tar -xf "$_av_tmp/amnezia.tar" -C "$_av_tmp" \
        || { rm -rf "$_av_tmp"; error "failed to extract AmneziaVPN $_av_tag"; }
    _av_bin=$(find "$_av_tmp" -type f -name '*.bin' | head -n 1)
    [ -n "$_av_bin" ] || { rm -rf "$_av_tmp"; error "AmneziaVPN installer not found in tarball"; }
    chmod +x "$_av_bin"
    as_root "$_av_bin" install --root "$_av_dir" \
        --accept-licenses --accept-messages --confirm-command -p minimal
    _av_rc=$?
    rm -rf "$_av_tmp"
    check_error "$_av_rc" "AmneziaVPN headless install failed"
    as_root ln -sf "$_av_dir/AmneziaVPN" /usr/local/bin/amneziavpn
}
