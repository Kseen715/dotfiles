# test/ref/zip_sh_ref.sh — the sh implementation of modules/zip.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/zip.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# modules/zip.sh — zip + unzip archivers. ONE copy, POSIX (was .../modules/zip.sh).
run_step "Installing zip and unzip" pkg_install zip unzip
