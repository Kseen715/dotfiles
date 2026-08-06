# session: x11
# modules/arandr.sh — display layout, the X11 replacement for nwg-displays
# (i3-sugg §2). Two halves that belong together: arandr is the GUI you drag
# monitors around in, autorandr is what remembers the result and re-applies it
# on hotplug.
#
# After arranging a layout once, save it with `autorandr --save <name>`; the i3
# config runs `autorandr --change` at startup so docking picks the profile up.
# The udev hotplug hook ships with the package.

run_step "Installing arandr + autorandr" pkg_install arandr autorandr
