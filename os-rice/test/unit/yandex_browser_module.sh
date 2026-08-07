#!/bin/sh
# Proves modules/yandex-browser.sh: the per-distro install route (apt -> vendor
# repo builder, pacman -> AUR) and the low-RAM flags layer — dotfiles-owned
# switches stamped into a user-level copy of every launcher, before %U, on every
# Exec= line. Hermetic (no net/root; pkg_install is stubbed).
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
for _p in dnf xbps apk portage; do
    OSR_PKG=$_p
    assert_eq "yandex-browser" "$(_pkgmap_one yandex-browser)" \
        "$_p has no row - pkg_install fails loudly instead of installing a lookalike"
done
OSR_PKG=apt
if command -v provide_yandex_browser >/dev/null 2>&1; then
    ok "provide_yandex_browser builder is defined (source: row resolves)"
else
    fail "provide_yandex_browser builder missing"
fi

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

. "$OSR_ROOT/modules/yandex-browser.sh"

assert_contains "$OUT" 'PKG yandex-browser' "installs yandex-browser via pkg_install"
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
. "$OSR_ROOT/modules/yandex-browser.sh" 2>"$ERR"
assert_contains "$ERR" 'low-RAM flags are not applied' "a missing .desktop warns instead of silently skipping"

rm -rf "$OSR_HOME" "$SYS"; rm -f "$OUT" "$ERR"
finish
