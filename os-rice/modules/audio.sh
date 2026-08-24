# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/audio.sh — PipeWire + the controls a desktop actually touches
# (i3-sugg §6). The distro-agnostic sibling of modules/pipewire.sh, which is
# Arch-only (it performs the PulseAudio->PipeWire package swap that only pacman
# needs). Never list both in one rice.
#
# sof-firmware is not optional on modern Intel laptops — without it there is no
# sound at all, and nothing in the logs points at a missing firmware blob.
#
# On systemd the daemons are socket-activated user units and start themselves.
# On runit/OpenRC there are no user units, so the i3 config execs pipewire,
# pipewire-pulse and wireplumber at session start; a second start is a no-op.

run_step "Installing PipeWire" pkg_install pipewire wireplumber

run_step "Installing audio controls" pkg_install \
    pavucontrol pwvucontrol pamixer pulsemixer ncpamixer playerctl \
    alsa-utils alsa-firmware sof-firmware

# Bluetooth audio codecs. SBC is the floor everything supports; AAC/aptX/LDAC
# only exist if the codec libraries are present when WirePlumber starts, and a
# missing one degrades silently to SBC - the headset works, it just sounds worse
# and nothing says why. Enable the ones you want in WirePlumber's bluez config.
run_step "Installing Bluetooth audio codecs" pkg_install sbc libfreeaptx libldac
