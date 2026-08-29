#!/bin/sh
# Proves modules/yandex-browser.c: the per-distro install route (apt -> vendor
# repo builder, pacman -> AUR) and the low-RAM flags layer — dotfiles-owned
# switches stamped into a user-level copy of every launcher, before %U, on every
# Exec= line. Hermetic (no net/root).
#
# The routing half is a pkgmap question and is asked of lib/pkg.sh's _pkgmap_one
# directly; the flags half is the module, which is C now and so runs through the
# core with a `yandex-browser` already on PATH — that is the source: provider's
# own §2 probe, and it is what keeps this test from building a browser.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_DOTFILES OSR_PKG=apt
NO_COLOR=1; OSR_USER=$(id -un); export OSR_USER   # as_user becomes a no-op
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"; . "$OSR_LIB/config.sh"; . "$OSR_LIB/net.sh"
. "$OSR_LIB/pkg.sh"; . "$OSR_LIB/build.sh"
. "$HERE/../lib.sh"

OUT=$(mktemp)
run_step() { shift; "$@"; }
pkg_install() { echo "PKG $*" >>"$OUT"; }

# --- pkgmap: the install route per target ------------------------------------
OSR_ARCH=x86_64; export OSR_ARCH
assert_eq "source:provide_yandex_browser" "$(_pkgmap_one yandex-browser)" \
    "apt takes the vendor repo builder (no Debian/Ubuntu archive carries it)"
OSR_PKG=pacman
assert_eq "aur:yandex-browser" "$(_pkgmap_one yandex-browser)" "pacman keeps the AUR package"
OSR_PKG=xbps
assert_eq "source:provide_yandex_browser_deb" "$(_pkgmap_one yandex-browser)" \
    "xbps unpacks the vendor .deb (Void packages it nowhere)"
# The unpacked .deb resolves no shared libraries of its own - the closure is a row.
assert_contains "$OSR_LIB/pkgmap/xbps.map" '^yandex-browser-deps = .*gtk+3.*nss' \
    "the dependency closure the deb would have pulled is listed for xbps"
for _p in dnf apk portage; do
    OSR_PKG=$_p
    assert_eq "yandex-browser" "$(_pkgmap_one yandex-browser)" \
        "$_p has no row - pkg_install fails loudly instead of installing a lookalike"
done
OSR_PKG=apt
for _b in provide_yandex_browser provide_yandex_browser_deb; do
    if command -v "$_b" >/dev/null 2>&1; then
        ok "$_b builder is defined (source: row resolves)"
    else
        fail "$_b builder missing"
    fi
done

# --- the flags layer ---------------------------------------------------------
OSR_HOME=$(mktemp -d); export OSR_HOME
SYS=$(mktemp -d); OSR_DESKTOP_DIRS="$SYS"; export OSR_DESKTOP_DIRS
# Both names the deb ships; the reverse-DNS one must be tuned too.
cat >"$SYS/ru.yandex.desktop.browser.desktop" <<'EOF'
[Desktop Entry]
Name=Yandex Browser
Exec=/usr/bin/yandex-browser-stable %U
Type=Application
Actions=new-private-window;

[Desktop Action new-private-window]
Name=New incognito window
Exec=/usr/bin/yandex-browser-stable --incognito
EOF

cp "$SYS/ru.yandex.desktop.browser.desktop" "$SYS/yandex-browser.desktop"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip yandex_browser_module: %s is not built\n' "$OSR_BIN"
    finish
fi
BIN=$(mktemp -d)
for _t in sh env cat grep sed printf id rm mkdir mktemp test true false tee \
          chmod cut tr head sort wc dirname basename; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in /*) ln -sf "$_p" "$BIN/$_t" ;; esac
done
# The browser itself: present, so the source: provider skips the build (§2).
printf '#!/bin/sh\nexit 0\n' >"$BIN/yandex-browser"; chmod +x "$BIN/yandex-browser"

run_module() {
    env -i PATH="$BIN" OSR_ROOT="$OSR_ROOT" OSR_LIB="$OSR_LIB" \
        OSR_DOTFILES="$OSR_DOTFILES" OSR_PKG=apt OSR_ARCH=x86_64 \
        OSR_DISTRO=ubuntu OSR_CODENAME=noble OSR_INIT=systemd \
        OSR_USER="$OSR_USER" OSR_HOME="$OSR_HOME" HOME="$OSR_HOME" \
        OSR_DESKTOP_DIRS="$OSR_DESKTOP_DIRS" NO_COLOR=1 TERM=dumb \
        "$OSR_BIN" module run yandex-browser
}

run_module >"$OUT" 2>&1 || :

for _e in ru.yandex.desktop.browser yandex-browser; do
    DST="$OSR_HOME/.local/share/applications/$_e.desktop"
    assert_contains "$DST" '^Exec=/usr/bin/yandex-browser-stable --process-per-site .* %U$' \
        "$_e: flags land after the binary and before the %U field code"
    assert_contains "$DST" '^Exec=.*--renderer-process-limit=4.*--incognito$' \
        "$_e: the [Desktop Action] Exec is tuned too"
    assert_contains "$DST" '^Name=Yandex Browser$' "$_e: the rest of the entry is left as packaged"
    refute_contains "$DST" '#' "$_e: flags.conf comments are stripped, not passed as switches"
done

# A machine where the browser has no launcher yet: warn, never fail.
rm -f "$SYS"/*.desktop
ERR=$(mktemp)
run_module >/dev/null 2>"$ERR" || :
assert_contains "$ERR" 'low-RAM flags are not applied' "a missing .desktop warns instead of silently skipping"

rm -rf "$OSR_HOME" "$SYS" "$BIN"; rm -f "$OUT" "$ERR"
finish
