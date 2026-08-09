#!/bin/sh
# Proves the Überzug++ route. On a graphical session yazi picks the Ueberzug
# driver, not chafa: X11 returns it UNCONDITIONALLY (no compositor check, no
# chafa fallback), Wayland only under sway/Hyprland/niri/Wayfire. So the module's
# gate has to mirror Drivers::matches() in yazi, or a desktop gets a blank
# preview pane however new chafa is - and a container gets a pointless C++ build.
# Hermetic: no net, no cmake, session env is set per scenario.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB
NO_COLOR=1
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/net.sh"; . "$OSR_LIB/pkg.sh"
. "$OSR_LIB/build.sh"
. "$HERE/../lib.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
OUT="$TMP/out"; : >"$OUT"
BIN="$TMP/bin"; mkdir -p "$BIN"; PATH="$BIN:$PATH"; export PATH
# chafa new enough, so the chafa branch never fires and only the ueberzug
# decision is under test here.
printf '#!/bin/sh\nprintf "Chafa version 1.18.2\\n"\n' >"$BIN/chafa"; chmod +x "$BIN/chafa"

run_step()      { shift; "$@"; }
pkg_install()   { echo "PKG $*" >>"$OUT"; }
install_layer() { :; }
as_user()       { :; }
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd); export OSR_DOTFILES
OSR_HOME="$TMP/home"; export OSR_HOME OSR_USER=tester
OSR_THEME_DIR=''

# session <XDG_SESSION_TYPE> <DISPLAY> <WAYLAND_DISPLAY> <compositor-var-or-->
session() {
    : >"$OUT"
    XDG_SESSION_TYPE=$1; DISPLAY=$2; WAYLAND_DISPLAY=$3
    NIRI_SOCKET=''; SWAYSOCK=''; HYPRLAND_INSTANCE_SIGNATURE=''; WAYFIRE_SOCKET=''
    [ "$4" = - ] || eval "$4=/run/user/1000/sock"
    export XDG_SESSION_TYPE DISPLAY WAYLAND_DISPLAY \
           NIRI_SOCKET SWAYSOCK HYPRLAND_INSTANCE_SIGNATURE WAYFIRE_SOCKET
}

# --- the gate ----------------------------------------------------------------
session x11 :0 '' -
. "$OSR_ROOT/modules/yazi.sh" >/dev/null 2>&1
assert_contains "$OUT" "PKG ueberzugpp" "X11 session (the Debian/Alacritty box) installs ueberzugpp"

session '' :0 '' -
. "$OSR_ROOT/modules/yazi.sh" >/dev/null 2>&1
assert_contains "$OUT" "PKG ueberzugpp" "bare DISPLAY with no XDG_SESSION_TYPE is still X11"

session wayland '' wayland-0 HYPRLAND_INSTANCE_SIGNATURE
. "$OSR_ROOT/modules/yazi.sh" >/dev/null 2>&1
assert_contains "$OUT" "PKG ueberzugpp" "Wayland + Hyprland: yazi routes to Ueberzug, so install it"

session wayland '' wayland-0 SWAYSOCK
. "$OSR_ROOT/modules/yazi.sh" >/dev/null 2>&1
assert_contains "$OUT" "PKG ueberzugpp" "Wayland + sway: same"

session wayland '' wayland-0 -
. "$OSR_ROOT/modules/yazi.sh" >/dev/null 2>&1
refute_contains "$OUT" "PKG ueberzugpp" "Wayland on an unsupported compositor: yazi uses chafa, no build"

session '' '' '' -
. "$OSR_ROOT/modules/yazi.sh" >/dev/null 2>&1
refute_contains "$OUT" "PKG ueberzugpp" "headless (container/SSH): chafa is the adapter, no desktop build"
assert_contains "$OUT" "PKG yazi chafa" "headless still installs yazi + chafa"

# X11 wins even when a Wayland compositor var is somehow also set: that is the
# order yazi checks in, and getting it backwards installs the wrong thing.
session x11 :0 '' SWAYSOCK
. "$OSR_ROOT/modules/yazi.sh" >/dev/null 2>&1
assert_contains "$OUT" "PKG ueberzugpp" "XDG_SESSION_TYPE=x11 is checked before any Wayland signal"

# --- map rows ----------------------------------------------------------------
OSR_CODENAME=''; OSR_VERSION_ID=''; OSR_ARCH=x86_64
for _pm in apt dnf apk xbps; do
    OSR_PKG=$_pm
    assert_eq "source:provide_ueberzugpp" "$(_pkgmap_one ueberzugpp)" \
        "$_pm has no packaged ueberzugpp -> source builder"
    case "$(_pkgmap_one ueberzugpp-build-deps)" in
        *vips*) ok "$_pm resolves ueberzugpp-build-deps (libvips: the ENABLE_OPENCV=OFF image lib)" ;;
        *)      fail "$_pm ueberzugpp-build-deps lacks vips" ;;
    esac
done
for _pm in pacman portage; do
    OSR_PKG=$_pm
    assert_eq "ueberzugpp" "$(_pkgmap_one ueberzugpp)" "$_pm keeps ueberzugpp native (it is packaged there)"
done

# --- builder: deps first, then the upstream tag tarball ----------------------
OSR_PKG=apt
github_latest() { echo v2.9.10; }
osr_download()  { echo "DL $1" >>"$OUT"; return 1; }   # stop before cmake
error()         { echo "ERROR $*" >>"$OUT"; return 1; }
: >"$OUT"
provide_ueberzugpp >/dev/null 2>&1 || true
assert_contains "$OUT" "PKG build cmake ueberzugpp-build-deps" "builder installs the toolchain + deps"
assert_contains "$OUT" \
    "DL https://github.com/jstkdng/ueberzugpp/archive/refs/tags/v2.9.10.tar.gz" \
    "builder fetches the upstream tag tarball"

finish
