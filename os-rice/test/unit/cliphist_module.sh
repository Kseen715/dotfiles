#!/bin/sh
# Proves modules/cliphist.c's GNOME integration: autostart desktop file, Super+V
# unbind from GNOME Shell, gsettings custom-keybinding registration, idempotency,
# and that non-GNOME environments skip the GNOME block entirely — all hermetic
# (no net/root; gsettings is a mock state file).
#
# The module is C now, so it runs through the core rather than being sourced:
# PATH is reduced to a stub bin/, the stubs log their argv, and that log plus the
# mock gsettings state is what the module decided to do. Its parity with the
# frozen sh original is a separate question, asked in
# test/unit/module_c_parity.sh.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_ROOT OSR_DOTFILES
NO_COLOR=1; export NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"
. "$HERE/../lib.sh"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip cliphist_module: %s is not built\n' "$OSR_BIN"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM
BIN="$TMP/bin"; mkdir -p "$BIN"
LOG="$TMP/log"; export LOG
GSFILE="$TMP/gsettings"; export GSFILE

for _t in sh env cat grep sed printf id rm mkdir mktemp test true false tee \
          chmod cut tr head sort wc dirname basename; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in /*) ln -sf "$_p" "$BIN/$_t" ;; esac
done

cat >"$BIN/sudo" <<'EOF'
#!/bin/sh
printf 'sudo %s\n' "$*" >>"$LOG"
[ "$1" = "-u" ] && shift 2
exec "$@"
EOF
chmod +x "$BIN/sudo"

# The package manager for OSR_PKG=dnf: `rpm -q` answers "not installed" so the
# install batch is always taken, and dnf itself only logs.
cat >"$BIN/rpm" <<'EOF'
#!/bin/sh
printf 'rpm %s\n' "$*" >>"$LOG"
exit 1
EOF
cat >"$BIN/dnf" <<'EOF'
#!/bin/sh
printf 'dnf %s\n' "$*" >>"$LOG"
exit 0
EOF
# go is present, so the module neither installs it nor takes its self-heal path.
cat >"$BIN/go" <<'EOF'
#!/bin/sh
printf 'go %s\n' "$*" >>"$LOG"
exit 0
EOF
chmod +x "$BIN/rpm" "$BIN/dnf" "$BIN/go"
# No curl/wget in $BIN, so the /usr/local/bin shim fetch degrades to a warning
# instead of reaching the network.

# ---- Mock gsettings: a simple key-value store via a script on PATH ----------
_schema="org.gnome.settings-daemon.plugins.media-keys"
_base="/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings"
_child="$_schema.custom-keybinding"
_path="$_base/cliphist/"

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
    # "<schema> <key> <value>" for every key stored under the schema -- what
    # lib/gnome.c's osr_gnome_free_binding reads to find who holds the chord.
    list-recursively) grep "^$2 " "$GSFILE" 2>/dev/null || true; exit 0 ;;
    *)   exit 1 ;;
esac
MOCK_EOF
chmod +x "$BIN/gsettings"
ln -sf "$(command -v mv)" "$BIN/mv"

FACTS="OSR_ROOT=$OSR_ROOT OSR_LIB=$OSR_LIB OSR_DOTFILES=$OSR_DOTFILES
       OSR_PKG=dnf OSR_INIT=systemd OSR_DISTRO=fedora OSR_ARCH=x86_64
       OSR_USER=tester NO_COLOR=1 TERM=dumb COLUMNS=80
       OSR_LOG=$TMP/run.log OSR_VERBOSE=1"

# run <desktop> — the module under a session of that name, into a fresh $HOME.
run() {
    : >"$LOG"
    OSR_HOME="$TMP/home"; rm -rf "$OSR_HOME"; mkdir -p "$OSR_HOME"
    # shellcheck disable=SC2086
    env -i PATH="$BIN" LOG="$LOG" GSFILE="$GSFILE" $FACTS \
        OSR_HOME="$OSR_HOME" HOME="$OSR_HOME" \
        XDG_CURRENT_DESKTOP="$1" XDG_SESSION_DESKTOP="$2" \
        "$OSR_BIN" module run cliphist >"$TMP/out" 2>&1 || :
}

gset() { GSFILE="$GSFILE" "$BIN/gsettings" set "$1" "$2" "$3"; }

# ---- scenario 1: non-GNOME session -> GNOME block skipped entirely ----------
: >"$GSFILE"
run i3 i3
assert_contains "$LOG" 'dnf install .*cliphist.*ripgrep.*wofi.*wl-clipboard' \
    "installs packages regardless of session"
refute_contains "$TMP/out" 'GNOME autostart' "GNOME autostart not logged on i3"
assert_eq "" "$(cat "$GSFILE")" "no gsettings calls on non-GNOME"

# ---- scenario 2: GNOME (ubuntu:GNOME) -> autostart + unbind + keybind --------
: >"$GSFILE"
run ubuntu:GNOME gnome
assert_contains "$OSR_HOME/.config/autostart/cliphist-store.desktop" 'Cliphist Store' \
    "autostart desktop file created"
assert_contains "$OSR_HOME/.config/autostart/cliphist-store.desktop" 'wl-paste.*cliphist store' \
    "autostart exec watches clipboard"
assert_contains "$OSR_HOME/.config/autostart/cliphist-store.desktop" 'NoDisplay=true' \
    "autostart entry hidden from startup apps UI"

# ---- scenario 3: Super+V keybinding -> correct gsettings calls ---------------
: >"$GSFILE"
run GNOME gnome
# Parent array was set — check for the cliphist path in the array value.
assert_contains "$GSFILE" "$_path" "parent array contains cliphist path"
assert_contains "$GSFILE" "$_child:$_path name Clipboard History" "shortcut name set"
assert_contains "$GSFILE" "$_child:$_path binding <Super>v" "binding is Super+v"
assert_contains "$GSFILE" "$_child:$_path command cliphist-wofi-img | wl-copy" \
    "command pipes to wl-copy"

# ---- scenario 4: idempotent — second run does nothing -----------------------
: >"$GSFILE"
gset "$_schema" custom-keybindings "['$_path']"
gset "$_child:$_path" name "Clipboard History"
gset "$_child:$_path" binding "<Super>v"
gset "$_child:$_path" command "cliphist-wofi-img | wl-copy"
run GNOME gnome
# No new entry — the path was found, the registration returns early.
assert_eq "4" "$(wc -l < "$GSFILE")" \
    "no duplicate registration (idempotent, 4 pre-populated lines)"

# ---- scenario 5: unbind frees Super+V from toggle-message-tray --------------
: >"$GSFILE"
gset "org.gnome.shell.keybindings" toggle-message-tray "['<Super>v']"
run GNOME gnome
assert_contains "$GSFILE" "org.gnome.shell.keybindings toggle-message-tray \[\]" \
    "toggle-message-tray freed from Super+v"

# ---- scenario 6: appends to existing custom keybindings list ----------------
: >"$GSFILE"
gset "$_schema" custom-keybindings "['$_base/custom0/']"
gset "$_child:$_base/custom0/" name "Some Other Shortcut"
gset "$_child:$_base/custom0/" binding "<Super>t"
gset "$_child:$_base/custom0/" command "x-terminal-emulator"
run GNOME gnome
assert_contains "$GSFILE" "$_schema custom-keybindings \['$_base/custom0/', '$_path'\]" \
    "cliphist path appended alongside existing custom0"
assert_contains "$GSFILE" "$_child:$_path name Clipboard History" "cliphist shortcut registered"
assert_contains "$GSFILE" "$_child:$_path binding <Super>v" "correct binding on cliphist path"

finish
