# modules/pulseaudio.sh — PulseAudio + JACK audio stack, replacing PipeWire.
# ONE copy, POSIX. Mirror image of modules/pipewire.sh: the two are mutually
# exclusive, listing one removes the other. Arch package names, pacman-only.
[ "$OSR_PKG" = pacman ] || { info "PulseAudio swap is Arch-only - skipping"; return 0; }

# ponytail: the pipewire *core* is deliberately kept — xdg-desktop-portal-wlr
# (screen sharing) depends on it, so `pacman -R pipewire` would fail or break
# the Wayland session. Only the PulseAudio/JACK/ALSA replacement shims and the
# session manager go, which is what actually hands audio back to PulseAudio.
run_step "Removing PipeWire audio shims" pkg_remove \
    pipewire-pulse pipewire-jack pipewire-alsa pipewire-audio wireplumber
run_step "Installing PulseAudio + JACK" pkg_install \
    pulseaudio pulseaudio-alsa pulseaudio-jack pavucontrol jack2
