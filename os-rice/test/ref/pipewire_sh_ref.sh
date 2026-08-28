# test/ref/pipewire_sh_ref.sh — the sh implementation of modules/pipewire.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/pipewire.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/pipewire.sh — PipeWire audio stack, replacing PulseAudio/JACK. ONE
# copy, POSIX (was .../linux-arch-x86_64-hyprland-glass/pulseaudio-to-pipewire.sh).
# Mirror image of modules/pulseaudio.sh: the two are mutually exclusive, listing
# one removes the other. Package names are Arch's; the swap is pacman-only.
[ "$OSR_PKG" = pacman ] || { info "PipeWire swap is Arch-only - skipping"; return 0; }

run_step "Removing PulseAudio/JACK" pkg_remove \
    pulseaudio pulseaudio-ctl pulseaudio-equalizer \
    pulseaudio-jack pulseaudio-lirc pulseaudio-rtp \
    jack2 jack2-dbus
run_step "Installing PipeWire + WirePlumber" pkg_install \
    pipewire pipewire-pulse pipewire-alsa pipewire-jack \
    wireplumber pipewire-audio
# ponytail: no enable_service — the units are per-user and socket-activated by
# the Arch packages. Add `enable_service pipewire` if a headless/system-wide
# setup ever needs it.
