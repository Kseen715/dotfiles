# test/ref/polybar_sh_ref.sh — the sh implementation of modules/polybar.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/polybar.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11
# themable: yes
# modules/polybar.sh — polybar status bar, the X11 replacement for waybar
# (i3-sugg §2). Config split (§5): config.ini + modules.ini are dotfiles-owned
# (bar geometry, module definitions, the `include-file` lines) and colors.ini is
# rice-owned, so a rice switch repaints the bar without touching its layout.
#
# Companions the shipped modules invoke: pamixer (volume), playerctl (MPRIS),
# brightnessctl (backlight), lm_sensors (temps), gsimplecal (date popup). They
# are listed here rather than in the config so a missing binary is an install
# error, not a silently blank module.

run_step "Installing polybar" pkg_install polybar pamixer playerctl lm_sensors gsimplecal

_pb="$OSR_HOME/.config/polybar"
as_user mkdir -p "$_pb"

for _f in config.ini modules.ini launch.sh; do
    [ -f "$OSR_DOTFILES/polybar/$_f" ] || continue
    install_layer "$OSR_DOTFILES/polybar/$_f" "$_pb/$_f"
done
if [ -f "$_pb/launch.sh" ]; then as_user chmod +x "$_pb/launch.sh"; fi

# Module helper scripts. The custom/script modules in modules.ini name these by
# absolute path, so a bar whose scripts did not land shows empty modules rather
# than an error - install them with the config, not separately.
if [ -d "$OSR_DOTFILES/polybar/scripts" ]; then
    as_user mkdir -p "$_pb/scripts"
    for _s in "$OSR_DOTFILES/polybar/scripts"/*.sh; do
        [ -f "$_s" ] || continue
        install_layer "$_s" "$_pb/scripts/$(basename "$_s")"
        as_user chmod +x "$_pb/scripts/$(basename "$_s")"
    done
fi

install_theme_layer polybar colors.ini "$_pb/colors.ini" || :
