# session: wayland
# modules/wofi.sh — wofi application launcher + theme-owned config. ONE copy,
# POSIX. Wayland-only by construction (wofi is a layer-shell client); the X11
# half of the same job is modules/rofi.sh.
#
# Two homes, one module:
#   - Hyprland rices bind the launcher in the theme's hyprland.conf ($menu).
#   - A GNOME/Wayland session (Ubuntu resolute) has no compositor config to
#     write into, so the Super+R (Win+R) shortcut is registered through
#     gsettings here — the same route modules/cliphist.sh takes for Super+V.
#
# The package is native on every supported archive (Ubuntu universe carries
# 1.5.1 on resolute), so pkgmap needs no row: `wofi` resolves as-is on apt,
# pacman, dnf, xbps and apk.
run_step "Installing wofi" pkg_install wofi

# ---- helper functions (must be defined before they are called) --------------

# _gnome_free_super_r — free Super+R from any GNOME Shell / mutter keybinding
# holding it, so our custom shortcut can take it. Stock GNOME leaves Super+R
# unbound, but a Shell keybinding silently WINS over a custom one, so a taken
# key would mean a dead Win+R with nothing to see. Derived from what gsettings
# reports rather than a hand-kept key list: a key that gains the binding in a
# future GNOME is freed the day it does.
_gnome_free_super_r() {
    _freed=0
    for _schema in org.gnome.shell.keybindings org.gnome.desktop.wm.keybindings; do
        # list-recursively prints "<schema> <key> <value>"; match the binding
        # whole (quoted on both sides) so <Shift><Super>r and friends survive.
        _keys=$(as_user gsettings list-recursively "$_schema" 2>/dev/null \
            | grep -i "'<super>r'" | cut -d' ' -f2)
        for _key in $_keys; do
            as_user gsettings set "$_schema" "$_key" "[]" 2>/dev/null || true
            info "  freed Super+R from $_schema.$_key"
            _freed=1
        done
    done
    [ "$_freed" -eq 0 ] && info "  Super+R was not bound to a known GNOME Shell key -- nothing to unbind"
    return 0
}

# _gnome_wofi_keybind — register Super+R -> wofi drun via gsettings.
# Idempotent: skips if the shortcut already exists under a known path.
#
# The command is a toggle, not a bare launch: wofi has no single-instance lock,
# so a second Win+R on an open launcher would stack a second copy over the
# first. `pkill wofi || wofi --show drun` closes the open one instead, and the
# explicit `sh -c` matters because gnome-settings-daemon splits the command
# with shell-style ARGV parsing and never runs a shell of its own.
_gnome_wofi_keybind() {
    _schema="org.gnome.settings-daemon.plugins.media-keys"
    _schema_child="$_schema.custom-keybinding"
    _base="/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings"
    _path="$_base/wofi/"

    # Already registered?
    _existing=$(as_user gsettings get "$_schema" custom-keybindings 2>/dev/null || true)
    if echo "$_existing" | grep -q "$_path"; then
        info "  wofi Super+R shortcut already registered"
        return 0
    fi

    as_user gsettings set "$_schema_child:$_path" name "Application Launcher"
    as_user gsettings set "$_schema_child:$_path" binding "<Super>r"
    as_user gsettings set "$_schema_child:$_path" command \
        "sh -c 'pkill wofi || wofi --show drun'"

    # Append to the parent list. "Already has an entry" is decided by looking for
    # a path separator rather than by matching the empty forms: gsettings spells
    # empty at least three ways depending on version and on whether the key was
    # ever written ("@as []", "[]", "''"), and appending to one of those produces
    # a list whose first element is garbage. Every real element is a dconf path,
    # so a "/" in the value is the one reliable sign there is something to keep.
    case "$_existing" in
        */*) as_user gsettings set "$_schema" custom-keybindings "${_existing%]}, '$_path']" ;;
        *)   as_user gsettings set "$_schema" custom-keybindings "['$_path']" ;;
    esac

    info "  wofi Super+R shortcut registered at $_path"
}

# ---- GNOME detection and setup ---------------------------------------------
# Same two-variable probe as modules/cliphist.sh: Ubuntu sets
# XDG_CURRENT_DESKTOP=ubuntu:GNOME, upstream GNOME sets plain GNOME.
_is_gnome=0
case "${XDG_CURRENT_DESKTOP:-}" in *GNOME*|*gnome*) _is_gnome=1 ;; esac
case "${XDG_SESSION_DESKTOP:-}" in *gnome*|*GNOME*) _is_gnome=1 ;; esac

if [ "$_is_gnome" -eq 1 ]; then
    run_step "wofi unbind Super+R from GNOME Shell" _gnome_free_super_r
    run_step "wofi Super+R shortcut" _gnome_wofi_keybind
fi

# ---- wofi config (theme-owned) ----------------------------------------------
if [ -n "${OSR_THEME_DIR:-}" ]; then apply_config wofi; fi
