#!/bin/sh
# Proves modules/gnome-overview.c: the bare-Super overview tap is disabled by
# emptying org.gnome.mutter overlay-key on a GNOME session, that a re-run is a
# no-op on the stored value (idempotent), and that a non-GNOME session touches
# gsettings not at all. Hermetic: gsettings is a mock state file, no root, no net.
#
# The module is C, so it runs through the core rather than being sourced; the
# mock gsettings is the whole observation either way. Its parity with the frozen
# sh original is asked in test/unit/module_c_parity.sh.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB
NO_COLOR=1; OSR_USER=$(id -un); export OSR_USER   # as_user becomes a no-op
OSR_ROOT="$OSR_ROOT"; export OSR_ROOT OSR_LIB NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"; . "$OSR_LIB/gnome.sh"
. "$HERE/../lib.sh"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip gnome_overview_module: %s is not built\n' "$OSR_BIN"
    exit 0
fi
OSR_HOME=${HOME}; export OSR_HOME
run_module() { "$OSR_BIN" module run gnome-overview; }

# ---- Mock gsettings ---------------------------------------------------------
BIN=$(mktemp -d)
GSFILE=$(mktemp)
cat >"$BIN/gsettings" <<'MOCK_EOF'
#!/bin/sh
GSFILE="${GSFILE:-/dev/null}"
case "$1" in
    get) sed -n "s|^$2 $3 ||p" "$GSFILE"; exit 0 ;;
    set) grep -v "^$2 $3 " "$GSFILE" >"$GSFILE.tmp" 2>/dev/null || true
         mv "$GSFILE.tmp" "$GSFILE"
         printf '%s %s %s\n' "$2" "$3" "$4" >>"$GSFILE"; exit 0 ;;
    *)   exit 1 ;;
esac
MOCK_EOF
chmod +x "$BIN/gsettings"
PATH="$BIN:$PATH"; export PATH GSFILE

run_step() { shift; "$@"; }

# ---- scenario 1: non-GNOME session -> nothing touched ----------------------
: >"$GSFILE"
XDG_CURRENT_DESKTOP=i3 XDG_SESSION_DESKTOP=i3
export XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP

run_module >/dev/null

assert_eq "" "$(cat "$GSFILE")" "no gsettings calls on non-GNOME"

# ---- scenario 2: GNOME -> overlay-key emptied ------------------------------
: >"$GSFILE"
XDG_CURRENT_DESKTOP=ubuntu:GNOME XDG_SESSION_DESKTOP=gnome
export XDG_CURRENT_DESKTOP XDG_SESSION_DESKTOP
gsettings set org.gnome.mutter overlay-key "Super_L"   # stock value

run_module >/dev/null

assert_eq "org.gnome.mutter overlay-key " "$(cat "$GSFILE")" \
    "overlay-key set to the empty string (Super tap watches nothing)"

# ---- scenario 3: re-run leaves the same single empty value -----------------
run_module >/dev/null

assert_eq "1" "$(wc -l < "$GSFILE")" "re-run writes no second value (idempotent)"

rm -f "$GSFILE"; rm -rf "$BIN"
finish
