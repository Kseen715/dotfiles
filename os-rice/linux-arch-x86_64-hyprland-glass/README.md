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

1. On first start, you'll need to launch `qpwgraph` to set up the audio patchbay. Save the configuration to `~/.patchbay`. Next time `qpwgraph` will be launched automatically with that configuration.

If you want to install additional modules/apps from the list, you can run (with comma-separated values):

```bash
./install-module.sh <module-name-1>,<module-name-2>,<module-name-3>...
```

## Keybinds

| Keybind | Action |
|---------|--------|
| <kbd>Super + Enter</kbd> | Open Terminal |
| <kbd>Super + E</kbd> | Open File Manager |
| <kbd>Super + D</kbd> | Open application launcher |
| <kbd>Super + T</kbd> | Open btop/htop/top |
| <kbd>Super + Q</kbd> | Kill focused application |
| <kbd>Super + Shift + S</kbd> | Take screenshot of area (requires Hyprshot) |
| <kbd>Super + Shift + C</kbd> | Color picker (requires Hyprpicker) |
| <kbd>Super + Alt + Space</kbd> | Toggle focused window to be floating or tiled |
| <kbd>Super + F</kbd> | Toggle focused window to full-screen view |
| <kbd>Super + J</kbd> | Swap tiling vertical/horizontal |
| <kbd>Super + P</kbd> | Pseudo-tile mode |
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

## Parts

- Window manager - [Hyprland](https://github.com/hyprwm/Hyprland)
- Display manager - [SDDM](https://github.com/sddm/sddm)
- Compositor - [Hyprland](https://github.com/hyprwm/Hyprland)
- Status bar - [waybar](https://github.com/Alexays/Waybar)
- Lock screen - [gtklock](https://github.com/jovanlanik/gtklock)
- Text editor - [Kate](https://kate-editor.org/)
- Terminal emulator - [wezterm](https://github.com/wezterm/wezterm)/[foot](https://codeberg.org/dnkl/foot/)
- File manager - [Nautilus](https://apps.gnome.org/Nautilus/)
- Audio manager - [pipewire](https://pipewire.org/)/[wireplumber](https://gitlab.freedesktop.org/pipewire/wireplumber/)/[Helvum](https://gitlab.freedesktop.org/pipewire/helvum)/[easyeffects](https://github.com/wwmm/easyeffects)
- Launcher - [wofi](https://github.com/SimplyCEO/wofi)
- Notifications - [mako](https://github.com/emersion/mako)
- Clipboard manager - [cliphist](https://github.com/sentriz/cliphist)
- Brightness manager - [luminance](https://github.com/sidevesh/Luminance)
- Display configuration - [nwg-displays](https://github.com/nwg-piotr/nwg-displays)

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

# Sound fix

Abstract from [StackExchange](https://unix.stackexchange.com/questions/560545/problem-with-audio-stuttering-choppy-in-every-single-distribution-ive-used)


```
sudo mkdir /etc/pipewire
sudo cp /usr/share/pipewire/pipewire.conf /etc/pipewire/
sudo vi /etc/pipewire/pipewire.conf
```
In section context.properties, comment out default.clock.rate
Add the following 4 lines beneath the line commented out in step 3 above:

```
default.clock.rate        = 192000
default.clock.quantum     = 512
default.clock.min-quantum = 32
default.clock.max-quantum = 4096
```

Save the file and Restart Pipewire:

```
systemctl --user restart pipewire-media-session pipewire-pulse pipewire
```

Enjoy clean music!
