#!/bin/sh
# Proves the Überzug++ route. On a graphical session yazi picks the Ueberzug
# driver, not chafa: X11 returns it UNCONDITIONALLY (no compositor check, no
# chafa fallback), Wayland only under sway/Hyprland/niri/Wayfire. So the module's
# gate has to mirror Drivers::matches() in yazi, or a desktop gets a blank
# preview pane however new chafa is - and a container gets a pointless C++ build.
# Hermetic: no net, no cmake, session env is set per scenario.
#
# The module is C now (modules/yazi.c), so the gate runs through the core with
# PATH reduced to a stub bin/ and the package manager a logging stub; the
# builder half below still exercises lib/build.sh directly, because that is the
# tier `_pkgmap_one` and `provide_ueberzugpp` belong to.
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

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip ueberzug_module: %s is not built\n' "$OSR_BIN"
    exit 0
fi
# The stub bin the C module runs inside: `apt-get`/`dpkg` stand in for the
# package layer and log what was asked for, which is the whole observation.
for _t in sh env cat grep sed printf id rm mkdir mktemp test true false tee \
          cp chmod cut tr head sort wc dirname basename; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in /*) ln -sf "$_p" "$BIN/$_t" ;; esac
done
cat >"$BIN/sudo" <<'EOF'
#!/bin/sh
[ "$1" = "-u" ] && shift 2
exec "$@"
EOF
# pacman, because yazi/chafa/ueberzugpp are all NATIVE rows there: an apt target
# routes them through source: builders instead, and this test is about which
# packages the module asks for, not how a given distro obtains them.
cat >"$BIN/pacman" <<'EOF'
#!/bin/sh
# One PKG line per install command, naming the batch: that is the shape the
# assertions below were written against when pkg_install was a shell stub.
[ "$1" = "-Q" ] && exit 1
_seen=0
_pkgs=""
for _a in "$@"; do
    case "$_a" in -*) [ "$_a" = "-S" ] && _seen=1; continue ;; esac
    [ "$_seen" = 1 ] && _pkgs="$_pkgs $_a"
done
[ -n "$_pkgs" ] && printf 'PKG%s\n' "$_pkgs" >>"$OUT"
exit 0
EOF
# `yazi` is a source: row on every target (any.map -> provide_yazi_bin), so a
# yazi already on PATH is what keeps this test from building one: that is the
# provider's own §2 probe. What is under test is the ueberzugpp decision beside
# it, which is a native row on pacman.
printf '#!/bin/sh\nexit 0\n' >"$BIN/yazi"
chmod +x "$BIN/sudo" "$BIN/pacman" "$BIN/yazi"

# run_yazi — the C module, with only the session variables varying.
run_yazi() {
    env -i PATH="$BIN" OUT="$OUT" OSR_ROOT="$OSR_ROOT" OSR_LIB="$OSR_LIB" \
        OSR_DOTFILES="$OSR_DOTFILES" OSR_PKG=pacman OSR_ARCH=x86_64 \
        OSR_DISTRO=arch OSR_INIT=systemd OSR_USER=tester \
        OSR_HOME="$OSR_HOME" HOME="$OSR_HOME" NO_COLOR=1 TERM=dumb \
        XDG_SESSION_TYPE="$XDG_SESSION_TYPE" DISPLAY="$DISPLAY" \
        WAYLAND_DISPLAY="$WAYLAND_DISPLAY" NIRI_SOCKET="$NIRI_SOCKET" \
        SWAYSOCK="$SWAYSOCK" WAYFIRE_SOCKET="$WAYFIRE_SOCKET" \
        HYPRLAND_INSTANCE_SIGNATURE="$HYPRLAND_INSTANCE_SIGNATURE" \
        "$OSR_BIN" module run yazi >/dev/null 2>&1 || :
}

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
run_yazi
assert_contains "$OUT" "PKG ueberzugpp" "X11 session (the Debian/Alacritty box) installs ueberzugpp"

session '' :0 '' -
run_yazi
assert_contains "$OUT" "PKG ueberzugpp" "bare DISPLAY with no XDG_SESSION_TYPE is still X11"

session wayland '' wayland-0 HYPRLAND_INSTANCE_SIGNATURE
run_yazi
assert_contains "$OUT" "PKG ueberzugpp" "Wayland + Hyprland: yazi routes to Ueberzug, so install it"

session wayland '' wayland-0 SWAYSOCK
run_yazi
assert_contains "$OUT" "PKG ueberzugpp" "Wayland + sway: same"

session wayland '' wayland-0 -
run_yazi
refute_contains "$OUT" "PKG ueberzugpp" "Wayland on an unsupported compositor: yazi uses chafa, no build"

session '' '' '' -
run_yazi
refute_contains "$OUT" "PKG ueberzugpp" "headless (container/SSH): chafa is the adapter, no desktop build"
assert_contains "$OUT" "PKG chafa" "headless still installs the chafa adapter"

# X11 wins even when a Wayland compositor var is somehow also set: that is the
# order yazi checks in, and getting it backwards installs the wrong thing.
session x11 :0 '' SWAYSOCK
run_yazi
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
