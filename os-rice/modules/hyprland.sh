# modules/hyprland.sh — Hyprland compositor + session wiring. ONE copy, POSIX
# (was .../modules/hyprland.sh, ~66 lines of bash+chown boilerplate). Package
# install goes through pkg_install; config is rice-owned (§5/§6) and copied via
# the framework's as_user/install_layer helpers instead of hand-rolled
# sudo -u + chown. The wayland-session launchers land in a system path, so they
# are copied as_root. DE-runtime module: installs in a container, but only a real
# GPU/display exercises it (§9).
run_step "Installing Hyprland" pkg_install \
    hyprland hyprshot xdg-desktop-portal-hyprland hyprland-qt-support hypridle \
    hyprutils aquamarine hyprgraphics hyprland-qtutils hyprpolkitagent qt6ct pop-gtk-theme

# User dirs the session expects (idempotent; owned by OSR_USER via as_user).
as_user mkdir -p "$OSR_HOME/.config/hypr" "$OSR_HOME/Downloads" \
    "$OSR_HOME/Pictures" "$OSR_HOME/.local/share"

# Rice-owned config: main hyprland.conf, autostart scripts, and the qt6ct theme.
if [ -n "$OSR_RICE_DIR" ]; then
    _hd="$OSR_RICE_DIR/config/hypr"
    [ -f "$_hd/hyprland.conf" ] && install_layer "$_hd/hyprland.conf" "$OSR_HOME/.config/hypr/hyprland.conf"
    for _s in start-easyeffects start-top start-wleave start-audio; do
        [ -f "$_hd/$_s.sh" ] || continue
        install_layer "$_hd/$_s.sh" "$OSR_HOME/.config/hypr/$_s.sh"
        as_user chmod +x "$OSR_HOME/.config/hypr/$_s.sh"
    done
    if [ -f "$OSR_RICE_DIR/config/qt6ct/qt6ct.conf" ]; then
        install_layer "$OSR_RICE_DIR/config/qt6ct/qt6ct.conf" "$OSR_HOME/.config/qt6ct/qt6ct.conf"
    fi
    # Wayland session launcher(s) live in a system path SDDM reads.
    _wd="$OSR_RICE_DIR/config/wayland-sessions"
    if [ -f "$_wd/hyprland.desktop" ]; then
        as_root mkdir -p /usr/share/wayland-sessions
        as_root cp -f "$_wd/hyprland.desktop" /usr/share/wayland-sessions/hyprland.desktop
        as_root cp -f "$_wd/start-hyprland.sh" /usr/share/wayland-sessions/start-hyprland.sh
        as_root chmod +x /usr/share/wayland-sessions/start-hyprland.sh
    fi
fi
