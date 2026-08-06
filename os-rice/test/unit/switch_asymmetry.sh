#!/bin/sh
# Proves §6: switching gruvbox -> nord swaps the rice-owned 90-theme, starship
# config and wallpaper, while user-owned 00-env/99-local and dotfiles aliases
# stay untouched.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_ROOT OSR_LIB OSR_DOTFILES OSR_PKG=apt
NO_COLOR=1; OSR_USER=$(id -un); export OSR_USER
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"; . "$OSR_LIB/config.sh"
. "$HERE/../lib.sh"

H=$(mktemp -d); OSR_HOME="$H"; export OSR_HOME
RC="$OSR_HOME/.config/osr/zsh/rc.d"

apply_rice() {
    OSR_RICE_DIR="$OSR_ROOT/rices/$1"; export OSR_RICE_DIR
    seed_once     "$OSR_DOTFILES/zsh/rc.d/00-env.zsh"     "$RC/00-env.zsh" >/dev/null
    install_layer "$OSR_DOTFILES/zsh/rc.d/20-aliases.zsh" "$RC/20-aliases.zsh" >/dev/null
    install_layer "$OSR_RICE_DIR/config/zsh/90-theme.zsh" "$RC/90-theme.zsh" >/dev/null
    compose_starship_config "$OSR_DOTFILES/starship/starship.toml" \
        "$OSR_RICE_DIR/config/starship.palette.toml" "$OSR_HOME/.config/starship.toml" >/dev/null
    seed_empty "$RC/99-local.zsh"
    apply_wallpaper >/dev/null
}

apply_rice gruvbox
echo 'export MY_MACHINE_VAR=1' >> "$RC/00-env.zsh"
echo 'alias mine="echo local"' >> "$RC/99-local.zsh"

apply_rice nord

assert_contains "$RC/90-theme.zsh" nord "90-theme swapped to nord"
# The composed starship.toml carries the base body AND the nord palette colors;
# the gruvbox accent must be gone (only the palette swaps, base stays).
assert_contains "$OSR_HOME/.config/starship.toml" "88c0d0" "starship palette swapped to nord colors"
assert_contains "$OSR_HOME/.config/starship.toml" 'palette = "theme"' "starship base body composed in"
refute_contains "$OSR_HOME/.config/starship.toml" "fabd2f" "old gruvbox accent gone after switch"
assert_contains "$RC/00-env.zsh" MY_MACHINE_VAR "00-env untouched (user territory)"
assert_contains "$RC/99-local.zsh" "alias mine" "99-local untouched (user territory)"
assert_contains "$RC/20-aliases.zsh" lsd "20-aliases present (dotfiles-owned)"

# --- the wallpaper half of the swap ------------------------------------------
# On its own fixture pair, because the repo's gruvbox/nord ship a
# `wallpapers/*.txt` "drop a real image here" placeholder rather than an image -
# and a placeholder resolves to no wallpaper at all (see wallpaper_layer.sh).
F=$(mktemp -d)
for r in one two; do
    mkdir -p "$F/$r/wallpapers"
    printf 'IMAGE-%s' "$r" >"$F/$r/wallpapers/$r.png"
done
OSR_RICE_DIR="$F/one"; export OSR_RICE_DIR; apply_wallpaper >/dev/null
assert_contains "$OSR_HOME/.config/osr/wallpaper" "one.png" "wallpaper recorded for the first rice"
OSR_RICE_DIR="$F/two"; export OSR_RICE_DIR; apply_wallpaper >/dev/null
assert_contains "$OSR_HOME/.config/osr/wallpaper" "two.png" "wallpaper swapped on rice switch"
[ -f "$OSR_HOME/Pictures/Wallpapers/one.png" ] \
    && ok "the previous rice's wallpaper file is kept (additive, only the pointer swaps)" \
    || fail "the previous rice's wallpaper file is kept (additive, only the pointer swaps)"

rm -rf "$H" "$F"
finish
