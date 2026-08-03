#!/bin/sh
# Proves modules/wezterm.sh §5 config ownership: `.wezterm.lua` is dotfiles-owned,
# the palette is a rice-owned theme installed into WezTerm's colors/ dir and
# overriding the dotfiles default, and the package resolves to the source build
# (never a flatpak/AppImage) — all hermetic (no net/root; pkg_install is stubbed,
# so no Rust build runs).
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB OSR_DOTFILES OSR_PKG=apt
NO_COLOR=1; OSR_USER=$(id -un); export OSR_USER   # as_user becomes a no-op
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"; . "$OSR_LIB/config.sh"; . "$OSR_LIB/net.sh"; . "$OSR_LIB/fonts.sh"
. "$OSR_LIB/pkg.sh"; . "$OSR_LIB/build.sh"
. "$HERE/../lib.sh"

OUT=$(mktemp)
# Fake fc-list on PATH reporting the font present -> the install step skips.
BIN=$(mktemp -d)
cat >"$BIN/fc-list" <<'EOF'
#!/bin/sh
echo "/f: JetBrainsMono Nerd Font:style=Regular"
EOF
chmod +x "$BIN/fc-list"; PATH="$BIN:$PATH"; export PATH

# --- pkgmap: wezterm is the source build on every target (apt included) -------
OSR_ARCH=x86_64 OSR_VERSION_ID=24.04 OSR_CODENAME=noble
export OSR_ARCH OSR_VERSION_ID OSR_CODENAME
assert_eq "source:provide_wezterm" "$(_pkgmap_one wezterm)" "apt/noble resolves wezterm to the source build"
OSR_PKG=pacman; assert_eq "source:provide_wezterm" "$(_pkgmap_one wezterm)" "pacman resolves wezterm to the source build"
OSR_PKG=apt
if command -v provide_wezterm >/dev/null 2>&1; then
    ok "provide_wezterm builder is defined (source: row resolves)"
else
    fail "provide_wezterm builder missing"
fi
# A shadowing pkg-config (brew/conda/nix) searches only its own prefix, so the
# build must be handed the system .pc dirs explicitly.
case "$(PKG_CONFIG_PATH= _osr_pkgconfig_path)" in
    "/usr/lib/$(uname -m)-linux-gnu/pkgconfig:"*) ok "pkgconfig path leads with the system multiarch dir" ;;
    *) fail "pkgconfig path missing the system multiarch dir" ;;
esac
assert_eq "/keep:/usr/lib/$(uname -m)-linux-gnu/pkgconfig" \
    "$(PKG_CONFIG_PATH=/keep _osr_pkgconfig_path | cut -d: -f1,2)" \
    "an existing PKG_CONFIG_PATH is preserved, not replaced"

# Stubs: run_step runs the wrapped command; downloads/pkg installs are recorded.
run_step() { shift; "$@"; }
pkg_install() { echo "PKG $*" >>"$OUT"; }
osr_download() { echo "DOWNLOAD $*" >>"$OUT"; return 1; }

# --- scenario 1: rice ships a palette -> rice theme wins over dotfiles default -
OSR_HOME=$(mktemp -d); export OSR_HOME
RICE=$(mktemp -d); OSR_RICE_DIR="$RICE"; export OSR_RICE_DIR
mkdir -p "$RICE/config/wezterm"
printf '# RICE-PALETTE-MARKER\n[colors]\nbackground = "#123456"\n' >"$RICE/config/wezterm/wezterm-theme.toml"

. "$OSR_ROOT/modules/wezterm.sh"

assert_contains "$OUT" 'PKG wezterm unzip fontconfig' "installs wezterm + font deps via pkg_install"
assert_contains "$OSR_HOME/.wezterm.lua" 'wezterm.config_builder' "base .wezterm.lua installed (dotfiles-owned)"
assert_contains "$OSR_HOME/.wezterm.lua" 'colors/osr-rice.toml' "base selects the rice palette layer when present"
assert_contains "$OSR_HOME/.config/wezterm/colors/osr-rice.toml" 'RICE-PALETTE-MARKER' \
    "rice palette overrides dotfiles default (90-theme)"
rm -rf "$OSR_HOME" "$RICE"

# --- scenario 2: rice ships no palette -> dotfiles default palette used -------
: >"$OUT"
OSR_HOME=$(mktemp -d); export OSR_HOME
RICE=$(mktemp -d); OSR_RICE_DIR="$RICE"; export OSR_RICE_DIR   # no config/wezterm

. "$OSR_ROOT/modules/wezterm.sh"

assert_contains "$OSR_HOME/.config/wezterm/colors/osr-rice.toml" '^name = "osr-rice"$' \
    "dotfiles default palette used when rice ships none"
refute_contains "$OUT" 'DOWNLOAD' "Nerd Font download skipped when already present (§2)"
rm -rf "$OSR_HOME" "$RICE"

# --- every rice that themes ghostty also themes wezterm ----------------------
for d in "$OSR_ROOT"/rices/*/config/ghostty/ghostty-theme; do
    [ -f "$d" ] || continue
    r=$(basename "$(dirname "$(dirname "$(dirname "$d")")")")
    if [ -f "$OSR_ROOT/rices/$r/config/wezterm/wezterm-theme.toml" ]; then
        ok "rice $r ships a wezterm palette"
    else
        fail "rice $r themes ghostty but not wezterm"
    fi
done

rm -rf "$BIN"; rm -f "$OUT"
finish
