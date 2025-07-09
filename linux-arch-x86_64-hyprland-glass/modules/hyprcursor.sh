info "Installing hyprcursor..."
trace pacman -S --needed --noconfirm hyprcursor
trace sudo -u "$DELEVATED_USER" $AUR_HELPER -S --needed --noconfirm bibata-cursor-theme-bin
# copy cursor theme from /usr/share/icons/Bibata-Modern-Ice into
# ~/.local/share/icons/
trace mkdir -p /home/$DELEVATED_USER/.local/share/icons/
trace chmod 775 /home/$DELEVATED_USER/.local/share/icons/
trace chown "$DELEVATED_USER":"$DELEVATED_USER" /home/$DELEVATED_USER/.local/share/icons/
trace cp -r /usr/share/icons/Bibata-Modern-Ice /home/$DELEVATED_USER/.local/share/icons/
if [ command -v gsettings ]; then
    trace gsettings set org.gnome.desktop.interface cursor-theme "Bibata-Modern-Ice"
    trace gsettings set org.gnome.desktop.interface cursor-size 24
fi
if [ command -v flatpak ]; then
    trace flatpak override --user --filesystem=/home/$DELEVATED_USER/.themes:ro --filesystem=/home/$DELEVATED_USER/.local/share/icons:ro
fi