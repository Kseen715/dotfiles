# modules/gtklock.sh — gtklock GTK screen locker + rice-owned config. ONE copy,
# POSIX (was .../modules/gtklock.sh). style.css carries a {{WALLPAPER_PATH}}
# placeholder the legacy sed-substituted at install; we resolve it to the rice's
# wallpaper. .face (lockscreen avatar) is seeded once and then left to the user.
run_step "Installing gtklock" pkg_install gtklock gtklock-userinfo-module

if [ -n "$OSR_RICE_DIR" ]; then
    _gd="$OSR_RICE_DIR/config/gtklock"
    [ -f "$_gd/config.ini" ] && install_layer "$_gd/config.ini" "$OSR_HOME/.config/gtklock/config.ini"

    # Resolve {{WALLPAPER_PATH}} -> the rice's first wallpaper (cosmetic bg).
    if [ -f "$_gd/style.css" ]; then
        _wp=""
        for _f in "$OSR_RICE_DIR"/wallpapers/*; do [ -f "$_f" ] && { _wp=$_f; break; }; done
        as_user mkdir -p "$OSR_HOME/.config/gtklock"
        sed "s#{{WALLPAPER_PATH}}#${_wp}#g" "$_gd/style.css" \
            | as_user tee "$OSR_HOME/.config/gtklock/style.css" >/dev/null
    fi
    # Seed the lockscreen avatar once (user territory afterwards).
    [ -f "$_gd/.face" ] && seed_once "$_gd/.face" "$OSR_HOME/.face"
fi
