#!/usr/bin/env bash

# Run all 'save.sh' scripts, cd'ing into each directory first
for dir in */; do
    cd $dir
    if [ -f save.sh ]; then
        ./save.sh
    fi
    cd ..
done
