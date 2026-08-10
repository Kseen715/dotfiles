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

# ---- GNOME: autostart cliphist store daemon --------------------------------
_desktop_autostart="$OSR_HOME/.config/autostart"

# ---- helper functions (must be defined before they are called) --------------

# _gnome_unbind_super_v — free Super+V from GNOME Shell's calendar/notification
# popover so our custom shortcut can take it.
_gnome_unbind_super_v() {
    _freed=0
    for _key in \
        "org.gnome.shell.keybindings toggle-message-tray" \
        "org.gnome.shell.keybindings toggle-calendar" \
        "org.gnome.shell.keybindings show-notification-list" \
        "org.gnome.desktop.wm.keybindings switch-input-source"; do
        _schema="${_key% *}"
        _prop="${_key##* }"
        _cur=$(as_user gsettings get "$_schema" "$_prop" 2>/dev/null || true)
        if echo "$_cur" | grep -qi "super.*v\|<Super>v"; then
            as_user gsettings set "$_schema" "$_prop" "[]" 2>/dev/null || true
            info "  freed Super+V from $_schema.$_prop (was $_cur)"
            _freed=1
        fi
    done
    [ "$_freed" -eq 0 ] && info "  Super+V was not bound to a known GNOME Shell key -- nothing to unbind"
}

# _gnome_cliphist_keybind — register Super+V → wofi clip history via gsettings.
# Idempotent: skips if the shortcut already exists under a known path.
_gnome_cliphist_keybind() {
    _schema="org.gnome.settings-daemon.plugins.media-keys"
    _schema_child="$_schema.custom-keybinding"
    _base="/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings"
    _path="$_base/cliphist/"

    # Already registered?
    _existing=$(as_user gsettings get "$_schema" custom-keybindings 2>/dev/null || true)
    if echo "$_existing" | grep -q "$_path"; then
        info "  cliphist Super+V shortcut already registered"
        return 0
    fi

    as_user gsettings set "$_schema_child:$_path" name "Clipboard History"
    as_user gsettings set "$_schema_child:$_path" binding "<Super>v"
    as_user gsettings set "$_schema_child:$_path" command \
        "cliphist-wofi-img | wl-copy"

    # Append to the parent list.
    if [ "$_existing" = "@as []" ] || [ -z "$_existing" ]; then
        as_user gsettings set "$_schema" custom-keybindings "['$_path']"
    else
        _list="${_existing%]}"
        as_user gsettings set "$_schema" custom-keybindings "$_list, '$_path']"
    fi

    info "  cliphist Super+V shortcut registered at $_path"
}

# ---- GNOME detection and setup ---------------------------------------------

_is_gnome=0
case "${XDG_CURRENT_DESKTOP:-}" in *GNOME*|*gnome*) _is_gnome=1 ;; esac
case "${XDG_SESSION_DESKTOP:-}" in *gnome*|*GNOME*) _is_gnome=1 ;; esac

if [ "$_is_gnome" -eq 1 ]; then
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

    # ---- GNOME: unbind Super+V from the calendar toggle first ---------------
    run_step "cliphist unbind Super+V from GNOME Shell" _gnome_unbind_super_v

    # ---- GNOME: Super+V keybinding to open wofi clip history ----------------
    run_step "cliphist Super+V shortcut" _gnome_cliphist_keybind
fi

# ---- wofi config (theme-aware) ---------------------------------------------
if [ -n "${OSR_THEME_DIR:-}" ]; then apply_config wofi; fi
