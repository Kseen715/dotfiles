#!/bin/sh
# Proves modules/serie.c §6 theme ownership: config.toml is a rice-owned theme
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
BIN=$(mktemp -d)
# pacman is the whole package layer here: `-Q` says nothing is installed, `-S`
# records. On pacman `serie` is a native row, so the rust prerequisite the
# module would otherwise pull in is never reached.
printf '#!/bin/sh\ncase "$1" in -S) printf "PKG %%s\\n" "$*" >>"%s" ;; -Q*) exit 1 ;; esac\nexit 0\n' \
    "$OUT" >"$BIN/pacman"
printf '#!/bin/sh\n[ "$1" = "-u" ] && shift 2\nexec "$@"\n' >"$BIN/sudo"
chmod +x "$BIN/pacman" "$BIN/sudo"
PATH="$BIN:$PATH"; export PATH

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip serie_module: %s is not built\n' "$OSR_BIN"
    exit 0
fi
export OSR_ROOT NO_COLOR
run_module() { "$OSR_BIN" module run serie >/dev/null 2>&1 || :; }

# --- scenario 1: rice ships a theme -> rice theme wins over dotfiles default --
OSR_HOME=$(mktemp -d); export OSR_HOME
THEME=$(mktemp -d); OSR_THEME_DIR="$THEME"; export OSR_THEME_DIR
mkdir -p "$THEME/config/serie"
printf '# RICE-THEME-MARKER\n[color]\nfg = "Reset"\n' >"$THEME/config/serie/config.toml"

run_module

assert_contains "$OUT" 'PKG .*serie' "installs serie via pkg_install"
assert_contains "$OSR_HOME/.config/serie/config.toml" 'RICE-THEME-MARKER' "rice theme overrides dotfiles default (90-theme)"
rm -rf "$OSR_HOME" "$THEME"

# --- scenario 2: rice ships no theme -> dotfiles default theme used -----------
: >"$OUT"
OSR_HOME=$(mktemp -d); export OSR_HOME
THEME=$(mktemp -d); OSR_THEME_DIR="$THEME"; export OSR_THEME_DIR   # no config/serie

run_module

assert_contains "$OSR_HOME/.config/serie/config.toml" '^\[color\]$' "dotfiles default theme used when rice ships none"
assert_contains "$OSR_HOME/.config/serie/config.toml" '^branches = ' "graph branch colors themed too"
rm -rf "$OSR_HOME" "$THEME"

rm -f "$OUT"; rm -rf "$BIN"
finish
