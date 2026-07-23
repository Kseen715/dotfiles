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

# apply_config <name> — copy a rice-owned config dir (rices/<rice>/config/<name>)
# into ~/.config/<name>, backing up once. Used by the manifest `config:`
# directive for DE configs (§5). Falls back gracefully if the dir is absent.
apply_config() {
    _ac_name=$1
    _ac_src="$OSR_RICE_DIR/config/$_ac_name"
    if [ ! -d "$_ac_src" ]; then
        warn "config '$_ac_name' not found in rice ($_ac_src) — skipping"
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
        info "no wallpaper setter (headless) — recorded $_wp"
    fi
}
