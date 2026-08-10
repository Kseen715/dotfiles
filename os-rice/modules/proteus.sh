# session: x11+wayland
# modules/proteus.sh — Proteus, the theme/wallpaper picker (§6a).
#
# The GUI half of `osr theme`: a rofi-style overlay that lists the themes with
# their wallpapers as previews and applies the one you pick. It is the only
# module here that is BOTH sessions rather than one, because that is its whole
# point - X11 (override-redirect) and Wayland (wlr-layer-shell) in one binary.
#
# Built from source in this repo (../proteus), not fetched: it is part of the
# dotfiles, and a picker that reads this repo's themes has no meaning apart
# from it. The build is dispatched by any.map -> source:provide_proteus, which
# lives in lib/build.sh alongside the other source: providers. `cargo install
# --path` puts the binary in $OSR_HOME/.local/bin, which the shell layers
# already have on PATH.
#
# Config split (§5): proteus.toml is dotfiles-owned (geometry, behaviour), and
# it deliberately carries no colors - Proteus reads the palette out of whichever
# theme is under the cursor, so there is no rice-owned layer to swap.
#
# No package is listed for libxkbcommon or libwayland even though Proteus uses
# both: they are dlopen'd, not linked, and the only session that needs them
# (Wayland) cannot exist without them - the compositor itself is a client of
# both. On X11 neither is touched, and the picker still builds and runs on a
# machine that has neither.

run_step "Installing proteus" pkg_install proteus

_pr_cfg="$OSR_HOME/.config/proteus"
as_user mkdir -p "$_pr_cfg"
if [ -f "$OSR_DOTFILES/proteus/proteus.toml" ]; then
    install_layer "$OSR_DOTFILES/proteus/proteus.toml" "$_pr_cfg/proteus.toml"
fi
