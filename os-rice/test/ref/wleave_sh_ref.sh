# test/ref/wleave_sh_ref.sh — the sh implementation of modules/wleave.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/wleave.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: wayland
# themable: yes
# modules/wleave.sh — wleave logout menu (AUR) + rice-owned config dir. ONE copy,
# POSIX (was .../modules/wleave.sh). scdoc is a native build/man dep. The config
# dir (layout, style.css, icons) is rice-owned (§6), copied whole.
run_step "Installing wleave (AUR)" pkg_install wleave scdoc
if [ -n "$OSR_THEME_DIR" ]; then apply_config wleave; fi
