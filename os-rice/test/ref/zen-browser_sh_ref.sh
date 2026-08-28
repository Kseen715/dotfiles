# test/ref/zen-browser_sh_ref.sh — the sh implementation of modules/zen-browser.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/zen-browser.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# themable: yes
# modules/zen-browser.sh — Zen Browser (AUR). ONE copy, POSIX
# (was .../apps/zen-browser.sh). Available module.
run_step "Installing Zen Browser (AUR)" pkg_install zen-browser

# Chrome colors: Zen is a Firefox fork, so it takes the same userChrome.css the
# firefox module installs - one template, both browsers (§6b). Profiles live
# under ~/.zen rather than ~/.mozilla/firefox; install_mozilla_layer resolves
# either from profiles.ini.
_zen_css=$(osr_theme_source firefox userChrome.css) || _zen_css=""
if [ -n "$_zen_css" ]; then
    install_mozilla_layer "$OSR_HOME/.zen" "" "$_zen_css"
    case "$_zen_css" in "${TMPDIR:-/tmp}"/osr-theme-*) rm -f "$_zen_css" ;; esac
fi
