# modules/easyeffects.sh — EasyEffects audio effects + its LADSPA/LV2 plugin set.
# ONE copy, POSIX (was .../modules/easyeffects.sh). Two AUR plugins (mda-lv2,
# libdeep-filter-ladspa) come through the aur: rows in pacman.map.
run_step "Installing EasyEffects" pkg_install easyeffects
run_step "Installing EasyEffects plugins" pkg_install \
    lsp-plugins lsp-plugins-ladspa calf libebur128 zam-plugins zita-convolver \
    speex soundtouch rnnoise libsamplerate libsndfile libbs2b fftw speexdsp \
    nlohmann-json onetbb
run_step "Installing EasyEffects AUR plugins" pkg_install mda-lv2 libdeep-filter-ladspa
as_user mkdir -p "$OSR_HOME/.config/easyeffects" "$OSR_HOME/.config/dconf"
