# session: x11+wayland
# modules/micro.sh — micro terminal editor + layered config. ONE copy, POSIX,
# distro-agnostic (native everywhere).
#
# Config split (§5): micro keeps everything in one settings.json and has no
# include, so the split is by composition — the dotfiles base carries behaviour
# and the rice fragment carries the `colorscheme` key naming the palette file it
# also ships (compose_json_config merges the two).

run_step "Installing micro" pkg_install micro

_mi="$OSR_HOME/.config/micro"

if [ -f "$OSR_DOTFILES/micro/settings.json" ]; then
    _mi_frag=""
    if [ -n "${OSR_THEME_DIR:-}" ] && [ -f "$OSR_THEME_DIR/config/micro/settings.json" ]; then
        _mi_frag="$OSR_THEME_DIR/config/micro/settings.json"
    fi
    compose_json_config "$OSR_DOTFILES/micro/settings.json" "$_mi_frag" "$_mi/settings.json"
fi

# The colorscheme file itself (rice-owned, swapped on switch §6). Named after
# the rice so two rices' schemes can coexist in the same colorschemes dir.
if [ -n "${OSR_THEME_DIR:-}" ]; then
    for _mi_cs in "$OSR_THEME_DIR"/config/micro/*.micro; do
        [ -f "$_mi_cs" ] || continue
        install_layer "$_mi_cs" "$_mi/colorschemes/$(basename "$_mi_cs")"
    done
fi
