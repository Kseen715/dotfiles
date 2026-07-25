# lib/config.sh — layered config by ownership (POSIX sh)
#
# §5: split every config along ownership layers with distinct lifecycles, and
# write only what os-rice owns. Layers are drop-in files sourced in lexical
# order; where a single target file is unavoidable, own only a marked block.
#
#   00-env   user/machine   seeded once if absent, then kept
#   10/20-*  dotfiles        overwrite on update, rice-independent
#   90-*     rice            swapped on rice switch
#   99-local machine         seeded empty once, never touched

# --- rice-theme selection for standalone module installs (§6) ----------------
#
# In rice mode the manifest fixes which rice owns the 90-theme layers. In
# --module mode there is no rice, so a standalone `osr module starship` would
# install the base config but skip every rice-owned theme. This lets the user
# pick which rice supplies those layers, so a single module still lands themed.

# The rice used when no choice can be made (non-interactive: CI, piped, curl|sh).
OSR_DEFAULT_THEME_RICE=${OSR_DEFAULT_THEME_RICE:-xin}

# osr_theme_rices — list rice names that carry a config/ dir (a themeable rice).
osr_theme_rices() {
    for _tr_d in "$OSR_ROOT"/rices/*/; do
        [ -d "${_tr_d}config" ] || continue
        basename "$_tr_d"
    done
}

# _osr_theme_menu — numbered picker. Prompt + input go through /dev/tty (never
# stdout: this runs inside $(...) so stdout is the captured return value). Echoes
# the chosen rice name. Empty/invalid/EOF input falls back to the default rice.
_osr_theme_menu() {
    # rice names are single words by construction -> safe to word-split.
    # shellcheck disable=SC2046
    set -- $(osr_theme_rices)
    [ "$#" -gt 0 ] || { printf '%s' "$OSR_DEFAULT_THEME_RICE"; return; }
    {
        printf 'Select a rice theme for this module:\n'
        _tm_n=1
        for _tm_r in "$@"; do
            if [ "$_tm_r" = "$OSR_DEFAULT_THEME_RICE" ]; then
                printf '  %d) %s (default)\n' "$_tm_n" "$_tm_r"
            else
                printf '  %d) %s\n' "$_tm_n" "$_tm_r"
            fi
            _tm_n=$((_tm_n + 1))
        done
        printf 'Enter number [default %s]: ' "$OSR_DEFAULT_THEME_RICE"
    } >/dev/tty
    _tm_ans=""
    read -r _tm_ans </dev/tty || _tm_ans=""
    case "$_tm_ans" in
        "")           printf '%s' "$OSR_DEFAULT_THEME_RICE"; return ;;
        *[!0-9]*)     printf '%s' "$OSR_DEFAULT_THEME_RICE"; return ;;
    esac
    if [ "$_tm_ans" -ge 1 ] && [ "$_tm_ans" -le "$#" ]; then
        eval "printf '%s' \"\${$_tm_ans}\""
    else
        printf '%s' "$OSR_DEFAULT_THEME_RICE"
    fi
}

# osr_resolve_theme_rice [wanted] — set OSR_RICE + OSR_RICE_DIR for module mode.
# Resolution order (§6): explicit --theme > interactive menu > default rice.
# After this, a module's `[ -f "$OSR_RICE_DIR/config/..." ]` theme guards fire
# exactly as in a rice install.
osr_resolve_theme_rice() {
    _rt_want=${1:-}
    if [ -n "$_rt_want" ]; then
        [ -d "$OSR_ROOT/rices/$_rt_want/config" ] \
            || error "no such rice theme: '$_rt_want' (see: osr list)"
        _rt_pick=$_rt_want
    elif [ -t 0 ] && [ -t 1 ] && [ -r /dev/tty ]; then
        _rt_pick=$(_osr_theme_menu)
    else
        _rt_pick=$OSR_DEFAULT_THEME_RICE
        info "no interactive terminal - using default rice theme '$_rt_pick'"
    fi
    OSR_RICE=$_rt_pick
    OSR_RICE_DIR="$OSR_ROOT/rices/$_rt_pick"
    export OSR_RICE OSR_RICE_DIR
    info "rice theme: $OSR_RICE"
}

# seed_once <src> <dst> — copy src to dst only if dst is absent (00-env). After
# seeding, dst is user territory os-rice never rewrites.
seed_once() {
    _so_src=$1
    _so_dst=$2
    if [ -e "$_so_dst" ]; then
        info "keeping existing $_so_dst (seeded once)"
        return 0
    fi
    as_user mkdir -p "$(dirname "$_so_dst")"
    as_user cp -f "$_so_src" "$_so_dst"
}

# seed_empty <dst> — create an empty file if absent (99-local).
seed_empty() {
    [ -e "$1" ] && return 0
    as_user mkdir -p "$(dirname "$1")"
    as_user touch "$1"
}

# install_layer <src> <dst> — overwrite-on-update layer (10/20/90). Backs up a
# pre-existing non-os-rice file once, then keeps it in sync (rerun-safe, §2).
install_layer() {
    backup_copy "$1" "$2"
}

# install_zsh_loader <rc_dir> <zshrc> — own a marked loader block in the target
# .zshrc that sources rc.d/*.zsh in lexical order (§5). Only the block is
# rewritten; a user's own .zshrc content around it is preserved.
install_zsh_loader() {
    _il_rcdir=$1
    _il_zshrc=$2
    ensure_block "$_il_zshrc" "loader" <<EOF
for _f in "$_il_rcdir"/*.zsh; do [ -r "\$_f" ] && . "\$_f"; done
unset _f
EOF
}

# compose_starship_config <base> <palette-fragment> <dst> — build the installed
# starship.toml from the shared dotfiles base + a rice's palette. starship.toml
# has no include mechanism (unlike foot.ini), so the §5 base/theme split is
# realized by composition instead of layering: the base body (its default
# `[palettes.theme]` table, which is LAST in the file, stripped) followed by the
# rice's `[palettes.theme]` table. Regenerated deterministically on switch, so
# the whole file is rice-owned output but only the palette actually changes.
compose_starship_config() {
    _cs_base=$1
    _cs_frag=$2
    _cs_dst=$3
    [ -f "$_cs_base" ] || error "compose_starship_config: base not found: $_cs_base"
    [ -f "$_cs_frag" ] || error "compose_starship_config: palette not found: $_cs_frag"
    _cs_tmp="${TMPDIR:-/tmp}/osr-starship-$$.toml"
    {
        # Base minus its default palette table (from the header to EOF), then the
        # rice palette in its place. The default palette must stay last in base.
        sed '/^\[palettes\.theme\]/,$d' "$_cs_base"
        cat "$_cs_frag"
    } > "$_cs_tmp"
    # backup_copy gives the once-only .bak + content-equal skip (rerun-safe, §2).
    backup_copy "$_cs_tmp" "$_cs_dst"
    rm -f "$_cs_tmp"
}

# apply_config <name> — copy a rice-owned config dir (rices/<rice>/config/<name>)
# into ~/.config/<name>, backing up once. Used by the manifest `config:`
# directive for DE configs (§5). Falls back gracefully if the dir is absent.
apply_config() {
    _ac_name=$1
    _ac_src="$OSR_RICE_DIR/config/$_ac_name"
    if [ ! -d "$_ac_src" ]; then
        warn "config '$_ac_name' not found in rice ($_ac_src) - skipping"
        return 0
    fi
    _ac_dst="$OSR_HOME/.config/$_ac_name"
    info "applying config: $_ac_name -> $_ac_dst"
    as_user mkdir -p "$_ac_dst"
    # copy contents (trailing /.) so the dir itself is not nested
    as_user cp -rf "$_ac_src/." "$_ac_dst/"
}

# apply_wallpaper — pick the rice's wallpaper, record it as rice-owned state, and
# set it if a compositor/setter exists. Degrades to record-only when headless
# (containers, no DE) so it never fails a run (§6, §9).
apply_wallpaper() {
    _wp=""
    for _f in "$OSR_RICE_DIR"/wallpapers/*; do
        [ -f "$_f" ] || continue
        _wp=$_f
        break
    done
    [ -n "$_wp" ] || return 0

    # Record the choice — this is what a switch swaps even with no display.
    as_user mkdir -p "$OSR_HOME/.config/osr"
    printf '%s\n' "$_wp" | as_user tee "$OSR_HOME/.config/osr/wallpaper" >/dev/null

    if command -v swww >/dev/null 2>&1; then
        as_user swww img "$_wp" >/dev/null 2>&1 || warn "swww failed to set wallpaper"
    elif command -v hyprctl >/dev/null 2>&1; then
        as_user hyprctl hyprpaper wallpaper ",$_wp" >/dev/null 2>&1 || warn "hyprpaper failed"
    elif command -v feh >/dev/null 2>&1; then
        as_user feh --bg-scale "$_wp" >/dev/null 2>&1 || warn "feh failed to set wallpaper"
    else
        info "no wallpaper setter (headless) - recorded $_wp"
    fi
}
