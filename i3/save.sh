#!/usr/bin/env bash

# Save $HOME/.config/i3 to current directory
# if directory exists, ask to overwrite
if [ -d ./.config/i3 ]; then
    echo "[✨] Directory .config/i3 already exists. Overwrite? (y/N)"
    read answer
    if [ "$answer" = "y" ]; then
        cp -r $HOME/.config/i3 ./.config/i3
        echo "[✨] Copied $HOME/.config/i3 to ./.config/i3"
    fi
else
    # make sure the directory exists
    mkdir -p ./.config
    echo "[✨] Created directory ./.config"
    cp -r $HOME/.config/i3 ./.config/i3
    echo "[✨] Copied $HOME/.config/i3 to ./.config/i3"
fi