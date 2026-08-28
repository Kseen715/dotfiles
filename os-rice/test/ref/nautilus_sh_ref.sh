# test/ref/nautilus_sh_ref.sh — the sh implementation of modules/nautilus.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/nautilus.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/nautilus.sh — GNOME Files (Nautilus) file manager. ONE copy, POSIX
# (was .../modules/nautilus.sh). Native, no config.
run_step "Installing Nautilus" pkg_install nautilus
