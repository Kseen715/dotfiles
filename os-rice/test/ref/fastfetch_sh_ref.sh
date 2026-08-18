# test/ref/fastfetch_sh_ref.sh — the sh implementation of modules/fastfetch.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/fastfetch.c) must do: test/unit/module_c_parity.sh runs both
# under stubbed package tooling and diffs what they did, the rendered config
# file included. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/fastfetch.sh — fastfetch system info tool + layered config. ONE copy,
# POSIX, distro-agnostic. "Easiest method per distro" is expressed entirely in
# the pkgmap: native package on arch/fedora/void/alpine/gentoo (bare
# passthrough), and the official prebuilt .deb on Debian/Ubuntu (apt.map ->
# provide_fastfetch_deb), where fastfetch is packaged natively only on very
# recent releases.
#
# Config split (§5): fastfetch reads exactly one config.jsonc, so the rice owns
# the whole installed file (it is nothing but presentation) while the dotfiles
# base is the fallback for a rice that ships none.
#
# https://github.com/fastfetch-cli/fastfetch

run_step "Installing fastfetch" pkg_install fastfetch

if install_theme_layer fastfetch config.jsonc "$OSR_HOME/.config/fastfetch/config.jsonc"; then
    :
elif [ -f "$OSR_DOTFILES/fastfetch/config.jsonc" ]; then
    install_layer "$OSR_DOTFILES/fastfetch/config.jsonc" \
        "$OSR_HOME/.config/fastfetch/config.jsonc"
fi
