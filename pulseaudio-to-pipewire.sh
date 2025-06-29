#!/bin/bash
# remove jack and pulsuaudio
sudo pacman -R --noconfirm \
    pulseaudio pulseaudio-ctl pulseaudio-equalizer \
    pulseaudio-jack pulseaudio-lirc pulseaudio-rtp \
    jack2 jack2-dbus
# install pipewire and wireplumber
sudo pacman -S --needed --noconfirm \
    pipewire pipewire-pulse pipewire-alsa pipewire-jack \
    wireplumber pipewire-audio