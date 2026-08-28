#!/bin/sh
# Proves modules/btop.c §5 config ownership: btop.conf is dotfiles-owned and
# selects the "rice" theme name, while the palette is a rice-owned theme layer
# installed as themes/rice.theme, overriding the dotfiles default. Hermetic (no
# net/root; the package install is stubbed).
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_DOTFILES OSR_PKG=apt
NO_COLOR=1; OSR_USER=$(id -un); export OSR_USER   # as_user becomes a no-op
export OSR_ROOT NO_COLOR
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"; . "$OSR_LIB/config.sh"
. "$HERE/../lib.sh"

# The module is C now, so it runs through the core rather than being sourced,
# and the package manager is stubbed on PATH instead of as a shell function.
# What is asserted is unchanged: the files that end up in $HOME.
OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip btop_module: %s is not built\n' "$OSR_BIN"
    exit 0
fi
OUT=$(mktemp)
BIN=$(mktemp -d)
printf '#!/bin/sh\nprintf "PKG %%s\\n" "$*" >>"%s"\nexit 0\n' "$OUT" >"$BIN/apt-get"
printf '#!/bin/sh\nexit 1\n' >"$BIN/dpkg"
# The install escalates; there is no terminal here to authenticate at, so sudo
# is a pass-through. The stubs above are what actually answers.
printf '#!/bin/sh\n[ "$1" = "-u" ] && shift 2\nexec "$@"\n' >"$BIN/sudo"
chmod +x "$BIN/apt-get" "$BIN/dpkg" "$BIN/sudo"
PATH="$BIN:$PATH"; export PATH
run_module() { "$OSR_BIN" module run btop >/dev/null 2>&1 || :; }

# --- scenario 1: rice ships a theme -> rice theme wins over dotfiles default --
OSR_HOME=$(mktemp -d); export OSR_HOME
THEME=$(mktemp -d); OSR_THEME_DIR="$THEME"; export OSR_THEME_DIR
mkdir -p "$THEME/config/btop"
printf '# RICE-THEME-MARKER\ntheme[main_fg]="#123456"\n' >"$THEME/config/btop/btop.theme"

run_module

assert_contains "$OUT" 'PKG .*btop' "installs btop via pkg_install"
assert_contains "$OSR_HOME/.config/btop/btop.conf" '^color_theme = "rice"$' "base selects the rice theme name"
assert_contains "$OSR_HOME/.config/btop/themes/rice.theme" 'RICE-THEME-MARKER' "rice theme overrides dotfiles default (90-theme)"
rm -rf "$OSR_HOME" "$THEME"

# --- scenario 2: rice ships no theme -> dotfiles default theme used -----------
: >"$OUT"
OSR_HOME=$(mktemp -d); export OSR_HOME
THEME=$(mktemp -d); OSR_THEME_DIR="$THEME"; export OSR_THEME_DIR   # no config/btop

run_module

assert_contains "$OSR_HOME/.config/btop/themes/rice.theme" '^theme\[main_fg\]=' "dotfiles default theme used when rice ships none"
rm -rf "$OSR_HOME" "$THEME"

rm -f "$OUT"; rm -rf "$BIN"
finish
