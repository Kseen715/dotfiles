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

# Theme selection (which theme owns the 90-* layers) lives in lib/theme.sh:
# osr_resolve_theme sets OSR_THEME + OSR_THEME_DIR, and every module's
# `[ -f "$OSR_THEME_DIR/config/..." ]` guard reads from there.

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

# install_xprofile_loader <dir> <xprofile> — own a marked loader block in
# ~/.xprofile that sources <dir>/*.sh in lexical order (§5), the same shape as
# install_zsh_loader. ~/.xprofile is read by every display manager and by
# ~/.xinitrc, and it is the only place session-wide env (XDG_CURRENT_DESKTOP,
# toolkit theme vars, Java/Firefox workarounds) reaches GUI apps started by the
# WM. It is a single unavoidable file, so os-rice owns only its block and the
# real content lives in layered drop-ins:
#
#   00-env.sh   user/machine  seeded once
#   10-*.sh     dotfiles      session env, overwritten on update
#   90-theme.sh rice          toolkit theme vars, swapped on rice switch
#   99-local.sh machine       seeded empty, never touched
install_xprofile_loader() {
    _ix_dir=$1
    _ix_file=$2
    ensure_block "$_ix_file" "xprofile-loader" <<EOF
for _f in "$_ix_dir"/*.sh; do [ -r "\$_f" ] && . "\$_f"; done
unset _f
EOF
}

# compose_json_config <base> <rice-fragment> <dst> — build an installed JSON
# config from the dotfiles base with the rice's keys merged over it (§5 by
# composition, same idea as compose_starship_config).
#
# Several editors (VS Code, micro, ...) keep everything in one settings.json and
# have no include mechanism, so the ownership split cannot be two files on disk:
# the base carries behaviour, the rice carries the theme keys, and the installed
# file is generated output. Edit the base or the fragment, never the result.
#
# Falls back to installing the base unthemed when python3 is missing, because a
# missing merge tool is a cosmetic loss, not a reason to fail a rice (§9).
compose_json_config() {
    _cj_base=$1
    _cj_frag=$2
    _cj_dst=$3
    [ -f "$_cj_base" ] || error "compose_json_config: base not found: $_cj_base"
    if [ ! -f "$_cj_frag" ] || ! command -v python3 >/dev/null 2>&1; then
        [ -f "$_cj_frag" ] || info "no rice fragment for $(basename "$_cj_dst") - installing the base"
        command -v python3 >/dev/null 2>&1 \
            || warn "python3 not available - installing $(basename "$_cj_dst") without the rice theme"
        install_layer "$_cj_base" "$_cj_dst"
        return 0
    fi
    _cj_tmp="${TMPDIR:-/tmp}/osr-json-$$.json"
    python3 - "$_cj_base" "$_cj_frag" > "$_cj_tmp" <<'PYEOF'
import json, sys
base = json.load(open(sys.argv[1]))
base.update(json.load(open(sys.argv[2])))
json.dump(base, sys.stdout, indent=2, ensure_ascii=False)
sys.stdout.write("\n")
PYEOF
    backup_copy "$_cj_tmp" "$_cj_dst"
    rm -f "$_cj_tmp"
}

# --- theme templates: one config per app, one palette per theme --------------
#
# §6b. A theme is a set of COLORS; the shape of an app's config file is the same
# whatever the colors are. Keeping a full btop/ghostty/wezterm/serie config in
# every theme meant every new app had to be written once per theme and every new
# theme once per app - N*M files to keep in sync, and in practice they drifted.
#
# So the app's config lives ONCE, next to the rest of that app's dotfiles, with
# `{{role}}` placeholders where a color goes, and a theme carries only the
# `color:` block that fills them in. Adding an app is one template; adding a
# theme is one palette.
#
# A theme can still ship a literal file under its own config/ - every module
# prefers that over the template. That is the escape hatch for a theme whose
# look is not a palette substitution (glass's blur, rosemary's GTK sheets), and
# it is what makes the migration incremental rather than a flag day.

# render_theme_template <src> <dst> — write <src> to <dst> with every `{{role}}`
# replaced by the current theme's value for it.
#
# An unresolved `{{...}}` is a warning, not a failure: the file still lands, and
# a role one theme forgot must not take a whole theme switch down with it (§9).
# The warning names both the placeholder and the template, so the gap is findable.
render_theme_template() {
    _rt_src=$1
    _rt_dst=$2
    [ -f "$_rt_src" ] || error "render_theme_template: template not found: $_rt_src"
    [ -n "${OSR_THEME:-}" ] || error "render_theme_template: no theme resolved"
    _rt_sed="${TMPDIR:-/tmp}/osr-theme-sed-$$"
    _osr_theme_sed "$OSR_THEME" >"$_rt_sed"
    sed -f "$_rt_sed" "$_rt_src" >"$_rt_dst"
    rm -f "$_rt_sed"
    # {{WALLPAPER_PATH}} is deliberately left: it is filled by
    # install_wallpaper_layer in a second pass, because the value is an installed
    # path rather than anything the theme's palette knows (§6).
    if grep -v 'WALLPAPER_PATH' "$_rt_dst" | grep -q '{{'; then
        warn "theme '$OSR_THEME' defines no $(grep -v WALLPAPER_PATH "$_rt_dst" | sed -n 's/.*{{\([A-Za-z0-9_]*\)}}.*/\1/p' | sort -u | tr '\n' ' ')- left unsubstituted in $(basename "$_rt_src")"
    fi
}

# osr_theme_source <app-dir> <name> — echo a path to this theme's version of an
# app's theme layer, ready to install: the theme's own literal file when it ships
# one, else the app's template rendered with the theme palette. Returns non-zero
# with no output when neither exists.
#
# The precedence lives here, once, so no module re-implements it. A rendered file
# is a temp the CALLER removes - which is why install_theme_layer below exists and
# is what modules normally use; the raw form is for the one consumer that does not
# just copy the file (starship composes it onto a base).
osr_theme_source() {
    _ts_app=$1
    _ts_name=$2
    if [ -n "${OSR_THEME_DIR:-}" ] && [ -f "$OSR_THEME_DIR/config/$_ts_app/$_ts_name" ]; then
        printf '%s' "$OSR_THEME_DIR/config/$_ts_app/$_ts_name"
        return 0
    fi
    _ts_tmpl="$OSR_DOTFILES/$_ts_app/$_ts_name.tmpl"
    [ -f "$_ts_tmpl" ] && [ -n "${OSR_THEME:-}" ] || return 1
    _ts_out="${TMPDIR:-/tmp}/osr-theme-$_ts_app-$$-$(basename "$_ts_name")"
    render_theme_template "$_ts_tmpl" "$_ts_out"
    printf '%s' "$_ts_out"
}

# install_theme_layer <app-dir> <name> <dst> — the whole per-app theme rule in
# one call: theme file or rendered template, installed as a 90-layer. Non-zero
# when the theme has neither, which is a module's cue to fall back to its
# unthemed dotfiles default.
install_theme_layer() {
    _tl_src=$(osr_theme_source "$1" "$2") || return 1
    install_layer "$_tl_src" "$3"
    case "$_tl_src" in "${TMPDIR:-/tmp}"/osr-theme-*) rm -f "$_tl_src" ;; esac
}

# --- Mozilla profiles (Firefox / Thunderbird) --------------------------------
#
# Mozilla apps keep their settings in a randomly-named profile directory, so
# there is no fixed path to install a layer into: `install_layer` has nothing to
# aim at until a profile exists. These two helpers resolve the real directories
# and write the same two files into each one.
#
#   user.js          prefs applied at every start (ours, overwrite-on-update)
#   chrome/userChrome.css  the theme layer (rice-owned, swapped on switch §6)
#
# userChrome.css only takes effect with
# toolkit.legacyUserProfileCustomizations.stylesheets=true, which the shipped
# user.js sets — the two must be installed together or the theme silently does
# nothing.

# osr_mozilla_profiles <app-dir> — echo every profile directory under <app-dir>
# (e.g. ~/.mozilla/firefox). Reads profiles.ini when present, and falls back to
# globbing *.default* so a profile created before profiles.ini is written still
# gets the layer. Echoes nothing when the app has never been run.
osr_mozilla_profiles() {
    _mp_root=$1
    [ -d "$_mp_root" ] || return 0
    if [ -f "$_mp_root/profiles.ini" ]; then
        # `Path=` is relative to the root unless IsRelative=0; absolute paths
        # start with / and are echoed unchanged.
        sed -n 's/^[[:space:]]*Path=//p' "$_mp_root/profiles.ini" | while read -r _mp_p; do
            case "$_mp_p" in
                /*) [ -d "$_mp_p" ] && printf '%s\n' "$_mp_p" ;;
                *)  [ -d "$_mp_root/$_mp_p" ] && printf '%s\n' "$_mp_root/$_mp_p" ;;
            esac
        done
    else
        for _mp_d in "$_mp_root"/*.default* "$_mp_root"/*.dev-edition*; do
            [ -d "$_mp_d" ] && printf '%s\n' "$_mp_d"
        done
    fi
}

# install_mozilla_layer <app-dir> <user.js|""> <userChrome.css|""> — install the
# prefs and/or theme into every profile of a Mozilla app. A profile-less app
# (never launched) is not an error: it warns and returns, because the alternative
# is guessing a profile name that the app would then ignore.
install_mozilla_layer() {
    _ml_root=$1
    _ml_js=$2
    _ml_css=$3
    _ml_n=0
    for _ml_p in $(osr_mozilla_profiles "$_ml_root"); do
        _ml_n=$((_ml_n + 1))
        if [ -n "$_ml_js" ] && [ -f "$_ml_js" ]; then
            install_layer "$_ml_js" "$_ml_p/user.js"
        fi
        if [ -n "$_ml_css" ] && [ -f "$_ml_css" ]; then
            as_user mkdir -p "$_ml_p/chrome"
            install_layer "$_ml_css" "$_ml_p/chrome/userChrome.css"
        fi
    done
    if [ "$_ml_n" -eq 0 ]; then
        warn "no profile under $_ml_root yet - launch the app once, then rerun this module"
    else
        info "applied Mozilla layer to $_ml_n profile(s) under $_ml_root"
    fi
    return 0
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

# _osr_foot_knows_theme_sections — true when the installed foot understands the
# `[colors-dark]` palette section (foot >= 1.26). An absent or unparseable foot
# answers no, which is the safe direction: `[colors]` is accepted by every foot
# version, `[colors-dark]` only by new ones.
_osr_foot_knows_theme_sections() {
    command -v foot >/dev/null 2>&1 || return 1
    _fk_ver=$(foot --version 2>/dev/null | sed -n 's/^foot version: \([0-9][0-9.]*\).*/\1/p')
    [ -n "$_fk_ver" ] || return 1
    _fk_maj=${_fk_ver%%.*}
    _fk_min=${_fk_ver#"$_fk_maj"}; _fk_min=${_fk_min#.}; _fk_min=${_fk_min%%.*}
    [ -n "$_fk_min" ] || _fk_min=0
    case "$_fk_maj$_fk_min" in *[!0-9]*) return 1 ;; esac
    [ "$_fk_maj" -gt 1 ] && return 0
    [ "$_fk_maj" -eq 1 ] && [ "$_fk_min" -ge 26 ]
}

# install_foot_palette <src> <dst> — install a foot palette layer, adapting the
# section name to the installed foot (§9: degrade, never break the terminal).
#
# foot 1.26 renamed the palette sections (`[colors]` -> `[colors-dark]`,
# `[colors2]` -> `[colors-light]`). The old names still work but log a
# deprecation warning on every start; older foot, meanwhile, rejects
# `[colors-dark]` as an invalid section name and refuses to start at all. Across
# distros both versions are in the wild, so the palettes are written with the
# current name and downgraded here for a foot that predates it.
install_foot_palette() {
    _fp_src=$1
    _fp_dst=$2
    if _osr_foot_knows_theme_sections; then
        backup_copy "$_fp_src" "$_fp_dst"
        return 0
    fi
    _fp_tmp="${TMPDIR:-/tmp}/osr-foot-colors-$$.ini"
    sed 's/^\[colors-dark\]$/[colors]/; s/^\[colors-light\]$/[colors2]/' "$_fp_src" >"$_fp_tmp"
    backup_copy "$_fp_tmp" "$_fp_dst"
    rm -f "$_fp_tmp"
}

# _osr_alacritty_ver — echo "<major> <minor>" of the installed Alacritty, or ""
# when there is none on PATH / the version line is unparseable.
_osr_alacritty_ver() {
    command -v alacritty >/dev/null 2>&1 || return 0
    alacritty --version 2>/dev/null \
        | sed -n 's/^alacritty \([0-9][0-9]*\)\.\([0-9][0-9]*\).*/\1 \2/p' | head -n 1
}

# install_alacritty_config <src> <dst> — install the Alacritty base config,
# adapting it to the installed Alacritty (§9: degrade, never break the terminal).
#
# Alacritty 0.14 moved `import` (with live_config_reload, working_directory and
# ipc_socket) into a new `[general]` section. Older TOML-era builds - 0.13, still
# what Debian bookworm and Ubuntu jammy carry - reject the section outright and
# fall back to their defaults, losing the rice palette with it. The config is
# written in the current shape and downgraded here for an Alacritty that predates
# it: deleting the `[general]` header alone turns `import` back into the
# top-level key 0.13 expects, which is why that section holds `import` and
# nothing else.
#
# Below 0.13 the config format was YAML (alacritty.yml) and this file is ignored
# entirely - warn rather than pretend it landed.
install_alacritty_config() {
    _aa_src=$1
    _aa_dst=$2
    _aa_ver=$(_osr_alacritty_ver)
    _aa_maj=${_aa_ver%% *}
    _aa_min=${_aa_ver##* }
    if [ -z "$_aa_ver" ]; then
        # No parseable version (not installed yet, or a future scheme): assume
        # current. A stray `[general]` only costs the palette on 0.13; guessing
        # "old" would cost it on every modern build instead.
        backup_copy "$_aa_src" "$_aa_dst"
        return 0
    fi
    if [ "$_aa_maj" -eq 0 ] && [ "$_aa_min" -lt 13 ]; then
        warn "alacritty $_aa_maj.$_aa_min predates the TOML config (0.13) - it reads alacritty.yml and will ignore $_aa_dst"
    fi
    if [ "$_aa_maj" -gt 0 ] || [ "$_aa_min" -ge 14 ]; then
        backup_copy "$_aa_src" "$_aa_dst"
        return 0
    fi
    _aa_tmp="${TMPDIR:-/tmp}/osr-alacritty-$$.toml"
    sed '/^\[general\]$/d' "$_aa_src" >"$_aa_tmp"
    backup_copy "$_aa_tmp" "$_aa_dst"
    rm -f "$_aa_tmp"
}

# apply_config <name> — copy a theme-owned config dir (themes/<theme>/config/<name>)
# into ~/.config/<name>, backing up once. Used by the theme.list `config:`
# directive for whole-dir DE configs (§5) that no module owns. Falls back
# gracefully if the dir is absent.
apply_config() {
    _ac_name=$1
    _ac_src="$OSR_THEME_DIR/config/$_ac_name"
    if [ ! -d "$_ac_src" ]; then
        warn "config '$_ac_name' not found in theme ($_ac_src) - skipping"
        return 0
    fi
    _ac_dst="$OSR_HOME/.config/$_ac_name"
    info "applying config: $_ac_name -> $_ac_dst"
    as_user mkdir -p "$_ac_dst"
    # copy contents (trailing /.) so the dir itself is not nested
    as_user cp -rf "$_ac_src/." "$_ac_dst/"
}

# --- wallpaper: one resolution, one installed copy (§6) ----------------------
#
# Four consumers want the same answer: hyprpaper's `preload =`, hyprland's
# `env = WALLPAPER_PATH`, gtklock's style.css background, and apply_wallpaper's
# recorded state. The legacy bundle let each hard-code or sed its own path (and
# the hyprland/hyprpaper pair silently disagreed, so `$WALLPAPER_PATH` pointed at
# a file nothing had copied). Resolve it once here, install it once into a
# user-owned dir, and hand every consumer the same absolute path.

# osr_is_image <path> — true for a file with an image extension.
# Extension-filtered on purpose: several themes carry a `wallpapers/*.txt`
# "drop a real image here" placeholder, and that must never resolve to a
# wallpaper - painting a text file is worse than painting nothing.
osr_is_image() {
    [ -f "$1" ] || return 1
    case "$1" in
        *.jpg|*.jpeg|*.png|*.webp|*.bmp|*.gif|*.JPG|*.JPEG|*.PNG|*.WEBP) return 0 ;;
        *) return 1 ;;
    esac
}

# osr_theme_wallpapers [theme-dir] — echo every image a theme ships, one per
# line, in glob (lexical) order. A theme may carry several: the first is its
# default, and the rest are what the picker scrolls through.
osr_theme_wallpapers() {
    _tw_dir=${1:-${OSR_THEME_DIR:-}}
    [ -n "$_tw_dir" ] || return 0
    for _tw_f in "$_tw_dir"/wallpapers/*; do
        osr_is_image "$_tw_f" || continue
        printf '%s\n' "$_tw_f"
    done
}

# osr_theme_wallpaper — echo the wallpaper to use for the current theme, "" when
# there is none. The user's per-theme choice (state: wallpaper.<theme>) wins over
# the theme's default, which is what makes a picked wallpaper survive switching
# away to another theme and back.
osr_theme_wallpaper() {
    [ -n "${OSR_THEME_DIR:-}" ] || return 0
    if [ -n "${OSR_THEME:-}" ]; then
        _rw_pick=$(osr_state_get "wallpaper.$OSR_THEME" 2>/dev/null || true)
        # A recorded choice that no longer exists (image deleted, checkout moved)
        # falls back to the theme default rather than failing the apply.
        if [ -n "$_rw_pick" ] && osr_is_image "$_rw_pick"; then
            printf '%s' "$_rw_pick"
            return 0
        fi
    fi
    osr_theme_wallpapers | head -n 1 | tr -d '\n'
}

# osr_install_wallpaper — copy the theme wallpaper to ~/Pictures/Wallpapers and
# echo the installed path ("" when the theme ships none). The installed copy, not
# the path inside the repo, is what the configs point at: the wallpaper then
# survives moving or deleting the dotfiles checkout. Rerun-safe (§2) - an
# identical file is left alone.
osr_install_wallpaper() {
    _iw_src=$(osr_theme_wallpaper)
    [ -n "$_iw_src" ] || return 0
    osr_install_wallpaper_file "$_iw_src"
}

# osr_install_wallpaper_file <src> — copy one image into the user's Wallpapers
# dir and echo where it landed.
osr_install_wallpaper_file() {
    _iw_src=$1
    [ -n "$_iw_src" ] || return 0
    _iw_dir="$OSR_HOME/Pictures/Wallpapers"
    _iw_dst="$_iw_dir/$(basename "$_iw_src")"
    if [ -f "$_iw_dst" ] && command -v cmp >/dev/null 2>&1 && cmp -s "$_iw_src" "$_iw_dst"; then
        printf '%s' "$_iw_dst"
        return 0
    fi
    as_user mkdir -p "$_iw_dir"
    as_user cp -f "$_iw_src" "$_iw_dst"
    printf '%s' "$_iw_dst"
}

# install_wallpaper_layer <src> <dst> — install a theme-owned config layer that
# carries the `{{WALLPAPER_PATH}}` placeholder, substituting the installed
# wallpaper path. Same overwrite-on-update semantics as install_layer; the
# placeholder is what keeps the path out of the theme's config files, so a theme
# with a different image needs no module change. A theme with no wallpaper
# substitutes empty - the config still lands, it just paints nothing.
install_wallpaper_layer() {
    _wl_src=$1
    _wl_dst=$2
    _wl_wp=$(osr_install_wallpaper)
    _wl_tmp="${TMPDIR:-/tmp}/osr-wallpaper-layer-$$"
    sed "s#{{WALLPAPER_PATH}}#${_wl_wp}#g" "$_wl_src" >"$_wl_tmp"
    backup_copy "$_wl_tmp" "$_wl_dst"
    rm -f "$_wl_tmp"
}

# osr_wallpaper_set_live <path> — hand the image to whichever setter this
# session has. Best-effort by design: on a headless box (container, ssh, CI)
# there is nothing to paint, and that is not a failure (§9).
osr_wallpaper_set_live() {
    _wl_img=$1
    if command -v swww >/dev/null 2>&1; then
        as_user swww img "$_wl_img" >/dev/null 2>&1 || warn "swww failed to set wallpaper"
    elif command -v hyprctl >/dev/null 2>&1; then
        as_user hyprctl hyprpaper wallpaper ",$_wl_img" >/dev/null 2>&1 || warn "hyprpaper failed"
    elif command -v feh >/dev/null 2>&1; then
        as_user feh --bg-scale "$_wl_img" >/dev/null 2>&1 || warn "feh failed to set wallpaper"
    else
        info "no wallpaper setter (headless) - recorded $_wl_img"
    fi
    return 0
}

# osr_wallpaper_record <path> — the one place the applied wallpaper is written
# down. ~/.config/osr/wallpaper is a bare path on a line because non-shell
# consumers read it (a lock screen, a bar, Proteus); the state file carries the
# same value keyed by theme so a per-theme choice survives switching away.
osr_wallpaper_record() {
    _wr_img=$1
    as_user mkdir -p "$OSR_HOME/.config/osr"
    printf '%s\n' "$_wr_img" | as_user tee "$OSR_HOME/.config/osr/wallpaper" >/dev/null
    if command -v osr_state_set >/dev/null 2>&1; then
        osr_state_set wallpaper "$_wr_img"
    fi
}

# apply_wallpaper — install the theme's wallpaper, record it as theme-owned state,
# and set it if a compositor/setter exists. Degrades to record-only when headless
# (containers, no DE) so it never fails a run (§6, §9).
apply_wallpaper() {
    _wp=$(osr_install_wallpaper)
    [ -n "$_wp" ] || return 0
    osr_wallpaper_record "$_wp"
    osr_wallpaper_set_live "$_wp"
}

# osr_wallpaper_library — every image the user can choose between: the current
# theme's own, then whatever is already in ~/Pictures/Wallpapers (which is where
# osr_install_wallpaper puts every image it has ever applied, so this accretes
# into a library across themes). Deduplicated by basename, theme first.
osr_wallpaper_library() {
    _wlib_seen=""
    for _wlib_f in $(osr_theme_wallpapers); do
        _wlib_b=$(basename "$_wlib_f")
        _wlib_seen="$_wlib_seen|$_wlib_b"
        printf '%s\n' "$_wlib_f"
    done
    for _wlib_f in "${OSR_HOME:-$HOME}"/Pictures/Wallpapers/*; do
        osr_is_image "$_wlib_f" || continue
        _wlib_b=$(basename "$_wlib_f")
        case "$_wlib_seen" in *"|$_wlib_b"*) continue ;; esac
        _wlib_seen="$_wlib_seen|$_wlib_b"
        printf '%s\n' "$_wlib_f"
    done
}

# osr_choose_wallpaper <path> — make <path> the wallpaper of the current theme:
# remember the choice, install a copy into the library, paint it.
#
# The choice is keyed by theme on purpose. A wallpaper is part of how a theme
# looks, so switching nord -> gruvbox -> nord must bring back the image that was
# picked for nord, not the last one seen.
osr_choose_wallpaper() {
    _cw_src=$1
    osr_is_image "$_cw_src" || error "not an image: $_cw_src"
    case "$_cw_src" in
        /*) ;;
        *) _cw_src="$(cd -- "$(dirname -- "$_cw_src")" && pwd)/$(basename "$_cw_src")" ;;
    esac
    if [ -n "${OSR_THEME:-}" ] && command -v osr_state_set >/dev/null 2>&1; then
        osr_state_set "wallpaper.$OSR_THEME" "$_cw_src"
    fi
    _cw_installed=$(osr_install_wallpaper_file "$_cw_src")
    # This function's stdout IS its return value (the installed path), so the
    # setter's logging goes to stderr. `info` writes to stdout everywhere else
    # in the codebase, and a "no wallpaper setter (headless)" line landing in a
    # caller's `$( )` would silently corrupt the path it just asked for.
    {
        osr_wallpaper_record "$_cw_installed"
        osr_wallpaper_set_live "$_cw_installed"
    } >&2
    printf '%s' "$_cw_installed"
}
