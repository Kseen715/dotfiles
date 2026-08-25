# ~/.config/xprofile.d/10-session.sh — dotfiles-owned session env (os-rice §5).
# Sourced by ~/.xprofile, which every display manager and ~/.xinitrc reads, so
# these reach every GUI app the WM starts. Overwritten on update; put machine
# env in 00-env.sh and overrides in 99-local.sh — os-rice never rewrites those.
#
# Rice-owned toolkit theme vars (GTK_THEME, XCURSOR_THEME, ...) live in
# 90-theme.sh and are swapped on a rice switch (§6).

# PATH. This is not redundant with the zsh layers, and assuming it was is a
# whole class of silently-dead keybinding: i3 execs through /bin/sh, which reads
# no zshrc, so a binding gets the PATH lightdm handed the session and nothing
# else. Anything installed by `cargo install --path` or lib/build.sh lands in
# ~/.local/bin, which is NOT on that PATH - so `bindsym $mod+Shift+d exec
# proteus` resolved to nothing at all, and looked exactly like a broken binding
# rather than a missing directory.
#
# Same reasoning for .desktop files and rofi: neither goes through a login
# shell either. Guarded so re-sourcing ~/.xprofile cannot stack duplicates.
case ":$PATH:" in
    *":$HOME/.local/bin:"*) ;;
    *) export PATH="$HOME/.local/bin:$PATH" ;;
esac

# Portals and GTK's prefers-color-scheme both key off these two. Getting
# XDG_CURRENT_DESKTOP wrong is why a Flatpak file dialog opens blank.
export XDG_CURRENT_DESKTOP=i3
export XDG_SESSION_TYPE=x11
export XDG_SESSION_DESKTOP=i3

# Qt: without a platform theme, Qt apps ignore your theme and look like 2009.
# `qt5ct`, not `qt6ct`, because one variable has to serve both Qt versions: the
# qt6ct plugin registers BOTH keys (qt6ct, qt5ct) and still reads its own
# ~/.config/qt6ct, while the Qt5 plugin answers to `qt5ct` only. Setting `qt6ct`
# leaves every Qt5 app (VLC's interface among them) on the default light palette
# — dark toolbar, white menus.
export QT_QPA_PLATFORMTHEME=qt5ct
export QT_AUTO_SCREEN_SCALE_FACTOR=1

# Java/Swing (JetBrains, MATLAB) renders a grey rectangle under any
# non-reparenting WM without this. The second line fixes its font rendering.
export _JAVA_AWT_WM_NONREPARENTING=1
export _JAVA_OPTIONS='-Dawt.useSystemAAFontSettings=on -Dswing.aatext=true'

# Input method (modules/fcitx5.sh). These three are the whole integration: a
# toolkit with no *_IM_MODULE falls back to raw XIM and shows no candidate
# window, which reads as "fcitx5 is broken" when it is running perfectly.
export GTK_IM_MODULE=fcitx
export QT_IM_MODULE=fcitx
export XMODIFIERS=@im=fcitx

# Firefox pixel-precise scrolling; LibreOffice looks native under GTK.
export MOZ_USE_XINPUT2=1
export SAL_USE_VCLPLUGIN=gtk3

# Where downloads and screenshots go is decided by xdg-user-dirs (modules/xdg.sh).
export EDITOR=micro
export VISUAL=micro
export BROWSER=firefox
export TERMINAL=ghostty
