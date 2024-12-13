#!/usr/bin/env bash

# Run all 'install.sh' scripts, cd'ing into each directory first
for dir in */; do
    cd $dir
    if [ -f install.sh ]; then
        ./install.sh
    fi
    cd ..
done