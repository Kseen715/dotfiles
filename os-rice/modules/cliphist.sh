# session: wayland
# modules/cliphist.sh — cliphist clipboard history + wofi image preview helper.
# ONE copy, POSIX (was .../modules/cliphist.sh). ripgrep backs the search; the
# wofi image thumbnailer is a small upstream script fetched to /usr/local/bin.
# go is a build prerequisite for cliphist-wofi-img — installed on demand (§4:
# order is the dependency graph, but this module self-heals if go is absent).
run_step "Installing cliphist" pkg_install cliphist ripgrep

if [ -n "$OSR_THEME_DIR" ] && [ -f "$OSR_THEME_DIR/config/hypr/start-cliphist-store.sh" ]; then
    install_layer "$OSR_THEME_DIR/config/hypr/start-cliphist-store.sh" \
        "$OSR_HOME/.config/hypr/start-cliphist-store.sh"
    as_user chmod +x "$OSR_HOME/.config/hypr/start-cliphist-store.sh"
fi

command -v go >/dev/null 2>&1 || pkg_install go
run_step "Installing cliphist-wofi-img (go)" \
    as_user go install github.com/pdf/cliphist-wofi-img@latest

# Upstream wofi image-preview shim to /usr/local/bin (system path -> as_root).
if [ ! -x /usr/local/bin/cliphist-wofi-img ]; then
    _cw="${TMPDIR:-/tmp}/cliphist-wofi-img"
    if osr_download "https://raw.githubusercontent.com/sentriz/cliphist/refs/heads/master/contrib/cliphist-wofi-img" "$_cw"; then
        as_root install -m 0755 "$_cw" /usr/local/bin/cliphist-wofi-img
        rm -f "$_cw"
    else
        warn "failed to fetch cliphist-wofi-img shim - skipping"
    fi
fi

as_user mkdir -p "$OSR_HOME/.cache/cliphist/thumbs"
