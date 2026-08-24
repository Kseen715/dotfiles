# session: wayland
# themable: yes
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
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
if [ -n "$OSR_THEME_DIR" ]; then
    _hd="$OSR_THEME_DIR/config/hypr"
    # hyprland.conf exports `env = WALLPAPER_PATH,{{WALLPAPER_PATH}}` for the
    # session; the placeholder resolves to the same installed file hyprpaper and
    # gtklock paint (config.sh). The legacy config hard-coded a `~/Pictures/...`
    # path there, which hyprland does not tilde-expand.
    [ -f "$_hd/hyprland.conf" ] && install_wallpaper_layer "$_hd/hyprland.conf" "$OSR_HOME/.config/hypr/hyprland.conf"
    # Every autostart script hyprland.conf's exec-once lines reference. Each
    # script guards on its own binary, so installing one whose module was not
    # selected is inert. (start-cliphist-store.sh is cliphist.sh's, installed
    # there so `osr module cliphist` alone still lands it.)
    for _s in start-audio start-amnezia-vpn-client start-mako start-easyeffects \
              start-top start-wleave; do
        [ -f "$_hd/$_s.sh" ] || continue
        install_layer "$_hd/$_s.sh" "$OSR_HOME/.config/hypr/$_s.sh"
        as_user chmod +x "$OSR_HOME/.config/hypr/$_s.sh"
    done
    if [ -f "$OSR_THEME_DIR/config/qt6ct/qt6ct.conf" ]; then
        install_layer "$OSR_THEME_DIR/config/qt6ct/qt6ct.conf" "$OSR_HOME/.config/qt6ct/qt6ct.conf"
    fi
    # Wayland session launcher(s) live in a system path SDDM reads. They stay
    # root-owned and world-executable (0755) - the legacy chowned them to the
    # target user so "sddm can run it", which SDDM never needed.
    _wd="$OSR_THEME_DIR/config/wayland-sessions"
    if [ -f "$_wd/hyprland.desktop" ]; then
        as_root mkdir -p /usr/share/wayland-sessions
        as_root cp -f "$_wd/hyprland.desktop" /usr/share/wayland-sessions/hyprland.desktop
        as_root cp -f "$_wd/start-hyprland.sh" /usr/share/wayland-sessions/start-hyprland.sh
        as_root chmod 0755 /usr/share/wayland-sessions/start-hyprland.sh
    fi
    # Second session entry for a VMware guest: the launcher adds the software
    # renderer + cursor workarounds (GSK_RENDERER=cairo, WLR_NO_HARDWARE_CURSORS)
    # that Hyprland needs without a real GPU. Offered alongside the normal entry
    # rather than replacing it, so the greeter still lists both (§9: VM-only path,
    # verified on hardware).
    if [ "${OSR_VIRT:-none}" = vmware ] && [ -f "$_wd/hyprland-vmware.desktop" ]; then
        as_root mkdir -p /usr/share/wayland-sessions
        as_root cp -f "$_wd/hyprland-vmware.desktop" /usr/share/wayland-sessions/hyprland-vmware.desktop
        as_root cp -f "$_wd/start-hyprland-vmware.sh" /usr/share/wayland-sessions/start-hyprland-vmware.sh
        as_root chmod 0755 /usr/share/wayland-sessions/start-hyprland-vmware.sh
    fi
fi
