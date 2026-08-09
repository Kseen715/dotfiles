#!/bin/sh
# Proves modules/alacritty.sh §5 config ownership and the version adaptation:
# `alacritty.toml` is dotfiles-owned behaviour-only config, the palette is a
# rice-owned theme that overrides the dotfiles default, and the `[general]`
# section is downgraded to a top-level `import` for an Alacritty older than 0.14
# (lib/config.sh). Hermetic (no net/root; the package install is stubbed).
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_DOTFILES OSR_PKG=apt
NO_COLOR=1; OSR_USER=$(id -un); export OSR_USER   # as_user becomes a no-op
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"; . "$OSR_LIB/config.sh"; . "$OSR_LIB/net.sh"; . "$OSR_LIB/fonts.sh"
. "$HERE/../lib.sh"

OUT=$(mktemp)
BIN=$(mktemp -d)
# Fake fc-list on PATH reporting the font present -> the install step skips.
cat >"$BIN/fc-list" <<'EOF'
#!/bin/sh
echo "/f: JetBrainsMono Nerd Font:style=Regular"
EOF
# Fake alacritty whose --version is set per scenario via $FAKE_ALACRITTY_VER.
cat >"$BIN/alacritty" <<'EOF'
#!/bin/sh
echo "alacritty ${FAKE_ALACRITTY_VER:-0.15.1} (1234abc)"
EOF
chmod +x "$BIN/fc-list" "$BIN/alacritty"; PATH="$BIN:$PATH"; export PATH

# Stubs: run_step runs the wrapped command; downloads/pkg installs are recorded.
run_step() { shift; "$@"; }
pkg_install() { echo "PKG $*" >>"$OUT"; }
osr_download() { echo "DOWNLOAD $*" >>"$OUT"; return 1; }

# --- scenario 1: current Alacritty + a rice palette --------------------------
FAKE_ALACRITTY_VER=0.15.1; export FAKE_ALACRITTY_VER
OSR_HOME=$(mktemp -d); export OSR_HOME
THEME=$(mktemp -d); OSR_THEME_DIR="$THEME"; export OSR_THEME_DIR
mkdir -p "$THEME/config/alacritty"
printf '# RICE-PALETTE-MARKER\n[colors.primary]\nbackground = "#123456"\n' \
    >"$THEME/config/alacritty/alacritty-theme.toml"

. "$OSR_ROOT/modules/alacritty.sh" >/dev/null 2>&1

_CFG="$OSR_HOME/.config/alacritty/alacritty.toml"
assert_contains "$OUT" 'PKG alacritty unzip fontconfig' "installs alacritty + font deps via pkg_install"
assert_contains "$_CFG" 'JetBrainsMono Nerd Font' "base config installed (dotfiles-owned)"
assert_contains "$_CFG" '^\[general\]$' "0.14+ keeps the [general] section"
assert_contains "$_CFG" 'import = \["~/.config/alacritty/alacritty-theme.toml"\]' \
    "base imports the rice palette layer"
assert_contains "$_CFG" '^TERM = "xterm-256color"$' \
    "base pins a TERM every remote host knows (no ssh-terminfo in alacritty)"
assert_contains "$_CFG" 'action = "ClearHistory"' "base ships its keybinding layer"
assert_contains "$_CFG" '^key = "ArrowLeft"$' "bindings use the 0.13+ W3C key names"
refute_contains "$_CFG" '^key = "\(Left\|Right\|Back\)"$' \
    "no YAML-era key names (0.13 drops them silently instead of erroring)"
refute_contains "$_CFG" '^\[colors' "base carries NO palette - colors are theme-owned (§5)"
refute_contains "$_CFG" '^opacity' "base carries no opacity - transparency is theme-owned (§6)"
assert_contains "$OSR_HOME/.config/alacritty/alacritty-theme.toml" 'RICE-PALETTE-MARKER' \
    "rice palette overrides the dotfiles default (90-theme)"
refute_contains "$OUT" 'DOWNLOAD' "Nerd Font download skipped when already present (§2)"
rm -rf "$OSR_HOME" "$THEME"

# --- scenario 2: rice ships no palette -> dotfiles default -------------------
: >"$OUT"
OSR_HOME=$(mktemp -d); export OSR_HOME
THEME=$(mktemp -d); OSR_THEME_DIR="$THEME"; export OSR_THEME_DIR   # no config/alacritty

. "$OSR_ROOT/modules/alacritty.sh" >/dev/null 2>&1

_THEME="$OSR_HOME/.config/alacritty/alacritty-theme.toml"
assert_contains "$_THEME" '^\[colors.normal\]$' "dotfiles default palette used when rice ships none"
assert_contains "$_THEME" '^opacity = ' "the theme layer owns window.opacity"
rm -rf "$OSR_HOME" "$THEME"

# --- scenario 3: Alacritty 0.13 -> [general] downgraded to a top-level import -
: >"$OUT"
FAKE_ALACRITTY_VER=0.13.2
OSR_HOME=$(mktemp -d); export OSR_HOME
THEME=$(mktemp -d); OSR_THEME_DIR="$THEME"; export OSR_THEME_DIR

. "$OSR_ROOT/modules/alacritty.sh" >/dev/null 2>&1

_CFG="$OSR_HOME/.config/alacritty/alacritty.toml"
refute_contains "$_CFG" '^\[general\]$' "0.13 drops the [general] section it cannot parse"
assert_contains "$_CFG" '^import = \[' "0.13 keeps import as the top-level key it expects"
assert_contains "$_CFG" '^TERM = "xterm-256color"$' "the rest of the base config is untouched"
rm -rf "$OSR_HOME" "$THEME"

# --- scenario 4: pre-TOML Alacritty -> warn, do not pretend it landed --------
FAKE_ALACRITTY_VER=0.12.3
OSR_HOME=$(mktemp -d); export OSR_HOME
CAP=$(install_alacritty_config "$OSR_DOTFILES/alacritty/alacritty.toml" \
    "$OSR_HOME/.config/alacritty/alacritty.toml" 2>&1)
printf '%s\n' "$CAP" | grep -q 'alacritty.yml' \
    && ok "0.12 warns that the TOML config is ignored (§9)" \
    || fail "no pre-TOML warning for alacritty 0.12"
rm -rf "$OSR_HOME"

rm -rf "$BIN"; rm -f "$OUT"
finish
