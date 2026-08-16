#!/bin/sh
# Proves the DataGrip route: the map row resolves to the vendor tarball on every
# manager, the JetBrains release feed is parsed per-arch, and provide_datagrip is
# VERSION-idempotent - it skips the ~1 GB download when the installed tree is
# already current and replaces the tree when it is behind, writing the launcher
# symlink and the .desktop entry either way. Hermetic (no net, no root, no /opt:
# the prefix is pointed at a sandbox and every download is stubbed).
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_DOTFILES OSR_PKG=apt
NO_COLOR=1; OSR_USER=$(id -un); export OSR_USER   # as_user becomes a no-op
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"
. "$OSR_LIB/net.sh"; . "$OSR_LIB/pkg.sh"; . "$OSR_LIB/build.sh"
. "$HERE/../lib.sh"

SANDBOX=$(mktemp -d)
OUT="$SANDBOX/calls"; DESK="$SANDBOX/datagrip.desktop"; : >"$OUT"
OSR_DATAGRIP_PREFIX="$SANDBOX/opt/datagrip"
OSR_HOME="$SANDBOX/home"; mkdir -p "$OSR_HOME"

run_step() { shift; "$@"; }
pkg_install() { echo "PKG $*" >>"$OUT"; }
# Root steps run for real inside the sandbox; the three that would touch the
# system are recorded instead (tee keeps its payload so the entry is assertable).
as_root() {
    case "$1" in
        tee) shift; cat >"$DESK"; echo "ROOT tee $*" >>"$OUT" ;;
        ln|update-desktop-database) echo "ROOT $*" >>"$OUT" ;;
        chown) echo "ROOT $*" >>"$OUT" ;;
        *) "$@" ;;
    esac
}

# --- the feed: one arch key per download, no version hard-coded --------------
FEED='{"DG":[{"date":"2026-08-04","type":"release","downloads":{"linuxARM64":{"link":"https://download.jetbrains.com/datagrip/datagrip-2026.2.3-aarch64.tar.gz","size":1105857143},"linux":{"link":"https://download.jetbrains.com/datagrip/datagrip-2026.2.3.tar.gz","size":1082236229},"windows":{"link":"https://download.jetbrains.com/datagrip/datagrip-2026.2.3.exe","size":844943592}},"patches":{"unix":[{"link":"https://download.jetbrains.com/datagrip/DB-262-patch-unix.jar"}]},"version":"2026.2.3","majorVersion":"2026.2","build":"262.9437.163"}]}'
osr_fetch_stdout() { printf '%s' "$FEED"; }

for _p in apt dnf pacman apk xbps portage; do
    OSR_PKG=$_p
    assert_eq "source:provide_datagrip" "$(_pkgmap_one datagrip)" \
        "$_p resolves datagrip to the vendor tarball builder"
done
OSR_PKG=apt

OSR_ARCH=x86_64
assert_eq "2026.2.3 https://download.jetbrains.com/datagrip/datagrip-2026.2.3.tar.gz 1082236229" \
    "$(_datagrip_latest)" "x86_64 takes the linux tarball + its size (not linuxARM64, not a patch jar)"
OSR_ARCH=aarch64
assert_eq "2026.2.3 https://download.jetbrains.com/datagrip/datagrip-2026.2.3-aarch64.tar.gz 1105857143" \
    "$(_datagrip_latest)" "aarch64 takes the linuxARM64 tarball + its size"
OSR_ARCH=riscv64
( _datagrip_latest >/dev/null 2>&1 ) && fail "an arch JetBrains does not build for should error" \
    || ok "an arch JetBrains does not build for errors instead of 404ing later"
OSR_ARCH=x86_64

# --- a stubbed "download": produce the tarball layout the vendor ships --------
# DataGrip-<version>/ at the root, product-info.json beside bin/.
DG_TARBALL_VERSION=2026.2.3
osr_download() {
    echo "DOWNLOAD $1 size=$3" >>"$OUT"
    _stage="$SANDBOX/fake"; rm -rf "$_stage"
    mkdir -p "$_stage/DataGrip-$DG_TARBALL_VERSION/bin"
    printf '{"name":"DataGrip","version":"%s","buildNumber":"262.9437.163"}\n' \
        "$DG_TARBALL_VERSION" >"$_stage/DataGrip-$DG_TARBALL_VERSION/product-info.json"
    printf '#!/bin/sh\n' >"$_stage/DataGrip-$DG_TARBALL_VERSION/bin/datagrip"
    chmod +x "$_stage/DataGrip-$DG_TARBALL_VERSION/bin/datagrip"
    : >"$_stage/DataGrip-$DG_TARBALL_VERSION/bin/datagrip.png"
    tar -czf "$2" -C "$_stage" "DataGrip-$DG_TARBALL_VERSION"
}

# --- first install ------------------------------------------------------------
provide_datagrip
assert_contains "$OUT" "DOWNLOAD https://download.jetbrains.com/datagrip/datagrip-2026.2.3.tar.gz size=1082236229" \
    "a box with no DataGrip downloads the current tarball, size in hand for the progress readout"
assert_eq "2026.2.3" "$(_datagrip_version_at "$OSR_DATAGRIP_PREFIX")" \
    "the tree lands at the prefix with its product-info.json"
assert_contains "$DESK" "^Exec=$OSR_DATAGRIP_PREFIX/bin/datagrip %f" "the entry execs the installed launcher"
assert_contains "$DESK" "^Icon=$OSR_DATAGRIP_PREFIX/bin/datagrip.png" "the entry uses the icon out of the tarball"
assert_contains "$DESK" "^StartupWMClass=jetbrains-datagrip" "the entry carries the IDE's real WM class"
assert_contains "$OUT" "ROOT ln -sf $OSR_DATAGRIP_PREFIX/bin/datagrip /usr/local/bin/datagrip" \
    "datagrip goes on PATH via /usr/local/bin"
assert_eq "" "$(find "$SANDBOX/opt" -maxdepth 1 -name '.datagrip-*' 2>/dev/null)" \
    "the staging directory is cleaned up"

# --- rerun on a current box: no download, entry still repaired ---------------
: >"$OUT"; rm -f "$DESK"
provide_datagrip
refute_contains "$OUT" "DOWNLOAD" "an up-to-date tree skips the ~1 GB download"
assert_contains "$DESK" "^Exec=" "the .desktop entry is rewritten even when nothing was downloaded"

# --- upstream moves ahead: the old tree is replaced in place -----------------
: >"$OUT"
FEED=$(printf '%s' "$FEED" | sed 's/2026\.2\.3/2026.2.4/g')
DG_TARBALL_VERSION=2026.2.4
provide_datagrip
assert_contains "$OUT" "DOWNLOAD https://download.jetbrains.com/datagrip/datagrip-2026.2.4.tar.gz" \
    "a behind tree downloads the new release"
assert_eq "2026.2.4" "$(_datagrip_version_at "$OSR_DATAGRIP_PREFIX")" \
    "the upgrade replaces the tree at the same prefix"
assert_eq "1" "$(find "$SANDBOX/opt" -maxdepth 1 -type d ! -path "$SANDBOX/opt" | wc -l | tr -d ' ')" \
    "no second tree is left behind next to it"

# --- foreign installs are reported, never removed ----------------------------
mkdir -p "$OSR_HOME/.local/share/JetBrains/Toolbox/apps/DataGrip"
WARNED=$(_datagrip_report_foreign 2>&1)
printf '%s' "$WARNED" >"$SANDBOX/warned"
assert_contains "$SANDBOX/warned" "Toolbox" "a Toolbox DataGrip is reported"
[ -d "$OSR_HOME/.local/share/JetBrains/Toolbox/apps/DataGrip" ] \
    && ok "the Toolbox install is left in place" || fail "the Toolbox install was touched"

rm -rf "$SANDBOX"
finish
