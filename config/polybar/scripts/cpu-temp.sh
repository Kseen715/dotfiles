#!/bin/sh
# polybar/scripts/cpu-temp.sh — hottest CPU core, in whole degrees C.
#
# A script and not polybar's internal/temperature, for one reason: that module
# wants either `thermal-zone = N` or a literal `hwmon-path`, and neither is
# stable. This laptop has NO /sys/class/thermal/thermal_zone* at all, so the
# internal module disabled itself outright ("The file
# '/sys/class/thermal/thermal_zone0/temp' does not exist") and the bar just
# lost its temperature. Meanwhile hwmon numbering is assigned in probe order,
# so hwmon3 today can be hwmon1 after a reboot or a module load.
#
# Resolving coretemp by NAME is the only form that survives both. k10temp and
# zenpower cover AMD; the thermal zones are the last resort for a machine whose
# CPU exposes no hwmon at all.
set -u

for _n in coretemp k10temp zenpower; do
    for _h in /sys/class/hwmon/hwmon*; do
        [ -r "$_h/name" ] || continue
        [ "$(cat "$_h/name" 2>/dev/null)" = "$_n" ] || continue
        # Hottest of whatever it exposes: package where there is one, else the
        # busiest core. Reporting core 0 alone under-reports a loaded CPU.
        _max=""
        for _f in "$_h"/temp*_input; do
            [ -r "$_f" ] || continue
            _v=$(cat "$_f" 2>/dev/null) || continue
            case "$_v" in ''|*[!0-9]*) continue ;; esac
            if [ -z "$_max" ] || [ "$_v" -gt "$_max" ]; then _max=$_v; fi
        done
        if [ -n "$_max" ]; then
            printf '%d\n' "$((_max / 1000))"
            exit 0
        fi
    done
done

for _z in /sys/class/thermal/thermal_zone*/temp; do
    [ -r "$_z" ] || continue
    _v=$(cat "$_z" 2>/dev/null) || continue
    case "$_v" in ''|*[!0-9]*) continue ;; esac
    printf '%d\n' "$((_v / 1000))"
    exit 0
done

# Nothing readable. Print nothing: polybar renders an empty module rather than
# a zero, which is honest - this machine has no temperature to show.
exit 0
