# test/ref/telegram_sh_ref.sh — the sh implementation of modules/telegram.sh, FROZEN.
#
# The last pure-sh version, kept as the specification of what the C module
# (modules/telegram.c) must do: test/unit/module_c_parity.sh runs both under
# stubbed package tooling and diffs what they did. Nothing installs it.
#
# --- original -----------------------------------------------------------------
#
# session: x11+wayland
# themable: yes
# modules/telegram.sh — Telegram Desktop from the vendor tarball
# (telegram.org/desktop), plus the theme palette.
#
# The tarball and not the distro package: the packaged builds lag, and Telegram
# ships its own updater that keeps the unpacked tree current on its own (see
# provide_telegram, which is why the tree is user-owned). webkit2gtk is the
# system runtime its in-app browser view uses and is still a native package.
#
# provide_telegram is called directly rather than through `pkg_install telegram`
# for the same reason as datagrip: the source: provider's probe is presence-based
# (§4), so a rice would install it once and never update it, while the builder
# compares versions - which makes `osr module telegram` the repair path.
run_step "Installing Telegram" provide_telegram
run_step "Installing the Telegram webview runtime" pkg_install webkit2gtk-4.1
as_user mkdir -p "$OSR_HOME/.local/share/TelegramDesktop"

# Telegram paints its own widgets, so the GTK and Qt layers do nothing to it -
# its only theming input is a .tdesktop-palette (§6b, telegram/*.tmpl).
if install_theme_layer telegram os-rice.tdesktop-palette \
    "$OSR_HOME/.local/share/TelegramDesktop/os-rice.tdesktop-palette"; then
    # Applying it is a click, and that is Telegram's limitation: the selected
    # theme lives in the encrypted tdata blob, so nothing outside the app can
    # select it. Not a command-line argument either - a file path on the command
    # line goes to handleStartFiles, i.e. the "send this file" flow.
    #
    # The click only has to happen ONCE, though: a theme applied from a real file
    # path is watched (ChatBackground::refreshThemeWatcher), and Telegram
    # re-applies it by itself whenever that file changes. install_layer rewrites
    # this path in place with cp, so a later `osr theme <name>` re-themes a
    # RUNNING Telegram with no interaction at all.
    _tg_palette="$OSR_HOME/.local/share/TelegramDesktop/os-rice.tdesktop-palette"
    info "Telegram theme written to $_tg_palette"
    info "apply it once inside Telegram: Settings > Chat Settings > Choose from file, and pick that file (or send it to Saved Messages, click it, 'Apply This Theme')"
    info "after that Telegram watches the file - every later 'osr theme' re-themes it live"
fi
