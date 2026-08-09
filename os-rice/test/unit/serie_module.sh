#!/bin/sh
# Proves modules/serie.sh §6 theme ownership: config.toml is a rice-owned theme
# layer that overrides the dotfiles default. Hermetic (no net/root; the package
# install and the cargo/rust prerequisite are stubbed).
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_DOTFILES OSR_PKG=pacman
NO_COLOR=1; OSR_USER=$(id -un); export OSR_USER   # as_user becomes a no-op
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"; . "$OSR_LIB/config.sh"
. "$HERE/../lib.sh"

OUT=$(mktemp)
run_step() { shift; "$@"; }
pkg_install() { echo "PKG $*" >>"$OUT"; }
_pkgmap_one() { echo serie; }   # native package -> no rust module sourced

# --- scenario 1: rice ships a theme -> rice theme wins over dotfiles default --
OSR_HOME=$(mktemp -d); export OSR_HOME
THEME=$(mktemp -d); OSR_THEME_DIR="$THEME"; export OSR_THEME_DIR
mkdir -p "$THEME/config/serie"
printf '# RICE-THEME-MARKER\n[color]\nfg = "Reset"\n' >"$THEME/config/serie/config.toml"

. "$OSR_ROOT/modules/serie.sh"

assert_contains "$OUT" 'PKG serie' "installs serie via pkg_install"
assert_contains "$OSR_HOME/.config/serie/config.toml" 'RICE-THEME-MARKER' "rice theme overrides dotfiles default (90-theme)"
rm -rf "$OSR_HOME" "$THEME"

# --- scenario 2: rice ships no theme -> dotfiles default theme used -----------
: >"$OUT"
OSR_HOME=$(mktemp -d); export OSR_HOME
THEME=$(mktemp -d); OSR_THEME_DIR="$THEME"; export OSR_THEME_DIR   # no config/serie

. "$OSR_ROOT/modules/serie.sh"

assert_contains "$OSR_HOME/.config/serie/config.toml" '^\[color\]$' "dotfiles default theme used when rice ships none"
assert_contains "$OSR_HOME/.config/serie/config.toml" '^branches = ' "graph branch colors themed too"
rm -rf "$OSR_HOME" "$THEME"

rm -f "$OUT"
finish
