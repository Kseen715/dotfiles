# linux-arch-x86_64-hyprland-glass (legacy — retained reference)

This rice has been **salvaged into the DRY os-rice framework**:

- Rice manifest + configs + wallpaper: `os-rice/rices/arch-hyprland-glass/`
- Distro-agnostic modules: `os-rice/modules/*.sh`
- Shared libs (detect / pkg / aur / preflight / build): `os-rice/lib/`
- Install: `os-rice/osr install arch-hyprland-glass`

## Why these files still exist

Everything the container matrix could verify (native package installs, config
layering, idempotency) was ported and tested on `archlinux:latest`, then its
legacy source deleted. The files kept here are the ones whose **correctness can
only be validated on real hardware or a VM** (per DESIGN §9), so they stay as an
un-validated reference until a real-machine / QEMU smoke test confirms the port:

- **GPU / kernel / VM**: `modules/{dkms,cpu-microcodes,vmware-init,waydroid}.sh`.
  (`gpu-drivers.sh` is gone: the whole generation matrix — NVIDIA
  open/570xx/470xx/390xx/340xx/nouveau, AMD amdgpu/r600/r300/amber, Intel
  iris/crocus/amber — now lives in `os-rice/modules/gpu-drivers.sh` with
  `test/unit/gpu_drivers.sh` covering it per family. `gpu-drivers.md` stays: its
  per-driver-version reliability notes are hardware findings, not code.)
  (Hardware detection
  itself now lives only in `lib/detect.sh`; the legacy `src/detect-gpu.sh` and
  `linux-debian/src/detect-hwaccel.sh` probes are deleted.)
- **Display-manager / compositor runtime**: `modules/{sddm,hyprland}.sh` and the
  Hyprland/Wayland DE modules (`waybar`, `wofi`, `mako`, `wleave`, `wlogout`,
  `hyprlock`, `hyprpaper`, `hypridle`, `hyprpicker`, `hyprcursor`, `gtklock`,
  `swaylock`, `waylock`, `cliphist`, `luminance`, `nwg-displays`, `printer`,
  `easyeffects`, `qpwgraph`, `helvum`, `nautilus`, `loupe`, `wezterm`) — package
  install + config copy are ported and container-verified, but a live
  compositor/display is needed to confirm end-to-end behavior.
- **Un-ported system helpers** (no framework equivalent yet):
  `setup-mirrors.sh`, `setup-swap.sh`, `pulseaudio-to-pipewire.sh`,
  `build-amneziavpn-client.sh`.

Once validated on hardware, the corresponding new modules supersede these and
this folder can be removed. Any deleted legacy file is recoverable from git
history (commit `63bbfd9`).
