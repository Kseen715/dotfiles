# linux-arch-x86_64-hyprland-glass
@ 2025.06.30

<p align="center">
  <img src="https://github.com/Kseen715/imgs/blob/main/sakura_kharune.png" title="Logo" alt="Logo" width="150" height="150"/>
</p>

## Installation

> [!NOTE]
> All scripts should be run as user, not as root. They will ask for `sudo` password when needed.
>
> If you run them as root, pass `--delevated <user-name>` to the scripts, where `<user-name>` is the user you want to set up the environment for. They will ask `sudo` password for that user when needed.

1. The installation is based on `minimal` Arch Linux installation. Assuming you have a working Arch Linux installation with network access, you can follow the steps below.
1. If you have a `pulseaudio` installed, you can remove it and install `pipewire` instead:

    ```bash
    ./pulseaudio-to-pipewire.sh
    ```

1. Rate `pacman` mirrors based on speed and update the mirror list:

    ```bash
    ./setup-mirrors.sh
    ```

1. Install the hyprland with all dependencies:

    ```bash
    ./setup.sh
    ```

1. Install all the apps that I use:

    ```bash
    ./setup-apps.sh
    ```

## Keybinds

| Keybind | Action |
|---------|--------|
| <kbd>Super + Enter</kbd> | Open Terminal |
| <kbd>Super + E</kbd> | Open File Manager |
| <kbd>Super + D</kbd> | Open application launcher |
| <kbd>Super + Q</kbd> | Kill focused application |
| <kbd>Super + Shift + S</kbd> | Take screenshot of area (requires Hyprshot) |
| <kbd>Super + Alt + Space</kbd> | Toggle focused window to be floating or tiled |
| <kbd>Super + F</kbd> | Toggle focused window to full-screen view |
| <kbd>Super + 1-0</kbd> | Switch to workspace 1-10 |
| <kbd>Super + Ctrl + Right</kbd> | Switch to next workspace |
| <kbd>Super + Ctrl + Left</kbd> | Switch to previous workspace |
| <kbd>Super + Shift + 1-0</kbd> | Move focused window to workspace 1-10 |
| <kbd>Super + LMB</kbd> | Move focused window |
| <kbd>Super + RMB</kbd> | Resize focused window |
| <kbd>Super + Left/Right/Up/Down</kbd> | Move focus to window in that direction |
| <kbd>Super + Shift + Left/Right/Up/Down</kbd> | Swap focused window with window in that direction |
| <kbd>Super + Alt + Left/Right/Up/Down</kbd> | Resize focused window in that direction |
| <kbd>Alt + Tab</kbd> | Cycle windows / if floating bring to top |

## Helpful links

- [Hyprland Wiki](https://wiki.hyprland.org/)
- [Hyprland GitHub](https://github.com/hyprwm/Hyprland)
- [Arch Wiki - Hyprland](https://wiki.archlinux.org/title/Hyprland)
- [Font glyphs for configs](https://nerdfonts.ytyng.com/)
- [SDDM QML configs by Keyitdev](https://github.com/Keyitdev/sddm-astronaut-theme)

## Log files for debugging

- `~/.local/share/sddm/wayland-session.log` - SDDM log file (Wayland)
- `~/.local/share/sddm/xorg-session.log` - SDDM log file (Xorg)
- `~/.config/hypr/hypr.log` - Hyprland log file
- `~/.cache/hyprland/hyprlandCrashReportXXX.log` - Hyprland crash report
