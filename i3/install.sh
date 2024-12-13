#!/usr/bin/env bash

# Install .config/i3 to $HOME
# if directory exists, ask to overwrite
if [ -d $HOME/.config/i3 ]; then
    echo "Directory .config/i3 already exists. Overwrite? (y/N)"
    read answer
    if [ "$answer" = "y" ]; then
        cp -r ./.config/i3 $HOME/.config/i3
    fi
else
    cp -r ./.config/i3 $HOME/.config/i3
fi