#!/bin/sh
# Proves modules/wofi.sh: package install, GNOME Super+R custom-keybinding
# registration, the toggle command, idempotency, appending alongside an existing
# custom shortcut, freeing Super+R from a GNOME Shell key that holds it, and that
# a non-GNOME session touches gsettings not at all -- all hermetic (no net/root;
# gsettings is a mock state file).
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_DOTFILES OSR_PKG=apt
NO_COLOR=1; OSR_USER=$(id -un); export OSR_USER   # as_user becomes a no-op
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"; . "$OSR_LIB/config.sh"; . "$OSR_LIB/gnome.sh"
. "$HERE/../lib.sh"

OUT=$(mktemp)

# Constants matching the module's gsettings schema/paths.
_schema="org.gnome.settings-daemon.plugins.media-keys"
_base="/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings"
_child="$_schema.custom-keybinding"
_path="$_base/wofi/"
_cmd="sh -c 'pkill wofi || wofi --show drun'"

# ---- Mock gsettings: a key-value store via a script on PATH -----------------
# get/set as in the cliphist mock, plus list-recursively, which _gnome_free_super_r
# reads to discover which Shell key (if any) is sitting on Super+R.
BIN=$(mktemp -d)
GSFILE=$(mktemp)
cat >"$BIN/gsettings" <<'MOCK_EOF'
#!/bin/sh
GSFILE="${GSFILE:-/dev/null}"
_gs_get() {
    # Real gsettings on Ubuntu returns "''" (exit 0) for non-existent keys.
    if grep -q "^$1 " "$GSFILE" 2>/dev/null; then
        sed -n "s|^$1 ||p" "$GSFILE"
    else
        echo "''"
    fi
}
_gs_set() {
    grep -v "^$1 " "$GSFILE" >"${GSFILE}.tmp" 2>/dev/null || true
    mv "${GSFILE}.tmp" "$GSFILE"
    printf '%s %s\n' "$1" "$2" >>"$GSFILE"
}
case "$1" in
    get) _gs_get "$2 $3"; exit 0 ;;
    set) _gs_set "$2 $3" "$4"; exit 0 ;;
    # "<schema> <key> <value>" for every key stored under the schema.
    list-recursively) grep "^$2 " "$GSFILE" 2>/dev/null || true; exit 0 ;;
    *)   exit 1 ;;
esac
MOCK_EOF
chmod +x "$BIN/gsettings"
PATH="$BIN:$PATH"; export PATH GSFILE

# Stubs.
run_step()     { shift; "$@"; }
pkg_install()  { echo "PKG $*" >>"$OUT"; }
apply_config() { echo "APPLY_CONFIG $*" >>"$OUT"; }
warn()         { echo "WARN $*" >>"$OUT"; }

# ---- scenario 1: non-GNOME session -> GNOME block skipped entirely ----------
: >"$OUT"; : >"$GSFILE"
OSR_HOME=$(mktemp -d); export OSR_HOME
OSR_THEME_DIR=""
XDG_CURRENT_DESKTOP=i3 XDG_SESSION_DESKTOP=i3
export XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP

. "$OSR_ROOT/modules/wofi.sh" >/dev/null

assert_contains "$OUT" 'PKG wofi' "installs wofi regardless of session"
assert_eq "" "$(cat "$GSFILE")" "no gsettings calls on non-GNOME"
refute_contains "$OUT" 'APPLY_CONFIG' "no theme config applied without OSR_THEME_DIR"
rm -rf "$OSR_HOME"

# ---- scenario 2: GNOME (ubuntu:GNOME) -> Super+R registered ----------------
: >"$OUT"; : >"$GSFILE"
OSR_HOME=$(mktemp -d); export OSR_HOME
OSR_THEME_DIR="$OSR_ROOT/themes/xin"
XDG_CURRENT_DESKTOP=ubuntu:GNOME XDG_SESSION_DESKTOP=gnome
export XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP

. "$OSR_ROOT/modules/wofi.sh" >/dev/null

assert_contains "$GSFILE" "$_schema custom-keybindings \['$_path'\]" "parent array holds the wofi path"
assert_contains "$GSFILE" "$_child:$_path name Application Launcher" "shortcut name set"
assert_contains "$GSFILE" "$_child:$_path binding <Super>r" "binding is Super+r (Win+R)"
assert_contains "$GSFILE" "$_child:$_path command $_cmd" "command toggles wofi drun via sh -c"
assert_contains "$OUT" 'APPLY_CONFIG wofi' "theme-owned wofi config applied"
rm -rf "$OSR_HOME"

# ---- scenario 3: idempotent -- second run adds nothing ---------------------
: >"$OUT"; : >"$GSFILE"
OSR_HOME=$(mktemp -d); export OSR_HOME

gsettings set "$_schema" custom-keybindings "['$_path']"
gsettings set "$_child:$_path" name "Application Launcher"
gsettings set "$_child:$_path" binding "<Super>r"
gsettings set "$_child:$_path" command "$_cmd"

. "$OSR_ROOT/modules/wofi.sh" >/dev/null

# Pre-population wrote 4 lines (array + name + binding + command); no extras.
assert_eq "4" "$(wc -l < "$GSFILE")" "no duplicate registration (idempotent)"
rm -rf "$OSR_HOME"

# ---- scenario 4: appends alongside an existing custom shortcut -------------
: >"$OUT"; : >"$GSFILE"
OSR_HOME=$(mktemp -d); export OSR_HOME

gsettings set "$_schema" custom-keybindings "['$_base/cliphist/']"
gsettings set "$_child:$_base/cliphist/" binding "<Super>v"

. "$OSR_ROOT/modules/wofi.sh" >/dev/null

assert_contains "$GSFILE" "$_schema custom-keybindings \['$_base/cliphist/', '$_path'\]" "wofi path appended, cliphist kept"
assert_contains "$GSFILE" "$_child:$_base/cliphist/ binding <Super>v" "existing Super+V shortcut untouched"
rm -rf "$OSR_HOME"

# ---- scenario 5: frees Super+R from a GNOME Shell key that holds it --------
# A Shell keybinding WINS over a custom one, so leaving it bound would mean a
# dead Win+R. The neighbouring <Shift><Super>r key must survive: it is a
# different chord, and the module matches the binding whole.
: >"$OUT"; : >"$GSFILE"
OSR_HOME=$(mktemp -d); export OSR_HOME

gsettings set "org.gnome.shell.keybindings" show-screen-recording-ui "['<Super>r']"
gsettings set "org.gnome.desktop.wm.keybindings" begin-resize "['<Shift><Super>r']"

. "$OSR_ROOT/modules/wofi.sh" >/dev/null

assert_contains "$GSFILE" "org.gnome.shell.keybindings show-screen-recording-ui \[\]" "Super+R freed from the Shell key"
assert_contains "$GSFILE" "org.gnome.desktop.wm.keybindings begin-resize \['<Shift><Super>r'\]" "<Shift><Super>r left alone"
assert_contains "$GSFILE" "$_child:$_path binding <Super>r" "wofi shortcut still registered after the unbind"
rm -rf "$OSR_HOME"

rm -f "$OUT" "$GSFILE"; rm -rf "$BIN"
finish
