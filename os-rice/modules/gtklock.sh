# session: wayland
# themable: yes
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/gtklock.sh — gtklock GTK screen locker + rice-owned config. ONE copy,
# POSIX (was .../modules/gtklock.sh). style.css carries a {{WALLPAPER_PATH}}
# placeholder the legacy sed-substituted at install; we resolve it to the rice's
# wallpaper. .face (lockscreen avatar) is seeded once and then left to the user.
run_step "Installing gtklock" pkg_install gtklock gtklock-userinfo-module

if [ -n "$OSR_THEME_DIR" ]; then
    _gd="$OSR_THEME_DIR/config/gtklock"
    [ -f "$_gd/config.ini" ] && install_layer "$_gd/config.ini" "$OSR_HOME/.config/gtklock/config.ini"

    # Resolve {{WALLPAPER_PATH}} -> the installed wallpaper (cosmetic bg). Shared
    # with hyprpaper/hyprland so all three paint the same file (config.sh).
    if [ -f "$_gd/style.css" ]; then
        install_wallpaper_layer "$_gd/style.css" "$OSR_HOME/.config/gtklock/style.css"
    fi
    # Seed the lockscreen avatar once (user territory afterwards).
    [ -f "$_gd/.face" ] && seed_once "$_gd/.face" "$OSR_HOME/.face"
fi
