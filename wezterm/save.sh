#!/usr/bin/env bash

SEP=','

DIRECTORIES_TO_SAVE=""
DIRECTORIES_LOCAL=""

FILES_TO_SAVE="~/.wezterm.lua"
FILES_LOCAL="./.wezterm.lua"

YES=false

# Parse arguments
while getopts "y" opt; do
    case $opt in
        y) YES=true ;;
    esac
done

# Convert comma-separated strings to arrays
IFS="$SEP" read -r -a DIRS_TO_SAVE <<< "$DIRECTORIES_TO_SAVE"
IFS="$SEP" read -r -a DIRS_LOCAL <<< "$DIRECTORIES_LOCAL"
IFS="$SEP" read -r -a FILES_TO_SAVE_ARR <<< "$FILES_TO_SAVE"
IFS="$SEP" read -r -a FILES_LOCAL_ARR <<< "$FILES_LOCAL"

# Process directories
for i in "${!DIRS_TO_SAVE[@]}"; do
    dir_to_save=$(echo "${DIRS_TO_SAVE[$i]}" | xargs)
    dir_local=$(echo "${DIRS_LOCAL[$i]}" | xargs)
    
    if [ ! -d "$dir_to_save" ]; then
        echo "[❌] Source directory $dir_to_save doesn't exist"
        continue
    fi

    if [ -d "$dir_local" ]; then
        if [ "$YES" = true ]; then
            cp -r "$dir_to_save/." "$dir_local/"
            echo "[✨] Saved $dir_to_save to $dir_local"
        else
            echo "[❓] Local directory $dir_local exists. Overwrite? (y/N)"
            read -r answer
            if [ "$answer" = "y" ]; then
                cp -r "$dir_to_save/." "$dir_local/"
                echo "[✨] Saved $dir_to_save to $dir_local"
            fi
        fi
    else
        mkdir -p "$dir_local"
        cp -r "$dir_to_save/." "$dir_local/"
        echo "[✨] Created and saved to $dir_local"
    fi
done

# Process files
for i in "${!FILES_TO_SAVE_ARR[@]}"; do
    file_to_save=$(echo "${FILES_TO_SAVE_ARR[$i]}" | xargs)
    file_local=$(echo "${FILES_LOCAL_ARR[$i]}" | xargs)
    
    if [ ! -f "$file_to_save" ]; then
        echo "[❌] Source file $file_to_save doesn't exist"
        continue
    fi

    if [ -f "$file_local" ]; then
        if [ "$YES" = true ]; then
            cp "$file_to_save" "$file_local"
            echo "[✨] Saved $file_to_save to $file_local"
        else
            echo "[❓] Local file $file_local exists. Overwrite? (y/N)"
            read -r answer
            if [ "$answer" = "y" ]; then
                cp "$file_to_save" "$file_local"
                echo "[✨] Saved $file_to_save to $file_local"
            fi
        fi
    else
        mkdir -p "$(dirname "$file_local")"
        cp "$file_to_save" "$file_local"
        echo "[✨] Saved $file_to_save to $file_local"
    fi
done