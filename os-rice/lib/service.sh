# lib/service.sh — universal, idempotent service control (POSIX sh, §8 G3)
#
# Two verbs work on any init; no module ever calls systemctl directly again.
# Dispatch on OSR_INIT (from detect.sh); check current state before acting.

# service_resolve <logical> — map a logical service name to the real unit name
# for this init, via servicemap. Rows exist only for names that differ (§8).
service_resolve() {
    _sr_name=$1
    if [ -f "$OSR_LIB/servicemap" ]; then
        _sr_line=$(grep "^[[:space:]]*$_sr_name[[:space:]]*=" "$OSR_LIB/servicemap" 2>/dev/null | head -n 1)
        if [ -n "$_sr_line" ]; then
            printf '%s' "${_sr_line#*=}" | sed 's/^[[:space:]]*//; s/[[:space:]]*$//'
            return 0
        fi
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
                info "$_es_svc already enabled + running — skipping"
                return 0
            fi
            as_root systemctl enable --now "$_es_svc" ;;
        openrc)
            as_root rc-update add "$_es_svc" default
            as_root rc-service "$_es_svc" start ;;
        runit)
            [ -e "/var/service/$_es_svc" ] \
                || as_root ln -s "/etc/sv/$_es_svc" "/var/service/$_es_svc" ;;
        sysvinit)
            as_root update-rc.d "$_es_svc" enable
            as_root service "$_es_svc" start ;;
        *)  warn "enable_service: unknown init '$OSR_INIT' — skipping $_es_svc" ;;
    esac
}

# disable_service <logical> — stop + disable, idempotent.
disable_service() {
    _ds_svc=$(service_resolve "$1")
    case "$OSR_INIT" in
        systemd)
            systemctl is-enabled "$_ds_svc" >/dev/null 2>&1 || {
                info "$_ds_svc already disabled — skipping"; return 0; }
            as_root systemctl disable --now "$_ds_svc" ;;
        openrc)
            as_root rc-service "$_ds_svc" stop
            as_root rc-update del "$_ds_svc" default ;;
        runit)
            [ -e "/var/service/$_ds_svc" ] && as_root rm -f "/var/service/$_ds_svc" ;;
        sysvinit)
            as_root service "$_ds_svc" stop
            as_root update-rc.d "$_ds_svc" disable ;;
        *)  warn "disable_service: unknown init '$OSR_INIT' — skipping $_ds_svc" ;;
    esac
}
