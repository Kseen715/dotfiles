# session: x11+wayland
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
