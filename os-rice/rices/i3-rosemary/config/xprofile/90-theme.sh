# ~/.config/xprofile.d/90-theme.sh — rice-owned toolkit theme env
# (void-i3-rosemuted). Swapped on rice switch (§6); the session env in
# 10-session.sh and your 00-env.sh / 99-local.sh are untouched by a switch.
#
# These have to be env vars, not config files: GTK_THEME is the only thing some
# GTK4/libadwaita apps honour, and XCURSOR_* is what X clients read before any
# settings daemon is up.

export GTK_THEME=Adwaita:dark
export GTK_APPLICATION_PREFER_DARK_THEME=1
export QT_QPA_PLATFORMTHEME=qt6ct
export XCURSOR_THEME=Vanilla-DMZ
export XCURSOR_SIZE=24
export OSR_RICE_THEME=rosemuted
