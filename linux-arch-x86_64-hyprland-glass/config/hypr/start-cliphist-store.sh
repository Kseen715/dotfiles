#!/bin/bash
# if cliphist is installed run it as a service.
if command -v cliphist >/dev/null 2>&1; then
    nohup wl-paste --type text --watch cliphist store &
    nohup wl-paste --type image --watch cliphist store &
fi