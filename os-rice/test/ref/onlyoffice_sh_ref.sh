# test/ref/onlyoffice_sh_ref.sh — the sh implementation of modules/onlyoffice.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/onlyoffice.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/onlyoffice.sh — ONLYOFFICE Desktop Editors (AUR). ONE copy, POSIX
# (was .../apps/onlyoffice.sh). Available module (not in default rice.list).
run_step "Installing ONLYOFFICE (AUR)" pkg_install onlyoffice
