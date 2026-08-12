#!/bin/sh
# os-rice — single shared installer.  Usage: install.sh [options] <rice>
#
#   install.sh gruvbox                 rice OSR_USER (auto-resolved)
#   install.sh --user alice gruvbox    rice a specific user (user-for-user, §8)
#   install.sh --verbose gruvbox       stream output, no spinners
#   install.sh --module zsh foot       install specific module(s), no rice
#   install.sh --theme nord gruvbox    install a rice painted with another theme
#   install.sh --theme-only --theme nord  apply a theme only (the hotkey path)
#   install.sh --list                  list available rices
#   install.sh --list-modules          list available modules
#
# POSIX sh throughout — runs under dash / busybox ash, not just bash (§Decisions).
set -eu

OSR_ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
OSR_LIB="$OSR_ROOT/lib"
# The dotfiles repo root is the parent of os-rice/ — configs live there.
OSR_DOTFILES=$(cd -- "$OSR_ROOT/.." && pwd)
export OSR_ROOT OSR_LIB OSR_DOTFILES

# ui + log first (colors/logging), then the rest as they exist. Sourcing by
# presence keeps the runner working while the harness is built up slice by slice.
. "$OSR_LIB/ui.sh"
. "$OSR_LIB/log.sh"
for _lib in detect user net pkg git service config theme state apply reload fonts gnome build preflight; do
    [ -f "$OSR_LIB/$_lib.sh" ] && . "$OSR_LIB/$_lib.sh"
done

usage() {
    # Quoted delimiter: this text contains backticks (`theme:`), which an
    # unquoted heredoc would run as a command substitution.
    cat <<'EOF'
Usage:
  install.sh [--user <name>] [--verbose] [--theme <name>] <rice>
                                                    install a rice
  install.sh --module [--theme <name>] <name>...    install module(s), no rice
  install.sh --theme-only --theme <name>            apply a theme only (no
                                                    packages, no sudo) - see osr
  install.sh --list                                 list available rices
  install.sh --list-themes                          list available themes
  install.sh --list-modules                         list available modules

  <rice>            name of a directory under os-rice/rices/
  --module          treat positionals as module names, not a rice
  --theme <name>    which theme supplies the 90-* appearance layers. In rice
                    mode it overrides the manifest's own `theme:`; in --module
                    mode it is the interactive picker's answer (default theme
                    if no TTY)
  --user <name>     account to install for (default: invoking user)
  --verbose         stream command output instead of spinners
EOF
}

list_rices() {
    for _d in "$OSR_ROOT"/rices/*/; do
        [ -f "$_d/rice.list" ] || continue
        printf '  %s\n' "$(basename "$_d")"
    done
}

list_modules() {
    for _f in "$OSR_ROOT"/modules/*.sh; do
        [ -f "$_f" ] || continue
        _b=$(basename "$_f")
        printf '  %s\n' "${_b%.sh}"
    done
}

list_themes() {
    for _t in $(osr_themes); do
        printf '  %-12s %s\n' "$_t" "$(osr_theme_meta "$_t" description)"
    done
}

# --- argument parsing --------------------------------------------------------
OSR_ARG_USER=""
OSR_ARG_THEME=""
OSR_MODULE_MODE=""
OSR_THEME_ONLY=""
OSR_NO_RELOAD=""
OSR_POS=""
while [ $# -gt 0 ]; do
    case "$1" in
        --user)         OSR_ARG_USER=${2:?--user needs a name}; shift 2 ;;
        --theme)        OSR_ARG_THEME=${2:?--theme needs a theme name}; shift 2 ;;
        --theme-only)   OSR_THEME_ONLY=1; shift ;;
        --no-reload)    OSR_NO_RELOAD=1; shift ;;
        --verbose)      OSR_VERBOSE=1; export OSR_VERBOSE; shift ;;
        --module)       OSR_MODULE_MODE=1; shift ;;
        --list)         echo "Available rices:"; list_rices; exit 0 ;;
        --list-themes)  echo "Available themes:"; list_themes; exit 0 ;;
        --list-modules) echo "Available modules:"; list_modules; exit 0 ;;
        -h|--help)      usage; exit 0 ;;
        -*)             error "unknown option: $1" ;;
        *)              OSR_POS="$OSR_POS $1"; shift ;;
    esac
done

# --- detection + identity ----------------------------------------------------
osr_detect
osr_resolve_user "$OSR_ARG_USER"

# --- theme-only: the hotkey path (§6a) ---------------------------------------
#
# Everything below this block - the hardware report, the sudo warm-up, the DMI
# probe that may INSTALL dmidecode - exists to make package decisions. A theme
# apply makes none, and it runs from a key press with no terminal attached, so
# it must not touch a package manager, prompt for a password, or take a second.
# It exits here rather than threading `if` through the rest of the file.
if [ -n "$OSR_THEME_ONLY" ]; then
    osr_apply_theme "$OSR_ARG_THEME"
    [ -n "$OSR_NO_RELOAD" ] || osr_reload_all
    success "theme '$OSR_THEME' applied"
    exit 0
fi

# VERSION_ID is absent on rolling releases (Arch/Void/Gentoo) — the ${:+} just
# drops the suffix there, no special-casing needed.
info "distro=$OSR_DISTRO${OSR_VERSION_ID:+ version_id=$OSR_VERSION_ID}${OSR_CODENAME:+ codename=$OSR_CODENAME}${OSR_VERSION:+ version=\"$OSR_VERSION\"}"
info "${OSR_ID_LIKE:+id_like=\"$OSR_ID_LIKE\" }pkg=$OSR_PKG init=$OSR_INIT"
info "kernel=$(uname -r)"
info "user=$OSR_USER home=$OSR_HOME"
# Warm the sudo credential for the whole run so escalating steps don't each
# prompt (§7). Best-effort and interactive-only: root-for-root and non-root
# user-space rices (§8) need no sudo, and CI/containers run as root — so a
# missing TTY is never fatal here; steps escalate lazily via as_root().
if [ "$(id -u)" -ne 0 ] && command -v sudo >/dev/null 2>&1 && [ -t 0 ]; then
    if sudo -v 2>/dev/null; then
        ( while true; do sudo -n true; sleep 60; kill -0 "$$" 2>/dev/null || exit; done ) &
    fi
fi

# Hardware lines: only report facets that were actually detected (§7). Printed
# after the sudo warm-up because DMI (RAM type/speed/slots) needs root — retry
# the probe now that a ticket may exist.
if [ "${OSR_RAM_STICKS:-0}" -eq 0 ]; then
    # Subshell: a failed install (no perms, no net) must not abort the run —
    # the RAM line just degrades to the size from /proc/meminfo.
    # Only worth installing where SMBIOS exists at all (the entry point is
    # present-but-unreadable for non-root; most ARM SoCs have no DMI tables).
    if ! command -v dmidecode >/dev/null 2>&1 &&
       [ -e /sys/firmware/dmi/tables/smbios_entry_point ]; then
        ( pkg_install dmidecode ) || true
    fi
    osr_detect_ram
fi
_hw=""
if [ -n "$OSR_CPU_MODEL" ];         then _hw="cpu=$OSR_CPU_MODEL"; fi
_hw="${_hw:+$_hw }arch=$OSR_CPU_ARCH"
if [ "${OSR_CPU_CORES:-0}" -gt 0 ]; then _hw="$_hw cores=$OSR_CPU_CORES"; fi
# Threads only when SMT actually doubles them up — "cores=4 threads=4" is noise.
if [ "${OSR_CPU_THREADS:-0}" -gt "${OSR_CPU_CORES:-0}" ]; then _hw="$_hw threads=$OSR_CPU_THREADS"; fi
if [ "$OSR_VIRT" != none ];         then _hw="$_hw virt=$OSR_VIRT"; fi
info "$_hw"

_ram=""
if [ -n "$OSR_RAM_TOTAL" ];              then _ram="ram=$OSR_RAM_TOTAL"; fi
if [ -n "$OSR_RAM_TYPE" ];               then _ram="${_ram:+$_ram }$OSR_RAM_TYPE"; fi
if [ -n "$OSR_RAM_SPEED" ];              then _ram="${_ram:+$_ram }$OSR_RAM_SPEED"; fi
if [ "${OSR_RAM_STICKS:-0}" -gt 0 ];     then _ram="$_ram sticks=$OSR_RAM_STICKS"; fi
if [ "${OSR_RAM_CHANNELS:-0}" -gt 0 ];   then _ram="$_ram channels=$OSR_RAM_CHANNELS"; fi
if [ -n "$_ram" ];                       then info "$_ram"; fi

_accel=""
if [ -n "$OSR_GPU_MODEL" ];  then _accel="gpu=$OSR_GPU_MODEL"
elif [ -n "$OSR_GPU_VENDOR" ]; then _accel="gpu=$OSR_GPU_VENDOR"; fi
if [ -n "$OSR_NPU_VENDOR" ]; then _accel="${_accel:+$_accel }npu=$OSR_NPU_VENDOR"; fi
if [ -n "$_accel" ];         then info "hwaccel: $_accel"; fi

# --- resolve what to run: a rice manifest, or explicit --module names --------
OSR_MODULES=""
OSR_REQUIRES=""
if [ -n "$OSR_MODULE_MODE" ]; then
    # Explicit module install: positionals are module names, there is no rice.
    # A standalone module still gets theme-owned 90-* layers: resolve which theme
    # supplies them (--theme > interactive picker > default theme, §6a), then a
    # module's `[ -f "$OSR_THEME_DIR/config/..." ]` theme guards fire normally.
    OSR_MODULES=$OSR_POS
    [ -n "$OSR_MODULES" ] || { usage >&2; error "no module specified"; }
    for _m in $OSR_MODULES; do
        [ -f "$OSR_ROOT/modules/$_m.sh" ] || error "module not found: $_m (try --list-modules)"
    done
    osr_resolve_theme "$OSR_ARG_THEME"
else
    # Rice install: exactly one positional names a rices/<rice>/ directory.
    OSR_RICE=""
    for _p in $OSR_POS; do
        [ -z "$OSR_RICE" ] || error "only one rice may be given (got '$OSR_RICE' and '$_p')"
        OSR_RICE=$_p
    done
    [ -n "$OSR_RICE" ] || { usage >&2; error "no rice specified"; }
    OSR_RICE_DIR="$OSR_ROOT/rices/$OSR_RICE"
    [ -f "$OSR_RICE_DIR/rice.list" ] || error "rice not found: $OSR_RICE (try --list)"
    export OSR_RICE OSR_RICE_DIR
    # Strip `#` comments + whitespace; collect module lines. `theme:`/`themes:`
    # are read back through lib/theme.sh, not here - they must still be matched
    # so a directive never falls through to the module list.
    # Module count is the progress denominator (§3).
    while IFS= read -r _line || [ -n "$_line" ]; do
        _line=${_line%%#*}
        _line=$(printf '%s' "$_line" | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')
        [ -n "$_line" ] || continue
        case "$_line" in
            require:*) OSR_REQUIRES="$OSR_REQUIRES ${_line#require:}" ;;
            theme:*)   ;;  # osr_rice_default_theme
            themes:*)  ;;  # osr_rice_themes (the picker's offer set)
            *)         OSR_MODULES="$OSR_MODULES $_line" ;;
        esac
    done < "$OSR_RICE_DIR/rice.list"

    # The theme that owns this run's 90-* layers: --theme wins, else the
    # manifest's `theme:`, else the default. Overriding it is supported on
    # purpose - a rice is a package set, and any theme paints any of them.
    _rice_theme=$OSR_ARG_THEME
    [ -n "$_rice_theme" ] || _rice_theme=$(osr_rice_default_theme "$OSR_RICE")
    [ -n "$_rice_theme" ] || _rice_theme=$OSR_DEFAULT_THEME
    osr_resolve_theme "$_rice_theme"

    # Preconditions (§10 Tier 1): fail clean before any mutation if the host
    # can't run this rice. Runs on switch too — you can't switch into a rice the
    # hardware can't support.
    # shellcheck disable=SC2086  # intentional word-split over predicates
    [ -n "$OSR_REQUIRES" ] && osr_preflight $OSR_REQUIRES
fi

OSR_STEP_TOTAL=0
for _m in $OSR_MODULES; do OSR_STEP_TOTAL=$((OSR_STEP_TOTAL + 1)); done
OSR_STEP_N=0
export OSR_STEP_TOTAL OSR_STEP_N

# --- run modules -------------------------------------------------------------
run_module() {
    _mod=$1
    _path="$OSR_ROOT/modules/$_mod.sh"
    [ -f "$_path" ] || error "module not found: $_mod ($_path)"
    OSR_STEP_N=$((OSR_STEP_N + 1))
    info "$(step_prefix)module: $_mod"
    # shellcheck disable=SC1090
    . "$_path"
}

for _m in $OSR_MODULES; do
    run_module "$_m"
done

# --- copy theme-owned whole-dir configs + wallpaper (rice mode only) ---------
# The `config:` dirs come from the THEME's manifest, not the rice's: they are
# appearance (GTK colors, xsettingsd, fontconfig) and must travel with the theme
# onto whichever rice it is applied to.
if [ -z "$OSR_MODULE_MODE" ]; then
    osr_apply_theme_configs
    apply_wallpaper
    # Record what is now applied. `rice` is what makes a later `osr theme` cheap
    # AND correct: it narrows the layer set to this manifest's modules, so a
    # theme switch never writes configs for programs this rice never installed.
    osr_state_set rice "$OSR_RICE"
    osr_state_set theme "$OSR_THEME"
    osr_state_set applied "$(date +%s 2>/dev/null || echo 0)"
fi

if [ -n "$OSR_MODULE_MODE" ]; then
    success "module(s) installed:$OSR_MODULES"
elif [ "${OSR_MODE:-install}" = "switch" ]; then
    success "switched to rice '$OSR_RICE' (packages accreted, theme layers replaced)"
else
    success "rice '$OSR_RICE' installed"
fi
