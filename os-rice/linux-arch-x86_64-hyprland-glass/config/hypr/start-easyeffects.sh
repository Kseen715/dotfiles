#!/bin/bash
# Check if easyeffects is available before trying to run it
if command -v easyeffects >/dev/null 2>&1; then
    nohup easyeffects --gapplication-service &
fi