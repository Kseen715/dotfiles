# ~/.config/xprofile.d/10-session.sh — dotfiles-owned session env (os-rice §5).
# Sourced by ~/.xprofile, which every display manager and ~/.xinitrc reads, so
# these reach every GUI app the WM starts. Overwritten on update; put machine
# env in 00-env.sh and overrides in 99-local.sh — os-rice never rewrites those.
#
# Rice-owned toolkit theme vars (GTK_THEME, XCURSOR_THEME, ...) live in
# 90-theme.sh and are swapped on a rice switch (§6).

# Portals and GTK's prefers-color-scheme both key off these two. Getting
# XDG_CURRENT_DESKTOP wrong is why a Flatpak file dialog opens blank.
export XDG_CURRENT_DESKTOP=i3
export XDG_SESSION_TYPE=x11
export XDG_SESSION_DESKTOP=i3

# Qt: without a platform theme, Qt apps ignore your theme and look like 2009.
export QT_QPA_PLATFORMTHEME=qt6ct
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
