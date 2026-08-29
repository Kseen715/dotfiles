# test/ref/firefox_sh_ref.sh — the sh implementation of modules/firefox.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/firefox.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# themable: yes
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

# Where the profile actually is. There is no single answer any more, and every
# wrong guess has the same symptom: the module reports success and Firefox is
# untouched, because the layer landed in a directory the browser never reads.
#
#   ~/.mozilla/firefox            the classic root. Firefox still prefers it
#                                 when it exists, so it stays first.
#   ~/.config/mozilla/firefox     XDG base directories, which Firefox honours by
#                                 default as of 154. A machine that first ran
#                                 Firefox on 154+ has ONLY this one - there is no
#                                 ~/.mozilla at all. Spelled from OSR_HOME and
#                                 not from $XDG_CONFIG_HOME on purpose: this
#                                 module runs as root, where that variable
#                                 points at /root.
#   ~/snap/... and ~/.var/app/... a sandboxed build keeps its profile inside the
#                                 sandbox and leaves every classic root empty.
#                                 Ubuntu's `firefox` deb is a snap stub, and
#                                 fighting the package is not worth it when
#                                 following the profile costs one line.
#
# The order below is Firefox's own resolution order, so os-rice writes into the
# profile the browser will read rather than into the one it would have made.
_ff_root="$OSR_HOME/.mozilla/firefox"
if [ ! -d "$_ff_root" ]; then
    for _ff_alt in \
        "$OSR_HOME/.config/mozilla/firefox" \
        "$OSR_HOME/snap/firefox/common/.mozilla/firefox" \
        "$OSR_HOME/.var/app/org.mozilla.firefox/.mozilla/firefox"
    do
        [ -d "$_ff_alt" ] || continue
        _ff_root="$_ff_alt"
        info "profile root is $_ff_root (not the classic ~/.mozilla/firefox)"
        break
    done
fi
_ff_js=""
_ff_css=""
[ -f "$OSR_DOTFILES/firefox/user.js" ] && _ff_js="$OSR_DOTFILES/firefox/user.js"
_ff_css=$(osr_theme_source firefox userChrome.css) || _ff_css=""

# Say so when the theme half resolved to nothing. Without this the module still
# reports success, installs user.js, and leaves a Firefox that is half-themed -
# the prefs applied, the colors not - with no line anywhere naming the reason.
[ -n "$_ff_css" ] || warn "no Firefox theme layer: neither themes/${OSR_THEME:-?}/config/firefox/userChrome.css nor a rendered firefox/userChrome.css.tmpl - Firefox keeps its default chrome"

# A machine that has never launched Firefox has no profile directory, and
# install_mozilla_layer can only warn and return — which is why a fresh rice
# install ends with an unstyled, default-light Firefox and no obvious reason
# why. Create the profile instead of waiting for the user to: -CreateProfile is
# headless, takes under a second, and writes the profiles.ini that the browser
# then adopts on its first real start. Do it BEFORE resolving the layer paths so
# the same run installs into it.
if [ -z "$(osr_mozilla_profiles "$_ff_root")" ] && command -v firefox >/dev/null 2>&1; then
    run_step "Firefox: creating the initial profile (none exists yet)" \
        as_user sh -c 'firefox -CreateProfile default-release >/dev/null 2>&1 || true'
fi

if [ -n "$_ff_js" ] || [ -n "$_ff_css" ]; then
    install_mozilla_layer "$_ff_root" "$_ff_js" "$_ff_css"
fi

# Verify rather than assume. userChrome.css is the one layer in this module with
# no visible failure mode of its own: Firefox reads it silently or ignores it
# silently, so the only place the truth can be told is here, right after writing
# it. Checked per profile, because a machine with two profiles and one styled is
# exactly the case that reads as "the theme is broken".
if [ -n "$_ff_css" ]; then
    for _ff_p in $(osr_mozilla_profiles "$_ff_root"); do
        [ -s "$_ff_p/chrome/userChrome.css" ] && continue
        warn "userChrome.css did not land in $_ff_p - Firefox there stays unstyled"
    done
fi
