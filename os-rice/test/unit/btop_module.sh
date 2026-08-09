#!/bin/sh
# Proves modules/btop.sh §5 config ownership: btop.conf is dotfiles-owned and
# selects the "rice" theme name, while the palette is a rice-owned theme layer
# installed as themes/rice.theme, overriding the dotfiles default. Hermetic (no
# net/root; the package install is stubbed).
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_DOTFILES OSR_PKG=apt
NO_COLOR=1; OSR_USER=$(id -un); export OSR_USER   # as_user becomes a no-op
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"; . "$OSR_LIB/config.sh"
. "$HERE/../lib.sh"

OUT=$(mktemp)
run_step() { shift; "$@"; }
pkg_install() { echo "PKG $*" >>"$OUT"; }

# --- scenario 1: rice ships a theme -> rice theme wins over dotfiles default --
OSR_HOME=$(mktemp -d); export OSR_HOME
THEME=$(mktemp -d); OSR_THEME_DIR="$THEME"; export OSR_THEME_DIR
mkdir -p "$THEME/config/btop"
printf '# RICE-THEME-MARKER\ntheme[main_fg]="#123456"\n' >"$THEME/config/btop/btop.theme"

. "$OSR_ROOT/modules/btop.sh"

assert_contains "$OUT" 'PKG btop' "installs btop via pkg_install"
assert_contains "$OSR_HOME/.config/btop/btop.conf" '^color_theme = "rice"$' "base selects the rice theme name"
assert_contains "$OSR_HOME/.config/btop/themes/rice.theme" 'RICE-THEME-MARKER' "rice theme overrides dotfiles default (90-theme)"
rm -rf "$OSR_HOME" "$THEME"

# --- scenario 2: rice ships no theme -> dotfiles default theme used -----------
: >"$OUT"
OSR_HOME=$(mktemp -d); export OSR_HOME
THEME=$(mktemp -d); OSR_THEME_DIR="$THEME"; export OSR_THEME_DIR   # no config/btop

. "$OSR_ROOT/modules/btop.sh"

assert_contains "$OSR_HOME/.config/btop/themes/rice.theme" '^theme\[main_fg\]=' "dotfiles default theme used when rice ships none"
rm -rf "$OSR_HOME" "$THEME"

rm -f "$OUT"
finish
