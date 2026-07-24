#!/bin/sh
# os-rice — single shared installer.  Usage: install.sh [options] <rice>
#
#   install.sh gruvbox                 rice OSR_USER (auto-resolved)
#   install.sh --user alice gruvbox    rice a specific user (user-for-user, §8)
#   install.sh --verbose gruvbox       stream output, no spinners
#   install.sh --module zsh foot       install specific module(s), no rice
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
for _lib in detect user net pkg git service config build; do
    [ -f "$OSR_LIB/$_lib.sh" ] && . "$OSR_LIB/$_lib.sh"
done

usage() {
    cat <<EOF
Usage:
  install.sh [--user <name>] [--verbose] <rice>     install a rice
  install.sh --module [--user <name>] <name>...     install module(s), no rice
  install.sh --list                                 list available rices
  install.sh --list-modules                         list available modules

  <rice>            name of a directory under os-rice/rices/
  --module          treat positionals as module names, not a rice
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

# --- argument parsing --------------------------------------------------------
OSR_ARG_USER=""
OSR_MODULE_MODE=""
OSR_POS=""
while [ $# -gt 0 ]; do
    case "$1" in
        --user)         OSR_ARG_USER=${2:?--user needs a name}; shift 2 ;;
        --verbose)      OSR_VERBOSE=1; export OSR_VERBOSE; shift ;;
        --module)       OSR_MODULE_MODE=1; shift ;;
        --list)         echo "Available rices:"; list_rices; exit 0 ;;
        --list-modules) echo "Available modules:"; list_modules; exit 0 ;;
        -h|--help)      usage; exit 0 ;;
        -*)             error "unknown option: $1" ;;
        *)              OSR_POS="$OSR_POS $1"; shift ;;
    esac
done

# --- detection + identity ----------------------------------------------------
osr_detect
osr_resolve_user "$OSR_ARG_USER"
info "distro=$OSR_DISTRO pkg=$OSR_PKG init=$OSR_INIT user=$OSR_USER home=$OSR_HOME"

# Warm the sudo credential for the whole run so escalating steps don't each
# prompt (§7). Best-effort and interactive-only: root-for-root and non-root
# user-space rices (§8) need no sudo, and CI/containers run as root — so a
# missing TTY is never fatal here; steps escalate lazily via as_root().
if [ "$(id -u)" -ne 0 ] && command -v sudo >/dev/null 2>&1 && [ -t 0 ]; then
    if sudo -v 2>/dev/null; then
        ( while true; do sudo -n true; sleep 60; kill -0 "$$" 2>/dev/null || exit; done ) &
    fi
fi

# --- resolve what to run: a rice manifest, or explicit --module names --------
OSR_MODULES=""
OSR_CONFIGS=""
if [ -n "$OSR_MODULE_MODE" ]; then
    # Explicit module install: positionals are module names, there is no rice.
    # Rice-owned config guards inside modules ([ -f "$OSR_RICE_DIR/..." ]) fall
    # through with OSR_RICE_DIR empty, and configs/wallpaper are skipped below.
    OSR_MODULES=$OSR_POS
    [ -n "$OSR_MODULES" ] || { usage >&2; error "no module specified"; }
    OSR_RICE=""; OSR_RICE_DIR=""; export OSR_RICE OSR_RICE_DIR
    for _m in $OSR_MODULES; do
        [ -f "$OSR_ROOT/modules/$_m.sh" ] || error "module not found: $_m (try --list-modules)"
    done
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
    # Strip `#` comments + whitespace; collect module lines and `config:` dirs.
    # Module count is the progress denominator (§3).
    while IFS= read -r _line || [ -n "$_line" ]; do
        _line=${_line%%#*}
        _line=$(printf '%s' "$_line" | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')
        [ -n "$_line" ] || continue
        case "$_line" in
            config:*) OSR_CONFIGS="$OSR_CONFIGS ${_line#config:}" ;;
            *)        OSR_MODULES="$OSR_MODULES $_line" ;;
        esac
    done < "$OSR_RICE_DIR/rice.list"
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

# --- copy rice-owned configs + wallpaper (rice mode only) --------------------
if [ -z "$OSR_MODULE_MODE" ]; then
    for _c in $OSR_CONFIGS; do
        apply_config "$_c"
    done
    apply_wallpaper
fi

if [ -n "$OSR_MODULE_MODE" ]; then
    success "module(s) installed:$OSR_MODULES"
elif [ "${OSR_MODE:-install}" = "switch" ]; then
    success "switched to rice '$OSR_RICE' (packages accreted, rice config replaced)"
else
    success "rice '$OSR_RICE' installed"
fi
