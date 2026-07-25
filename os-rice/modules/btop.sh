# modules/btop.sh — btop resource monitor + dotfiles config. ONE copy, POSIX,
# distro-agnostic (was linux-debian/modules/btop.sh). Native on every target
# except Debian 11 (bullseye), which gets the upstream static binary via a facet
# row in apt.map. btop.conf is dotfiles-owned config (§5), overwritten on update.
run_step "Installing btop" pkg_install btop
if [ -f "$OSR_DOTFILES/btop/btop.conf" ]; then
    install_layer "$OSR_DOTFILES/btop/btop.conf" "$OSR_HOME/.config/btop/btop.conf"
fi
