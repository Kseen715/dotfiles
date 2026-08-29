#!/bin/sh
# Proves modules/wezterm.c §5 config ownership: `.wezterm.lua` is dotfiles-owned,
# the palette is a rice-owned theme installed into WezTerm's colors/ dir and
# overriding the dotfiles default, and the package resolves to the source build
# (never a flatpak/AppImage) — all hermetic (no net/root; pkg_install is stubbed,
# so no Rust build runs).
#
# The routing half is a pkgmap question and is asked of lib/pkg.sh directly; the
# config half is the module, which is C now and runs through the core with a
# `wezterm` already on PATH — the source: provider's own §2 probe, and what
# keeps this test from starting a Rust build.
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
case "$(PKG_CONFIG_PATH='' _osr_pkgconfig_path)" in
    "/usr/lib/$(uname -m)-linux-gnu/pkgconfig:"*) ok "pkgconfig path leads with the system multiarch dir" ;;
    *) fail "pkgconfig path missing the system multiarch dir" ;;
esac
assert_eq "/keep:/usr/lib/$(uname -m)-linux-gnu/pkgconfig" \
    "$(PKG_CONFIG_PATH=/keep _osr_pkgconfig_path | cut -d: -f1,2)" \
    "an existing PKG_CONFIG_PATH is preserved, not replaced"

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip wezterm_module: %s is not built\n' "$OSR_BIN"
    finish
fi
# The stub bin the module runs inside. `wezterm` present -> the source: row is a
# §2 no-op; fc-list reports the font -> no download; dpkg says "not installed"
# so the native half of the batch is actually asked for, and apt-get logs it.
for _t in sh env cat grep sed printf id rm mkdir mktemp test true false tee \
          cp chmod cut tr head sort wc dirname basename; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in /*) ln -sf "$_p" "$BIN/$_t" ;; esac
done
printf '#!/bin/sh\n[ "$1" = "-u" ] && shift 2\nexec "$@"\n' >"$BIN/sudo"
printf '#!/bin/sh\nexit 1\n' >"$BIN/dpkg"
printf '#!/bin/sh\nexit 0\n' >"$BIN/wezterm"
cat >"$BIN/apt-get" <<'EOF'
#!/bin/sh
_seen=0; _pkgs=""
for _a in "$@"; do
    if [ "$_a" = install ]; then _seen=1; continue; fi
    case "$_a" in -*|*=*) continue ;; esac
    [ "$_seen" = 1 ] && _pkgs="$_pkgs $_a"
done
[ -n "$_pkgs" ] && printf 'PKG%s\n' "$_pkgs" >>"$OUT"
exit 0
EOF
chmod +x "$BIN/sudo" "$BIN/dpkg" "$BIN/wezterm" "$BIN/apt-get"

run_module() {
    env -i PATH="$BIN" OUT="$OUT" OSR_ROOT="$OSR_ROOT" OSR_LIB="$OSR_LIB" \
        OSR_DOTFILES="$OSR_DOTFILES" OSR_PKG=apt OSR_ARCH=x86_64 \
        OSR_DISTRO=ubuntu OSR_CODENAME=noble OSR_VERSION_ID=24.04 \
        OSR_INIT=systemd OSR_USER="$OSR_USER" OSR_HOME="$OSR_HOME" \
        HOME="$OSR_HOME" OSR_THEME_DIR="$OSR_THEME_DIR" OSR_THEME=demo \
        NO_COLOR=1 TERM=dumb "$OSR_BIN" module run wezterm >/dev/null 2>&1 || :
}

# --- scenario 1: rice ships a palette -> rice theme wins over dotfiles default -
OSR_HOME=$(mktemp -d); export OSR_HOME
THEME=$(mktemp -d); OSR_THEME_DIR="$THEME"; export OSR_THEME_DIR
mkdir -p "$THEME/config/wezterm"
printf '# RICE-PALETTE-MARKER\n[colors]\nbackground = "#123456"\n' >"$THEME/config/wezterm/wezterm-theme.toml"

run_module

assert_contains "$OUT" 'PKG unzip fontconfig' "installs the font deps via pkg_install (wezterm itself is the source: row)"
assert_contains "$OSR_HOME/.wezterm.lua" 'wezterm.config_builder' "base .wezterm.lua installed (dotfiles-owned)"
assert_contains "$OSR_HOME/.wezterm.lua" 'colors/osr-rice.toml' "base selects the rice palette layer when present"
assert_contains "$OSR_HOME/.config/wezterm/colors/osr-rice.toml" 'RICE-PALETTE-MARKER' \
    "rice palette overrides dotfiles default (90-theme)"
rm -rf "$OSR_HOME" "$THEME"

# --- scenario 2: rice ships no palette -> dotfiles default palette used -------
: >"$OUT"
OSR_HOME=$(mktemp -d); export OSR_HOME
THEME=$(mktemp -d); OSR_THEME_DIR="$THEME"; export OSR_THEME_DIR   # no config/wezterm

run_module

assert_contains "$OSR_HOME/.config/wezterm/colors/osr-rice.toml" '^name = "osr-rice"$' \
    "dotfiles default palette used when rice ships none"
refute_contains "$OUT" 'PKG.*curl' "Nerd Font download skipped when already present (§2)"
rm -rf "$OSR_HOME" "$THEME"

# --- every theme gets a wezterm palette, from its own file or the template ----
# This replaces an older assertion that every theme shipping a ghostty palette
# also shipped a wezterm one. That pairing was the N*M problem itself: it could
# only ever be satisfied by writing one more file per theme. The invariant worth
# holding is the outcome - every theme HAS a wezterm palette - which the template
# satisfies for free (§6b, lib/config.sh).
for d in "$OSR_ROOT"/themes/*/theme.list; do
    r=$(basename "$(dirname "$d")")
    if [ -f "$OSR_ROOT/themes/$r/config/wezterm/wezterm-theme.toml" ] \
        || [ -f "$OSR_DOTFILES/wezterm/wezterm-theme.toml.tmpl" ]; then
        ok "theme $r resolves a wezterm palette"
    else
        fail "theme $r has no wezterm palette and no template to render one"
    fi
done

rm -rf "$BIN"; rm -f "$OUT"
finish
