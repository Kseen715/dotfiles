---
title: arch-hyprland-glass rice
type: rice
tags:
  - kind/rice
  - topic/theming
  - topic/wayland
  - os/arch
---

# arch-hyprland-glass

The Hyprland "glass" desktop: SDDM with a QML theme, Hyprland + Waybar + wofi +
mako, gtklock, PipeWire with EasyEffects, and the application set on top.
Arch / x86_64 / systemd only — `rice.list` declares that up front (`require:`),
so a mismatched host fails before any mutation ([[os-rice/DESIGN#10. Rice preconditions — declare, fail before mutation|DESIGN 10]]).

```sh
os-rice/osr install arch-hyprland-glass
```

This rice is the salvaged form of the old `linux-arch-x86_64-hyprland-glass/`
bundle: ~30 bash scripts that each re-implemented logging, sudo, `pacman -S`,
`mkdir`/`chown` and config copying. All of it now runs on the shared harness —
`modules/*.sh` (one distro-agnostic copy each), `lib/` and this directory's
`config/`. The bundle is gone; anything from it is recoverable from git history
(the pre-migration tree is at commit `63bbfd9`; see
[[archive-decisions#Part 3 — deleted trees]]).

## What CI cannot prove here

The container matrix covers package installs, config layering and idempotency.
It cannot cover a display, a GPU, a real kernel or a hypervisor, so the modules
below are **correct-by-construction, not verified end to end** — they need a real
machine or a QEMU boot ([[os-rice/DESIGN#9. Testing — containers plus QEMU, no real machine|DESIGN 9]]). Treat a change to one of them as untested
until it has run on hardware.

- **Kernel / CPU / GPU / VM**: `dkms` (headers must match the *running* kernel),
  `cpu-microcodes`, `gpu-drivers` (the NVIDIA/AMD/Intel generation matrix has
  per-family unit tests, but no card), `vmware-init`, `waydroid` (needs binder).
- **Display manager + compositor runtime**: `sddm` and `hyprland` lay their files
  down correctly in a container, but only a live greeter and compositor exercise
  them. The same goes for every DE module hanging off Hyprland — `waybar`,
  `wofi`, `mako`, `wleave`, `hyprlock`, `hyprpaper`, `hypridle`, `hyprpicker`,
  `hyprcursor`, `gtklock`, `cliphist`, `luminance`, `nwg-displays`.
- **Hardware-attached**: `printer` (CUPS/SMB + the Canon captdriver), `easyeffects`
  / `pipewire` (the PulseAudio -> PipeWire swap needs real audio hardware),
  `swap` (a container guest is a no-op by design — the host owns its memory).
- **Mirrors**: `mirrors` (the `setup-mirrors.sh` port) is not in `rice.list` —
  ranking probes every Arch mirror and takes minutes. Run it on its own when a
  box has slow mirrors: `osr module mirrors` (`OSR_MIRRORS_FORCE=1` to re-rank
  later, `OSR_MIRRORS_N` to change how many are kept).

## Wallpaper

`wallpapers/` holds the image; `modules/hyprpaper.sh` copies it to
`~/Pictures/Wallpapers/` and substitutes the `{{WALLPAPER_PATH}}` placeholder in
`config/hypr/hyprpaper.conf`, `config/hypr/hyprland.conf` (`env =`) and
`config/gtklock/style.css`, so the daemon, the session and the locker all point
at the same installed file. Swapping the wallpaper is dropping a different image
in `wallpapers/` — no module or config edit.

## VMware guests

`modules/hyprland.sh` installs a second greeter entry, *Hyprland (VMware)*, when
`OSR_VIRT` is `vmware`. It launches through `start-hyprland-vmware.sh`, which
adds the software-rendering workarounds (`GSK_RENDERER=cairo`,
`WLR_RENDERER_ALLOW_SOFTWARE`, `WLR_NO_HARDWARE_CURSORS`) Hyprland needs without
a real GPU. The normal entry stays listed alongside it.
