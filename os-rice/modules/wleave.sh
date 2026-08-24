# session: wayland
# themable: yes
# legacy: sh  — port to C (modules/<name>.c + lib/modules.c); see DESIGN §11a
# modules/wleave.sh — wleave logout menu (AUR) + rice-owned config dir. ONE copy,
# POSIX (was .../modules/wleave.sh). scdoc is a native build/man dep. The config
# dir (layout, style.css, icons) is rice-owned (§6), copied whole.
run_step "Installing wleave (AUR)" pkg_install wleave scdoc
if [ -n "$OSR_THEME_DIR" ]; then apply_config wleave; fi
