# session: x11
# themable: yes
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/picom.sh — X11 compositor (i3-sugg §1.3). Mandatory, not cosmetic:
# without a compositor you get tearing, no transparency, Electron/Chromium
# flicker and broken shadows.
#
# Config split (§5): the dotfiles base carries behaviour (backend, vsync, blur
# method, exclusion lists) and ends with `@include "90-theme.conf"`, which the
# rice owns — corner radius, opacity, shadow color. picom resolves @include
# relative to the including file, so both live in ~/.config/picom/.

run_step "Installing picom" pkg_install picom

if [ -f "$OSR_DOTFILES/picom/picom.conf" ]; then
    install_layer "$OSR_DOTFILES/picom/picom.conf" "$OSR_HOME/.config/picom/picom.conf"
fi

if [ -n "${OSR_THEME_DIR:-}" ] && [ -f "$OSR_THEME_DIR/config/picom/90-theme.conf" ]; then
    install_layer "$OSR_THEME_DIR/config/picom/90-theme.conf" "$OSR_HOME/.config/picom/90-theme.conf"
else
    # The base @includes it unconditionally, so a rice that ships no picom theme
    # must still leave a readable file behind or picom refuses to start.
    seed_empty "$OSR_HOME/.config/picom/90-theme.conf"
fi
