# lib/theme.sh — the shell-callable surface of themes (POSIX sh)
#
# The reading, parsing and palette arithmetic are `osr theme` in the harness
# core (lib/theme.c). What stays here is what only a shell can do: set
# OSR_THEME/OSR_THEME_DIR for the modules that branch on them, end the run
# through error(), and loop over apply_config (itself a shell function).
#
# Byte-for-byte the sh original, frozen at test/ref/theme_sh_ref.sh and diffed
# by test/unit/theme_c_parity.sh.
#
# §6a: a rice is a set of PACKAGES, a theme is a set of APPEARANCE LAYERS. They
# were the same directory until themes were split out, which made a theme switch
# cost a full manifest run. Now:
#
#   themes/<name>/theme.list    metadata + palette (same `key: value` shape as
#                               rice.list - no parser, just `while read`)
#   themes/<name>/config/       the 90-* layers, one dir per app
#   themes/<name>/wallpapers/   0..n images
#
# Any theme applies onto any rice: the layers of apps that are not installed land
# in ~/.config and are simply never read. That is what makes `osr theme <name>`
# safe to bind to a hotkey - it never consults a package manager.
#
# rice.list declares `theme:` (installed with the rice) and `themes:` (the set
# the rice was designed against, offered by the picker).

# The theme used when no choice can be made (non-interactive: CI, piped, curl|sh).
OSR_DEFAULT_THEME=${OSR_DEFAULT_THEME:-xin}
export OSR_DEFAULT_THEME

if [ -z "${OSR_BIN:-}" ]; then
    . "${OSR_LIB:?theme.sh: source lib/ui.sh first, or export OSR_LIB}/ui.sh"
fi

# osr_themes — list every theme name (a dir under themes/ with a theme.list).
osr_themes() { "$OSR_BIN" theme list; }

# osr_theme_exists <name> — true when <name> is a real theme.
osr_theme_exists() { "$OSR_BIN" theme exists "${1:-}"; }

# _osr_theme_lines <file> — echo a manifest's directive lines with comments and
# surrounding whitespace stripped. The one parser both theme.list and the
# rice.list theme directives go through (lib/apply.sh reads rice.list with it).
_osr_theme_lines() { "$OSR_BIN" theme lines "$1"; }

# osr_theme_meta <name> <key> — echo the value of a single-valued `key:` field
# (display, description, polarity, session), "" when absent.
osr_theme_meta() { "$OSR_BIN" theme meta "$1" "$2"; }

# osr_theme_configs <name> — echo the whole config/ dirs the theme drops into
# ~/.config on apply (the `config:` lines), one per line.
osr_theme_configs() { "$OSR_BIN" theme configs "$1"; }

# osr_theme_color <name> <role> — echo the palette entry for a role, "" if unset.
# Roles: bg surface fg dim accent success error warning.
osr_theme_color() { "$OSR_BIN" theme color "$1" "$2"; }

# _osr_theme_sed <name> — echo a sed script that substitutes every `{{key}}` the
# theme defines: one rule per `color:` role, one per single-valued meta field,
# plus `{{THEME}}` for the theme's own name. This is what makes a theme a
# palette instead of a directory of app configs (lib/config.sh renders with it).
_osr_theme_sed() { "$OSR_BIN" theme sed "$1"; }

# _osr_hex_dec <#rrggbb> — echo "r,g,b".
_osr_hex_dec() { "$OSR_BIN" theme hex-dec "$1"; }

# _osr_theme_swatch <name> — echo a run of colored blocks for the theme's
# palette (truecolor, so a theme shows its own colors and not the terminal's).
_osr_theme_swatch() { "$OSR_BIN" theme swatch "$1"; }

# _osr_theme_menu — the numbered picker; prompt and input go through /dev/tty
# because this runs inside a `$(...)`, where stdout is the return value.
_osr_theme_menu() { "$OSR_BIN" theme menu; }

# osr_theme_session <name> — echo any|x11|wayland (defaults to any).
osr_theme_session() { "$OSR_BIN" theme session "$1"; }

# osr_rice_themes <rice> — echo the theme set a rice ships (`themes:` line),
# falling back to its `theme:` when it declares no set.
osr_rice_themes() { "$OSR_BIN" theme rice-themes "$1"; }

# osr_rice_default_theme <rice> — echo the rice's `theme:` (its installed theme).
osr_rice_default_theme() { "$OSR_BIN" theme rice-default "$1"; }

# osr_apply_theme_configs — drop the whole config/ dirs the current theme
# declares (`config:` in theme.list) into ~/.config. These are the appearance
# dirs no module owns; a module-owned layer is installed by its module instead.
# Stays shell: apply_config is a shell function in lib/config.sh.
osr_apply_theme_configs() {
    [ -n "${OSR_THEME:-}" ] || return 0
    for _atc in $(osr_theme_configs "$OSR_THEME"); do
        apply_config "$_atc"
    done
}

# osr_resolve_theme [wanted] — set OSR_THEME + OSR_THEME_DIR.
# Resolution order: explicit name > interactive menu > default theme.
# After this, a module's `[ -f "$OSR_THEME_DIR/config/..." ]` guards fire.
osr_resolve_theme() {
    _rt_want=${1:-}
    if [ -n "$_rt_want" ]; then
        osr_theme_exists "$_rt_want" \
            || error "no such theme: '$_rt_want' (see: osr themes)"
        _rt_pick=$_rt_want
    elif [ -t 0 ] && [ -t 1 ] && [ -r /dev/tty ]; then
        _rt_pick=$(_osr_theme_menu)
    else
        _rt_pick=$OSR_DEFAULT_THEME
        info "no interactive terminal - using default theme '$_rt_pick'"
    fi
    OSR_THEME=$_rt_pick
    OSR_THEME_DIR="$OSR_ROOT/themes/$_rt_pick"
    export OSR_THEME OSR_THEME_DIR
    info "theme: $OSR_THEME"
}

# osr_unset_theme — "this run paints nothing", explicitly.
#
# The counterpart to osr_resolve_theme for a module set where no module reads
# the theme (osr module themable says no for all of them): every theme guard is
# `[ -n "$OSR_THEME_DIR" ]`, so empty-and-exported is the value that makes them
# all decline, and nothing is asked of the user for an answer nothing consumes.
osr_unset_theme() {
    OSR_THEME=""
    OSR_THEME_DIR=""
    export OSR_THEME OSR_THEME_DIR
}
