#!/bin/sh
# Proves modules/ghostty.sh §5 config ownership: `config` is dotfiles-owned, the
# palette is a rice-owned theme that overrides the dotfiles default, and the base
# carries the ssh/transparency settings it is supposed to — all hermetic (no
# net/root; the package install is stubbed, so no Zig build runs).
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
chmod +x "$BIN/fc-list"; PATH="$BIN:$PATH"; export PATH

# Stubs: run_step runs the wrapped command; downloads/pkg installs are recorded.
run_step() { shift; "$@"; }
pkg_install() { echo "PKG $*" >>"$OUT"; }
osr_download() { echo "DOWNLOAD $*" >>"$OUT"; return 1; }

# --- scenario 1: rice ships a palette -> rice theme wins over dotfiles default -
OSR_HOME=$(mktemp -d); export OSR_HOME
RICE=$(mktemp -d); OSR_RICE_DIR="$RICE"; export OSR_RICE_DIR
mkdir -p "$RICE/config/ghostty"
printf '# RICE-PALETTE-MARKER\nbackground = #123456\n' >"$RICE/config/ghostty/ghostty-theme"

. "$OSR_ROOT/modules/ghostty.sh"

assert_contains "$OUT" 'PKG ghostty unzip fontconfig' "installs ghostty + font deps via pkg_install"
assert_contains "$OSR_HOME/.config/ghostty/config" 'JetBrainsMono' "config installed (dotfiles-owned base)"
assert_contains "$OSR_HOME/.config/ghostty/config" '^background-opacity = 0.85$' "base sets 0.85 transparency"
assert_contains "$OSR_HOME/.config/ghostty/config" 'ssh-terminfo' "base enables ssh terminfo shell integration"
assert_contains "$OSR_HOME/.config/ghostty/config" '^clipboard-write = allow$' "base allows OSC 52 writes from remote hosts"
assert_contains "$OSR_HOME/.config/ghostty/config" '^config-file = ?ghostty-theme$' "base includes the rice palette layer"
assert_contains "$OSR_HOME/.config/ghostty/ghostty-theme" 'RICE-PALETTE-MARKER' "rice palette overrides dotfiles default (90-theme)"
rm -rf "$OSR_HOME" "$RICE"

# --- scenario 2: rice ships no palette -> dotfiles default palette used -------
: >"$OUT"
OSR_HOME=$(mktemp -d); export OSR_HOME
RICE=$(mktemp -d); OSR_RICE_DIR="$RICE"; export OSR_RICE_DIR   # no config/ghostty

. "$OSR_ROOT/modules/ghostty.sh"

assert_contains "$OSR_HOME/.config/ghostty/ghostty-theme" '^palette = 0=' "dotfiles default palette used when rice ships none"
refute_contains "$OUT" 'DOWNLOAD' "Nerd Font download skipped when already present (§2)"
rm -rf "$OSR_HOME" "$RICE"

rm -rf "$BIN"; rm -f "$OUT"
finish
