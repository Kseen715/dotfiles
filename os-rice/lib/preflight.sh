# lib/preflight.sh — rice preconditions, checked before any mutation (§10 Tier 1)
#
# A rice declares `require: <predicate>` lines in its manifest; the runner
# collects them and calls osr_preflight BEFORE step 1, exiting non-zero with
# nothing written when a predicate is unmet. Predicates are cheap and data-only
# (no installs) — a functional capability probe (Vulkan init) is an early module
# instead (§10 Tier 2), not a require: predicate.
#
# Predicates (DESIGN §10 table):
#   arch:<m>       uname -m / OSR_ARCH matches
#   init:<i>       OSR_INIT matches (systemd/openrc/runit/sysvinit)
#   distro:<d>     OSR_DISTRO matches
#   release:<c>    OSR_CODENAME or OSR_VERSION_ID matches
#   cmd:<bin>      command -v <bin> succeeds
#   gpu:present    a GPU exists (/dev/dri/renderD* or OSR_GPU_COUNT > 0)

# osr_preflight_check <predicate> — true if the host satisfies it. Detection
# vars (OSR_*) are set by osr_detect; run preflight after it.
osr_preflight_check() {
    _pf_pred=$1
    _pf_val=${_pf_pred#*:}                # right of the first colon
    case "$_pf_pred" in
        arch:*)
            [ "$_pf_val" = "$OSR_ARCH" ] || [ "$_pf_val" = "$OSR_ARCH_DEB" ] ;;
        init:*)
            [ "$_pf_val" = "$OSR_INIT" ] ;;
        distro:*)
            [ "$_pf_val" = "$OSR_DISTRO" ] ;;
        release:*)
            [ "$_pf_val" = "$OSR_CODENAME" ] || [ "$_pf_val" = "$OSR_VERSION_ID" ] ;;
        cmd:*)
            command -v "$_pf_val" >/dev/null 2>&1 ;;
        gpu:present)
            [ "${OSR_GPU_COUNT:-0}" -gt 0 ] && return 0
            for _pf_d in /dev/dri/renderD*; do [ -e "$_pf_d" ] && return 0; done
            return 1 ;;
        *)
            warn "unknown require predicate '$_pf_pred' - ignoring"; return 0 ;;
    esac
}

# osr_preflight <predicate>... — check each; on the first unmet one, error out
# (which exits non-zero) before any module runs. A "detected" hint aids debugging.
osr_preflight() {
    for _pf_p in "$@"; do
        [ -n "$_pf_p" ] || continue
        if osr_preflight_check "$_pf_p"; then
            info "require $_pf_p - ok"
        else
            error "rice needs '$_pf_p' (detected: arch=$OSR_ARCH init=$OSR_INIT distro=$OSR_DISTRO gpu=${OSR_GPU_COUNT:-0})"
        fi
    done
}
