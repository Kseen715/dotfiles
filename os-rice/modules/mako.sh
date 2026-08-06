# session: wayland
# modules/mako.sh — mako notification daemon + rice-owned config. ONE copy, POSIX
# (was .../modules/mako.sh).
run_step "Installing mako" pkg_install mako
if [ -n "$OSR_RICE_DIR" ]; then apply_config mako; fi
