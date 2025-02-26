#!/usr/bin/env bash

SEP=','

DIRECTORIES_TO_INSTALL="$HOME/.config/i3"
DIRECTORIES_LOCAL="./.config/i3"

FILES_TO_INSTALL="~/.wezterm.lua"
FILES_LOCAL="./.wezterm.lua"

YES=false

# Parse arguments
while getopts "y" opt; do
    case $opt in
        y) YES=true ;;
    esac
done

# Convert comma-separated strings to arrays, splitting on SEP character
IFS="$SEP" read -r -a DIRS_TO_INSTALL <<< "$DIRECTORIES_TO_INSTALL"
IFS="$SEP" read -r -a DIRS_LOCAL <<< "$DIRECTORIES_LOCAL"
IFS="$SEP" read -r -a FILES_TO_INSTALL_ARR <<< "$FILES_TO_INSTALL"
IFS="$SEP" read -r -a FILES_LOCAL_ARR <<< "$FILES_LOCAL"

# Process directories
for i in "${!DIRS_TO_INSTALL[@]}"; do
    dir_to_save=$(echo "${DIRS_TO_INSTALL[$i]}" | xargs)
    dir_local=$(echo "${DIRS_LOCAL[$i]}" | xargs)
    
    if [ -d "$dir_to_save" ]; then
        if [ "$YES" = true ]; then
            cp -r "$dir_local" "$dir_to_save"
            echo "[✨] Copied $dir_local to $dir_to_save"
        else
            echo "[✨] Directory $dir_to_save already exists. Overwrite? (y/N)"
            read -r answer
            if [ "$answer" = "y" ]; then
                cp -r "$dir_local" "$dir_to_save"
                echo "[✨] Copied $dir_local to $dir_to_save"
            fi
        fi
    else
        mkdir -p "$(dirname "$dir_to_save")"
        echo "[✨] Created directory $(dirname "$dir_to_save")"
        cp -r "$dir_local" "$dir_to_save"
        echo "[✨] Copied $dir_local to $dir_to_save"
    fi
done

# Process files
for i in "${!FILES_TO_INSTALL_ARR[@]}"; do
    file_to_save=$(echo "${FILES_TO_INSTALL_ARR[$i]}" | xargs)
    file_local=$(echo "${FILES_LOCAL_ARR[$i]}" | xargs)
    
    if [ -f "$file_to_save" ]; then
        if [ "$YES" = true ]; then
            cp "$file_local" "$file_to_save"
            echo "[✨] Copied $file_local to $file_to_save"
        else
            echo "[✨] File $file_to_save already exists. Overwrite? (y/N)"
            read -r answer
            if [ "$answer" = "y" ]; then
                cp "$file_local" "$file_to_save"
                echo "[✨] Copied $file_local to $file_to_save"
            fi
        fi
    else
        mkdir -p "$(dirname "$file_to_save")"
        cp "$file_local" "$file_to_save"
        echo "[✨] Copied $file_local to $file_to_save"
    fi
done