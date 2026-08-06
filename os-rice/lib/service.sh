# lib/service.sh — universal, idempotent service control (POSIX sh, §8 G3)
#
# Two verbs work on any init; no module ever calls systemctl directly again.
# Dispatch on OSR_INIT (from detect.sh); check current state before acting.

# service_resolve <logical> — map a logical service name to the real unit name
# for this init, via servicemap. Rows exist only for names that differ (§8).
#
# A row key may carry an optional @init qualifier and the most specific match
# wins — `<name>@<init>` before the bare `<name>` — mirroring pkgmap's facets
# (§1a). That is what lets one logical name cover units whose *name* differs per
# init (`bluetooth.service` on systemd, `/etc/sv/bluetoothd` on runit) without
# any module growing an init `case`.
service_resolve() {
    _sr_name=$1
    if [ -f "$OSR_LIB/servicemap" ]; then
        for _sr_key in "$_sr_name@${OSR_INIT:-}" "$_sr_name"; do
            _sr_line=$(grep "^[[:space:]]*$_sr_key[[:space:]]*=" "$OSR_LIB/servicemap" 2>/dev/null | head -n 1)
            if [ -n "$_sr_line" ]; then
                printf '%s' "${_sr_line#*=}" | sed 's/[[:space:]]#.*$//; s/^[[:space:]]*//; s/[[:space:]]*$//'
                return 0
            fi
        done
    fi
    printf '%s' "$_sr_name"
}

# enable_service <logical> — enable + start now, idempotent.
enable_service() {
    _es_svc=$(service_resolve "$1")
    case "$OSR_INIT" in
        systemd)
            if systemctl is-enabled "$_es_svc" >/dev/null 2>&1 \
               && systemctl is-active "$_es_svc" >/dev/null 2>&1; then
                info "$_es_svc already enabled + running - skipping"
                return 0
            fi
            as_root systemctl enable --now "$_es_svc" ;;
        openrc)
            as_root rc-update add "$_es_svc" default
            as_root rc-service "$_es_svc" start ;;
        runit)
            # ln -s succeeds even when the target is missing, so an unpackaged
            # service would silently leave a dangling link that runsvdir then
            # complains about forever. Check first and degrade to a warning.
            # OSR_SV_DIR/OSR_SERVICE_DIR override the paths for tests (same
            # pattern as OSR_DRM/OSR_DRI in detect.sh).
            _es_svdir=${OSR_SV_DIR:-/etc/sv}
            _es_rundir=${OSR_SERVICE_DIR:-/var/service}
            if [ ! -d "$_es_svdir/$_es_svc" ]; then
                warn "no $_es_svdir/$_es_svc - skipping (package ships no runit service)"
                return 0
            fi
            [ -e "$_es_rundir/$_es_svc" ] \
                || as_root ln -s "$_es_svdir/$_es_svc" "$_es_rundir/$_es_svc" ;;
        sysvinit)
            as_root update-rc.d "$_es_svc" enable
            as_root service "$_es_svc" start ;;
        *)  warn "enable_service: unknown init '$OSR_INIT' - skipping $_es_svc" ;;
    esac
}

# disable_service <logical> — stop + disable, idempotent.
disable_service() {
    _ds_svc=$(service_resolve "$1")
    case "$OSR_INIT" in
        systemd)
            systemctl is-enabled "$_ds_svc" >/dev/null 2>&1 || {
                info "$_ds_svc already disabled - skipping"; return 0; }
            as_root systemctl disable --now "$_ds_svc" ;;
        openrc)
            as_root rc-service "$_ds_svc" stop
            as_root rc-update del "$_ds_svc" default ;;
        runit)
            _ds_rundir=${OSR_SERVICE_DIR:-/var/service}
            [ -e "$_ds_rundir/$_ds_svc" ] && as_root rm -f "$_ds_rundir/$_ds_svc" ;;
        sysvinit)
            as_root service "$_ds_svc" stop
            as_root update-rc.d "$_ds_svc" disable ;;
        *)  warn "disable_service: unknown init '$OSR_INIT' - skipping $_ds_svc" ;;
    esac
}
