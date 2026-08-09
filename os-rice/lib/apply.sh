# lib/apply.sh — theme-only apply: the hotkey path (POSIX sh)
#
# §6a. `osr switch <rice>` re-runs a whole manifest: package managers, source
# builds, services, the network. That is right for moving between rices and
# useless for changing how the desktop looks - it costs minutes and a sudo
# ticket, so nobody binds it to a key.
#
# A theme is only files. `osr theme <name>` therefore runs the SAME modules -
# never a second copy of the mapping from a theme's config/ to the installed
# path, which is the thing that would rot - with every mutating verb neutralized
# first. What survives is the file copying, which is what a theme is.
#
# The neutralized set is derived, not listed: every function defined in the libs
# that install, download, build or start things is replaced by a no-op, minus a
# small read-only allowlist. A new provider added to lib/build.sh is therefore
# inert here the day it is written, with no edit to this file (test/unit/
# theme_apply.sh asserts exactly that).

# Libs whose functions all mutate the system: neutralized wholesale.
OSR_APPLY_MUTATING_LIBS="pkg build net git service fonts"

# Read-only exceptions - queries with no side effect that modules branch on.
# Removing a query from this list is safe (the branch just takes its unknown
# path); adding a mutating verb to it is not.
OSR_APPLY_QUERY_OK="pkg_installed _pkgmap_one _spec_method _native_installed _native_held service_resolve osr_downloader _chafa_version _chafa_ok _osr_pkgconfig_path _yb_deb_url"

# _osr_apply_verbs — echo every function name defined by the mutating libs.
# Read out of the sources rather than a hand-kept list so it cannot drift.
_osr_apply_verbs() {
    for _av_l in $OSR_APPLY_MUTATING_LIBS; do
        [ -f "$OSR_LIB/$_av_l.sh" ] || continue
        sed -n 's/^\([a-z_][a-z0-9_]*\)().*/\1/p' "$OSR_LIB/$_av_l.sh"
    done
}

# osr_apply_stub_mutators — replace every mutating verb with a no-op that logs.
# Called once, after the libs are sourced and before the first module runs.
osr_apply_stub_mutators() {
    for _sm_f in $(_osr_apply_verbs); do
        case " $OSR_APPLY_QUERY_OK " in
            *" $_sm_f "*) continue ;;
        esac
        eval "$_sm_f() { debug \"theme-apply: skipped $_sm_f \$*\"; return 0; }"
    done

    # as_root is not from those libs and is not a no-op: a few theme layers are
    # genuinely root-owned (the LightDM greeter's conf lives in /etc). Escalate
    # only with a ticket already in hand - a hotkey has no terminal to type a
    # password into, and a blocked sudo prompt would hang the switch forever.
    if [ "$(id -u)" -eq 0 ] || sudo -n true 2>/dev/null; then
        OSR_APPLY_CAN_ROOT=1
    else
        OSR_APPLY_CAN_ROOT=""
    fi
    if [ -z "$OSR_APPLY_CAN_ROOT" ]; then
        as_root() {
            debug "theme-apply: no sudo ticket - skipped root step: $*"
            return 0
        }
    fi
}

# osr_apply_theme <name> — the whole theme-only apply, for a caller that has
# already resolved the target user (OSR_USER/OSR_HOME).
#
# It lives here rather than inline in install.sh so it can be driven against a
# throwaway HOME. That is not a testing nicety: install.sh resolves OSR_HOME
# from passwd, so a test that sets OSR_HOME and then runs install.sh writes to
# the REAL home of whoever runs the suite. A unit test must be able to exercise
# this path without that being possible.
osr_apply_theme() {
    osr_resolve_theme "${1:-}"

    # Neutralize every install/build/download/service verb, then run the same
    # modules a rice install runs. What is left of them is the file copying.
    osr_apply_stub_mutators

    _at_rice=$(osr_state_get rice)
    OSR_RICE=${_at_rice:-}
    [ -n "$OSR_RICE" ] && OSR_RICE_DIR="$OSR_ROOT/rices/$OSR_RICE"
    export OSR_RICE OSR_RICE_DIR
    _at_mods=$(osr_theme_modules "$_at_rice")

    OSR_STEP_TOTAL=0
    for _at_m in $_at_mods; do OSR_STEP_TOTAL=$((OSR_STEP_TOTAL + 1)); done
    OSR_STEP_N=0
    export OSR_STEP_TOTAL OSR_STEP_N
    info "applying theme '$OSR_THEME'${_at_rice:+ over rice '$_at_rice'} ($OSR_STEP_TOTAL layers)"

    for _at_m in $_at_mods; do
        OSR_STEP_N=$((OSR_STEP_N + 1))
        debug "$(step_prefix)layer: $_at_m"
        # A single broken module must not abort a theme switch and leave the
        # desktop half-painted - warn and carry on to the rest (§9). Sourced in
        # a subshell so a module's `error` (which exits) kills only its layer,
        # and its chatter goes to the run log rather than the user's terminal:
        # the interesting output of a theme apply is the one success line.
        # shellcheck disable=SC1090
        ( . "$OSR_ROOT/modules/$_at_m.sh" ) >>"$OSR_LOG" 2>&1 \
            || warn "layer '$_at_m' failed - skipped (see $OSR_LOG)"
    done

    osr_apply_theme_configs
    apply_wallpaper
    osr_state_set theme "$OSR_THEME"
    osr_state_set applied "$(date +%s 2>/dev/null || echo 0)"
}

# osr_theme_modules — echo the modules that carry a theme layer, in manifest
# order when the installed rice is known.
#
# A module is theme-carrying when it names $OSR_THEME_DIR; that is the same
# grep a person would run, and it cannot go stale. Narrowing by the recorded
# rice matters: without it a theme apply would write ~/.config/polybar on a
# Hyprland box, creating configs for programs that are not installed.
osr_theme_modules() {
    _tm_rice=${1:-}
    if [ -n "$_tm_rice" ] && [ -f "$OSR_ROOT/rices/$_tm_rice/rice.list" ]; then
        _osr_theme_lines "$OSR_ROOT/rices/$_tm_rice/rice.list" | while IFS= read -r _tm_l; do
            case "$_tm_l" in
                *:*) continue ;;    # require: / theme: / themes: - not modules
            esac
            [ -f "$OSR_ROOT/modules/$_tm_l.sh" ] || continue
            grep -q 'OSR_THEME_DIR' "$OSR_ROOT/modules/$_tm_l.sh" || continue
            printf '%s\n' "$_tm_l"
        done
    else
        # No recorded rice (first run, or a hand-built system): every module that
        # can paint something. Overshoots rather than under-paints.
        for _tm_f in "$OSR_ROOT"/modules/*.sh; do
            grep -q 'OSR_THEME_DIR' "$_tm_f" || continue
            _tm_b=$(basename "$_tm_f")
            printf '%s\n' "${_tm_b%.sh}"
        done
    fi
}
