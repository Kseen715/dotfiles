#!/bin/sh
# Proves modules/foot.sh §5 config ownership: foot.ini is dotfiles-owned, the
# palette is a rice-owned theme that overrides the dotfiles default, and the
# Nerd Font install skips when already present (§2) — all hermetic (no net/root).
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_DOTFILES OSR_PKG=dnf
NO_COLOR=1; OSR_USER=$(id -un); export OSR_USER   # as_user becomes a no-op
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"; . "$OSR_LIB/config.sh"; . "$OSR_LIB/net.sh"; . "$OSR_LIB/fonts.sh"
. "$HERE/../lib.sh"

OUT=$(mktemp)
# Fake fc-list on PATH reporting the font present -> the install step skips.
BIN=$(mktemp -d)
cat >"$BIN/fc-list" <<'EOF'
#!/bin/sh
echo "/f: JetBrainsMono Nerd Font:style=Regular"
EOF
chmod +x "$BIN/fc-list"
# Fake foot reporting $FAKE_FOOT_VERSION -> drives the palette section rename.
cat >"$BIN/foot" <<'EOF'
#!/bin/sh
echo "foot version: $FAKE_FOOT_VERSION +pgo +ime"
EOF
chmod +x "$BIN/foot"; PATH="$BIN:$PATH"; export PATH

# Stubs: run_step runs the wrapped command; downloads/pkg installs are recorded.
run_step() { shift; "$@"; }
pkg_install() { echo "PKG $*" >>"$OUT"; }
osr_download() { echo "DOWNLOAD $*" >>"$OUT"; return 1; }

# --- scenario 1: rice ships a palette -> rice theme wins over dotfiles default -
FAKE_FOOT_VERSION=1.26.0; export FAKE_FOOT_VERSION   # knows [colors-dark]
OSR_HOME=$(mktemp -d); export OSR_HOME
RICE=$(mktemp -d); OSR_RICE_DIR="$RICE"; export OSR_RICE_DIR
mkdir -p "$RICE/config/foot"
printf '[colors-dark]\n# RICE-PALETTE-MARKER\n' >"$RICE/config/foot/foot-colors.ini"

. "$OSR_ROOT/modules/foot.sh"

assert_contains "$OUT" 'PKG foot unzip fontconfig' "installs foot + font deps via pkg_install"
assert_contains "$OSR_HOME/.config/foot/foot.ini" 'JetBrainsMono' "foot.ini installed (dotfiles-owned base)"
assert_contains "$OSR_HOME/.config/foot/foot-colors.ini" 'RICE-PALETTE-MARKER' "rice palette overrides dotfiles default (90-theme)"
assert_contains "$OSR_HOME/.config/foot/foot-colors.ini" '^\[colors-dark\]$' "palette keeps [colors-dark] on foot >= 1.26"
refute_contains "$OUT" 'DOWNLOAD' "Nerd Font download skipped when already present (§2)"
rm -rf "$OSR_HOME" "$RICE"

# --- scenario 2: rice ships no palette -> dotfiles default palette used -------
# Old foot: [colors-dark] is an invalid section there, so it must be downgraded.
: >"$OUT"
FAKE_FOOT_VERSION=1.25.0
OSR_HOME=$(mktemp -d); export OSR_HOME
RICE=$(mktemp -d); OSR_RICE_DIR="$RICE"; export OSR_RICE_DIR   # no config/foot

. "$OSR_ROOT/modules/foot.sh"

assert_contains "$OSR_HOME/.config/foot/foot-colors.ini" 'regular0' "dotfiles default palette used when rice ships none"
assert_contains "$OSR_HOME/.config/foot/foot-colors.ini" '^\[colors\]$' "palette section downgraded for foot < 1.26"
refute_contains "$OSR_HOME/.config/foot/foot-colors.ini" 'colors-dark' "no [colors-dark] left for a foot that rejects it"
rm -rf "$OSR_HOME" "$RICE"

rm -rf "$BIN"; rm -f "$OUT"
finish
