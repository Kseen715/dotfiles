#!/bin/bash
# if cliphist is installed run it as a service.
if command -v cliphist >/dev/null 2>&1; then
    nohup wl-paste --watch cliphist store &
fi