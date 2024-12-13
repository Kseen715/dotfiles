#!/usr/bin/env bash

# Run all 'install.sh' scripts, cd'ing into each directory first
for dir in */; do
    echo "[✨] Entering directory: $dir"
    if ! cd "$dir"; then
        echo "[💢] Error: Failed to enter directory $dir"
        continue
    fi

    if [ -f install.sh ]; then
        echo "[✨] Running install.sh in $dir"
        ./install.sh "$@"
    else
        echo "[🔍] No install.sh found in $dir"
    fi

    if ! cd ..; then
        echo "[💢] Error: Failed to return from directory $dir"
        exit 1
    fi
done