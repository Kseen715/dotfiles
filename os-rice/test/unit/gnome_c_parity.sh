#!/bin/sh
# Proves lib/gnome.c drives gsettings exactly as lib/gnome.sh did: the same
# schemas probed in the same order, the same keys freed, the same custom
# keybinding list built out of every shape gsettings spells an empty list in,
# and the same idempotence skip.
#
# Hermetic like test/unit/service_c_parity.sh: PATH is reduced to a stub bin/,
# gsettings is a script that logs its argv and answers list-recursively from a
# per-scenario file, so no dconf on this machine is ever read or written.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip gnome_c_parity: %s is not built\n' "$OSR_BIN"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
BIN="$TMP/bin"; mkdir -p "$BIN"
REAL_SH=$(command -v sh)

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

# gsettings: logs every call, and answers reads from the scenario's files.
#   $STATE/<schema>.list   what list-recursively prints for that schema
#   $STATE/custom          what `get ... custom-keybindings` prints
cat >"$BIN/gsettings" <<'EOF'
#!/bin/sh
printf 'gsettings %s\n' "$*" >>"$LOG"
case "$1" in
    list-recursively)
        [ -f "$STATE/$2.list" ] && cat "$STATE/$2.list"
        ;;
    get)
        if [ "$3" = custom-keybindings ] && [ -f "$STATE/custom" ]; then
            cat "$STATE/custom"
        fi
        ;;
esac
exit 0
EOF
chmod +x "$BIN/gsettings"

FACTS="OSR_ROOT=$OSR_ROOT OSR_LIB=$OSR_LIB OSR_DISTRO=ubuntu OSR_ARCH=x86_64
       NO_COLOR=1 TERM=dumb OSR_PKG=apt OSR_INIT=systemd"
ME=$(id -un)

DESKTOP=GNOME
SESSION=

# seed <root> -- redefined per scenario; lays out one sandbox. $ROOT is bound.
seed() { :; }

run_side() {
    _root=$1; _tier=$2; _cmd=$3
    rm -rf "$_root"; mkdir -p "$_root/state"
    ROOT=$_root; seed
    : >"$_root/log"
    if [ "$_tier" = sh ]; then
        # shellcheck disable=SC2086
        env -i PATH="$BIN" LOG="$_root/log" STATE="$_root/state" $FACTS \
            OSR_USER="$ME" OSR_HOME="$_root" HOME="$_root" ROOT="$_root" \
            XDG_CURRENT_DESKTOP="$DESKTOP" XDG_SESSION_DESKTOP="$SESSION" \
            sh -c '
                . "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"
                . "$OSR_LIB/gnome.sh"
                eval "$1"' _ "$_cmd" 2>&1 || :
    else
        # shellcheck disable=SC2086
        env -i PATH="$BIN" LOG="$_root/log" STATE="$_root/state" $FACTS \
            OSR_USER="$ME" OSR_HOME="$_root" HOME="$_root" ROOT="$_root" \
            XDG_CURRENT_DESKTOP="$DESKTOP" XDG_SESSION_DESKTOP="$SESSION" \
            OSR_BIN="$OSR_BIN" \
            sh -c 'eval "\"$OSR_BIN\" gnome $1"' _ "$_cmd" 2>&1 || :
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

# status <label> <sh-snippet> <c-args> -- the same, plus the exit status, for
# the predicate whose whole answer IS its status.
status() {
    _label=$1
    run_side "$TMP/a" sh "$2" >/dev/null 2>&1; _a=$?
    run_side "$TMP/b" c "$3" >/dev/null 2>&1; _b=$?
    assert_eq "$_a" "$_b" "$_label: same status"
}

# --- 1. session detection -----------------------------------------------------
DESKTOP=GNOME;         SESSION=;      status "plain GNOME"          'gnome_is_session' 'is-session'
DESKTOP=ubuntu:GNOME;  SESSION=;      status "Ubuntu's prefixed form" 'gnome_is_session' 'is-session'
DESKTOP=GNOME-Classic; SESSION=;      status "GNOME Classic"        'gnome_is_session' 'is-session'
DESKTOP=;              SESSION=gnome; status "only the session var, lowercase" 'gnome_is_session' 'is-session'
DESKTOP=;              SESSION=GNOME; status "only the session var" 'gnome_is_session' 'is-session'
DESKTOP=Hyprland;      SESSION=;      status "Hyprland is not GNOME" 'gnome_is_session' 'is-session'
DESKTOP=KDE;           SESSION=sway;  status "neither var is GNOME"  'gnome_is_session' 'is-session'
DESKTOP=;              SESSION=;      status "an empty environment"  'gnome_is_session' 'is-session'
DESKTOP=GNOME

# --- 2. free_binding ----------------------------------------------------------
seed() { :; }
scene "nothing holds the chord" \
    'gnome_free_binding "<Super>r"' 'free-binding "<Super>r"'

seed() {
    printf "org.gnome.shell.keybindings show-screen-recording-ui ['<Super><Ctrl><Shift>r', '<Super>r']\n" \
        >"$ROOT/state/org.gnome.shell.keybindings.list"
}
scene "the Shell key holding it is freed" \
    'gnome_free_binding "<Super>r"' 'free-binding "<Super>r"'

# A near miss in the same list: <Shift><Super>r is a different chord and the
# quoting on both sides is what keeps it bound.
seed() {
    printf "org.gnome.shell.keybindings toggle-overview ['<Shift><Super>r']\n" \
        >"$ROOT/state/org.gnome.shell.keybindings.list"
}
scene "a longer chord ending in the same key is left alone" \
    'gnome_free_binding "<Super>r"' 'free-binding "<Super>r"'

# Two schemas, two keys each: order of schemas and of lines both matter.
seed() {
    printf "org.gnome.shell.keybindings open-application-menu ['<Super>r']\norg.gnome.shell.keybindings focus-active-notification ['<Super>n']\n" \
        >"$ROOT/state/org.gnome.shell.keybindings.list"
    printf "org.gnome.desktop.wm.keybindings begin-resize ['<Super>r']\n" \
        >"$ROOT/state/org.gnome.desktop.wm.keybindings.list"
    printf "org.gnome.mutter.keybindings toggle-tiled-right ['<Super>r']\n" \
        >"$ROOT/state/org.gnome.mutter.keybindings.list"
}
scene "every schema holding it is freed, in order" \
    'gnome_free_binding "<Super>r"' 'free-binding "<Super>r"'

# grep -iF: the case of the chord as gsettings reports it is not ours to choose.
seed() {
    printf "org.gnome.mutter.wayland.keybindings restore-shortcuts ['<super>V']\n" \
        >"$ROOT/state/org.gnome.mutter.wayland.keybindings.list"
}
scene "the match is case-insensitive" \
    'gnome_free_binding "<Super>v"' 'free-binding "<Super>v"'

# A line the listing wraps has no second field, so there is no key to set.
seed() {
    printf "org.gnome.shell.keybindings\n['<Super>r']\n" \
        >"$ROOT/state/org.gnome.shell.keybindings.list"
}
scene "a line with no key name sets nothing" \
    'gnome_free_binding "<Super>r"' 'free-binding "<Super>r"'

# --- 3. keybind ---------------------------------------------------------------
seed() { printf "@as []\n" >"$ROOT/state/custom"; }
scene "the first shortcut on a fresh box" \
    'gnome_keybind wofi "Application Launcher" "<Super>r" "wofi --show drun"' \
    'keybind wofi "Application Launcher" "<Super>r" "wofi --show drun"'

seed() { printf "[]\n" >"$ROOT/state/custom"; }
scene "the other empty spelling" \
    'gnome_keybind wofi "Application Launcher" "<Super>r" "wofi --show drun"' \
    'keybind wofi "Application Launcher" "<Super>r" "wofi --show drun"'

seed() { printf "''\n" >"$ROOT/state/custom"; }
scene "the third empty spelling" \
    'gnome_keybind wofi "Application Launcher" "<Super>r" "wofi --show drun"' \
    'keybind wofi "Application Launcher" "<Super>r" "wofi --show drun"'

seed() { : >"$ROOT/state/custom"; }
scene "gsettings answered nothing at all" \
    'gnome_keybind wofi "Application Launcher" "<Super>r" "wofi --show drun"' \
    'keybind wofi "Application Launcher" "<Super>r" "wofi --show drun"'

seed() {
    printf "['/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/wofi/']\n" \
        >"$ROOT/state/custom"
}
scene "a second shortcut is appended to the first" \
    'gnome_keybind cliphist "Clipboard History" "<Super>v" "sh -c '"'"'cliphist list | wofi -S dmenu'"'"'"' \
    'keybind cliphist "Clipboard History" "<Super>v" "sh -c '"'"'cliphist list | wofi -S dmenu'"'"'"'

scene "re-registering the same id is a skip" \
    'gnome_keybind wofi "Application Launcher" "<Super>r" "wofi --show drun"' \
    'keybind wofi "Application Launcher" "<Super>r" "wofi --show drun"'

# An unrelated third-party shortcut must survive the append.
seed() {
    printf "['/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom0/', '/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/custom1/']\n" \
        >"$ROOT/state/custom"
}
scene "somebody else's shortcuts are preserved" \
    'gnome_keybind wofi "Application Launcher" "<Super>r" "wofi --show drun"' \
    'keybind wofi "Application Launcher" "<Super>r" "wofi --show drun"'

finish
