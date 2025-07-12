#!/bin/bash

# launch qpwgraph with ~/.patchbay as the config file
if [ -f ~/.patchbay ]; then
    qpwgraph -a -m ~/.patchbay &
    pactl set-default-sink easyeffects_sink
    pactl set-default-source easyeffects_source
fi