#!/bin/sh
# Proves the AmneziaVPN install order: the upstream RELEASE BINARY first, the
# source build only as the last resort (no asset for this arch, or GitHub
# unreachable) — one builder, no chain across map rows. Also proves the source
# recipe itself: cmake driven directly (configure as OSR_USER, install as root,
# component AmneziaVPN), unit/desktop/icon + PATH symlinks, all-skip on rerun
# (§2). Hermetic: no net, no compiler — every call is captured, nothing is built.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_PKG=pacman
NO_COLOR=1
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/net.sh"; . "$OSR_LIB/pkg.sh"
. "$OSR_LIB/service.sh"; . "$OSR_LIB/build.sh"
. "$HERE/../lib.sh"

# Hermetic PATH: the §2 probe is `command -v amneziavpn|AmneziaVPN`, so a real
# install on the developer's own box would make every scenario skip. /usr/bin:/bin
# covers what the libs shell out to and holds neither name.
PATH=/usr/bin:/bin; export PATH

OUT=$(mktemp)
OSR_HOME=$(mktemp -d); export OSR_HOME OSR_USER=tester
OSR_ARCH=x86_64; OSR_INIT=systemd; OSR_CODENAME=''; OSR_VERSION_ID=''
TMPDIR=$(mktemp -d); export TMPDIR

as_user() { echo "USER $*" >>"$OUT"; }
as_root() { echo "ROOT $*" >>"$OUT"; }
pkg_install() { echo "PKG $*" >>"$OUT"; }
enable_service() { echo "SERVICE $*" >>"$OUT"; }
# Stand in for the release fetch; $TAG_RC / $DL_RC decide whether a ready binary
# is available at all. github_latest error()s for real, so failure = non-zero here.
github_latest() { [ "${TAG_RC:-0}" = 0 ] || return 1; echo 4.8.21.0; }
osr_download() { echo "DL $1" >>"$OUT"; return "${DL_RC:-0}"; }
# The extracted QtIFW installer: created only when the download "succeeded".
tar() { [ "${DL_RC:-0}" = 0 ] || return 1; : >"$_av_tmp/AmneziaVPN.bin"; }

# --- map rows: the source build is nobody's route; it is reached, not selected -
assert_eq "aur:amneziavpn-bin" "$(_pkgmap_one amneziavpn)" \
    "Arch installs the pacman-tracked release binary from the AUR"
OSR_PKG=apt
assert_eq "source:provide_amneziavpn" "$(_pkgmap_one amneziavpn)" \
    "apt enters through provide_amneziavpn (release binary first)"
assert_eq "amneziavpn-src" "$(_pkgmap_one amneziavpn-src)" \
    "no amneziavpn-src row exists — the source build is not a selectable route"
OSR_PKG=pacman

# --- ready binary available -> installed, NOTHING is built -------------------
TAG_RC=0; DL_RC=0
provide_amneziavpn >/dev/null 2>&1
assert_contains "$OUT" "DL https://github.com/amnezia-vpn/amnezia-client/releases/download/4.8.21.0/" \
    "fetches the upstream release tarball"
assert_contains "$OUT" "ROOT .*\.bin install --root /opt/AmneziaVPN" "runs the QtIFW installer headless"
assert_contains "$OUT" "ROOT ln -sf /opt/AmneziaVPN/AmneziaVPN /usr/local/bin/amneziavpn" "symlinks onto PATH"
refute_contains "$OUT" "cmake" "no source build while a ready binary exists"
refute_contains "$OUT" "git clone" "no checkout while a ready binary exists"

# --- release unreachable (GitHub API down) -> source fallback ----------------
: >"$OUT"
TAG_RC=1; DL_RC=0
provide_amneziavpn >/dev/null 2>&1
assert_contains "$OUT" "USER git clone --depth 1 --recursive" "unresolved tag falls back to the source build"

# --- release download fails -> source fallback -------------------------------
: >"$OUT"
TAG_RC=0; DL_RC=1
provide_amneziavpn >/dev/null 2>&1
assert_contains "$OUT" "^DL " "the release download is still attempted first"
assert_contains "$OUT" "USER cmake -S " "a failed download falls back to the source build"

# --- no release asset for this arch -> source fallback, no download attempt --
: >"$OUT"
OSR_ARCH=aarch64; TAG_RC=0; DL_RC=0
provide_amneziavpn >/dev/null 2>&1
refute_contains "$OUT" "^DL " "unsupported arch does not attempt a release download"
assert_contains "$OUT" "USER git clone" "unsupported arch falls back to the source build"
OSR_ARCH=x86_64

# --- the source recipe itself ------------------------------------------------
: >"$OUT"
provide_amneziavpn_source >/dev/null 2>&1
assert_contains "$OUT" "PKG build cmake git conan openssl" "installs the toolchain + conan"
assert_contains "$OUT" "qt6-5compat" "installs the Qt6 components the client needs"
refute_contains "$OUT" "qt6-webengine" "does not drag in qt6-webengine (nothing links it)"
assert_contains "$OUT" "USER cmake -S .* -DCMAKE_PREFIX_PATH=/usr" "configures against the distro Qt6, as the user"
assert_contains "$OUT" "DCMAKE_INSTALL_PREFIX=/opt/AmneziaVPN" "installs into the prefix the shipped unit expects"
assert_contains "$OUT" "USER env CMAKE_BUILD_PARALLEL_LEVEL=1 cmake --build" "build is serialized by default (>24GB link peak)"
assert_contains "$OUT" "ROOT cmake --install .* --component AmneziaVPN" "installs the AmneziaVPN component as root"
assert_contains "$OUT" "/etc/systemd/system/AmneziaVPN.service" "places the privileged-helper unit"
assert_contains "$OUT" "SERVICE AmneziaVPN" "enables the helper service on systemd"
assert_contains "$OUT" "/usr/local/bin/amneziavpn" "symlinks the lowercase name (matches the release route)"
assert_contains "$OUT" "/usr/local/bin/AmneziaVPN" "symlinks the .desktop Exec= name"
assert_contains "$OUT" "/usr/share/applications/AmneziaVPN.desktop" "installs the desktop entry"

# --- OSR_BUILD_JOBS is the RAM knob ------------------------------------------
: >"$OUT"
OSR_BUILD_JOBS=4 provide_amneziavpn_source >/dev/null 2>&1
assert_contains "$OUT" "CMAKE_BUILD_PARALLEL_LEVEL=4" "OSR_BUILD_JOBS raises build parallelism"

# --- non-systemd: no unit, but the client still installs ---------------------
: >"$OUT"
OSR_INIT=openrc
provide_amneziavpn_source >/dev/null 2>&1
refute_contains "$OUT" "SERVICE AmneziaVPN" "no service enable on a non-systemd init"
assert_contains "$OUT" "ROOT cmake --install" "the client is still installed on a non-systemd init"
OSR_INIT=systemd

# --- rerun with AmneziaVPN present -> all-skip (§2) --------------------------
: >"$OUT"
BIN=$(mktemp -d); : >"$BIN/AmneziaVPN"; chmod +x "$BIN/AmneziaVPN"; PATH="$BIN:$PATH"
CAP=$(provide_amneziavpn_source 2>&1)
printf '%s\n' "$CAP" | grep -q 'skipping' && ok "rerun all-skips when AmneziaVPN is present (§2)" \
    || fail "builder did not skip with AmneziaVPN already on PATH"
refute_contains "$OUT" "cmake" "nothing is rebuilt on a rerun"

rm -rf "$OSR_HOME" "$TMPDIR" "$BIN"; rm -f "$OUT"
finish
