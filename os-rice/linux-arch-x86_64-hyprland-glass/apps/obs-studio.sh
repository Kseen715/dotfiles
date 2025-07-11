info "Installing OBS Studio..."
trace pacman -S --needed --noconfirm obs-studio
trace mkdir -p /home/$DELEVATED_USER/.config/obs-studio
trace chown -R $DELEVATED_USER:$DELEVATED_USER /home/$DELEVATED_USER/.config/obs-studio