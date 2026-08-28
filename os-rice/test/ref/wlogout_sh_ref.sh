# test/ref/wlogout_sh_ref.sh — the sh implementation of modules/wlogout.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/wlogout.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: wayland
# themable: yes
# modules/wlogout.sh — wlogout logout menu (AUR) + rice-owned config dir. ONE
# copy, POSIX (was .../modules/wlogout.sh). Not in the default rice.list (wleave
# is used), kept as an available alternative module.
run_step "Installing wlogout (AUR)" pkg_install wlogout
if [ -n "$OSR_THEME_DIR" ]; then apply_config wlogout; fi
