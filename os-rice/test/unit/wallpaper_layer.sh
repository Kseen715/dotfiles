#!/bin/sh
# Proves the shared wallpaper resolution in lib/config.sh: a rice's image is
# found (and a `.txt` placeholder is not), it is installed once into the user's
# Wallpapers dir, and every {{WALLPAPER_PATH}} consumer - hyprpaper.conf,
# hyprland.conf's `env =`, gtklock's style.css - is filled with that ONE
# installed path. This is the bug the legacy port lost: the configs referenced
# `$WALLPAPER_PATH` while nothing ever copied the file it named.
# Hermetic: temp HOME + temp rice, no root, no packages.
set -eu
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_ROOT=$(cd -- "$HERE/../.." && pwd)
OSR_LIB="$OSR_ROOT/lib"; export OSR_LIB
NO_COLOR=1; OSR_USER=$(id -un); export OSR_USER   # as_user becomes a no-op
. "$OSR_LIB/ui.sh"; . "$OSR_LIB/log.sh"; . "$OSR_LIB/user.sh"; . "$OSR_LIB/config.sh"
. "$HERE/../lib.sh"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
OSR_HOME="$TMP/home"; export OSR_HOME
RICE="$TMP/rice"; OSR_RICE_DIR="$RICE"; export OSR_RICE_DIR
mkdir -p "$RICE/wallpapers" "$RICE/config" "$OSR_HOME"

# --- a placeholder-only rice has NO wallpaper --------------------------------
# rices/{xin,nord,catppuccin,gruvbox} ship `wallpapers/<name>.txt` saying "drop a
# real image here". Picking that as the wallpaper would paint a text file.
printf 'placeholder - drop a real image here\n' >"$RICE/wallpapers/xin.txt"
assert_eq "" "$(osr_rice_wallpaper)" "a .txt placeholder is not a wallpaper"
assert_eq "" "$(osr_install_wallpaper)" "nothing is installed for a placeholder-only rice"

# --- a real image wins and is installed to ~/Pictures/Wallpapers -------------
printf 'JPEGBYTES' >"$RICE/wallpapers/avogado6 - 2024.06.jpg"
assert_eq "$RICE/wallpapers/avogado6 - 2024.06.jpg" "$(osr_rice_wallpaper)" \
    "the image is picked over the placeholder"

WP=$(osr_install_wallpaper)
assert_eq "$OSR_HOME/Pictures/Wallpapers/avogado6 - 2024.06.jpg" "$WP" \
    "installed into the user's Wallpapers dir (survives deleting the checkout)"
[ -f "$WP" ] && ok "the file is really there" || fail "the file is really there"

# Rerun-safe (§2): an identical file is not recopied, and the answer is stable.
touch -t 202001010000 "$WP"
_before=$(ls -l "$WP")
assert_eq "$WP" "$(osr_install_wallpaper)" "second call returns the same path"
assert_eq "$_before" "$(ls -l "$WP")" "an identical wallpaper is left alone on rerun"

# --- {{WALLPAPER_PATH}} substitution, shared by all three consumers ----------
printf 'preload = {{WALLPAPER_PATH}}\nwallpaper = ,{{WALLPAPER_PATH}}\n' >"$RICE/hyprpaper.conf"
install_wallpaper_layer "$RICE/hyprpaper.conf" "$OSR_HOME/.config/hypr/hyprpaper.conf"
assert_contains "$OSR_HOME/.config/hypr/hyprpaper.conf" "^preload = $OSR_HOME/Pictures/Wallpapers/" \
    "hyprpaper preload gets the installed path"
refute_contains "$OSR_HOME/.config/hypr/hyprpaper.conf" 'WALLPAPER_PATH' \
    "no placeholder survives into the installed config"

printf 'background-image: url("{{WALLPAPER_PATH}}");\n' >"$RICE/style.css"
install_wallpaper_layer "$RICE/style.css" "$OSR_HOME/.config/gtklock/style.css"
assert_contains "$OSR_HOME/.config/gtklock/style.css" "url(\"$OSR_HOME/Pictures/Wallpapers/" \
    "gtklock paints the same installed file"

# --- apply_wallpaper records the INSTALLED path, not the in-repo one ---------
# The recorded path is what a rice switch swaps; pointing it inside the repo
# would break the moment the checkout moves.
apply_wallpaper
assert_contains "$OSR_HOME/.config/osr/wallpaper" "^$OSR_HOME/Pictures/Wallpapers/" \
    "apply_wallpaper records the installed path"
refute_contains "$OSR_HOME/.config/osr/wallpaper" "$RICE/wallpapers" \
    "the recorded path does not point into the rice dir"

# --- no rice / no wallpaper is a clean no-op, not a failure ------------------
rm -f "$RICE"/wallpapers/*
assert_eq "" "$(osr_rice_wallpaper)" "an empty wallpapers dir resolves to nothing"
OSR_RICE_DIR="" apply_wallpaper && ok "apply_wallpaper is a no-op with no rice" \
                                || fail "apply_wallpaper is a no-op with no rice"

finish
