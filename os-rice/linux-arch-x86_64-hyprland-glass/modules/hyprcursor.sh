info "Installing hyprcursor..."
trace pacman -S --needed --noconfirm hyprcursor
trace sudo -u "$DELEVATED_USER" $AUR_HELPER -S --needed --noconfirm bibata-cursor-theme-bin
# copy cursor theme from /usr/share/icons/Bibata-Modern-Ice into
# ~/.local/share/icons/
trace mkdir -p $DELEVATED_USER_HOME/.local/share/icons/
trace chmod 775 $DELEVATED_USER_HOME/.local/share/icons/
trace chown "$DELEVATED_USER":"$DELEVATED_USER" $DELEVATED_USER_HOME/.local/share/icons/
trace cp -r /usr/share/icons/Bibata-Modern-Ice $DELEVATED_USER_HOME/.local/share/icons/
if [ command -v gsettings ]; then
    trace gsettings set org.gnome.desktop.interface cursor-theme "Bibata-Modern-Ice"
    trace gsettings set org.gnome.desktop.interface cursor-size 24
fi
if [ command -v flatpak ]; then
    trace flatpak override --user --filesystem=$DELEVATED_USER_HOME/.themes:ro --filesystem=$DELEVATED_USER_HOME/.local/share/icons:ro
fi