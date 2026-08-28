# test/ref/loupe_sh_ref.sh — the sh implementation of modules/loupe.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/loupe.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/loupe.sh — Loupe image viewer. ONE copy, POSIX
# (was .../modules/loupe.sh). Native, no config. Available module.
run_step "Installing Loupe" pkg_install loupe
