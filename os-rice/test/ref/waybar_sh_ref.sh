# test/ref/waybar_sh_ref.sh — the sh implementation of modules/waybar.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/waybar.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: wayland
# themable: yes
# modules/waybar.sh — Waybar status bar + rice-owned config. ONE copy, POSIX
# (was .../modules/waybar.sh). gsimplecal (calendar popup) and ddcutil (monitor
# brightness via the custom ddc module) are companions the config invokes.
run_step "Installing Waybar" pkg_install waybar gsimplecal ddcutil
if [ -n "$OSR_THEME_DIR" ] && [ -d "$OSR_THEME_DIR/config/waybar" ]; then
    _wb="$OSR_THEME_DIR/config/waybar"
    install_layer "$_wb/config.jsonc" "$OSR_HOME/.config/waybar/config.jsonc"
    # The stylesheet is the palette half and comes from the shared template
    # (§6b); config.jsonc and the ddc script are the glass rice's own layout.
    install_theme_layer waybar style.css "$OSR_HOME/.config/waybar/style.css" \
        || install_layer "$_wb/style.css" "$OSR_HOME/.config/waybar/style.css"
    install_layer "$_wb/waybar-ddc-module.sh" "$OSR_HOME/.config/waybar/waybar-ddc-module.sh"
    as_user chmod +x "$OSR_HOME/.config/waybar/waybar-ddc-module.sh"
fi
