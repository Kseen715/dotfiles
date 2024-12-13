#!/usr/bin/env bash

YES=false

# Parse arguments
while getopts "y" opt; do
    case $opt in
        y) YES=true ;;
    esac
done

# Install .config/i3 to $HOME
# if directory exists, ask to overwrite
if [ -d $HOME/.config/i3 ]; then
    if [ "$YES" = true ]; then
        cp -r ./.config/i3 $HOME/.config/i3
        echo "[✨] Copied .config/i3 to $HOME/.config/i3"
    else
        echo "[✨] Directory .config/i3 already exists. Overwrite? (y/N)"
        read answer
        if [ "$answer" = "y" ]; then
            cp -r ./.config/i3 $HOME/.config/i3
            echo "[✨] Copied .config/i3 to $HOME/.config/i3"
        fi
    fi
else
    # make sure the directory exists
    mkdir -p $HOME/.config
    echo "[✨] Created directory $HOME/.config"
    cp -r ./.config/i3 $HOME/.config/i3
    echo "[✨] Copied .config/i3 to $HOME/.config/i3"
fi