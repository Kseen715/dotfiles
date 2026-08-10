#!/bin/sh
# Proves modules/cliphist.sh GNOME integration: autostart desktop file, Super+V
# unbind from GNOME Shell, gsettings custom-keybinding registration, idempotency,
# and that non-GNOME environments skip the GNOME block entirely — all hermetic
# (no net/root; gsettings is a mock state file).
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_DOTFILES OSR_PKG=dnf
NO_COLOR=1; OSR_USER=$(id -un); export OSR_USER   # as_user becomes a no-op
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"; . "$OSR_LIB/config.sh"; . "$OSR_LIB/net.sh"
. "$HERE/../lib.sh"

OUT=$(mktemp)

# Constants matching the module's gsettings schema/paths.
_schema="org.gnome.settings-daemon.plugins.media-keys"
_base="/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings"
_child="$_schema.custom-keybinding"

# ---- Mock gsettings: a simple key-value store via a script on PATH ----------
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
    *)   exit 1 ;;
esac
MOCK_EOF
chmod +x "$BIN/gsettings"
PATH="$BIN:$PATH"; export PATH GSFILE

# Stubs.
run_step() { shift; "$@"; }
pkg_install()  { echo "PKG $*" >>"$OUT"; }
osr_download() { echo "DOWNLOAD $*" >>"$OUT"; return 1; }
as_root()      { "$@"; }
install_layer(){ :; }
warn()         { echo "WARN $*" >>"$OUT"; }

# Fake go binary so "command -v go" succeeds (avoids pkg_install go + go install).
cat >"$BIN/go" <<'MOCK_EOF'
#!/bin/sh
exit 0
MOCK_EOF
chmod +x "$BIN/go"

# ---- scenario 1: non-GNOME session -> GNOME block skipped entirely ----------
: >"$OUT"; : >"$GSFILE"
OSR_HOME=$(mktemp -d); export OSR_HOME
XDG_CURRENT_DESKTOP=i3 XDG_SESSION_DESKTOP=i3
export XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP

. "$OSR_ROOT/modules/cliphist.sh" >/dev/null

assert_contains "$OUT" 'PKG cliphist ripgrep wofi wl-clipboard' "installs packages regardless of session"
refute_contains "$OUT" 'GNOME autostart' "GNOME autostart not logged on i3"
assert_eq "" "$(cat "$GSFILE")" "no gsettings calls on non-GNOME"
rm -rf "$OSR_HOME"

# ---- scenario 2: GNOME (ubuntu:GNOME) -> autostart + unbind + keybind --------
: >"$OUT"; : >"$GSFILE"
OSR_HOME=$(mktemp -d); export OSR_HOME
XDG_CURRENT_DESKTOP=ubuntu:GNOME XDG_SESSION_DESKTOP=gnome
export XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP

. "$OSR_ROOT/modules/cliphist.sh" >/dev/null

assert_contains "$OSR_HOME/.config/autostart/cliphist-store.desktop" 'Cliphist Store' "autostart desktop file created"
assert_contains "$OSR_HOME/.config/autostart/cliphist-store.desktop" 'wl-paste.*cliphist store' "autostart exec watches clipboard"
assert_contains "$OSR_HOME/.config/autostart/cliphist-store.desktop" 'NoDisplay=true' "autostart entry hidden from startup apps UI"
rm -rf "$OSR_HOME"

# ---- scenario 3: Super+V keybinding -> correct gsettings calls ---------------
: >"$OUT"; : >"$GSFILE"
OSR_HOME=$(mktemp -d); export OSR_HOME
XDG_CURRENT_DESKTOP=GNOME XDG_SESSION_DESKTOP=gnome
export XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP

. "$OSR_ROOT/modules/cliphist.sh" >/dev/null

_base="/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings"
_schema="org.gnome.settings-daemon.plugins.media-keys"
_child="$_schema.custom-keybinding"
_path="$_base/cliphist/"

# Parent array was set — check for the cliphist path in the array value.
assert_contains "$GSFILE" "$_path" "parent array contains cliphist path"

# Name, binding, command set on child schema.
assert_contains "$GSFILE" "$_child:$_path name Clipboard History" "shortcut name set"
assert_contains "$GSFILE" "$_child:$_path binding <Super>v" "binding is Super+v"
assert_contains "$GSFILE" "$_child:$_path command cliphist-wofi-img | wl-copy" "command pipes to wl-copy"
rm -rf "$OSR_HOME"

# ---- scenario 4: idempotent — second run does nothing -----------------------
: >"$OUT"; : >"$GSFILE"
OSR_HOME=$(mktemp -d); export OSR_HOME

# Pre-populate gsettings with our shortcut already registered.
gsettings set "$_schema" custom-keybindings "['$_path']"
gsettings set "$_child:$_path" name "Clipboard History"
gsettings set "$_child:$_path" binding "<Super>v"
gsettings set "$_child:$_path" command "cliphist-wofi-img | wl-copy"

. "$OSR_ROOT/modules/cliphist.sh" >/dev/null

# No new entry — the path was found, module returns early.
# Pre-population wrote 4 lines (array + name + binding + command); no extras.
_count=$(wc -l < "$GSFILE")
assert_eq "4" "$_count" "no duplicate registration (idempotent, 4 pre-populated lines)"
rm -rf "$OSR_HOME"

# ---- scenario 5: unbind frees Super+V from toggle-message-tray --------------
: >"$OUT"; : >"$GSFILE"
OSR_HOME=$(mktemp -d); export OSR_HOME

# Simulate GNOME Shell holding Super+V for toggle-message-tray.
gsettings set "org.gnome.shell.keybindings" toggle-message-tray "['<Super>v']"

. "$OSR_ROOT/modules/cliphist.sh" >/dev/null

assert_contains "$GSFILE" "org.gnome.shell.keybindings toggle-message-tray \[\]" "toggle-message-tray freed from Super+v"
rm -rf "$OSR_HOME"

# ---- scenario 6: appends to existing custom keybindings list ----------------
: >"$OUT"; : >"$GSFILE"
OSR_HOME=$(mktemp -d); export OSR_HOME

# Pre-populate with an existing unrelated custom shortcut.
gsettings set "$_schema" custom-keybindings "['$_base/custom0/']"
gsettings set "$_child:$_base/custom0/" name "Some Other Shortcut"
gsettings set "$_child:$_base/custom0/" binding "<Super>t"
gsettings set "$_child:$_base/custom0/" command "x-terminal-emulator"

. "$OSR_ROOT/modules/cliphist.sh" >/dev/null

# Our shortcut appended alongside existing custom0.
assert_contains "$GSFILE" "$_schema custom-keybindings \['$_base/custom0/', '$_path'\]" "cliphist path appended alongside existing custom0"
assert_contains "$GSFILE" "$_child:$_path name Clipboard History" "cliphist shortcut registered"
assert_contains "$GSFILE" "$_child:$_path binding <Super>v" "correct binding on cliphist path"
rm -rf "$OSR_HOME"

rm -f "$OUT" "$GSFILE"; rm -rf "$BIN"
finish
