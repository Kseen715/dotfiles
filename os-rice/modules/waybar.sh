# session: wayland
# themable: yes
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
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
