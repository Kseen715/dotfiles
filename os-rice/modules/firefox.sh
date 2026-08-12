# session: x11+wayland
# modules/firefox.sh — Firefox + a low-RAM prefs layer + the rice's colors.
#
# Config split (§5), realized through Mozilla's two profile-level hooks:
#
#   user.js                 dotfiles-owned (10) — the low-memory tuning below.
#                           Re-applied at every start, overwritten on update.
#   chrome/userChrome.css   rice-owned (90) — chrome colors, swapped on switch.
#
# Both land in EVERY profile (install_mozilla_layer walks profiles.ini), because
# a Mozilla profile directory has a random name and there is no fixed path to
# install into. A machine that has never launched Firefox has no profile yet —
# the helper says so and the module still succeeds; rerun after the first start.
#
# Why user.js and not prefs.js: prefs.js is rewritten by the browser on exit, so
# anything written there is lost. user.js is read-only input, applied on top.
#
# The low-RAM set targets the two things that actually dominate Firefox's RSS on
# a small machine: the number of content processes, and how many back/forward
# page states are kept alive in memory. See dotfiles/firefox/user.js.

run_step "Installing Firefox" pkg_install firefox

_ff_root="$OSR_HOME/.mozilla/firefox"
_ff_js=""
_ff_css=""
[ -f "$OSR_DOTFILES/firefox/user.js" ] && _ff_js="$OSR_DOTFILES/firefox/user.js"
_ff_css=$(osr_theme_source firefox userChrome.css) || _ff_css=""

if [ -n "$_ff_js" ] || [ -n "$_ff_css" ]; then
    install_mozilla_layer "$_ff_root" "$_ff_js" "$_ff_css"
fi
