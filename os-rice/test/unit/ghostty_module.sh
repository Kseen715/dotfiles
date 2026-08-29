#!/bin/sh
# Proves modules/ghostty.c §5 config ownership: `config` is dotfiles-owned, the
# palette is a rice-owned theme that overrides the dotfiles default, and the base
# carries the ssh/transparency settings it is supposed to — all hermetic (no
# net/root; the package install is stubbed, so no Zig build runs).
#
# The module is C now, so it runs through the core with PATH reduced to a stub
# bin/ — a `ghostty` on PATH is the source: provider's own §2 probe, and what
# keeps this test from starting a Zig compile.
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

OSR_BIN=${OSR_BIN:-$OSR_ROOT/build/osr}
if [ ! -x "$OSR_BIN" ]; then
    printf '  skip ghostty_module: %s is not built\n' "$OSR_BIN"
    finish
fi
for _t in sh env cat grep sed printf id rm mkdir mktemp test true false tee \
          cp chmod touch cut tr head sort wc dirname basename; do
    _p=$(command -v "$_t" 2>/dev/null) || :
    case "$_p" in /*) ln -sf "$_p" "$BIN/$_t" ;; esac
done
printf '#!/bin/sh\n[ "$1" = "-u" ] && shift 2\nexec "$@"\n' >"$BIN/sudo"
# dnf: `rpm -q` says not installed, so the native half of the batch is asked for
# and the install command names it.
printf '#!/bin/sh\nexit 1\n' >"$BIN/rpm"
cat >"$BIN/dnf" <<'EOF'
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
# ghostty present: the source: row is a §2 no-op, and `+version` answers 1.2.0
# so the shell-integration features are the full set.
cat >"$BIN/ghostty" <<'EOF'
#!/bin/sh
[ "$1" = "+version" ] && printf 'Version: 1.2.0\n'
exit 0
EOF
chmod +x "$BIN/sudo" "$BIN/rpm" "$BIN/dnf" "$BIN/ghostty"

run_module() {
    env -i PATH="$BIN" OUT="$OUT" OSR_ROOT="$OSR_ROOT" OSR_LIB="$OSR_LIB" \
        OSR_DOTFILES="$OSR_DOTFILES" OSR_PKG=dnf OSR_ARCH=x86_64 \
        OSR_DISTRO=fedora OSR_INIT=systemd OSR_USER="$OSR_USER" \
        OSR_HOME="$OSR_HOME" HOME="$OSR_HOME" OSR_THEME_DIR="$OSR_THEME_DIR" \
        OSR_THEME=demo NO_COLOR=1 TERM=dumb \
        "$OSR_BIN" module run ghostty >/dev/null 2>&1 || :
}

# --- scenario 1: rice ships a palette -> rice theme wins over dotfiles default -
OSR_HOME=$(mktemp -d); export OSR_HOME
THEME=$(mktemp -d); OSR_THEME_DIR="$THEME"; export OSR_THEME_DIR
mkdir -p "$THEME/config/ghostty"
printf '# RICE-PALETTE-MARKER\nbackground = #123456\n' >"$THEME/config/ghostty/ghostty-theme"

run_module

assert_contains "$OUT" 'PKG unzip fontconfig' "installs the font deps via pkg_install (ghostty itself is the source: row)"
assert_contains "$OSR_HOME/.config/ghostty/config" 'JetBrainsMono' "config installed (dotfiles-owned base)"
assert_contains "$OSR_HOME/.config/ghostty/config" '^background-opacity = 0.85$' "base sets 0.85 transparency"
assert_contains "$OSR_HOME/.config/ghostty/config" 'ssh-terminfo' "base enables ssh terminfo shell integration"
assert_contains "$OSR_HOME/.config/ghostty/config" '^clipboard-write = allow$' "base allows OSC 52 writes from remote hosts"
assert_contains "$OSR_HOME/.config/ghostty/config" '^config-file = ?ghostty-theme$' "base includes the rice palette layer"
assert_contains "$OSR_HOME/.config/ghostty/ghostty-theme" 'RICE-PALETTE-MARKER' "rice palette overrides dotfiles default (90-theme)"
rm -rf "$OSR_HOME" "$THEME"

# --- scenario 2: rice ships no palette -> dotfiles default palette used -------
: >"$OUT"
OSR_HOME=$(mktemp -d); export OSR_HOME
THEME=$(mktemp -d); OSR_THEME_DIR="$THEME"; export OSR_THEME_DIR   # no config/ghostty

run_module

assert_contains "$OSR_HOME/.config/ghostty/ghostty-theme" '^palette = 0=' "dotfiles default palette used when rice ships none"
refute_contains "$OUT" 'PKG.*curl' "Nerd Font download skipped when already present (§2)"
rm -rf "$OSR_HOME" "$THEME"

rm -rf "$BIN"; rm -f "$OUT"
finish
