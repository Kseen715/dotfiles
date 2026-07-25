# modules/sddm.sh — SDDM display manager + the rice's "glass" QML theme. ONE copy,
# POSIX (was .../modules/sddm.sh). Theme + conf live in system paths (as_root);
# the service is enabled through enable_service (§8). DE/display module: installs
# and lays down files in a container, but only a real display exercises it (§9).
run_step "Installing SDDM" pkg_install sddm qt6-5compat qt6-declarative qt6-svg

if [ -n "$OSR_RICE_DIR" ]; then
    _sd="$OSR_RICE_DIR/config/sddm"
    as_root mkdir -p /etc/sddm.conf.d
    [ -f "$_sd/hyprland.main.conf" ] && as_root cp -f "$_sd/hyprland.main.conf" /etc/sddm.conf.d/sddm.conf
    [ -f "$_sd/theme.conf.user" ]    && as_root cp -f "$_sd/theme.conf.user" /etc/sddm.conf.d/theme.conf.user
    if [ -d "$_sd/glass-theme" ]; then
        as_root mkdir -p /usr/share/sddm/themes
        as_root cp -rf "$_sd/glass-theme" /usr/share/sddm/themes/
    fi
fi

enable_service sddm
