# test/ref/input_sh_ref.sh — the sh implementation of modules/input.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/input.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11
# modules/input.sh — pointer, keyboard and remapping (i3-sugg §5). The X server
# and xkb data come from modules/xorg.sh; this is the layer on top of them:
# gestures, dual-role keys, numlock, per-window layout.
#
#   libinput-gestures  three/four-finger swipes -> i3 workspace switching
#   xcape              tap Ctrl for Escape, hold it for Ctrl (X11-level)
#   keyd               the same idea at the kernel level, so it also works in a
#                      TTY, in games that grab the keyboard, and under Wayland
#   kbdd               remembers the keyboard layout per window
#   numlockx           numlock on at session start (X has no BIOS state)
#
# Deliberately NOT installed: `interception-tools` and `kmonad` are the other two
# kernel-level remappers, and running two of them at once fights over the same
# evdev grabs. Pick one — `osr module input` gives you keyd; swap it here if you
# prefer another. `fusuma` is Ruby-gem-only and is not packaged on Void.

run_step "Installing input tools" pkg_install \
    xf86-input-libinput xkeyboard-config setxkbmap xorg-xmodmap \
    libinput-gestures xcape keyd kbdd numlockx

# libinput-gestures needs the user in the `input` group to read /dev/input.
if command -v libinput-gestures >/dev/null 2>&1; then
    if id -nG "$OSR_USER" 2>/dev/null | grep -qw input; then
        info "$OSR_USER already in the input group - skipping"
    else
        info "adding $OSR_USER to the input group (libinput-gestures needs /dev/input)"
        as_root usermod -aG input "$OSR_USER" || warn "could not add $OSR_USER to input"
    fi
fi

if [ -f "$OSR_DOTFILES/input/libinput-gestures.conf" ]; then
    install_layer "$OSR_DOTFILES/input/libinput-gestures.conf" \
        "$OSR_HOME/.config/libinput-gestures.conf"
fi

# keyd is a system daemon with a root-owned config; seeded once, then yours.
if [ ! -f /etc/keyd/default.conf ] && [ -f "$OSR_DOTFILES/input/keyd-default.conf" ]; then
    info "seeding /etc/keyd/default.conf"
    as_root mkdir -p /etc/keyd
    as_root cp -f "$OSR_DOTFILES/input/keyd-default.conf" /etc/keyd/default.conf
fi
enable_service keyd || warn "could not enable keyd (needs a real init)"
