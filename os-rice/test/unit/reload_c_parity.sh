#!/bin/sh
# Proves lib/reload.c pokes the running desktop exactly as lib/reload.sh did:
# the same probes in the same order, the same signal to the same process, the
# same never-fatal swallowing of a failed reloader, and the same closing line.
#
# Hermetic: PATH is reduced to a stub bin/ where every reloader logs its argv
# and every liveness probe answers from a scenario file, so nothing on the
# machine running the suite is signalled, restarted, or has its dconf written.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip reload_c_parity: %s is not built\n' "$OSR_BIN"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
BIN="$TMP/bin"; mkdir -p "$BIN"
REAL_SH=$(command -v sh)

# id is real and unlogged: both tiers ask it the same question (which uid owns
# the session) and neither's answer is the thing under test.
for _t in env cat cut grep sed awk tr head tail printf id mktemp rm cp mv mkdir \
          ln ls find sort wc dirname basename test true false; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in
        /*) ln -sf "$_p" "$BIN/$_t" ;;
        *)  for _d in /usr/bin /bin /usr/local/bin; do
                [ -x "$_d/$_t" ] && { ln -sf "$_d/$_t" "$BIN/$_t"; break; }
            done ;;
    esac
done
ln -sf "$REAL_SH" "$BIN/sh"

# pgrep: alive iff $STATE/proc.<name> exists. -x <name> is the last argument.
cat >"$BIN/pgrep" <<'EOF'
#!/bin/sh
printf 'pgrep %s\n' "$*" >>"$LOG"
eval _name=\${$#}
[ -f "$STATE/proc.$_name" ]
EOF

# Every reloader: logged, and failing when the scenario says so.
for _tool in pkill xrdb i3-msg hyprctl dunstctl makoctl; do
    cat >"$BIN/$_tool" <<EOF
#!/bin/sh
printf '$_tool %s\n' "\$*" >>"\$LOG"
[ -f "\$STATE/fail.$_tool" ] && exit 1
exit 0
EOF
done

# gsettings: get answers from $STATE/gtk-theme, set is logged only.
cat >"$BIN/gsettings" <<'EOF'
#!/bin/sh
printf 'gsettings %s\n' "$*" >>"$LOG"
case "$1" in
    get) [ -f "$STATE/gtk-theme" ] && cat "$STATE/gtk-theme"; [ -f "$STATE/fail.gsettings" ] && exit 1 ;;
    set) [ -f "$STATE/fail.gsettings" ] && exit 1 ;;
esac
exit 0
EOF
for _f in pgrep pkill xrdb i3-msg hyprctl dunstctl makoctl gsettings; do
    chmod +x "$BIN/$_f"
done

FACTS="OSR_ROOT=$OSR_ROOT OSR_LIB=$OSR_LIB OSR_DISTRO=arch OSR_ARCH=x86_64
       NO_COLOR=1 TERM=dumb OSR_PKG=pacman OSR_INIT=systemd"
ME=$(id -un)

DISP=":0"
WDISP=""

# seed -- redefined per scenario; declares which processes are alive. $ROOT
# is bound. `alive <name>` is the whole vocabulary.
alive() { : >"$ROOT/state/proc.$1"; }
seed() { :; }

run_side() {
    _root=$1; _tier=$2; _cmd=$3
    rm -rf "$_root"; mkdir -p "$_root/state" "$_root/.config/polybar"
    ROOT=$_root; seed
    : >"$_root/log"
    if [ "$_tier" = sh ]; then
        # shellcheck disable=SC2086
        env -i PATH="$BIN" LOG="$_root/log" STATE="$_root/state" $FACTS \
            OSR_USER="$ME" OSR_HOME="$_root" HOME="$_root" ROOT="$_root" \
            DISPLAY="$DISP" WAYLAND_DISPLAY="$WDISP" OSR_DEBUG=1 \
            sh -c '
                . "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"
                . "$OSR_LIB/reload.sh"
                eval "$1"' _ "$_cmd" 2>&1 || :
    else
        # shellcheck disable=SC2086
        env -i PATH="$BIN" LOG="$_root/log" STATE="$_root/state" $FACTS \
            OSR_USER="$ME" OSR_HOME="$_root" HOME="$_root" ROOT="$_root" \
            DISPLAY="$DISP" WAYLAND_DISPLAY="$WDISP" OSR_DEBUG=1 \
            OSR_BIN="$OSR_BIN" \
            sh -c 'eval "\"$OSR_BIN\" reload $1"' _ "$_cmd" 2>&1 || :
    fi
}

# scene <label> <sh-snippet> <c-args> -- both tiers, output and command log.
scene() {
    _label=$1
    _sh_out=$(run_side "$TMP/a" sh "$2" | sed "s|$TMP/a|ROOT|g")
    _c_out=$(run_side "$TMP/b" c "$3" | sed "s|$TMP/b|ROOT|g")
    assert_eq "$_sh_out" "$_c_out" "$_label: same output"
    assert_eq "$(sed "s|$TMP/a|ROOT|g" <"$TMP/a/log")" \
              "$(sed "s|$TMP/b|ROOT|g" <"$TMP/b/log")" "$_label: same commands"
}

# --- 1. no session ------------------------------------------------------------
DISP=""; WDISP=""
seed() { :; }
scene "no display server at all" 'osr_reload_all' 'all'

# --- 2. X11 -------------------------------------------------------------------
DISP=":0"; WDISP=""
seed() { :; }
scene "an X session with nothing running" 'osr_reload_all' 'all'

seed() { printf '*.foreground: #fff\n' >"$ROOT/.Xresources"; }
scene "Xresources are merged before anything else" 'osr_reload_all' 'all'

seed() { printf 'x\n' >"$ROOT/.Xresources"; : >"$ROOT/state/fail.xrdb"; }
scene "a failed xrdb is swallowed" 'osr_reload_all' 'all'

seed() { alive i3; }
scene "i3 is restarted, not reloaded" 'osr_reload_x11' 'x11'

seed() { alive polybar; }
scene "polybar with no launcher gets SIGUSR1" 'osr_reload_x11' 'x11'

seed() {
    alive polybar
    printf '#!/bin/sh\nexit 0\n' >"$ROOT/.config/polybar/launch.sh"
    chmod +x "$ROOT/.config/polybar/launch.sh"
}
scene "polybar's launcher is preferred when it is executable" 'osr_reload_x11' 'x11'

seed() {
    alive polybar
    printf '#!/bin/sh\nexit 0\n' >"$ROOT/.config/polybar/launch.sh"
}
scene "a non-executable launcher falls back to the signal" 'osr_reload_x11' 'x11'

seed() { alive picom; }
scene "picom re-reads on SIGUSR1" 'osr_reload_x11' 'x11'

seed() { alive xsettingsd; }
scene "xsettingsd gets SIGHUP, not SIGUSR1" 'osr_reload_x11' 'x11'

seed() { alive i3; alive polybar; alive picom; alive xsettingsd
         printf 'x\n' >"$ROOT/.Xresources"; }
scene "a full X11 desktop, in order" 'osr_reload_all' 'all'

# The i3 probe is `command -v i3-msg` AND running: a live i3 with no client
# binary must not be poked.
seed() { alive i3; }
DISP=":0"
scene "an X session where every reloader is present" 'osr_reload_x11' 'x11'

# --- 3. Wayland ---------------------------------------------------------------
DISP=""; WDISP="wayland-1"
seed() { :; }
scene "a Wayland session with nothing running" 'osr_reload_all' 'all'

seed() { alive Hyprland; }
scene "Hyprland reloads through hyprctl" 'osr_reload_wayland' 'wayland'

seed() { alive waybar; }
scene "waybar gets SIGUSR2, never SIGUSR1" 'osr_reload_wayland' 'wayland'

seed() { alive Hyprland; alive waybar; }
scene "a full Wayland desktop" 'osr_reload_all' 'all'

# X11 reloaders must not fire on a Wayland-only session.
seed() { alive i3; alive polybar; alive Hyprland; }
scene "the X branch is skipped without DISPLAY" 'osr_reload_all' 'all'

# --- 4. notifications ---------------------------------------------------------
DISP=":0"; WDISP=""
seed() { alive dunst; }
scene "dunst reloads through dunstctl" 'osr_reload_notify' 'notify'

seed() { alive mako; }
scene "a modern mako reloads through makoctl" 'osr_reload_notify' 'notify'

seed() { alive mako; : >"$ROOT/state/fail.makoctl"; }
scene "an old mako falls back to SIGUSR2" 'osr_reload_notify' 'notify'

seed() { alive dunst; alive mako; }
scene "both daemons running" 'osr_reload_notify' 'notify'

# --- 5. GTK -------------------------------------------------------------------
seed() { printf "'Adwaita-dark'\n" >"$ROOT/state/gtk-theme"; }
scene "the GTK theme is toggled off and back" 'osr_reload_gtk' 'gtk'

seed() { :; }
scene "no theme reported, nothing poked" 'osr_reload_gtk' 'gtk'

seed() { printf "'Adwaita-dark'\n" >"$ROOT/state/gtk-theme"; : >"$ROOT/state/fail.gsettings"; }
scene "a failed gsettings get is not fatal" 'osr_reload_gtk' 'gtk'

# --- 6. the whole thing, both sessions at once --------------------------------
DISP=":0"; WDISP="wayland-1"
seed() {
    alive i3; alive waybar; alive dunst
    printf 'x\n' >"$ROOT/.Xresources"
    printf "'Nord'\n" >"$ROOT/state/gtk-theme"
}
scene "an XWayland-ish box runs both branches" 'osr_reload_all' 'all'

finish
