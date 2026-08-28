# test/ref/ncdu_sh_ref.sh — the sh implementation of modules/ncdu.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/ncdu.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
run_step "Installing ncdu" pkg_install ncdu
