---
title: dotfiles
type: index
updated: 2026-08-28
tags:
  - kind/index
  - topic/dotfiles
---

# dotfiles

Configs for apps, plus the machinery that installs them.

> [!important] Current direction
> os-rice's backbone is being rewritten from POSIX sh into C — one binary,
> `build/osr`, the same shape the Windows core already has. See
> [[os-rice/DESIGN#13. The port, and what is left of it|DESIGN 13]] for what is
> done and what is left.

## Start here

| note | what it is |
| --- | --- |
| [[os-rice/README\|os-rice]] | the installer: modules, rices, themes, `osr` |
| [[os-rice/DESIGN\|os-rice/DESIGN]] | why it is shaped that way — the design in force |
| [[PLAN_UNIVERSAL]] | the compiled C core: Windows today, XP and bare boards later |
| [[proteus/README\|Proteus]] | the GUI theme picker (Rust, X11 + Wayland) |
| [[archive-decisions]] | superseded decisions and deleted trees. History, not documentation |

## Rices

| rice | note |
| --- | --- |
| `arch-hyprland-glass` | [[os-rice/rices/arch-hyprland-glass/arch-hyprland-glass\|Wayland / Hyprland glass]] |
| `i3-rosemary` | [[os-rice/rices/i3-rosemary/i3-rosemary\|i3 / X11, muted rose]] |
| `gruvbox` | [[os-rice/rices/gruvbox/gruvbox\|shell + KDE gruvbox]] |
| `xin` `catppuccin` `nord` | shell/CLI only, no README of their own |

## Reference notes

- [[os-rice/modules/WINDOWS|The `win-` modules]] — the OS passes over a Windows machine
- [[os-rice/i3-sugg|i3-sugg]] — what an X11 desktop needs, and why
- [[os-rice/i3-void-packages|i3-void-packages]] — those components mapped to Void `xbps` packages
- [[wezterm/README|wezterm]]

## App configs

One directory per app (`zsh/`, `hypr/` under a rice, `waybar/`, `yazi/`, …).
os-rice owns the layered files in them; `99-local.*` is always yours.

> [!tip] Theme templates live with the app, not with the theme
> `<app>/<file>.tmpl` is written once, against the palette vocabulary in
> [[os-rice/DESIGN#6b. A theme is a palette, not a directory of app configs|DESIGN 6b]].
> A theme is `theme.list` — a palette, not a folder of configs.
