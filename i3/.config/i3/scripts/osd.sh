#!/bin/sh
# osd.sh — the on-screen feedback a full desktop gives you for the volume and
# brightness keys, and that a bare i3 does not (i3-sugg §6, §7.4).
#
# Two jobs, and the second is the one that matters:
#
#   1. change the level                — one command per backend
#   2. draw the result as a notification with a progress bar, replacing the
#      previous one instead of stacking a queue of them
#
# The replace id (-r) is what makes it an OSD rather than a notification spam
# storm: dunst redraws the same popup in place when the id repeats. The two ids
# are arbitrary but must be stable and distinct, so audio and backlight get their
# own popup instead of overwriting each other.
#
# Backends are probed, not assumed. pamixer is the nice one and is what the i3
# bindings used to call directly, but it reached Debian only in bookworm and
# Ubuntu only in noble (see lib/pkgmap/apt.map); pactl ships with every PipeWire
# and PulseAudio install there is, and wpctl is the native PipeWire route. Any
# one of the three is enough, which is the point — the keys work everywhere.
set -eu

OSD_AUDIO_ID=2593
OSD_LIGHT_ID=2594
STEP=5

# _notify <id> <icon> <summary> <value> — one popup, redrawn in place. The
# int:value hint is what dunst (and every other spec-compliant server) turns into
# a progress bar; without it this is a bare line of text.
_notify() {
    command -v dunstify >/dev/null 2>&1 \
        && { dunstify -a osd -u low -i "$2" -h "int:value:$4" \
                 -h "string:x-dunst-stack-tag:osd$1" -r "$1" -t 1500 "$3" >/dev/null 2>&1; return 0; }
    command -v notify-send >/dev/null 2>&1 \
        && notify-send -a osd -u low -i "$2" -h "int:value:$4" "$3" >/dev/null 2>&1 || :
}

# --- audio -------------------------------------------------------------------

_vol_get() {
    if command -v pamixer >/dev/null 2>&1; then
        pamixer --get-volume 2>/dev/null || echo 0
    elif command -v pactl >/dev/null 2>&1; then
        # "Volume: front-left: 45875 /  70% / -9.29 dB, ..." — the first percent
        # is the one being set; a stereo sink prints the same number twice.
        pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null \
            | sed -n 's/.*[^0-9]\([0-9]\{1,3\}\)%.*/\1/p' | head -n 1
    elif command -v wpctl >/dev/null 2>&1; then
        wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null \
            | awk '{printf "%d", $2 * 100}'
    else
        echo 0
    fi
}

_muted() {
    if command -v pamixer >/dev/null 2>&1; then
        [ "$(pamixer --get-mute 2>/dev/null)" = true ]
    elif command -v pactl >/dev/null 2>&1; then
        pactl get-sink-mute @DEFAULT_SINK@ 2>/dev/null | grep -q yes
    elif command -v wpctl >/dev/null 2>&1; then
        wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null | grep -q MUTED
    else
        return 1
    fi
}

# _vol <up|down|mute|micmute> — apply, then draw. Volume is capped at 100%:
# every backend here will happily go past it, and software gain above unity is
# distortion, not loudness.
_vol() {
    if command -v pamixer >/dev/null 2>&1; then
        case "$1" in
            # No --allow-boost: pamixer stops at 100% without it, which is
            # the ceiling we want anyway.
            up)      pamixer -i "$STEP" ;;
            down)    pamixer -d "$STEP" ;;
            mute)    pamixer -t ;;
            micmute) pamixer --default-source -t ;;
        esac
    elif command -v pactl >/dev/null 2>&1; then
        case "$1" in
            up)      pactl set-sink-volume @DEFAULT_SINK@ "+${STEP}%"
                     # pactl has no ceiling of its own, so clamp after the fact.
                     _cur=$(_vol_get); case "$_cur" in ''|*[!0-9]*) _cur=0 ;; esac
                     [ "$_cur" -gt 100 ] && pactl set-sink-volume @DEFAULT_SINK@ 100% || : ;;
            down)    pactl set-sink-volume @DEFAULT_SINK@ "-${STEP}%" ;;
            mute)    pactl set-sink-mute @DEFAULT_SINK@ toggle ;;
            micmute) pactl set-source-mute @DEFAULT_SOURCE@ toggle ;;
        esac
    elif command -v wpctl >/dev/null 2>&1; then
        case "$1" in
            up)      wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ "${STEP}%+" ;;
            down)    wpctl set-volume @DEFAULT_AUDIO_SINK@ "${STEP}%-" ;;
            mute)    wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle ;;
            micmute) wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle ;;
        esac
    else
        _notify "$OSD_AUDIO_ID" audio-volume-muted "no mixer installed" 0
        return 0
    fi

    if [ "$1" = micmute ]; then
        _notify "$OSD_AUDIO_ID" audio-input-microphone "microphone toggled" 0
    elif _muted; then
        _notify "$OSD_AUDIO_ID" audio-volume-muted "muted" 0
    else
        _v=$(_vol_get); case "$_v" in ''|*[!0-9]*) _v=0 ;; esac
        _notify "$OSD_AUDIO_ID" audio-volume-high "volume  ${_v}%" "$_v"
    fi
}

# --- backlight ---------------------------------------------------------------
# brightnessctl only; `light` and `xbacklight` are the alternatives and the rice
# picks one (modules/brightnessctl.sh). A desktop with no backlight device has
# nothing to set, and saying so beats a raw "No such file or directory".

_light() {
    command -v brightnessctl >/dev/null 2>&1 || {
        _notify "$OSD_LIGHT_ID" display-brightness "no backlight control installed" 0
        return 0
    }
    case "$1" in
        # 5%- on a laptop that is already at minimum is not an error worth
        # killing the keybinding over.
        up)   brightnessctl -q set "${STEP}%+" || : ;;
        down) brightnessctl -q set "${STEP}%-" || : ;;
    esac
    _p=$(brightnessctl -m 2>/dev/null | cut -d, -f4 | tr -d '%')
    case "$_p" in ''|*[!0-9]*) _p=0 ;; esac
    _notify "$OSD_LIGHT_ID" display-brightness "brightness  ${_p}%" "$_p"
}

case "${1:-}" in
    volume-up)   _vol up ;;
    volume-down) _vol down ;;
    mute)        _vol mute ;;
    mic-mute)    _vol micmute ;;
    light-up)    _light up ;;
    light-down)  _light down ;;
    *) echo "usage: osd.sh {volume-up|volume-down|mute|mic-mute|light-up|light-down}" >&2
       exit 2 ;;
esac
