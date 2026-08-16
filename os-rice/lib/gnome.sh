# lib/gnome.sh — GNOME session detection and custom-keybinding plumbing, one
# copy for every module that wants a Win-key shortcut in a GNOME session
# (wofi's Super+R, cliphist's Super+V, and whatever comes next).
#
# A GNOME session has no compositor config file to write a keybind into the way
# a Hyprland or i3 rice does; the shortcut lives in dconf, under
# org.gnome.settings-daemon.plugins.media-keys, and is registered through
# gsettings. Two steps, always in this order:
#
#   1. gnome_free_binding "<Super>r" — a GNOME Shell / mutter keybinding on the
#      same chord silently WINS over a custom one, so a chord left bound
#      upstream means a dead shortcut with nothing to see.
#   2. gnome_keybind wofi "Application Launcher" "<Super>r" "sh -c '...'"
#
# Every function here is idempotent and safe to re-run.

# gnome_is_session — 0 when this is a GNOME session, 1 otherwise. Two variables
# because Ubuntu sets XDG_CURRENT_DESKTOP=ubuntu:GNOME while upstream GNOME sets
# plain GNOME, and a display manager may only set XDG_SESSION_DESKTOP.
gnome_is_session() {
    case "${XDG_CURRENT_DESKTOP:-}" in *GNOME*|*gnome*) return 0 ;; esac
    case "${XDG_SESSION_DESKTOP:-}" in *GNOME*|*gnome*) return 0 ;; esac
    return 1
}

# gnome_free_binding <binding> — unbind every GNOME Shell / mutter key holding
# <binding>, so a custom shortcut can take the chord. Derived from what
# gsettings reports rather than a hand-kept key list: a key that gains the
# binding in a future GNOME is freed the day it does.
#
# The match is the binding quoted on both sides, so freeing "<Super>r" leaves
# "<Shift><Super>r" — a different chord living in the same list — alone.
gnome_free_binding() {
    _gfb_binding="$1"
    _gfb_freed=0
    for _gfb_schema in \
        org.gnome.shell.keybindings \
        org.gnome.desktop.wm.keybindings \
        org.gnome.mutter.keybindings \
        org.gnome.mutter.wayland.keybindings; do
        # list-recursively prints "<schema> <key> <value>" per key.
        _gfb_keys=$(as_user gsettings list-recursively "$_gfb_schema" 2>/dev/null \
            | grep -iF "'$_gfb_binding'" | cut -d' ' -f2)
        for _gfb_key in $_gfb_keys; do
            as_user gsettings set "$_gfb_schema" "$_gfb_key" "[]" 2>/dev/null || true
            info "  freed $_gfb_binding from $_gfb_schema.$_gfb_key"
            _gfb_freed=1
        done
    done
    [ "$_gfb_freed" -eq 0 ] && \
        info "  $_gfb_binding was not bound to a known GNOME Shell key -- nothing to unbind"
    return 0
}

# gnome_keybind <id> <name> <binding> <command> — register a custom shortcut at
# .../custom-keybindings/<id>/. Idempotent: a path already in the parent list is
# left as it is.
#
# gnome-settings-daemon splits <command> with shell-style ARGV parsing but never
# runs a shell of its own, so anything with a pipe or an `||` in it has to be
# spelled as an explicit `sh -c '...'` by the caller.
gnome_keybind() {
    _gk_id="$1"; _gk_name="$2"; _gk_binding="$3"; _gk_command="$4"
    _gk_schema="org.gnome.settings-daemon.plugins.media-keys"
    _gk_child="$_gk_schema.custom-keybinding"
    _gk_path="/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/$_gk_id/"

    _gk_existing=$(as_user gsettings get "$_gk_schema" custom-keybindings 2>/dev/null || true)
    if echo "$_gk_existing" | grep -q "$_gk_path"; then
        info "  $_gk_id $_gk_binding shortcut already registered"
        return 0
    fi

    as_user gsettings set "$_gk_child:$_gk_path" name "$_gk_name"
    as_user gsettings set "$_gk_child:$_gk_path" binding "$_gk_binding"
    as_user gsettings set "$_gk_child:$_gk_path" command "$_gk_command"

    # Append to the parent list. "Already has an entry" is decided by looking for
    # a path separator rather than by matching the empty forms: gsettings spells
    # empty at least three ways depending on version and on whether the key was
    # ever written ("@as []", "[]", "''"), and appending to one of those produces
    # a list whose first element is garbage. Every real element is a dconf path,
    # so a "/" in the value is the one reliable sign there is something to keep.
    case "$_gk_existing" in
        */*) as_user gsettings set "$_gk_schema" custom-keybindings "${_gk_existing%]}, '$_gk_path']" ;;
        *)   as_user gsettings set "$_gk_schema" custom-keybindings "['$_gk_path']" ;;
    esac

    info "  $_gk_id $_gk_binding shortcut registered at $_gk_path"
}
