# session: x11+wayland
# modules/thunderbird.sh — mail/calendar. Same Mozilla profile machinery as
# modules/firefox.sh: a dotfiles-owned user.js and a rice-owned userChrome.css,
# installed into every profile under ~/.thunderbird (§5/§6).
#
# `evolution` is the packaged GTK alternative and `aerc`/`neomutt` the TUI ones
# (i3-sugg §9) — this module installs one mail client, not three.
#
# Note the profile root differs from Firefox's: ~/.thunderbird, not
# ~/.mozilla/thunderbird, on every current build.

run_step "Installing Thunderbird" pkg_install thunderbird

_tb_root="$OSR_HOME/.thunderbird"
_tb_js=""
_tb_css=""
[ -f "$OSR_DOTFILES/thunderbird/user.js" ] && _tb_js="$OSR_DOTFILES/thunderbird/user.js"
if [ -n "${OSR_RICE_DIR:-}" ] && [ -f "$OSR_RICE_DIR/config/thunderbird/userChrome.css" ]; then
    _tb_css="$OSR_RICE_DIR/config/thunderbird/userChrome.css"
fi

if [ -n "$_tb_js" ] || [ -n "$_tb_css" ]; then
    install_mozilla_layer "$_tb_root" "$_tb_js" "$_tb_css"
fi
