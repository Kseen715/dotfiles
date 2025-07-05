info "Installing easyeffects..."
trace pacman -S --needed --noconfirm easyeffects
sudo -u "$DELEVATED_USER" mkdir -p /home/$DELEVATED_USER/.config/dconf
sudo -u "$DELEVATED_USER" mkdir -p /home/$DELEVATED_USER/.config/easyeffects/
info "Installing easyeffects plugins..."
trace pacman -S --needed --noconfirm lsp-plugins lsp-plugins-ladspa calf libebur128 zam-plugins zita-convolver speex soundtouch rnnoise libsamplerate libsndfile libbs2b fftw speexdsp nlohmann-json onetbb
sudo -u "$DELEVATED_USER" paru -S --needed --noconfirm mda-lv2-git libdeep_filter_ladspa-bin