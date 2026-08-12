# session: wayland
# modules/wofi.sh — wofi application launcher + theme-owned config. ONE copy,
# POSIX. Wayland-only by construction (wofi is a layer-shell client); the X11
# half of the same job is modules/rofi.sh.
#
# Two homes, one module:
#   - Hyprland rices bind the launcher in the theme's hyprland.conf ($menu).
#   - A GNOME/Wayland session (Ubuntu resolute) has no compositor config to
#     write into, so the Super+R (Win+R) shortcut is registered through
#     gsettings via lib/gnome.sh — the same route modules/cliphist.sh takes for
#     Super+V.
#
# The package is native on every supported archive (Ubuntu universe carries
# 1.5.1 on resolute), so pkgmap needs no row: `wofi` resolves as-is on apt,
# pacman, dnf, xbps and apk.
run_step "Installing wofi" pkg_install wofi

# ---- GNOME: Super+R -> wofi ------------------------------------------------
# Helpers live in lib/gnome.sh (shared with cliphist's Super+V). The command is
# a toggle, not a bare launch: wofi has no single-instance lock, so a second
# Win+R on an open launcher would stack a second copy over the first.
# `pkill wofi || wofi --show drun` closes the open one instead.
if gnome_is_session; then
    run_step "wofi unbind Super+R from GNOME Shell" gnome_free_binding "<Super>r"
    run_step "wofi Super+R shortcut" gnome_keybind \
        wofi "Application Launcher" "<Super>r" "sh -c 'pkill wofi || wofi --show drun'"
fi

# ---- wofi config (theme-owned) ----------------------------------------------
install_theme_layer wofi config    "$OSR_HOME/.config/wofi/config"    || :
install_theme_layer wofi style.css "$OSR_HOME/.config/wofi/style.css" || :
