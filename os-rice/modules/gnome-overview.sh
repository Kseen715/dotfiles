# session: x11+wayland
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/gnome-overview.sh — free the bare Super key in a GNOME session.
#
# Tapping Super alone opens the Activities overview, and that overview is the
# slowest thing in the session: GNOME Shell animates every window into a
# thumbnail grid and starts the app search provider chain before the keypress
# feels answered. It also swallows the tap for every other use of the key.
#
# org.gnome.mutter overlay-key holds the keysym mutter watches for; the empty
# string means "watch for nothing". Chords keep working - <Super>r and friends
# are separate bindings, so a launcher bound there is unaffected. The overview
# itself is not removed, only the tap-to-open: <Super>s and the Activities
# corner still reach it.
#
# No package: mutter is the GNOME session. Inert outside GNOME.
if gnome_is_session; then
    run_step "Disabling the Super-tap overview" \
        as_user gsettings set org.gnome.mutter overlay-key ''
fi
