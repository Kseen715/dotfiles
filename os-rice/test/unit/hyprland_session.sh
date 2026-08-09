#!/bin/sh
# Proves modules/hyprland.sh lays down a complete session: every autostart script
# hyprland.conf's exec-once lines reference (the port had dropped start-mako and
# start-amnezia-vpn-client), the wallpaper env resolved to a real path, and the
# VMware session entry - installed only under a VMware hypervisor, alongside the
# normal entry rather than replacing it. Hermetic: temp HOME + temp rice, every
# system write captured by an as_root mock, packages stubbed.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_PKG=pacman
NO_COLOR=1; OSR_USER=$(id -un); export OSR_USER   # as_user becomes a no-op
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"; . "$OSR_LIB/config.sh"
. "$HERE/../lib.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
OUT="$TMP/out"
THEME="$TMP/rice"; OSR_THEME_DIR="$THEME"; export OSR_THEME_DIR

run_step()    { shift; "$@"; }
pkg_install() { echo "PKG $*" >>"$OUT"; }
# as_root logs the escalation and redirects system paths into the sandbox, so
# /usr/share/wayland-sessions on the machine running the tests is never touched.
SYSROOT="$TMP/sys"
as_root() {
    echo "ROOT $*" >>"$OUT"
    _cmd=$1; shift
    case "$_cmd" in
        mkdir) mkdir -p "$SYSROOT/$2" ;;
        cp)    _dst=$(eval "echo \${$#}"); mkdir -p "$SYSROOT/$(dirname "$_dst")"
               cp -f "$2" "$SYSROOT/$_dst" ;;
        chmod) : ;;
    esac
}

# --- rice fixture: the real config layout, minimal contents ------------------
mkdir -p "$THEME/config/hypr" "$THEME/config/qt6ct" "$THEME/config/wayland-sessions" \
         "$THEME/wallpapers"
printf 'IMAGE' >"$THEME/wallpapers/glass.jpg"
printf 'env = WALLPAPER_PATH,{{WALLPAPER_PATH}}\nexec-once = hyprpaper\n' \
    >"$THEME/config/hypr/hyprland.conf"
for s in start-audio start-amnezia-vpn-client start-mako start-easyeffects \
         start-top start-wleave; do
    printf '#!/bin/sh\n# %s\n' "$s" >"$THEME/config/hypr/$s.sh"
done
printf 'qt6ct\n' >"$THEME/config/qt6ct/qt6ct.conf"
printf 'Exec=/usr/share/wayland-sessions/start-hyprland.sh\n' \
    >"$THEME/config/wayland-sessions/hyprland.desktop"
printf 'exec Hyprland\n' >"$THEME/config/wayland-sessions/start-hyprland.sh"
printf 'Name=Hyprland (VMware)\n' >"$THEME/config/wayland-sessions/hyprland-vmware.desktop"
printf 'export GSK_RENDERER=cairo\nexec Hyprland\n' \
    >"$THEME/config/wayland-sessions/start-hyprland-vmware.sh"

# run_module <virt>
run_module() {
    rm -rf "$TMP/home" "$SYSROOT"; : >"$OUT"
    OSR_HOME="$TMP/home"; export OSR_HOME
    OSR_VIRT=$1; export OSR_VIRT
    . "$OSR_ROOT/modules/hyprland.sh" >>"$OUT" 2>&1
}

# --- bare metal --------------------------------------------------------------
run_module none

assert_contains "$OUT" 'PKG hyprland' "installs the compositor via pkg_install"

# Every exec-once target from the rice's hyprland.conf must exist and be run-able.
for s in start-audio start-amnezia-vpn-client start-mako start-easyeffects \
         start-top start-wleave; do
    if [ -x "$OSR_HOME/.config/hypr/$s.sh" ]; then
        ok "$s.sh installed and executable"
    else
        fail "$s.sh installed and executable"
    fi
done

assert_contains "$OSR_HOME/.config/hypr/hyprland.conf" \
    "^env = WALLPAPER_PATH,$OSR_HOME/Pictures/Wallpapers/glass.jpg$" \
    "WALLPAPER_PATH resolves to the installed wallpaper (absolute, no tilde)"
refute_contains "$OSR_HOME/.config/hypr/hyprland.conf" '{{' \
    "no placeholder survives into the installed hyprland.conf"

[ -f "$OSR_HOME/.config/qt6ct/qt6ct.conf" ] && ok "qt6ct theme installed" \
                                            || fail "qt6ct theme installed"
[ -f "$SYSROOT/usr/share/wayland-sessions/start-hyprland.sh" ] \
    && ok "the normal session launcher lands in the system path" \
    || fail "the normal session launcher lands in the system path"
[ -e "$SYSROOT/usr/share/wayland-sessions/hyprland-vmware.desktop" ] \
    && fail "no VMware session entry on bare metal" \
    || ok "no VMware session entry on bare metal"

# The launchers stay root-owned 0755 - the legacy chowned them to the target user
# "so sddm can run it", which SDDM never needed.
refute_contains "$OUT" 'ROOT chown' "session launchers are not chowned to the user"

# --- VMware guest ------------------------------------------------------------
run_module vmware

[ -f "$SYSROOT/usr/share/wayland-sessions/hyprland-vmware.desktop" ] \
    && ok "VMware session entry installed under a VMware hypervisor" \
    || fail "VMware session entry installed under a VMware hypervisor"
assert_contains "$SYSROOT/usr/share/wayland-sessions/start-hyprland-vmware.sh" \
    'GSK_RENDERER=cairo' "the VMware launcher carries the software-render workarounds"
[ -f "$SYSROOT/usr/share/wayland-sessions/hyprland.desktop" ] \
    && ok "the normal session entry is kept alongside it" \
    || fail "the normal session entry is kept alongside it"

finish
