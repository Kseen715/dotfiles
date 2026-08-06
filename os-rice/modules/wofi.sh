# session: wayland
# modules/wofi.sh — wofi application launcher + rice-owned config. ONE copy,
# POSIX (was .../modules/wofi.sh).
run_step "Installing wofi" pkg_install wofi
if [ -n "$OSR_RICE_DIR" ]; then apply_config wofi; fi
