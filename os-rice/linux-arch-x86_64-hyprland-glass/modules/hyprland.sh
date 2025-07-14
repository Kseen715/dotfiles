info "Installing hyprland..."
# check if hyprland is not already installed
if ! command -v hyprctl &>/dev/null; then
    trace rm /usr/share/wayland-sessions/hyprland.desktop
fi
trace pacman -S --needed --noconfirm hyprland hyprshot xdg-desktop-portal-hyprland hyprland-qt-support hypridle hyprutils aquamarine hyprgraphics hyprland-qtutils hyprpolkitagent qt6ct pop-gtk-theme
info "Installing hyprland dotfiles..."
trace sudo -u "$DELEVATED_USER" mkdir -p /home/$DELEVATED_USER/.config
trace chmod 775 /home/$DELEVATED_USER/.config
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config
trace sudo -u "$DELEVATED_USER" mkdir -p /home/$DELEVATED_USER/.config/hypr
trace chmod 775 /home/$DELEVATED_USER/.config/hypr
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config/hypr
trace mkdir -p /home/$DELEVATED_USER/Downloads
trace chmod 775 /home/$DELEVATED_USER/Downloads
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/Downloads
trace mkdir -p /home/$DELEVATED_USER/Pictures
trace chmod 775 /home/$DELEVATED_USER/Pictures
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/Pictures
trace cp $SCRIPT_DIR/config/hypr/hyprland.conf /home/$DELEVATED_USER/.config/hypr/

# Start of easyeffects
trace cp $SCRIPT_DIR/config/hypr/start-easyeffects.sh /home/$DELEVATED_USER/.config/hypr/start-easyeffects.sh
trace chmod +x /home/$DELEVATED_USER/.config/hypr/start-easyeffects.sh
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config/hypr/start-easyeffects.sh

# Start of top
trace cp $SCRIPT_DIR/config/hypr/start-top.sh /home/$DELEVATED_USER/.config/hypr/start-top.sh
trace chmod +x /home/$DELEVATED_USER/.config/hypr/start-top.sh
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config/hypr/start-top.sh

# Start of wleave
trace cp $SCRIPT_DIR/config/hypr/start-wleave.sh /home/$DELEVATED_USER/.config/hypr/start-wleave.sh
trace chmod +x /home/$DELEVATED_USER/.config/hypr/start-wleave.sh
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config/hypr/start-wleave.sh

# Start of audio
trace cp $SCRIPT_DIR/config/hypr/start-audio.sh /home/$DELEVATED_USER/.config/hypr/start-audio.sh
trace chmod +x /home/$DELEVATED_USER/.config/hypr/start-audio.sh
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config/hypr/start-audio.sh

# Config of qt6ct
trace mkdir -p /home/$DELEVATED_USER/.config/qt6ct
trace chmod 755 /home/$DELEVATED_USER/.config/qt6ct
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config/qt6ct
trace cp $SCRIPT_DIR/config/qt6ct/qt6ct.conf /home/$DELEVATED_USER/.config/qt6ct/qt6ct.conf
trace chmod 644 /home/$DELEVATED_USER/.config/qt6ct/qt6ct.conf
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.config/qt6ct/qt6ct.conf

trace mkdir -p /home/$DELEVATED_USER/.local/share
trace chmod 777 /home/$DELEVATED_USER/.local/share
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.local/share

trace mkdir -p /usr/share/wayland-sessions
trace chmod 755 /usr/share/wayland-sessions
trace cp $SCRIPT_DIR/config/wayland-sessions/hyprland.desktop /usr/share/wayland-sessions/hyprland.desktop
trace cp $SCRIPT_DIR/config/wayland-sessions/start-hyprland.sh /usr/share/wayland-sessions/start-hyprland.sh
trace chmod +x /usr/share/wayland-sessions/start-hyprland.sh
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /usr/share/wayland-sessions/start-hyprland.sh

if [ "$VIRT" = "vmware" ]; then
    trace cp $SCRIPT_DIR/config/wayland-sessions/hyprland-vmware.desktop /usr/share/wayland-sessions/hyprland-vmware.desktop
    trace cp $SCRIPT_DIR/config/wayland-sessions/start-hyprland-vmware.sh /usr/share/wayland-sessions/start-hyprland-vmware.sh
    trace chmod +x /usr/share/wayland-sessions/start-hyprland-vmware.sh
    # Give execute permissions to the delevated user, so sddm can run it
    trace chown "$DELEVATED_USER":"$DELEVATED_USER" /usr/share/wayland-sessions/start-hyprland-vmware.sh
fi
