info "Installing Steam..."
# we need to add STEAM_FORCE_DESKTOPUI_SCALING=1 steam to the environment variables somewhere for wayland support
if ! trace grep -q "STEAM_FORCE_DESKTOPUI_SCALING" $DELEVATED_USER_HOME/.bashrc; then
    info "Adding STEAM_FORCE_DESKTOPUI_SCALING=1 to $DELEVATED_USER_HOME/.bashrc"
    trace "printf '\nexport STEAM_FORCE_DESKTOPUI_SCALING=1\n' | tee -a $DELEVATED_USER_HOME/.bashrc > /dev/null"
else
    warning "STEAM_FORCE_DESKTOPUI_SCALING already exists in $DELEVATED_USER_HOME/.bashrc"
fi

trace pacman -S --needed --noconfirm ttf-liberation lib32-systemd
trace sudo -u "$DELEVATED_USER" $AUR_HELPER -S --needed --noconfirm steam

trace mkdir -p $DELEVATED_USER_HOME/.local/share/Steam
trace chmod 775 $DELEVATED_USER_HOME/.local/share/Steam
trace chown "$DELEVATED_USER":"$DELEVATED_USER" $DELEVATED_USER_HOME/.local/share/Steam

# if using systemd-resolved, create a symlink for resolv.conf
if [ -d /run/systemd/resolve ]; then
    info "Systemd-resolved detected, creating symlink for resolv.conf"
    trace ln -sf ../run/systemd/resolve/stub-resolv.conf /etc/resolv.conf
else
    info "Systemd-resolved not detected, skipping resolv.conf symlink creation"
fi