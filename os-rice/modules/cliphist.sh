# session: wayland
# modules/cliphist.sh — cliphist clipboard history + wofi image preview helper.
# ONE copy, POSIX (was .../modules/cliphist.sh). ripgrep backs the search; the
# wofi image thumbnailer is a small upstream script fetched to /usr/local/bin.
# go is a build prerequisite for cliphist-wofi-img — installed on demand (§4:
# order is the dependency graph, but this module self-heals if go is absent).
#
# GNOME/Wayland: autostarts the cliphist store daemon via ~/.config/autostart
# and registers a Super+V (Win+V) custom shortcut to open wofi clip history.
run_step "Installing cliphist" pkg_install cliphist ripgrep wofi wl-clipboard

# ---- Hyprland: themed start script -----------------------------------------
if [ -n "${OSR_THEME_DIR:-}" ] && [ -f "$OSR_THEME_DIR/config/hypr/start-cliphist-store.sh" ]; then
    install_layer "$OSR_THEME_DIR/config/hypr/start-cliphist-store.sh" \
        "$OSR_HOME/.config/hypr/start-cliphist-store.sh"
    as_user chmod +x "$OSR_HOME/.config/hypr/start-cliphist-store.sh"
fi

# ---- cliphist-wofi-img helper ----------------------------------------------
command -v go >/dev/null 2>&1 || pkg_install go
run_step "Installing cliphist-wofi-img (go)" \
    as_user go install github.com/pdf/cliphist-wofi-img@latest

# Upstream wofi image-preview shim to /usr/local/bin (system path -> as_root).
if [ ! -x /usr/local/bin/cliphist-wofi-img ]; then
    _cw="${TMPDIR:-/tmp}/cliphist-wofi-img"
    if osr_download "https://raw.githubusercontent.com/sentriz/cliphist/refs/heads/master/contrib/cliphist-wofi-img" "$_cw"; then
        as_root install -m 0755 "$_cw" /usr/local/bin/cliphist-wofi-img
        rm -f "$_cw"
    else
        warn "failed to fetch cliphist-wofi-img shim - skipping"
    fi
fi

as_user mkdir -p "$OSR_HOME/.cache/cliphist/thumbs"

_desktop_autostart="$OSR_HOME/.config/autostart"

# ---- GNOME: autostart daemon + Super+V shortcut -----------------------------
# Keybinding helpers live in lib/gnome.sh (shared with wofi's Super+R). Super+V
# is stock GNOME's message-tray/calendar toggle, and a Shell binding wins over a
# custom one, so it has to be freed before ours is registered.
if gnome_is_session; then
    run_step "cliphist GNOME autostart" \
        as_user mkdir -p "$_desktop_autostart"

    # cliphist store daemon — runs on login, watches both text and image clipboard.
    _cliphist_desktop="$_desktop_autostart/cliphist-store.desktop"
    if [ ! -f "$_cliphist_desktop" ]; then
        as_user tee "$_cliphist_desktop" >/dev/null <<'AUTOSTART_EOF'
[Desktop Entry]
Type=Application
Name=Cliphist Store
Comment=Wayland clipboard history daemon
Exec=sh -c 'wl-paste --type text --watch cliphist store & wl-paste --type image --watch cliphist store & wait'
X-GNOME-Autostart-enabled=true
NoDisplay=true
AUTOSTART_EOF
    fi

    run_step "cliphist unbind Super+V from GNOME Shell" gnome_free_binding "<Super>v"
    run_step "cliphist Super+V shortcut" gnome_keybind \
        cliphist "Clipboard History" "<Super>v" "cliphist-wofi-img | wl-copy"
fi

# ---- wofi config (theme-aware) ---------------------------------------------
if [ -n "${OSR_THEME_DIR:-}" ]; then apply_config wofi; fi
