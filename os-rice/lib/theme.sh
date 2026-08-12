# lib/theme.sh — themes as first-class objects (POSIX sh)
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

# osr_themes — list every theme name (a dir under themes/ with a theme.list).
osr_themes() {
    for _t_d in "$OSR_ROOT"/themes/*/; do
        [ -f "${_t_d}theme.list" ] || continue
        basename "$_t_d"
    done
}

# osr_theme_exists <name> — true when <name> is a real theme.
osr_theme_exists() {
    [ -n "${1:-}" ] && [ -f "$OSR_ROOT/themes/$1/theme.list" ]
}

# _osr_theme_lines <file> — echo the manifest's directive lines with comments and
# surrounding whitespace stripped. The one parser both theme.list and the
# rice.list theme directives go through.
#
# The comment rule is narrower than install.sh's `${line%%#*}` because a palette
# value IS a hash: `color: bg #2e3440` would strip to nothing. A comment is
# therefore `#` at the start of a line, or a hash with whitespace on BOTH sides
# (`... # like this`) - and `#rrggbb` never has a space after the hash, so the
# two can never be confused. Writing a trailing comment as `#no-space` leaves it
# in the value on purpose: guessing there would put the ambiguity back.
# One sed over the whole file, not one per line: the picker asks for several
# fields of several themes per keystroke, and a subprocess per line turns that
# into hundreds of forks.
_osr_theme_lines() {
    [ -f "$1" ] || return 0
    sed '
        s/^[[:space:]]*#.*$//
        s/[[:space:]]#[[:space:]].*$//
        s/[[:space:]]#$//
        s/^[[:space:]]*//
        s/[[:space:]]*$//
        /^$/d' "$1"
}

# osr_theme_meta <name> <key> — echo the value of a single-valued `key:` field
# (display, description, polarity, session), "" when absent.
osr_theme_meta() {
    _tm_v=$(_osr_theme_lines "$OSR_ROOT/themes/$1/theme.list" \
        | sed -n "s/^$2:[[:space:]]*//p" | head -n 1)
    printf '%s' "$_tm_v"
}

# osr_theme_configs <name> — echo the whole config/ dirs the theme drops into
# ~/.config on apply (the `config:` lines), one per line.
osr_theme_configs() {
    _tc_f="$OSR_ROOT/themes/$1/theme.list"
    _osr_theme_lines "$_tc_f" | sed -n 's/^config:[[:space:]]*//p' | tr ' ' '\n' | grep -v '^$' || true
}

# osr_theme_color <name> <role> — echo the palette entry for a role, "" if unset.
# Roles: bg surface fg dim accent success error warning.
osr_theme_color() {
    _tk_v=$(_osr_theme_lines "$OSR_ROOT/themes/$1/theme.list" \
        | sed -n "s/^color:[[:space:]]*$2[[:space:]][[:space:]]*//p" | head -n 1)
    printf '%s' "$_tk_v"
}

# _osr_hex_dec <#rrggbb> — echo "r,g,b". POSIX arithmetic understands 0x hex.
_osr_hex_dec() {
    _hd=${1#\#}
    printf '%d,%d,%d' "$((0x${_hd%????}))" "$((0x${_hd#??} / 0x100))" "$((0x${_hd#????}))"
}

# _osr_theme_sed <name> — echo a sed script that substitutes every `{{key}}` the
# theme defines: one rule per `color:` role, one per single-valued meta field,
# plus `{{THEME}}` for the theme's own name.
#
# This is what makes a theme a palette instead of a directory of app configs. An
# app's colors used to be written out once per theme, so adding a theme meant
# writing N files and adding an app meant editing N themes - the product, not the
# sum. With one template per app the app is written once and a theme is only its
# colors, so both axes grow independently.
#
# Every color role gets three spellings, because a color is written three ways
# across the configs os-rice owns and a template must never hard-code one to get
# the shape its app parses:
#
#   {{role}}      #rrggbb     GTK, Xresources, most TUIs
#   {{role_rgb}}  rrggbb      foot, and anything CSS-adjacent that adds its own #
#   {{role_dec}}  r,g,b       KDE color schemes, konsole, CSS rgba()
#   {{role_sgr}}  r;g;b       ANSI truecolor escapes (fastfetch, any 38;2;... run)
#
# The decimal form is computed here in sh arithmetic rather than stored in
# theme.list: it is the same number twice, and two spellings of one value in the
# palette is exactly the drift this design exists to remove.
#
# The delimiter is `|`, never `#`: every value here is a `#rrggbb`. A value
# containing `|` would break its own rule, which no color or theme name can.
# `config:` is excluded because it is multi-valued (0..n lines); a template that
# wants a config dir names it directly.
_osr_theme_sed() {
    printf 's|{{THEME}}|%s|g\n' "$1"
    # The decimal spellings need arithmetic, so they are built in the shell
    # rather than by the one-pass sed below.
    _osr_theme_lines "$OSR_ROOT/themes/$1/theme.list" \
        | sed -n 's/^color:[[:space:]]*\([A-Za-z0-9_]*\)[[:space:]][[:space:]]*#\([0-9a-fA-F]\{6\}\)$/\1 \2/p' \
        | while read -r _ts_role _ts_hex; do
            _ts_d=$(_osr_hex_dec "$_ts_hex")
            printf 's|{{%s_dec}}|%s|g\n' "$_ts_role" "$_ts_d"
            printf 's|{{%s_sgr}}|%s|g\n' "$_ts_role" "$(printf '%s' "$_ts_d" | tr , ';')"
        done
    _osr_theme_lines "$OSR_ROOT/themes/$1/theme.list" | sed -n '
        s/^color:[[:space:]]*\([A-Za-z0-9_]*\)[[:space:]][[:space:]]*#\(.*\)$/s|{{\1}}|#\2|g\
s|{{\1_rgb}}|\2|g/p
        t
        s/^color:[[:space:]]*\([A-Za-z0-9_]*\)[[:space:]][[:space:]]*\(.*\)$/s|{{\1}}|\2|g/p
        t
        /^config:/d
        s/^\([a-z][a-z0-9_]*\):[[:space:]]*\(.*\)$/s|{{\1}}|\2|g/p'
}

# osr_theme_session <name> — echo any|x11|wayland (defaults to any).
osr_theme_session() {
    _ts_v=$(osr_theme_meta "$1" session)
    [ -n "$_ts_v" ] || _ts_v=any
    printf '%s' "$_ts_v"
}

# osr_rice_themes <rice> — echo the theme set a rice ships (`themes:` line),
# falling back to its `theme:` when it declares no set.
osr_rice_themes() {
    _rt_f="$OSR_ROOT/rices/$1/rice.list"
    _rt_v=$(_osr_theme_lines "$_rt_f" | sed -n 's/^themes:[[:space:]]*//p' | head -n 1)
    [ -n "$_rt_v" ] || _rt_v=$(_osr_theme_lines "$_rt_f" | sed -n 's/^theme:[[:space:]]*//p' | head -n 1)
    printf '%s' "$_rt_v" | tr ' ' '\n' | grep -v '^$' || true
}

# osr_rice_default_theme <rice> — echo the rice's `theme:` (its installed theme).
osr_rice_default_theme() {
    _rd_v=$(_osr_theme_lines "$OSR_ROOT/rices/$1/rice.list" \
        | sed -n 's/^theme:[[:space:]]*//p' | head -n 1)
    printf '%s' "$_rd_v"
}

# osr_apply_theme_configs — drop the whole config/ dirs the current theme
# declares (`config:` in theme.list) into ~/.config. These are the appearance
# dirs no module owns; a module-owned layer is installed by its module instead.
osr_apply_theme_configs() {
    [ -n "${OSR_THEME:-}" ] || return 0
    for _atc in $(osr_theme_configs "$OSR_THEME"); do
        apply_config "$_atc"
    done
}

# _osr_theme_swatch <name> — echo a run of colored blocks for the theme's
# palette. 48;2;r;g;b (truecolor) on purpose, never the 0-15 palette indices:
# the point of the preview is to show the theme's own colors, and an indexed
# color would be repainted by whatever palette the terminal is currently wearing
# - every theme would look identical.
_osr_theme_swatch() {
    for _sw_role in background surface foreground accent \
                    ansi_red ansi_green ansi_yellow ansi_blue ansi_magenta ansi_cyan; do
        _sw_hex=$(osr_theme_color "$1" "$_sw_role")
        case "$_sw_hex" in \#??????) ;; *) continue ;; esac
        printf '\033[48;2;%sm    ' "$(_osr_hex_dec "$_sw_hex" | tr , ';')"
    done
    printf '\033[0m'
}

# _osr_theme_menu — numbered picker. Prompt + input go through /dev/tty (never
# stdout: this runs inside $(...) so stdout is the captured return value). Echoes
# the chosen theme name. Empty/invalid/EOF input falls back to the default theme.
_osr_theme_menu() {
    # theme names are single words by construction -> safe to word-split.
    # shellcheck disable=SC2046
    set -- $(osr_themes)
    [ "$#" -gt 0 ] || { printf '%s' "$OSR_DEFAULT_THEME"; return; }
    {
        printf 'Select a theme:\n'
        _tm_n=1
        for _tm_r in "$@"; do
            printf '  %d) %-12s %s\n' "$_tm_n" "$_tm_r" "$(_osr_theme_swatch "$_tm_r")"
            _tm_n=$((_tm_n + 1))
        done
        printf 'Enter number [default %s]: ' "$OSR_DEFAULT_THEME"
    } >/dev/tty
    _tm_ans=""
    read -r _tm_ans </dev/tty || _tm_ans=""
    case "$_tm_ans" in
        "")           printf '%s' "$OSR_DEFAULT_THEME"; return ;;
        *[!0-9]*)     printf '%s' "$OSR_DEFAULT_THEME"; return ;;
    esac
    if [ "$_tm_ans" -ge 1 ] && [ "$_tm_ans" -le "$#" ]; then
        eval "printf '%s' \"\${$_tm_ans}\""
    else
        printf '%s' "$OSR_DEFAULT_THEME"
    fi
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
