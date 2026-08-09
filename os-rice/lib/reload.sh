# lib/reload.sh — make a swapped layer visible without a logout (POSIX sh)
#
# §6a. Installing the files is half a theme switch; the running programs still
# hold the old ones. Every reloader here is best-effort and independent:
#
#   - probe first, act second. `command -v` plus a liveness check, because
#     `i3-msg` on a Wayland box is not an error to report, it is a program that
#     is not running.
#   - never fatal. A reload that fails leaves the file on disk correct and the
#     app repainting at its next start; aborting the switch would leave the
#     desktop half-painted instead, which is strictly worse (§9).
#   - never restart what would lose state. dunst/mako are reloaded, not killed;
#     a terminal is left alone entirely (its palette is re-read per window, and
#     restarting it would close a shell someone is typing in).
#
# Deliberately NOT here: logind/DM restarts, `hyprctl dispatch exit`, killing
# the compositor. A theme switch may never end a session.

# _osr_running <name> — true when a process by that name is alive for this user.
# pgrep is not on every base install (busybox has it, some minimal images do not)
# so fall back to a `ps` scan rather than skipping the reload entirely.
_osr_running() {
    if command -v pgrep >/dev/null 2>&1; then
        pgrep -u "$(id -u "${OSR_USER:-$(id -un)}" 2>/dev/null || id -u)" -x "$1" >/dev/null 2>&1
    else
        ps -e 2>/dev/null | awk -v n="$1" '$NF == n || $4 == n { found = 1 } END { exit !found }'
    fi
}

# _osr_try <label> <cmd...> — run a reloader, log it, and swallow the outcome.
_osr_try() {
    _rt_label=$1
    shift
    if "$@" >/dev/null 2>&1; then
        debug "reload: $_rt_label"
        OSR_RELOADED="${OSR_RELOADED:+$OSR_RELOADED }$_rt_label"
        return 0
    fi
    debug "reload: $_rt_label failed (ignored)"
    return 0
}

# osr_reload_x11 — X resources, the i3 stack and the X compositor.
osr_reload_x11() {
    [ -n "${DISPLAY:-}" ] || return 0

    # Xresources first: i3/rofi/xterm colors are read from the X server's
    # database, so merging must happen BEFORE the WM re-reads its config.
    if [ -f "${OSR_HOME:-$HOME}/.Xresources" ] && command -v xrdb >/dev/null 2>&1; then
        _osr_try "xrdb" xrdb -merge "${OSR_HOME:-$HOME}/.Xresources"
    fi

    if command -v i3-msg >/dev/null 2>&1 && _osr_running i3; then
        # `restart` (not `reload`) is what re-reads colors AND re-execs the bar;
        # i3 preserves the layout and every client across it.
        _osr_try "i3" i3-msg -q restart
    fi

    # polybar has no reload IPC: the launcher script the polybar module installs
    # is the supported way to bring the bars back with the new colors.ini.
    if _osr_running polybar; then
        if [ -x "${OSR_HOME:-$HOME}/.config/polybar/launch.sh" ]; then
            _osr_try "polybar" "${OSR_HOME:-$HOME}/.config/polybar/launch.sh"
        else
            _osr_try "polybar" pkill -USR1 -x polybar
        fi
    fi

    # picom: SIGUSR1 re-reads the config in every version that has ever shipped.
    #
    # `if`, not `probe && act`: this file is sourced into a `set -e` installer,
    # where a bare `false && cmd` list is a fatal exit status. A compositor that
    # is not running would then abort the run right after the layers landed -
    # the switch would look like it failed when it had already succeeded.
    if _osr_running picom; then
        _osr_try "picom" pkill -USR1 -x picom
    fi

    # xsettingsd carries the GTK2/3 theme name to running apps.
    if _osr_running xsettingsd; then
        _osr_try "xsettingsd" pkill -HUP -x xsettingsd
    fi
}

# osr_reload_wayland — the Hyprland stack.
osr_reload_wayland() {
    [ -n "${WAYLAND_DISPLAY:-}" ] || return 0

    if command -v hyprctl >/dev/null 2>&1 && _osr_running Hyprland; then
        _osr_try "hyprland" hyprctl reload
    fi

    # waybar reloads on SIGUSR2 (SIGUSR1 toggles visibility - sending that would
    # hide the bar and look exactly like a crash).
    if _osr_running waybar; then
        _osr_try "waybar" pkill -USR2 -x waybar
    fi
}

# osr_reload_notify — the notification daemons, either session.
osr_reload_notify() {
    if command -v dunstctl >/dev/null 2>&1 && _osr_running dunst; then
        _osr_try "dunst" dunstctl reload
    fi
    # mako gained `makoctl reload` in 1.7; older builds re-read on SIGUSR2.
    if command -v makoctl >/dev/null 2>&1 && _osr_running mako; then
        if makoctl reload >/dev/null 2>&1; then
            OSR_RELOADED="${OSR_RELOADED:+$OSR_RELOADED }mako"
        else
            _osr_try "mako" pkill -USR2 -x mako
        fi
    fi
}

# osr_reload_gtk — the GTK/Qt theme names for apps already running.
#
# gsettings is what GTK3/4 apps watch; xsettingsd (above) covers GTK2 and X-only
# toolkits. The theme's own gtk.css is re-read by GTK apps on a settings change,
# which is why poking the setting is enough and no app needs restarting.
osr_reload_gtk() {
    command -v gsettings >/dev/null 2>&1 || return 0
    _rg_iface="org.gnome.desktop.interface"
    _rg_theme=$(gsettings get "$_rg_iface" gtk-theme 2>/dev/null) || return 0
    [ -n "$_rg_theme" ] || return 0
    # Set it to something else and back: GTK only reacts to a CHANGE, and after a
    # theme swap the name is usually identical while the files behind it are not.
    _osr_try "gtk" sh -c "gsettings set $_rg_iface gtk-theme 'Adwaita' && gsettings set $_rg_iface gtk-theme $_rg_theme"
}

# osr_reload_all — every reloader that applies to this session.
osr_reload_all() {
    OSR_RELOADED=""
    # No display server at all (a container, an ssh session, a CI box): there is
    # nothing on screen to repaint. Returning here keeps a theme apply from
    # poking dbus/gsettings on a machine with no session behind it, which would
    # be both pointless and, for dconf, a mutation nobody asked for.
    if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
        info "no display server - layers are on disk for the next start"
        return 0
    fi
    osr_reload_x11
    osr_reload_wayland
    osr_reload_notify
    osr_reload_gtk
    if [ -n "$OSR_RELOADED" ]; then
        info "reloaded:$(printf '%s' " $OSR_RELOADED")"
    else
        info "nothing running to reload (layers are on disk for the next start)"
    fi
    return 0
}
