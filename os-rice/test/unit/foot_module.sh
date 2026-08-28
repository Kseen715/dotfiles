#!/bin/sh
# Proves modules/foot.c §5 config ownership: foot.ini is dotfiles-owned, the
# palette is a rice-owned theme that overrides the dotfiles default, and the
# Nerd Font install skips when already present (§2) — all hermetic (no net/root).
#
# The module is C now, so it runs through the core rather than being sourced;
# the package manager and fc-list are stubbed on PATH instead of as shell
# functions, and what is asserted — the files in $HOME — is unchanged.
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

# Package tooling and the downloader, on PATH: dnf reports nothing installed,
# and curl records rather than fetches (nothing here should need it - the font
# is already present, which is the point of scenario 1).
printf '#!/bin/sh\ncase "$1" in install) printf "PKG %%s\\n" "$*" >>"%s" ;; esac\nexit 0\n' "$OUT" >"$BIN/dnf"
printf '#!/bin/sh\nexit 1\n' >"$BIN/rpm"
printf '#!/bin/sh\nprintf "DOWNLOAD %%s\\n" "$*" >>"%s"\nexit 1\n' "$OUT" >"$BIN/curl"
printf '#!/bin/sh\n[ "$1" = "-u" ] && shift 2\nexec "$@"\n' >"$BIN/sudo"
chmod +x "$BIN/dnf" "$BIN/rpm" "$BIN/curl" "$BIN/sudo"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip foot_module: %s is not built\n' "$OSR_BIN"
    exit 0
fi
export OSR_ROOT NO_COLOR
run_module() { "$OSR_BIN" module run foot >/dev/null 2>&1 || :; }

# --- scenario 1: rice ships a palette -> rice theme wins over dotfiles default -
FAKE_FOOT_VERSION=1.26.0; export FAKE_FOOT_VERSION   # knows [colors-dark]
OSR_HOME=$(mktemp -d); export OSR_HOME
THEME=$(mktemp -d); OSR_THEME_DIR="$THEME"; export OSR_THEME_DIR
mkdir -p "$THEME/config/foot"
printf '[colors-dark]\n# RICE-PALETTE-MARKER\n' >"$THEME/config/foot/foot-colors.ini"

run_module

assert_contains "$OUT" 'PKG .*foot.*unzip.*fontconfig' \
    "installs foot + font deps via pkg_install"
assert_contains "$OSR_HOME/.config/foot/foot.ini" 'JetBrainsMono' "foot.ini installed (dotfiles-owned base)"
assert_contains "$OSR_HOME/.config/foot/foot-colors.ini" 'RICE-PALETTE-MARKER' "rice palette overrides dotfiles default (90-theme)"
assert_contains "$OSR_HOME/.config/foot/foot-colors.ini" '^\[colors-dark\]$' "palette keeps [colors-dark] on foot >= 1.26"
refute_contains "$OUT" 'DOWNLOAD' "Nerd Font download skipped when already present (§2)"
rm -rf "$OSR_HOME" "$THEME"

# --- scenario 2: rice ships no palette -> dotfiles default palette used -------
# Old foot: [colors-dark] is an invalid section there, so it must be downgraded.
: >"$OUT"
FAKE_FOOT_VERSION=1.25.0
OSR_HOME=$(mktemp -d); export OSR_HOME
THEME=$(mktemp -d); OSR_THEME_DIR="$THEME"; export OSR_THEME_DIR   # no config/foot

run_module

assert_contains "$OSR_HOME/.config/foot/foot-colors.ini" 'regular0' "dotfiles default palette used when rice ships none"
assert_contains "$OSR_HOME/.config/foot/foot-colors.ini" '^\[colors\]$' "palette section downgraded for foot < 1.26"
refute_contains "$OSR_HOME/.config/foot/foot-colors.ini" 'colors-dark' "no [colors-dark] left for a foot that rejects it"
rm -rf "$OSR_HOME" "$THEME"

rm -rf "$BIN"; rm -f "$OUT"
finish
