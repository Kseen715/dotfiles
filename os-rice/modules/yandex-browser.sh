# session: x11+wayland
# modules/yandex-browser.sh — Yandex Browser + a low-RAM flags layer. ONE copy,
# POSIX (was .../apps/yandex-browser.sh). Available module.
#
# Install route by target (§1, all of it in pkgmap):
#   pacman  aur:yandex-browser
#   apt     source:provide_yandex_browser — the vendor's own apt repo, which no
#           Debian/Ubuntu archive carries (yandex.ru/support/browser install docs)
# Anything else has no package and fails loudly rather than installing a
# lookalike, same convention as vscode on apt.
#
# Config split (§5). Yandex Browser is Chromium, so there is no user.js to write:
# the memory knobs are command-line switches, and the only place a switch can be
# attached without touching /usr is the .desktop entry. dotfiles owns the switch
# list (yandex-browser/flags.conf) and this module stamps it into a user-level
# copy of each launcher in ~/.local/share/applications, which XDG resolves before
# the packaged one. No rice layer: Chromium takes no user stylesheet.
#
# Consequence worth knowing: the flags reach the browser when it is started from
# the menu/rofi/a mailto handler, i.e. every normal launch. Typing
# `yandex-browser` in a terminal bypasses the .desktop entry and gets defaults.

run_step "Installing Yandex Browser" pkg_install yandex-browser

_yb_flags_file="$OSR_DOTFILES/yandex-browser/flags.conf"
if [ -f "$_yb_flags_file" ]; then
    # One switch per line, # comments — flatten to a single argument string.
    _yb_flags=$(sed 's/#.*$//' "$_yb_flags_file" | tr '\n' ' ' | tr -s ' ' | sed 's/^ *//; s/ *$//')

    # ponytail: a variable only so the unit test can aim at a fixture dir.
    _yb_appdirs=${OSR_DESKTOP_DIRS:-"/usr/share/applications /usr/local/share/applications"}
    _yb_apps="$OSR_HOME/.local/share/applications"
    as_user mkdir -p "$_yb_apps"
    _yb_done=""
    for _yb_dir in $_yb_appdirs; do
        # Two entries ship with the deb under different names — the reverse-DNS
        # ru.yandex.desktop.browser.desktop and the plain yandex-browser.desktop
        # (the one that is the http handler). Both exec the same binary and both
        # get the flags: which one a launcher picks is not ours to predict.
        for _yb_src in "$_yb_dir"/*yandex*browser*.desktop; do
            [ -f "$_yb_src" ] || continue
            info "installing low-RAM launcher: $(basename "$_yb_src")"
            # Insert the switches straight after the binary in every Exec= line
            # ([Desktop Action] entries included) and before the %U field code:
            # positional arguments after it are URLs, not switches.
            sed "s|^Exec=\([^ ]*\)|Exec=\1 $_yb_flags|" "$_yb_src" \
                | as_user tee "$_yb_apps/$(basename "$_yb_src")" >/dev/null
            _yb_done=1
        done
    done
    # The browser is installed either way, but without a launcher nothing carries
    # the flags — say so instead of leaving the tuning silently unapplied.
    [ -n "$_yb_done" ] \
        || warn "no yandex-browser .desktop found - the low-RAM flags are not applied (rerun this module after the install)"
    command -v update-desktop-database >/dev/null 2>&1 \
        && as_user update-desktop-database "$_yb_apps" >/dev/null 2>&1 || :
fi
