# session: wayland
# modules/wlogout.sh — wlogout logout menu (AUR) + rice-owned config dir. ONE
# copy, POSIX (was .../modules/wlogout.sh). Not in the default rice.list (wleave
# is used), kept as an available alternative module.
run_step "Installing wlogout (AUR)" pkg_install wlogout
if [ -n "$OSR_RICE_DIR" ]; then apply_config wlogout; fi
