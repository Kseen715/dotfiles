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

# CHAFA_MIN — the chafa yazi actually needs, and the reason this builder exists.
# Yazi drives the adapter as `chafa -f symbols --relative off --probe off
# --polite on --passthrough none --animate off --view-size WxH <img>`, and
# --probe landed in chafa 1.16.0 (yazi documents >= 1.16.0 for the same reason).
# An older chafa exits on the unrecognized option, so the preview pane just stays
# blank - no error in the UI, nothing in the log. Version, not presence, is the
# thing to test.
CHAFA_MIN=1.16

# _chafa_version — MAJOR.MINOR of the chafa on PATH, empty when there is none.
# `chafa --version` opens with "Chafa version 1.14.5".
_chafa_version() {
    command -v chafa >/dev/null 2>&1 || return 0
    chafa --version 2>/dev/null | head -n 1 \
        | sed -n 's/^[^0-9]*\([0-9][0-9]*\.[0-9][0-9]*\).*/\1/p'
}

# _chafa_ok — true when the chafa on PATH is >= CHAFA_MIN.
_chafa_ok() {
    _cok_have=$(_chafa_version)
    [ -n "$_cok_have" ] || return 1
    _cok_hmaj=${_cok_have%%.*}; _cok_hmin=${_cok_have#*.}
    _cok_wmaj=${CHAFA_MIN%%.*}; _cok_wmin=${CHAFA_MIN#*.}
    if [ "$_cok_hmaj" -ne "$_cok_wmaj" ]; then
        [ "$_cok_hmaj" -gt "$_cok_wmaj" ]
    else
        [ "$_cok_hmin" -ge "$_cok_wmin" ]
    fi
}

# provide_chafa — build chafa from the upstream release tarball, for the targets
# whose archive is older than CHAFA_MIN (the table in apt.map). Upstream ships a
# SOURCE tarball only - no prebuilt Linux binary anywhere - so those releases
# have to compile, unlike every other builder here. It is a small C project:
# glib + freetype are the only mandatory deps, PNG/GIF decode is built in
# (LodePNG / libnsgif), and jpeg/webp/tiff come from the optional loaders in
# chafa-build-deps so the formats a file manager actually previews all render.
#
# Idempotency goes BEYOND _via_source's `command -v chafa` probe (§2): presence
# is not sufficiency here, so the builder re-checks the version itself and
# returns early when the chafa on PATH is already new enough. That also makes it
# safe to call directly, which modules/yazi.sh does to repair a box that already
# had an old distro chafa - that one satisfies the probe and would otherwise
# never be replaced. /usr/local/bin precedes /usr/bin in the default PATH, so
# the built chafa wins even where the old package stays installed.
provide_chafa() {
    if _chafa_ok; then
        info "chafa $(_chafa_version) is already >= $CHAFA_MIN - skipping the source build"
        return 0
    fi
    pkg_install build chafa-build-deps tar xz
    _cf_ver=$(github_latest hpjansson/chafa); _cf_ver=${_cf_ver#v}   # tags carry no v
    _cf_tmp=$(mktemp -d)
    osr_download "https://github.com/hpjansson/chafa/releases/download/${_cf_ver}/chafa-${_cf_ver}.tar.xz" \
        "$_cf_tmp/chafa.tar.xz" || { rm -rf "$_cf_tmp"; error "failed to download chafa $_cf_ver"; }
    tar -xf "$_cf_tmp/chafa.tar.xz" -C "$_cf_tmp" \
        || { rm -rf "$_cf_tmp"; error "failed to extract chafa $_cf_ver"; }
    # The dist tarball is pre-autotooled - ./configure is already generated, so
    # no autogen.sh run and no autoconf/automake/libtool in the dep list.
    _cf_src="$_cf_tmp/chafa-${_cf_ver}"
    ( cd "$_cf_src" && env PKG_CONFIG_PATH="$(_osr_pkgconfig_path)" ./configure --prefix=/usr/local \
        && make -j"${OSR_BUILD_JOBS:-$(nproc 2>/dev/null || echo 2)}" ) \
        || { rm -rf "$_cf_tmp"; error "chafa build failed"; }
    as_root make -C "$_cf_src" install \
        || { rm -rf "$_cf_tmp"; error "chafa install failed"; }
    # libchafa lands in /usr/local/lib; refresh the loader cache so the binary
    # finds it on distros that do not scan that dir by default.
    as_root ldconfig >/dev/null 2>&1 || true
    rm -rf "$_cf_tmp"
}

# provide_ueberzugpp — build Überzug++ from the upstream release tarball, for the
# targets that package it nowhere: Arch, Gentoo, openSUSE and NixOS carry it,
# Debian/Ubuntu/Fedora/Void/Alpine carry nothing (repology). It is what yazi
# actually uses on a graphical session - see the gate in modules/yazi.sh - so on
# those distros there is no packaged route to a working image preview at all.
#
# Upstream installs BOTH names: the binary `ueberzug` plus a symlink `ueberzugpp`
# (CMakeLists: file(CREATE_LINK ueberzug ... SYMBOLIC)). That symlink is the one
# that matters, because yazi spawns exactly `ueberzugpp layer -so <driver>`.
#
# -DENABLE_OPENCV=OFF picks libvips for image loading instead, which is the far
# lighter dep tree (OpenCV headers alone dwarf this whole build) - upstream
# documents the swap. X11 and Wayland outputs are both compiled in so one recipe
# covers every session; the gate decides whether to build at all, not which.
# CLI11 / nlohmann-json / fmt / spdlog / range-v3 come from the distro where the
# name is known, and CMake's FetchContent pulls whichever are missing - the
# "Downloadable Dependencies" path upstream supports.
provide_ueberzugpp() {
    pkg_install build cmake ueberzugpp-build-deps
    _uz_tag=$(github_latest jstkdng/ueberzugpp)      # e.g. v2.9.10
    _uz_ver=${_uz_tag#v}
    _uz_tmp=$(mktemp -d)
    osr_download "https://github.com/jstkdng/ueberzugpp/archive/refs/tags/${_uz_tag}.tar.gz" \
        "$_uz_tmp/ueberzugpp.tar.gz" \
        || { rm -rf "$_uz_tmp"; error "failed to download ueberzugpp $_uz_tag"; }
    tar -xf "$_uz_tmp/ueberzugpp.tar.gz" -C "$_uz_tmp" \
        || { rm -rf "$_uz_tmp"; error "failed to extract ueberzugpp $_uz_tag"; }
    _uz_src="$_uz_tmp/ueberzugpp-${_uz_ver}"
    [ -f "$_uz_src/CMakeLists.txt" ] \
        || { rm -rf "$_uz_tmp"; error "no CMakeLists.txt in the ueberzugpp tarball - its layout changed"; }
    env PKG_CONFIG_PATH="$(_osr_pkgconfig_path)" cmake -S "$_uz_src" -B "$_uz_src/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DENABLE_OPENCV=OFF \
        -DENABLE_X11=ON \
        -DENABLE_WAYLAND=ON \
        || { rm -rf "$_uz_tmp"; error "ueberzugpp cmake configure failed"; }
    env CMAKE_BUILD_PARALLEL_LEVEL="${OSR_BUILD_JOBS:-$(nproc 2>/dev/null || echo 2)}" \
        cmake --build "$_uz_src/build" \
        || { rm -rf "$_uz_tmp"; error "ueberzugpp build failed"; }
    as_root cmake --install "$_uz_src/build" \
        || { rm -rf "$_uz_tmp"; error "ueberzugpp install failed"; }
    as_root ldconfig >/dev/null 2>&1 || true
    rm -rf "$_uz_tmp"
    command -v ueberzugpp >/dev/null 2>&1 \
        || error "ueberzugpp installed but not on PATH - yazi spawns it by that exact name"
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
# it and a rerun is a no-op.
#
# The release is x86_64-only and can be missing/unreachable, so this builder ends
# in provide_amneziavpn_source — the LAST-RESORT route (a heavy Qt build), never
# taken while a ready binary exists. Same shape as provide_yazi_bin: the fallback
# lives inside ONE builder, not as a chain across map rows (DESIGN rejects those).
provide_amneziavpn() {
    if [ "$OSR_ARCH" != x86_64 ]; then
        warn "no AmneziaVPN release binary for arch $OSR_ARCH - falling back to the source build"
        provide_amneziavpn_source
        return $?
    fi
    # Subshell: github_latest calls error() (a hard exit) when the API is
    # unreachable — catching it here keeps that a fallback, not a dead run.
    _av_tag=$(github_latest amnezia-vpn/amnezia-client 2>/dev/null) || _av_tag=""  # e.g. 4.8.21.0 (no v)
    if [ -n "$_av_tag" ]; then
        _av_dir=/opt/AmneziaVPN
        _av_tmp=$(mktemp -d)
        if osr_download \
            "https://github.com/amnezia-vpn/amnezia-client/releases/download/${_av_tag}/AmneziaVPN_${_av_tag}_linux_x64.tar" \
            "$_av_tmp/amnezia.tar" \
           && tar -xf "$_av_tmp/amnezia.tar" -C "$_av_tmp"; then
            _av_bin=$(find "$_av_tmp" -type f -name '*.bin' | head -n 1)
        else
            _av_bin=""
        fi
        if [ -n "$_av_bin" ]; then
            chmod +x "$_av_bin"
            as_root "$_av_bin" install --root "$_av_dir" \
                --accept-licenses --accept-messages --confirm-command -p minimal
            _av_rc=$?
            rm -rf "$_av_tmp"
            # A release that downloaded but refused to install is a broken target,
            # not a "no binary available" case — a 40-minute build won't fix it.
            check_error "$_av_rc" "AmneziaVPN headless install failed"
            as_root ln -sf "$_av_dir/AmneziaVPN" /usr/local/bin/amneziavpn
            return 0
        fi
        rm -rf "$_av_tmp"
    fi
    warn "AmneziaVPN release binary unavailable (tag='${_av_tag:-unresolved}') - falling back to the source build. Have God with you: a full Qt/QML compile and link, plus its conan deps, can be >24GB RSS observed. Lower OSR_BUILD_JOBS or add swap if it OOMs."
    provide_amneziavpn_source
}

# provide_amneziavpn_source — build the AmneziaVPN client from source, the route
# upstream documents (README "Hacking guide"). Salvaged from the legacy
# linux-arch-x86_64-hyprland-glass/build-amneziavpn-client.sh, with two fixes the
# legacy script needs: upstream DELETED deploy/build_linux.sh (it is deploy/build.sh
# now), and that wrapper only finds Qt in the Qt-online-installer layout
# (~/Qt, /opt/Qt) - never a distro Qt6. It is a thin cmake configure+build and
# never installs anything, so drive cmake directly instead: same three commands,
# no env-var gymnastics, and `cmake --install` puts the tree where the shipped
# systemd unit already expects it (/opt/AmneziaVPN/bin/AmneziaVPN-service).
#
# NOT a route of its own: nothing maps to it. It is the last resort inside
# provide_amneziavpn, reached only when the release binary is unavailable (no
# asset for this arch, or GitHub unreachable). Arch keeps aur:amneziavpn-bin —
# the same release binary, pacman-tracked — and never lands here.
# HEAVY, and a real-desktop concern (§9): a full Qt/QML app plus its conan deps.
# Linking is the peak - >24GB RSS observed - so OSR_BUILD_JOBS caps build
# parallelism (default 1: one link at a time). Raise it on a box with the RAM.
provide_amneziavpn_source() {
    # The caller's §2 probe is `command -v amneziavpn`; this catches the other
    # half — a prebuilt install that left AmneziaVPN on PATH under its own name.
    if command -v AmneziaVPN >/dev/null 2>&1; then
        info "AmneziaVPN already installed - skipping the source build"
        return 0
    fi
    [ "$OSR_ARCH" = x86_64 ] || warn "AmneziaVPN upstream builds/tests x86_64 only - building on $OSR_ARCH anyway"
    # Qt 6.10+ with the components client/ + service/ ask for (Quick, Svg,
    # QuickControls2, Core5Compat, RemoteObjects, LinguistTools, DBus). No
    # qt6-webengine: the legacy script's list predates the QML client, nothing
    # links it now and it is the single largest dep in that list.
    pkg_install build cmake git conan openssl \
        qt6-base qt6-declarative qt6-svg qt6-tools qt6-5compat qt6-remoteobjects qt6-wayland

    # A failed build KEEPS the checkout so a retry resumes instead of recompiling
    # from scratch (same contract as provide_wezterm).
    _as_src="${TMPDIR:-/tmp}/osr-amneziavpn-src"
    if [ -f "$_as_src/CMakeLists.txt" ]; then
        info "reusing the existing amnezia-client checkout ($_as_src) - rebuild is incremental"
        # Only the reuse path needs this; --recursive already did it on a fresh clone.
        ( cd "$_as_src" && as_user git submodule update --init --recursive ) \
            || error "amnezia-client submodule checkout failed"
    else
        as_user rm -rf "$_as_src"
        as_user git clone --depth 1 --recursive \
            https://github.com/amnezia-vpn/amnezia-client.git "$_as_src" \
            || error "failed to clone amnezia-client"
    fi

    # Configure + build as OSR_USER: cmake's conan provider writes ~/.conan2 and
    # pulls the prebuilt recipes, which must NOT land in root's home (§8).
    # CMAKE_PREFIX_PATH=/usr points it at the distro Qt6.
    as_user cmake -S "$_as_src" -B "$_as_src/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH=/usr \
        -DCMAKE_INSTALL_PREFIX=/opt/AmneziaVPN \
        || error "amnezia-client cmake configure failed"
    as_user env CMAKE_BUILD_PARALLEL_LEVEL="${OSR_BUILD_JOBS:-1}" \
        cmake --build "$_as_src/build" \
        || error "amnezia-client build failed (checkout kept at $_as_src - rerun to resume; OOM? lower OSR_BUILD_JOBS or add swap)"
    as_root cmake --install "$_as_src/build" --component AmneziaVPN \
        || error "amnezia-client install failed"

    # What upstream's deploy/data/linux/post_install.sh does, minus the SteamOS
    # and killall branches: the component drops the unit/desktop/icon at the
    # prefix root, and something has to place them.
    as_root install -Dm 0644 /opt/AmneziaVPN/AmneziaVPN.desktop /usr/share/applications/AmneziaVPN.desktop \
        || warn "failed to install the AmneziaVPN desktop entry"
    as_root install -Dm 0644 /opt/AmneziaVPN/AmneziaVPN.png /usr/share/pixmaps/AmneziaVPN.png \
        || warn "failed to install the AmneziaVPN icon"
    # Exec=AmneziaVPN in the .desktop, and the rice autostart probes `AmneziaVPN`;
    # `amneziavpn` matches what provide_amneziavpn puts on PATH. Both, one binary.
    as_root ln -sf /opt/AmneziaVPN/bin/AmneziaVPN /usr/local/bin/AmneziaVPN
    as_root ln -sf /opt/AmneziaVPN/bin/AmneziaVPN /usr/local/bin/amneziavpn
    # The privileged helper the client talks to. systemd-only unit, so no
    # enable_service() on other inits - the client is simply unusable there.
    if [ "$OSR_INIT" = systemd ]; then
        as_root install -m 0644 /opt/AmneziaVPN/AmneziaVPN.service /etc/systemd/system/AmneziaVPN.service
        as_root systemctl daemon-reload
        enable_service AmneziaVPN
    else
        warn "AmneziaVPN ships a systemd unit only - the background service is not enabled on $OSR_INIT"
    fi
    as_user rm -rf "$_as_src"
}

# provide_thunderbird_tarball — Thunderbird from Mozilla's official Linux tarball
# (download.mozilla.org), installed as a tree under /opt with a symlink and a
# .desktop entry. The Debian/Ubuntu route, for two reasons the archive cannot fix:
#
#   snap     On Ubuntu 24.04+ the archive's `thunderbird` is a transitional stub
#            whose only job is `snap install thunderbird`. The snap relocates the
#            profile root to ~/snap/thunderbird/common/.thunderbird, so the §5/§6
#            layer modules/thunderbird.sh writes to ~/.thunderbird is read by
#            nothing.
#   ESR      Debian pins an ESR (91/115/128 on bullseye..trixie). Native
#            Exchange/EWS accounts need Thunderbird 140+.
#
# Not the Mozilla APT repo: packages.mozilla.org carries firefox only — asking it
# for thunderbird silently falls through to the archive's snap stub, which is
# exactly the thing this builder exists to avoid.
#
# The tarball is x86_64-only (Mozilla publishes no aarch64 Linux build), so other
# arches get a clear error instead of a mystery 404. Clearing the snap out of the
# way is the module's job, not this builder's - it has to happen BEFORE
# pkg_install, or `command -v thunderbird` finds /snap/bin/thunderbird and the
# source: probe (§4) skips the install entirely.
provide_thunderbird_tarball() {
    [ "$OSR_ARCH" = x86_64 ] \
        || error "Mozilla publishes no Linux $OSR_ARCH Thunderbird build - use the distro package (an ESR without Exchange/EWS) on this arch"

    pkg_install tar xz
    _tb_tmp=$(mktemp -d)
    info "downloading the latest Thunderbird from download.mozilla.org"
    osr_download "https://download.mozilla.org/?product=thunderbird-latest&os=linux64&lang=en-US" \
        "$_tb_tmp/thunderbird.tar.xz" || { rm -rf "$_tb_tmp"; error "failed to download Thunderbird"; }
    tar -xf "$_tb_tmp/thunderbird.tar.xz" -C "$_tb_tmp" \
        || { rm -rf "$_tb_tmp"; error "failed to extract the Thunderbird tarball"; }
    [ -x "$_tb_tmp/thunderbird/thunderbird" ] \
        || { rm -rf "$_tb_tmp"; error "no thunderbird binary in the tarball"; }
    as_root rm -rf /opt/thunderbird
    as_root mv "$_tb_tmp/thunderbird" /opt/thunderbird \
        || { rm -rf "$_tb_tmp"; error "failed to install Thunderbird into /opt"; }
    rm -rf "$_tb_tmp"
    # /usr/local/bin precedes /usr/bin, so this wins over any leftover wrapper.
    as_root ln -sf /opt/thunderbird/thunderbird /usr/local/bin/thunderbird

    # The tarball ships no .desktop entry (Mozilla leaves that to packagers), so
    # write the minimal one: without it the app has no menu entry and nothing
    # answers mailto:.
    as_root tee /usr/share/applications/thunderbird.desktop >/dev/null <<'DESKTOP'
[Desktop Entry]
Name=Thunderbird
Comment=Send and receive mail
Exec=/opt/thunderbird/thunderbird %u
Icon=/opt/thunderbird/chrome/icons/default/default128.png
Terminal=false
Type=Application
Categories=Network;Email;
MimeType=x-scheme-handler/mailto;message/rfc822;
StartupNotify=true
StartupWMClass=thunderbird
DESKTOP
    command -v update-desktop-database >/dev/null 2>&1 \
        && as_root update-desktop-database /usr/share/applications >/dev/null 2>&1 || :
    # /opt is root-owned, so Thunderbird's own updater cannot apply updates:
    # `osr install thunderbird` (this builder) is the update path.
}

# --- DataGrip (JetBrains tarball) --------------------------------------------
# JetBrains ships DataGrip as a Linux tarball only (no repo, no deb/rpm; the
# jetbrains.com/datagrip/download page's "Linux" tab is that .tar.gz). It is a
# self-contained tree with its own JBR, so unpacking it under /opt IS the
# supported install - Toolbox does the same thing in $HOME.
#
# OSR_DATAGRIP_PREFIX is the one tree this builder owns; anything else on the box
# (Toolbox, snap, flatpak, a distro/AUR package, a hand-unpacked /opt/datagrip-*)
# is reported and left alone - §5, we own only what we wrote.
OSR_DATAGRIP_PREFIX=/opt/datagrip
OSR_DATAGRIP_FEED="https://data.services.jetbrains.com/products/releases?code=DG&latest=true&type=release"

# _datagrip_latest — echo "<version> <url> <bytes>" for this arch's Linux
# tarball, resolved from JetBrains' releases feed so no version is hard-coded
# (G4). The feed is one JSON object per product with a downloads map; splitting
# on commas isolates each `"<key>":{"link":"...","size":N` fragment, so the arch
# key matches exactly ("linux" never matches "linuxARM64"). The size is what
# turns the ~1 GB fetch into a progress readout (osr_download's third argument);
# it is optional, so a feed that ever drops it just means a silent download.
_datagrip_latest() {
    case "$OSR_ARCH" in
        x86_64)  _dg_key=linux ;;
        aarch64) _dg_key=linuxARM64 ;;
        *)       error "JetBrains publishes no Linux $OSR_ARCH DataGrip build (x86_64/aarch64 only)" ;;
    esac
    _dg_feed=$(osr_fetch_stdout "$OSR_DATAGRIP_FEED") \
        || error "failed to query the JetBrains releases feed for DataGrip"
    _dg_v=$(printf '%s' "$_dg_feed" \
        | sed -n 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)
    _dg_u=$(printf '%s' "$_dg_feed" | tr ',' '\n' \
        | sed -n 's|.*"'"$_dg_key"'":{"link":"\([^"]*\)".*|\1|p' | head -n 1)
    # The size sits in the same object as the link, so it is read off the whole
    # feed rather than a comma-split fragment; the arch key is unique in it.
    _dg_sz=$(printf '%s' "$_dg_feed" \
        | sed -n 's|.*"'"$_dg_key"'":{"link":"[^"]*","size":\([0-9]*\).*|\1|p' | head -n 1)
    case "$_dg_u" in
        https://*.tar.gz) ;;
        *) error "could not resolve the DataGrip $_dg_key tarball from the JetBrains feed" ;;
    esac
    [ -n "$_dg_v" ] || error "could not resolve the DataGrip version from the JetBrains feed"
    printf '%s %s %s' "$_dg_v" "$_dg_u" "${_dg_sz:-0}"
}

# _datagrip_version_at <dir> — echo the version of an unpacked DataGrip tree.
# product-info.json sits at the root of every JetBrains IDE tarball and opens
# with the product name and version.
_datagrip_version_at() {
    [ -f "$1/product-info.json" ] || return 1
    _dv=$(sed -n 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$1/product-info.json" \
        | head -n 1)
    [ -n "$_dv" ] || return 1
    printf '%s' "$_dv"
}

# _datagrip_report_foreign — find the DataGrip copies this builder does NOT own
# and name them. They matter even though we never touch them: a Toolbox or snap
# launcher earlier on PATH is the one that actually opens when the user types
# `datagrip`, so a silent "upgraded" here would be an upgrade of a tree nobody
# runs. Reported, never removed (§5, G2).
_datagrip_report_foreign() {
    command -v snap >/dev/null 2>&1 && snap list datagrip >/dev/null 2>&1 \
        && warn "a DataGrip snap is installed - it is separate from $OSR_DATAGRIP_PREFIX and is not upgraded here"
    command -v flatpak >/dev/null 2>&1 && flatpak info com.jetbrains.DataGrip >/dev/null 2>&1 \
        && warn "the com.jetbrains.DataGrip flatpak is installed - separate from $OSR_DATAGRIP_PREFIX, not upgraded here"
    _native_installed datagrip \
        && warn "a native 'datagrip' package is installed - separate from $OSR_DATAGRIP_PREFIX, not upgraded here"
    for _df in "$OSR_HOME"/.local/share/JetBrains/Toolbox/apps/[Dd]ata[Gg]rip*; do
        [ -d "$_df" ] || continue
        warn "JetBrains Toolbox has its own DataGrip at $_df - Toolbox updates that one, this module updates $OSR_DATAGRIP_PREFIX"
    done
    for _df in /opt/[Dd]ata[Gg]rip*; do
        [ -d "$_df" ] || continue
        [ "$_df" = "$OSR_DATAGRIP_PREFIX" ] && continue
        warn "an unpacked DataGrip tree at $_df is not owned by this module - remove it if it is a leftover"
    done
    return 0
}

# _datagrip_desktop_entry — launcher symlink + menu entry for the installed tree.
# The tarball ships no .desktop (JetBrains leaves that to Toolbox), so write the
# minimal one; the icon comes out of the tree itself, which is why this runs
# after the tree is in place. Rerun-safe, so it also repairs a box whose entry
# was lost while the tree stayed current.
_datagrip_desktop_entry() {
    _dg_exec="$OSR_DATAGRIP_PREFIX/bin/datagrip"
    [ -x "$_dg_exec" ] || _dg_exec="$OSR_DATAGRIP_PREFIX/bin/datagrip.sh"
    [ -x "$_dg_exec" ] || error "no DataGrip launcher under $OSR_DATAGRIP_PREFIX/bin"
    # /usr/local/bin precedes /usr/bin, and this is also the _via_source probe.
    as_root ln -sf "$_dg_exec" /usr/local/bin/datagrip

    _dg_icon=""
    for _di in "$OSR_DATAGRIP_PREFIX/bin/datagrip.png" "$OSR_DATAGRIP_PREFIX/bin/datagrip.svg"; do
        [ -f "$_di" ] && { _dg_icon=$_di; break; }
    done
    [ -n "$_dg_icon" ] || warn "no datagrip icon in the tarball - the menu entry will use the theme fallback"
    # StartupWMClass is what the IDE actually sets on its window
    # (jetbrains-datagrip); without it the taskbar shows a second, unnamed entry.
    as_root tee /usr/share/applications/datagrip.desktop >/dev/null <<DESKTOP
[Desktop Entry]
Name=DataGrip
Comment=Database IDE
Exec=$_dg_exec %f
Icon=${_dg_icon:-datagrip}
Terminal=false
Type=Application
Categories=Development;IDE;Database;
Keywords=sql;database;jetbrains;
StartupNotify=true
StartupWMClass=jetbrains-datagrip
DESKTOP
    command -v update-desktop-database >/dev/null 2>&1 \
        && as_root update-desktop-database /usr/share/applications >/dev/null 2>&1 || :
}

# provide_datagrip — install or UPGRADE DataGrip from the vendor tarball.
#
# Idempotency goes beyond _via_source's `command -v datagrip` probe (§2, same
# shape as provide_chafa): presence is not sufficiency for an IDE that ships a
# new build every few weeks, so the builder compares the installed tree's
# product-info.json against the feed and returns early only when they match.
# That is what makes `osr module datagrip` the update path - modules/datagrip.sh
# calls this function directly for exactly that reason.
#
# Staging happens inside /opt, not $TMPDIR: the tarball is ~1 GB and unpacks to
# ~2.5 GB, which a tmpfs /tmp on a 16 GB box will not hold, and it makes the
# swap a same-filesystem rename instead of a cross-device copy. The old tree is
# removed only once the new one is unpacked and verified.
provide_datagrip() {
    pkg_install tar gzip
    _dg_latest=$(_datagrip_latest)
    _dg_ver=${_dg_latest%% *}
    _dg_rest=${_dg_latest#* }
    _dg_url=${_dg_rest%% *}
    _dg_size=${_dg_rest#* }
    _datagrip_report_foreign

    _dg_have=$(_datagrip_version_at "$OSR_DATAGRIP_PREFIX" 2>/dev/null) || _dg_have=""
    if [ "$_dg_have" = "$_dg_ver" ]; then
        info "DataGrip $_dg_ver is already the current release - skipping the download"
        _datagrip_desktop_entry
        return 0
    fi
    if [ -n "$_dg_have" ]; then
        info "upgrading DataGrip $_dg_have -> $_dg_ver"
    else
        info "installing DataGrip $_dg_ver"
    fi

    _dg_parent=$(dirname "$OSR_DATAGRIP_PREFIX")
    as_root mkdir -p "$_dg_parent"
    _dg_tmp=$(as_root mktemp -d "$_dg_parent/.datagrip-XXXXXX") \
        || error "failed to create a staging directory under $_dg_parent"
    # Handed to the invoking user so the download and the unpack are the same
    # unprivileged osr_download/tar every other builder uses (osr_download is a
    # shell function, so it cannot cross an `as_root`/`as_user` sudo boundary);
    # ownership goes back to root after the swap.
    as_root chown "$(id -un)" "$_dg_tmp"
    info "downloading $(basename "$_dg_url") ($((${_dg_size:-0} / 1048576)) MiB)"
    osr_download "$_dg_url" "$_dg_tmp/datagrip.tar.gz" "$_dg_size" \
        || { as_root rm -rf "$_dg_tmp"; error "failed to download $_dg_url"; }
    tar -xzf "$_dg_tmp/datagrip.tar.gz" -C "$_dg_tmp" \
        || { as_root rm -rf "$_dg_tmp"; error "failed to extract the DataGrip tarball"; }
    rm -f "$_dg_tmp/datagrip.tar.gz"
    # The tarball unpacks into a single versioned dir (DataGrip-2026.2.3/).
    _dg_src=$(find "$_dg_tmp" -mindepth 1 -maxdepth 1 -type d | head -n 1)
    [ -n "$_dg_src" ] && [ -f "$_dg_src/product-info.json" ] \
        || { as_root rm -rf "$_dg_tmp"; error "the DataGrip tarball has an unexpected layout (no product-info.json)"; }

    as_root rm -rf "$OSR_DATAGRIP_PREFIX"
    as_root mv "$_dg_src" "$OSR_DATAGRIP_PREFIX" \
        || { as_root rm -rf "$_dg_tmp"; error "failed to install DataGrip into $OSR_DATAGRIP_PREFIX"; }
    as_root rm -rf "$_dg_tmp"
    as_root chown -R 0:0 "$OSR_DATAGRIP_PREFIX"
    _datagrip_desktop_entry
    # /opt is root-owned, so the IDE's own updater cannot patch this tree:
    # `osr module datagrip` (this builder) is the update path.
}

# provide_yandex_browser — Yandex Browser on Debian/Ubuntu from the vendor's own
# apt repo (repo.yandex.ru/yandex-browser/deb), the route yandex.ru/support/
# browser/ru/about/install documents. A repo and not the one-off
# yandex-browser-stable_amd64.deb: the .deb writes the same source list in its
# postinst anyway, so adding it up front is the same end state with apt-managed
# updates from the first run.
#
# The key is installed armored as /etc/apt/keyrings/yandex-browser.asc and
# referenced by signed-by=, which needs no gpg on the box (apt reads armored keys
# by extension) and scopes the key to this one repo instead of trusting it
# archive-wide the way the deprecated apt-key would.
#
# amd64-only: the repo declares i386 but ships an empty index for it, and there
# is no arm build at all. _via_source probes `command -v yandex-browser` (§4),
# but the package installs `yandex-browser-stable` — hence the symlink, which
# both satisfies the probe (a rerun is a no-op) and gives the short command.
provide_yandex_browser() {
    [ "$OSR_ARCH" = x86_64 ] \
        || error "Yandex publishes no Linux $OSR_ARCH browser build (amd64 only)"

    _yb_key=/etc/apt/keyrings/yandex-browser.asc
    _yb_tmp=$(mktemp)
    info "adding the Yandex Browser apt repository"
    osr_download https://repo.yandex.ru/yandex-browser/YANDEX-BROWSER-KEY.GPG "$_yb_tmp" \
        || { rm -f "$_yb_tmp"; error "failed to download the Yandex Browser signing key"; }
    as_root install -Dm 0644 "$_yb_tmp" "$_yb_key"
    rm -f "$_yb_tmp"
    printf 'deb [arch=amd64 signed-by=%s] https://repo.yandex.ru/yandex-browser/deb stable main\n' \
        "$_yb_key" | as_root tee /etc/apt/sources.list.d/yandex-browser.list >/dev/null

    as_root env DEBIAN_FRONTEND=noninteractive apt-get update -q \
        || warn "apt-get update failed after adding the Yandex Browser repo - trying the install anyway"
    as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y yandex-browser-stable
    check_error $? "failed to install yandex-browser-stable"
    as_root ln -sf /usr/bin/yandex-browser-stable /usr/local/bin/yandex-browser

    # The postinst just wrote the vendor's OWN list for this repo, with a
    # different signed-by than ours - which apt 3.0 (Debian 13+) refuses to parse
    # at all, taking every later apt call on the box down with it. Our list was
    # only ever the bootstrap; hand the repo over now that the vendor owns it.
    _apt_prune_bootstrap_lists
    as_root env DEBIAN_FRONTEND=noninteractive apt-get update -q \
        || warn "apt-get update failed after handing the Yandex repo to the vendor list"
}

# _yb_deb_url — echo the current yandex-browser-stable .deb URL, resolved from
# the vendor repo's own Packages index (uncompressed, ~4 KB, one package per
# stanza) so no version is ever hard-coded here (G4).
_yb_deb_url() {
    _yu_base=https://repo.yandex.ru/yandex-browser/deb
    _yu_file=$(osr_fetch_stdout "$_yu_base/dists/stable/main/binary-amd64/Packages" \
        | awk '/^Package: yandex-browser-stable$/ {f=1} f && /^Filename:/ {print $2; exit}')
    [ -n "$_yu_file" ] || error "could not resolve the yandex-browser-stable .deb from the vendor index"
    printf '%s/%s' "$_yu_base" "$_yu_file"
}

# provide_yandex_browser_deb — Yandex Browser on a target with no package of its
# own (Void). The vendor publishes deb + rpm and nothing else, and there is no
# void-packages template (not even in nonfree), so the only route is unpacking
# the official .deb: it is a self-contained Chromium tree under /opt/yandex plus
# a launcher symlink and .desktop entries, with no maintainer scripts that matter
# outside dpkg. bsdtar reads both the outer `ar` container and the inner
# compressed data tarball (xz today), so no dpkg/binutils is needed.
#
# Unpacked into a staging dir first, then ./opt and ./usr are copied into place
# and ./etc is deliberately left behind: everything the deb puts there exists for
# dpkg's world only - a daily cron job that runs `apt-get update`, and an
# autostart entry whose whole job is the "make me your default browser" nag.
#
# The one thing dpkg's postinst did that has to be repeated: the SUID sandbox
# helper gets its setuid bit back. bsdtar drops it when not extracting as root,
# and Chromium refuses to start without either that or unprivileged user
# namespaces. /usr/bin/yandex-browser-stable, which every .desktop entry execs by
# name, is a symlink inside the archive and rides along with the ./usr copy.
#
# Updates: nothing owns this tree, and _via_source's `command -v` probe (§4)
# makes a rerun a no-op, so `osr module yandex-browser` will NOT upgrade it.
# `sudo rm -rf /opt/yandex/browser` first, then rerun, is the update path.
provide_yandex_browser_deb() {
    [ "$OSR_ARCH" = x86_64 ] \
        || error "Yandex publishes no Linux $OSR_ARCH browser build (amd64 only)"

    # The deb declares its shared libraries as dependencies; unpacked, nothing
    # resolves them - the map row lists the same closure under one logical name.
    pkg_install yandex-browser-deps
    command -v bsdtar >/dev/null 2>&1 || error "bsdtar (libarchive) is required to unpack the Yandex Browser .deb"

    _yb_url=$(_yb_deb_url)
    _yb_tmp=$(mktemp -d)
    info "downloading Yandex Browser ($(basename "$_yb_url"))"
    osr_download "$_yb_url" "$_yb_tmp/yb.deb" || { rm -rf "$_yb_tmp"; error "failed to download $_yb_url"; }
    bsdtar -xf "$_yb_tmp/yb.deb" -C "$_yb_tmp" \
        || { rm -rf "$_yb_tmp"; error "failed to open the Yandex Browser .deb"; }
    _yb_data=$(find "$_yb_tmp" -maxdepth 1 -name 'data.tar*' | head -n 1)
    [ -n "$_yb_data" ] || { rm -rf "$_yb_tmp"; error "no data.tar in the Yandex Browser .deb"; }
    mkdir -p "$_yb_tmp/root"
    bsdtar -xf "$_yb_data" -C "$_yb_tmp/root" \
        || { rm -rf "$_yb_tmp"; error "failed to unpack the Yandex Browser .deb"; }
    [ -x "$_yb_tmp/root/opt/yandex/browser/yandex_browser" ] \
        || { rm -rf "$_yb_tmp"; error "no /opt/yandex/browser/yandex_browser in the .deb - its layout changed"; }

    as_root mkdir -p /opt/yandex
    as_root rm -rf /opt/yandex/browser
    as_root cp -a "$_yb_tmp/root/opt/yandex/browser" /opt/yandex/ \
        || { rm -rf "$_yb_tmp"; error "failed to install Yandex Browser into /opt"; }
    # ./usr is the launcher symlink, the two .desktop entries, icons and appdata.
    as_root cp -a "$_yb_tmp/root/usr/." /usr/
    rm -rf "$_yb_tmp"

    as_root chmod 4755 /opt/yandex/browser/yandex_browser-sandbox
    as_root ln -sf /opt/yandex/browser/yandex-browser /usr/local/bin/yandex-browser
    command -v update-desktop-database >/dev/null 2>&1 \
        && as_root update-desktop-database /usr/share/applications >/dev/null 2>&1 || :
}

# provide_proteus — build the theme/wallpaper picker from this repo's Rust crate
# (../proteus). Proteus is part of the dotfiles: it reads this repo's theme
# directory and has no meaning apart from it, so a source build is the only route
# on every target — same class as wezterm (any.map).
#
# Needs a toolchain: list `rust` before `proteus` in the rice (manifest order is
# the dependency graph, §4). modules/rust.sh installs rustup into ~/.cargo.
# Idempotency is _via_source's `command -v proteus` probe (§2).
provide_proteus() {
    _pr_cargo="$OSR_HOME/.cargo/bin/cargo"
    as_user test -x "$_pr_cargo" \
        || error "cargo not found for proteus - install 'rust' before proteus (manifest order, section 4)"

    _pr_src="$OSR_DOTFILES/proteus"
    [ -f "$_pr_src/Cargo.toml" ] \
        || error "proteus sources not found at $_pr_src"

    info "building proteus from $_pr_src"
    # --locked: the Cargo.lock this repo was tested against.
    # --root sets the install prefix; the binary lands in $OSR_HOME/.local/bin,
    # which is on PATH for the shell layers.
    as_user "$_pr_cargo" install --locked --path "$_pr_src" --root "$OSR_HOME/.local" --force
    check_error $? "proteus build failed"
}

# --- GPaste -------------------------------------------------------------------
# GPaste is versioned AGAINST GNOME Shell: v50.x is the branch that speaks the
# Shell 50 extension API, v45.x speaks 45's. "Latest" is therefore the wrong
# question - the right tag is the newest one whose major matches the running
# gnome-shell, which is what _gpaste_tag resolves.
GPASTE_REPO=Keruspe/GPaste

# _gpaste_gnome_major — major version of the installed gnome-shell ("50.1" -> 50).
_gpaste_gnome_major() {
    _gg=$(gnome-shell --version 2>/dev/null) || return 1
    _gg=${_gg##* }
    [ -n "$_gg" ] || return 1
    printf '%s' "${_gg%%.*}"
}

# _gpaste_client_major — major version of the gpaste-client on PATH, or nothing.
# `gpaste-client version` is the local binary's own string; `daemon-version`
# would D-Bus-activate the daemon, which is not something an installer should do.
_gpaste_client_major() {
    _gc=$(gpaste-client version 2>/dev/null) || return 1
    _gc=${_gc##* }
    printf '%s' "${_gc%%.*}"
}

# _gpaste_typelib_ok [root] — true when GPaste-2.typelib sits somewhere
# GIRepository actually searches. That is the introspection libdir it was built
# with, NOT anything reachable via XDG_DATA_DIRS - which is exactly the trap a
# /usr/local prefix falls into. Probing the system libdirs directly beats parsing
# that out of gobject-introspection, and covers multiarch and plain layouts both.
# `root` prefixes the paths (tests pass a fake root; the builder passes nothing).
_gpaste_typelib_ok() {
    for _gk in "lib/$(uname -m)-linux-gnu/girepository-1.0" \
               lib64/girepository-1.0 lib/girepository-1.0; do
        [ -f "${1:-}/usr/$_gk/GPaste-2.typelib" ] && return 0
    done
    return 1
}

# _gpaste_tag <gnome-major> — newest upstream tag on that GNOME major (v50.7),
# falling back to the newest tag overall if the major is not covered yet.
_gpaste_tag() {
    _gt_json=$(osr_fetch_stdout "https://api.github.com/repos/$GPASTE_REPO/tags?per_page=100" 2>/dev/null)
    # One field per line BEFORE the match: sed's .* is greedy, so on a payload
    # that puts several tags on one line it would keep only the last of them.
    _gt_tag=$(printf '%s\n' "$_gt_json" | tr ',' '\n' \
        | sed -n 's/.*"name"[[:space:]]*:[[:space:]]*"\(v[0-9][^"]*\)".*/\1/p' \
        | grep "^v$1\." | sort -V | tail -n 1)
    [ -n "$_gt_tag" ] || _gt_tag=$(github_latest "$GPASTE_REPO")
    printf '%s' "$_gt_tag"
}

# provide_gpaste — build GPaste from source, on the branch matching gnome-shell.
#
# Source is the only route on Debian/Ubuntu: resolute ships 45.3-5, whose only
# concession to modern GNOME is a downstream patch widening metadata.json's
# shell-version list to "50". The JS behind it is still the 45 extension. What
# that costs, all three from one root: the Shell refuses the extension's
# GrabAccelerators call, so the daemon logs "falling back to X11 keybinder" and
# starts polling X selections under Xwayland - that is the flickering selection
# and the twitching panel. And with no working extension, a Wayland session gives
# the daemon no clipboard to watch at all, so the history stays empty and no
# image ever reaches it. One version mismatch, three symptoms.
#
# Idempotency goes BEYOND _via_source's probe (§2), twice over: the probe looks
# for a `gpaste` binary that upstream never installs (it is gpaste-client), and
# an old distro GPaste would satisfy any name-only check while being exactly the
# thing that has to go. So the builder compares majors itself.
#
# -Dvapi=false drops valac: nothing here consumes the Vala bindings. Introspection
# stays ON - the shell extension imports GPaste through GIR and is dead without
# the typelib. The X keybinder stays ON too: it is the fallback that X11 sessions
# without a live extension need, and once the extension matches the Shell it is
# never engaged - fixing the mismatch is what silences it, not deleting it.
#
# The prefix is /usr, not the /usr/local every other builder here uses, and that
# is forced by GIRepository rather than chosen: its typelib search path is the
# libdir gobject-introspection itself was compiled with plus $GI_TYPELIB_PATH -
# XDG_DATA_DIRS does not enter into it. A /usr/local prefix therefore parks
# GPaste-2.typelib somewhere gnome-shell will never look, and the extension dies
# on `Requiring GPaste, version 2: Typelib file ... not found` while every other
# part of the install looks perfectly healthy. Symlinking the two typelibs into
# /usr would write to /usr anyway, just less honestly. Overwriting the distro's
# files is not a concern because they are removed above, not merged with.
provide_gpaste() {
    _gp_major=$(_gpaste_gnome_major) \
        || error "gnome-shell not found - GPaste is a GNOME Shell clipboard manager"

    # Matching versions are necessary but NOT sufficient: a GPaste installed under
    # the wrong prefix reports the right version from a shell extension that
    # cannot load. So the skip needs the typelib to be findable too, or a box left
    # in that state would skip its way out of ever being repaired.
    if [ "$(_gpaste_client_major 2>/dev/null)" = "$_gp_major" ] && _gpaste_typelib_ok; then
        info "GPaste $(gpaste-client version 2>/dev/null) already matches GNOME Shell $_gp_major - skipping the source build"
        return 0
    fi

    # The distro package owns the same D-Bus names, gsettings schema and
    # extension UUID as the build. Two GPastes is one too many - and for the
    # duplicate extension UUID the outcome is a coin toss over which copy the
    # Shell loads - so the old one goes before the new one lands.
    if command -v dpkg-query >/dev/null 2>&1; then
        _gp_old=$(dpkg-query -W -f '${Package} ${Status}\n' \
            'gpaste-2' 'libgpaste-2' 'libgpaste-2-common' 'gir1.2-gpaste-2' \
            'gnome-shell-extension-gpaste' 2>/dev/null \
            | sed -n 's/ install ok installed$//p')
        if [ -n "$_gp_old" ]; then
            info "removing the distro GPaste ($(echo $_gp_old | tr '\n' ' '))"
            as_root apt-get remove -y $_gp_old || warn "could not remove the distro GPaste - the build may collide with it"
        fi
    fi

    # An earlier build of this module used a /usr/local prefix (see the typelib
    # note above for why it cannot work). Left in place it is worse than useless:
    # /usr/local/bin precedes /usr/bin on PATH, so the broken gpaste-client keeps
    # winning, and /usr/local/share is ahead of /usr/share in XDG_DATA_DIRS, so
    # the Shell keeps loading the extension that cannot find its typelib. Every
    # path GPaste installs there carries "gpaste" in its name, which is what
    # makes this narrow enough to delete.
    if [ -x /usr/local/bin/gpaste-client ]; then
        info "removing the earlier /usr/local GPaste (wrong prefix for the typelib)"
        for _gp_dir in bin libexec lib include share; do
            [ -d "/usr/local/$_gp_dir" ] || continue
            as_root find "/usr/local/$_gp_dir" -iname '*gpaste*' -depth -exec rm -rf {} + 2>/dev/null || true
        done
    fi

    pkg_install build gpaste-build-deps

    _gp_tag=$(_gpaste_tag "$_gp_major")
    info "building GPaste $_gp_tag for GNOME Shell $_gp_major"
    _gp_tmp=$(mktemp -d)
    osr_download "https://github.com/$GPASTE_REPO/archive/refs/tags/${_gp_tag}.tar.gz" \
        "$_gp_tmp/gpaste.tar.gz" || { rm -rf "$_gp_tmp"; error "failed to download GPaste $_gp_tag"; }
    tar -xf "$_gp_tmp/gpaste.tar.gz" -C "$_gp_tmp" \
        || { rm -rf "$_gp_tmp"; error "failed to extract GPaste $_gp_tag"; }

    _gp_src="$_gp_tmp/GPaste-${_gp_tag#v}"
    [ -f "$_gp_src/meson.build" ] \
        || { rm -rf "$_gp_tmp"; error "no meson.build in the GPaste tarball - its layout changed"; }

    # Debian/Ubuntu put libs and typelibs under a multiarch libdir. Only pass it
    # when that dir is really there - the point is to land beside the system's
    # own typelibs, so the system's own layout is the thing worth copying.
    _gp_libdir=
    [ -d "/usr/lib/$(uname -m)-linux-gnu" ] && _gp_libdir="--libdir=lib/$(uname -m)-linux-gnu"

    env PKG_CONFIG_PATH="$(_osr_pkgconfig_path)" meson setup "$_gp_src/build" "$_gp_src" \
        --prefix=/usr \
        $_gp_libdir \
        --buildtype=release \
        -Dvapi=false \
        -Ddbus-services-dir=/usr/share/dbus-1/services \
        -Dcontrol-center-keybindings-dir=/usr/share/gnome-control-center/keybindings \
        -Dsystemd-user-unit-dir=/usr/lib/systemd/user \
        || { rm -rf "$_gp_tmp"; error "GPaste meson configure failed"; }
    meson compile -C "$_gp_src/build" -j "${OSR_BUILD_JOBS:-$(nproc 2>/dev/null || echo 2)}" \
        || { rm -rf "$_gp_tmp"; error "GPaste build failed"; }
    as_root meson install -C "$_gp_src/build" \
        || { rm -rf "$_gp_tmp"; error "GPaste install failed"; }

    # libgpaste lands in /usr/local/lib; refresh the loader cache so the daemon
    # finds it on distros that do not scan that dir by default.
    as_root ldconfig >/dev/null 2>&1 || true
    rm -rf "$_gp_tmp"

    command -v gpaste-client >/dev/null 2>&1 \
        || error "GPaste installed but gpaste-client is not on PATH"
}
