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
#
# The text this file used to build itself — the help, the two listings, the
# option loop, the manifest parser, the detected-facts report, the closing line
# — is C now (`osr install …` in the harness core). What stays is the
# orchestration, which cannot leave the shell: it sources lib/*.sh, calls their
# functions (osr_detect, osr_resolve_user, osr_apply_theme), and SOURCES each
# module. Byte-for-byte the sh original, frozen at test/ref/install_sh_ref.sh
# and diffed by test/unit/install_c_parity.sh.
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
for _lib in detect user net pkg git service config migrate theme apply reload fonts gnome build preflight; do
    [ -f "$OSR_LIB/$_lib.sh" ] && . "$OSR_LIB/$_lib.sh"
done

usage() { "$OSR_BIN" install usage; }

list_rices() { "$OSR_BIN" install list-rices; }

list_modules() { "$OSR_BIN" install list-modules; }

# The one listing that stays here: it is a query into lib/theme.sh, not a
# directory scan (osr_themes knows what a theme IS).
list_themes() {
    for _t in $(osr_themes); do
        printf '  %-12s %s\n' "$_t" "$(osr_theme_meta "$_t" description)"
    done
}

# --- argument parsing --------------------------------------------------------
# The loop is in C; what comes back is this file's own variables, plus
# OSR_ACTION for the paths that end the run right here.
eval "$("$OSR_BIN" install parse-args "$@")"
case "$OSR_ACTION" in
    usage)        usage; exit 0 ;;
    list)         echo "Available rices:"; list_rices; exit 0 ;;
    list-themes)  echo "Available themes:"; list_themes; exit 0 ;;
    list-modules) echo "Available modules:"; list_modules; exit 0 ;;
    error)        error "unknown option: $OSR_ACTION_ARG" ;;
    missing-arg)  error "$OSR_ACTION_ARG" ;;
esac

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

# The detected facts, one line each, only the facets that were detected (§7).
# VERSION_ID is absent on rolling releases (Arch/Void/Gentoo) and simply drops
# out of the line there.
"$OSR_BIN" install report base
# Warm the sudo credential for the whole run so escalating steps don't each
# prompt (§7). Best-effort and interactive-only: root-for-root and non-root
# user-space rices (§8) need no sudo, and CI/containers run as root — so a
# missing TTY is never fatal here; steps escalate lazily via as_root().
if [ "$(id -u)" -ne 0 ] && command -v sudo >/dev/null 2>&1 && [ -t 0 ]; then
    if sudo -v 2>/dev/null; then
        ( while true; do sudo -n true; sleep 60; kill -0 "$$" 2>/dev/null || exit; done ) &
    fi
fi

# Hardware lines: printed after the sudo warm-up because DMI (RAM type/speed/
# slots) needs root — retry the probe now that a ticket may exist.
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
"$OSR_BIN" install report hw

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
        [ -f "$OSR_ROOT/modules/$_m.sh" ] || "$OSR_BIN" module has "$_m" \
            || error "module not found: $_m (try --list-modules)"
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
    # The manifest: `#` comments and whitespace stripped, module lines collected,
    # `require:` lines split out. `theme:`/`themes:` are read back through
    # lib/theme.sh, not here - they must still be matched so a directive never
    # falls through to the module list. Module count is the progress
    # denominator (§3).
    eval "$("$OSR_BIN" install manifest "$OSR_RICE_DIR/rice.list")"

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

OSR_STEP_TOTAL=$("$OSR_BIN" install count "$OSR_MODULES")
OSR_STEP_N=0
export OSR_STEP_TOTAL OSR_STEP_N

# --- run modules -------------------------------------------------------------
# A module is either a C one in the harness core (modules/linux/*.c, listed by
# `osr module list`) or a shell script under modules/. The core wins where both
# exist, and a rice.list never says which kind it asked for.
run_module() {
    _mod=$1
    _path="$OSR_ROOT/modules/$_mod.sh"
    OSR_STEP_N=$((OSR_STEP_N + 1))
    export OSR_STEP_N
    "$OSR_BIN" log step "module: $_mod"
    if "$OSR_BIN" module has "$_mod"; then
        # No `|| warn`: a C module that fails has already said why and exited,
        # and `set -e` ends the run here - which is what a failing run_step
        # inside a .sh module did.
        "$OSR_BIN" module run "$_mod"
        return 0
    fi
    [ -f "$_path" ] || error "module not found: $_mod ($_path)"
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
    "$OSR_BIN" state set rice "$OSR_RICE"
    "$OSR_BIN" state set theme "$OSR_THEME"
    "$OSR_BIN" state set applied "$(date +%s 2>/dev/null || echo 0)"
fi

"$OSR_BIN" install final "$OSR_MODULE_MODE" "$OSR_MODULES" "${OSR_MODE:-install}" "${OSR_RICE:-}"
