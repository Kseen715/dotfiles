# test/ref/mako_sh_ref.sh — the sh implementation of modules/mako.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/mako.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: wayland
# themable: yes
# modules/mako.sh — mako notification daemon + rice-owned config. ONE copy, POSIX
# (was .../modules/mako.sh).
run_step "Installing mako" pkg_install mako
install_theme_layer mako config "$OSR_HOME/.config/mako/config" || :
