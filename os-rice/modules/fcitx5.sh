# session: x11+wayland
# modules/fcitx5.sh — input method for CJK, Cyrillic and anything else that
# needs composition (i3-sugg §5). fcitx5 over ibus: lighter, better Wayland
# support, and its Qt/GTK bridges are separate packages you can pick.
#
# The three toolkit bridges are what make it actually work — without
# fcitx5-gtk/fcitx5-qt an app falls back to raw XIM and you get no candidate
# window. The env vars matter just as much and live in the session layer
# (~/.config/xprofile.d/10-session.sh): GTK_IM_MODULE, QT_IM_MODULE, XMODIFIERS.
#
# Engines are opt-in per language; the three below cover Japanese, Chinese and
# Korean. `ibus` (+ ibus-anthy) is the packaged alternative — never run both.

run_step "Installing fcitx5" pkg_install \
    fcitx5 fcitx5-gtk fcitx5-qt fcitx5-configtool

run_step "Installing fcitx5 engines" pkg_install \
    fcitx5-mozc fcitx5-chinese-addons fcitx5-hangul

# Emoji picking without an IME switch: rofimoji drives the rofi launcher this
# rice already ships, so Super+. gets emoji with no extra daemon.
run_step "Installing emoji picker" pkg_install rofimoji
