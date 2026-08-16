# session: x11
# modules/rofi.sh — rofi launcher, the X11 replacement for wofi (i3-sugg §2).
# Also the app switcher, the emoji picker and the logout menu, so it replaces
# wleave/wlogout too — a rofi-modi script, not another package.
#
# Config split (§5): config.rasi + the launcher/powermenu layouts are
# dotfiles-owned; colors.rasi is rice-owned and `@import`ed by both, so a rice
# switch recolors every rofi surface at once.

run_step "Installing rofi" pkg_install rofi rofi-emoji rofi-calc

_ro="$OSR_HOME/.config/rofi"
as_user mkdir -p "$_ro"

for _f in config.rasi launcher.rasi powermenu.rasi; do
    [ -f "$OSR_DOTFILES/rofi/$_f" ] || continue
    install_layer "$OSR_DOTFILES/rofi/$_f" "$_ro/$_f"
done

install_theme_layer rofi colors.rasi "$_ro/colors.rasi" || :
