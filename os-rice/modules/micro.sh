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
    _mi_frag=$(osr_theme_source micro settings.json) || _mi_frag=""
    compose_json_config "$OSR_DOTFILES/micro/settings.json" "$_mi_frag" "$_mi/settings.json"
    case "$_mi_frag" in "${TMPDIR:-/tmp}"/osr-theme-*) rm -f "$_mi_frag" ;; esac
fi

# The colorscheme file itself (theme-owned, swapped on switch §6). Named after
# the theme, which is also what the settings.json fragment selects, so two
# themes' schemes coexist in the same colorschemes dir.
if [ -n "${OSR_THEME:-}" ]; then
    install_theme_layer micro theme.micro "$_mi/colorschemes/$OSR_THEME.micro" || :
fi
