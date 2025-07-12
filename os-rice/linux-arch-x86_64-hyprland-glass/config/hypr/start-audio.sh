#!/bin/bash
# pactl set-default-sink alsa_output.usb-Logitech_G733_Gaming_Headset-00.analog-stereo

# launch qpwgraph with ~/.patchbay as the config file
if [ -f ~/.patchbay ]; then
    qpwgraph -a -m ~/.patchbay &
fi