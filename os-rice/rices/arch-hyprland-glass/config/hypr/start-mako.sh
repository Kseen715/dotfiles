#!/bin/bash
# Check if mako is available before trying to run it
if command -v mako >/dev/null 2>&1; then
    nohup mako &
fi