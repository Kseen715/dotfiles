# i3wm full desktop — component checklist (distro-agnostic)

Scratch/planning doc. Not a rice, not a module. Source material: the legacy
`linux-arch-x86_64-hyprland-glass/` bundle + current `modules/` + `rices/arch-hyprland-glass/rice.list`.

Legend:

- `[have]` — module already exists in `os-rice/modules/`
- `[wl]` — Wayland-only, does **not** work under i3/X11, needs a swap
- **bold** = recommended default pick

Package names are logical (`lib/pkgmap/*.map` translates per distro). Where a
name genuinely differs across distros it is noted inline.

---

## 0. TL;DR minimum viable set

```
i3 picom rofi dunst polybar i3lock-color xss-lock feh xclip flameshot arandr
polkit-gnome gvfs udisks2 xdg-desktop-portal-gtk xdg-user-dirs gnome-keyring
xsettingsd qt6ct pavucontrol network-manager-applet
```

plus the `~/.xprofile` env block from §4. Sections 3 and 4 are the difference
between "i3 starts" and "every app behaves".

---

## 1. X session core

### 1.1 X server

| Component | Options | Notes |
|---|---|---|
| Server | **`xorg-server`** | Debian/Ubuntu: `xserver-xorg`; Fedora: `xorg-x11-server-Xorg` |
| Session start | **`xorg-xinit`** (`startx`) / display manager (§1.4) | `xinit` is one less daemon; a DM gets you PAM keyring unlock for free |
| Core utils | **`xorg-xrandr`** `xorg-xset` `xorg-xsetroot` `xorg-xprop` `xorg-xev` `xorg-xkill` `xorg-xdpyinfo` `xorg-xinput` | Debian bundles most as `x11-xserver-utils` + `x11-utils` |
| Window scripting | **`xdotool`** / `wmctrl` / `xdo` | `xdotool` is the one every i3 script assumes |
| Input config | `xorg-xinput`, `xkeyboard-config`, `/etc/X11/xorg.conf.d/` snippets | touchpad tap-to-click, natural scroll, layout switch |
| Keyboard layout | **`setxkbmap`** / `xkb-switch` / `kbdd` (per-window layout) | `setxkbmap -layout us,ru -option grp:alt_shift_toggle` |

### 1.2 Window manager

| Option | When |
|---|---|
| **`i3`** `[have]` | the ask. i3-gaps merged upstream since 4.22 — `gaps inner 8` works in stock i3 |
| `i3-wm` | Arch package name, already mapped in `pacman.map` |
| `swayfx` / `sway` `[wl]` | if you ever want the same config on Wayland — sway reads i3 config nearly verbatim |
| `leftwm` / `qtile` / `bspwm` | alternatives; bspwm needs `sxhkd` for keys |

Companion: `i3-wm` ships `i3-msg`, `i3bar`, `i3status`, `i3-nagbar`, `i3-input`,
`i3-save-tree` (layout persistence — useful with `append_layout`).

### 1.3 Compositor — **mandatory**

Without one: screen tearing, no transparency/blur, Electron/Chromium flicker,
broken window shadows, video stutter.

| Option | Notes |
|---|---|
| **`picom`** | the standard. Use a modern fork/version ≥10 for `--backend glx` + dual-kawase blur |
| `picom-ftlabs-git` / `picom-pijulius` | AUR forks with animations (window open/close/move) — closest to Hyprland feel |
| `compton` | dead ancestor of picom, don't |
| `xcompmgr` | minimal, no blur, only if picom breaks on your GPU |

```conf
# ~/.config/picom/picom.conf
backend = "glx";
vsync = true;
corner-radius = 10;
blur-method = "dual_kawase"; blur-strength = 6;
fading = true; fade-in-step = 0.06; fade-out-step = 0.06;
inactive-opacity = 0.94;
opacity-rule = [ "100:class_g = 'firefox' && focused" ];
```

### 1.4 Display / login manager

| Option | Notes |
|---|---|
| **`sddm`** `[have]` | already in the repo; Qt, themeable, works fine for X11 sessions |
| `lightdm` + `lightdm-gtk-greeter` / `lightdm-slick-greeter` | lightest full-featured DM |
| `ly` | TUI, tiny, no X deps, very i3-flavoured |
| `emptty` | TUI, even smaller |
| `greetd` + `gtkgreet`/`tuigreet` | modern, config-as-file; needs a session wrapper for X |
| none — `startx` + `~/.xinitrc` | zero daemons; pair with `pam_gnome_keyring` in `/etc/pam.d/login` or lose keyring auto-unlock |

### 1.5 Session bus & env propagation

Half of all "app silently does nothing" bugs live here.

```conf
# ~/.config/i3/config — first lines
exec --no-startup-id dbus-update-activation-environment --systemd --all
exec --no-startup-id systemctl --user import-environment DISPLAY XAUTHORITY
```

- `dbus` (or `dbus-broker` — faster, systemd-native)
- `systemd --user` / `elogind` + `seatd` on non-systemd distros
- `graphical-session.target` — hook user services (polybar, dunst, picom) onto it
  instead of `exec` lines if you want restart-on-crash

---

## 2. Bars, launchers, notifications, lock, wallpaper — the Wayland→X11 swap table

| Legacy `[wl]` module | X11 replacement (bold = pick) | Alternatives |
|---|---|---|
| `waybar` `[wl]` | **`polybar`** | `i3status-rust`, `i3blocks`, `i3status` (stock), `yambar`, `xmobar`, `tint2`, `lemonbar`+`succade`, `eww` (widgets, X11+Wayland) |
| `wofi` `[wl]` | **`rofi`** | `dmenu`, `fuzzel` `[wl]`, `albert`, `ulauncher`, `sirula` `[wl]`, `j4-dmenu-desktop` (fast .desktop feeder for dmenu) |
| `mako` `[wl]` | **`dunst`** | `xfce4-notifyd`, `deadd-notification-center`, `notify-osd`, `swaync` `[wl]` |
| `hyprlock`/`gtklock`/`waylock`/`swaylock` `[wl]` | **`i3lock-color`** + `xss-lock` | `betterlockscreen` (blurred wallpaper cache over i3lock-color), `xsecurelock` (most secure), `slock` (suckless, minimal), `physlock` (locks TTYs too), `xtrlock` |
| `hypridle` `[wl]` | **`xidlehook`** | `xautolock`, `xss-lock` + `xset dpms`, `swayidle` `[wl]` |
| `hyprpaper` `[wl]` | **`feh`** | `nitrogen` (GUI, multi-monitor profiles), `xwallpaper`, `hsetroot`, `variety` (rotating/online), `wpaperd` `[wl]` |
| `hyprpicker` `[wl]` | **`xcolor`** | `gpick`, `gcolor3`, `colorpicker`, `farge` |
| `cliphist` `[wl]` | **`copyq`** (GUI+history+scripting) | `greenclip` (+rofi), `clipmenu`, `parcellite`, `diodon`, `autocutsel` (fixes PRIMARY↔CLIPBOARD desync) |
| `wleave`/`wlogout` `[wl]` | **rofi script** (10 lines, no package) | `arcologout`, `oblogout`, `i3exit` script |
| `nwg-displays` `[wl]` | **`arandr`** (GUI) + **`autorandr`** (profiles/hotplug) | `xlayoutdisplay`, `wlr-randr` `[wl]`, raw `xrandr` |
| `luminance` `[wl]` | **`redshift`** | `gammastep`, `sct` (12 lines of C), `wlsunset` `[wl]`, `xflux` |
| `hyprcursor` `[wl]` | XCursor themes (§4.3) | hyprcursor format is unused on X11 |
| `grim`+`slurp` `[wl]` | **`flameshot`** | `maim`+`slop`, `scrot`, `shutter`, `spectacle`, `xfce4-screenshooter`, `escrotum` |
| `wf-recorder` `[wl]` | **`obs-studio`** `[have]` | `simplescreenrecorder`, `peek` (GIF), `ffmpeg -f x11grab`, `kooha` `[wl]` |
| `foot` `[have]` `[wl]` | **`ghostty`** `[have]` / **`wezterm`** `[have]` | `alacritty`, `kitty`, `urxvt`, `st`, `xterm` (keep one as a guaranteed fallback), `tilix`, `terminator` |
| `wl-clipboard` `[wl]` | **`xclip`** | `xsel`, `xcv` |

### 2.1 Clipboard notes

X11 has three selections (`PRIMARY`, `CLIPBOARD`, `SECONDARY`) and clipboard
contents **die with the owning process**. Either run a clipboard manager
(`copyq`, `clipmenu`, `greenclip`) or lose text when you close the app you
copied from. This has no Wayland analogue — don't skip it.

### 2.2 Screenshot examples

```sh
maim -s | xclip -sel clip -t image/png              # region → clipboard
maim -u -i $(xdotool getactivewindow) ~/shot.png    # active window
flameshot gui                                        # annotate GUI
ffmpeg -f x11grab -framerate 60 -i :0.0 -f pulse -i default out.mkv
```

---

## 3. The invisible glue — "apps work without problems"

This is the section people skip, then wonder why GParted does nothing and
Firefox can't upload files.

### 3.1 Polkit agent — **mandatory**

Without a running agent, every GUI app needing root fails **silently** (disk
tools, printer config, timeshift, virt-manager, blueman pairing).

| Option | Notes |
|---|---|
| **`polkit-gnome`** | classic, one binary, no deps beyond GTK |
| `mate-polkit` | same, MATE branding |
| `lxqt-policykit` | Qt-native, lighter if you're already Qt-themed |
| `polkit-kde-agent` | pulls in KDE frameworks — only if you run KDE apps anyway |
| `ts-polkitagent`, `polkit-dumb-agent` | minimal/experimental |

```conf
exec --no-startup-id /usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1
```

Also install `polkit` itself, and check `/etc/polkit-1/rules.d/` if you want
passwordless `udisks`/`NetworkManager` for your user.

### 3.2 XDG desktop portals

Needed by Flatpaks, snaps, Chromium/Electron, and anything sandboxed — file
pickers, screenshare, openURI, notifications, settings (theme/accent).

- `xdg-desktop-portal` (the frontend, always)
- **`xdg-desktop-portal-gtk`** — the backend for i3. Provides FileChooser,
  Settings, Print, AppChooser.
- `xdg-desktop-portal-kde` — only if you run Plasma apps and want native dialogs
- `xdg-desktop-portal-wlr` `[wl]` — **not** applicable to i3
- `xdg-desktop-portal-lxqt` — Qt-native alternative to -gtk
- `xdg-desktop-portal-xapp` — Cinnamon/Mint's, adds GTK settings passthrough

i3 has no portal backend of its own, so you **must** pin it:

```ini
# ~/.config/xdg-desktop-portal/i3-portals.conf
[preferred]
default=gtk
org.freedesktop.impl.portal.Settings=gtk
```

and export `XDG_CURRENT_DESKTOP=i3` — portals key off it, and it's also what
picks GTK's `prefers-color-scheme`.

Debug: `/usr/lib/xdg-desktop-portal -r -v`, `busctl --user tree org.freedesktop.portal.Desktop`.

### 3.3 Mounting, trash, removable media

| Component | Options |
|---|---|
| Virtual FS layer | **`gvfs`** (+ `gvfs-mtp` `gvfs-gphoto2` `gvfs-smb` `gvfs-nfs` `gvfs-afc` `gvfs-goa`) |
| Block device daemon | **`udisks2`** |
| Automount | **`udiskie`** (tray + notifications) / `devmon`+`udevil` / `autofs` / Thunar's `thunar-volman` |
| CLI mounting | `udisksctl mount -b /dev/sdb1`, `bashmount`, `pmount` |
| Android/MTP | `gvfs-mtp`, `jmtpfs`, `simple-mtpfs`, `android-file-transfer` |
| Network shares | `cifs-utils` (SMB), `nfs-utils`, `sshfs`, `rclone` |
| Trash | comes from `gvfs`; CLI: `trash-cli`, `gio trash`, `rmtrash` |

**Without gvfs, "Move to Trash" errors out in every GTK file manager**, and the
file-picker sidebar shows no devices.

### 3.4 XDG basics

- **`xdg-user-dirs`** + `xdg-user-dirs-gtk` — creates `~/Downloads`, `~/Pictures`,
  … which browsers' default download path and picker sidebars rely on. Run
  `xdg-user-dirs-update` once.
- **`shared-mime-info`** — MIME database; without it everything is
  `application/octet-stream`
- **`desktop-file-utils`** — `update-desktop-database`; without it "Open With"
  is empty
- **`xdg-utils`** — `xdg-open`, `xdg-mime`, `xdg-settings`, `xdg-screensaver`
- `handlr` / `mimeo` / `linopen` — sane replacements for `xdg-open`'s mime routing
- `~/.config/mimeapps.list` — the actual defaults file:
  ```ini
  [Default Applications]
  inode/directory=thunar.desktop
  text/html=firefox.desktop
  image/png=imv.desktop
  ```

### 3.5 Secrets / keyring

VSCode, Chrome/Chromium, git credential helper, Nextcloud, Nextcloud, Fractal,
Element all expect a Secret Service on D-Bus.

| Option | Notes |
|---|---|
| **`gnome-keyring`** + `libsecret` (+ `seahorse` GUI) | the default everything targets |
| `kwallet` + `kwalletmanager` + `kwallet-pam` | if you're Qt-heavy |
| `keepassxc` | can act as a Secret Service provider (`Settings → Secret Service Integration`) — one password store for everything |
| `pass` + `gopass` + `pass-secret-service` | CLI-first |

PAM wiring (otherwise you type the keyring password on every login):

```
# /etc/pam.d/login and /etc/pam.d/sddm
auth       optional  pam_gnome_keyring.so
session    optional  pam_gnome_keyring.so auto_start
```

Also: `gcr` (prompter UI), and `exec --no-startup-id gnome-keyring-daemon --start --components=secrets,ssh` if no DM starts it.
SSH agent options: `gnome-keyring`'s ssh component, `ssh-agent`, **`keychain`**, `gpg-agent --enable-ssh-support`.

### 3.6 Thumbnails & previews

| Component | Options |
|---|---|
| Thumbnailer service | **`tumbler`** (Thunar/XFCE, works standalone via D-Bus) |
| Video thumbs | **`ffmpegthumbnailer`** |
| PDF/PS | `poppler-glib`, `libgsf` |
| RAW photos | `libopenraw`, `raw-thumbnailer` |
| Office docs | `libgsf`, `libreoffice` thumbnailer |
| Fonts/EPUB | `tumbler` extras, `gnome-epub-thumbnailer` |
| Image loaders | `gdk-pixbuf2` + `webp-pixbuf-loader`, `librsvg`, `libheif`, `libavif`, `libjxl` |

Missing pixbuf loaders = blank icons for WebP/AVIF/HEIC everywhere, including
your image viewer and browser downloads panel.

### 3.7 Codecs

`gstreamer` + `gst-plugins-base` `gst-plugins-good` `gst-plugins-bad`
`gst-plugins-ugly` `gst-libav` `gst-plugin-pipewire`, plus `ffmpeg`.
Hardware decode: `libva` + `libva-intel-driver`/`intel-media-driver`/
`libva-mesa-driver`/`nvidia-vaapi-driver`, `libvdpau`, `vulkan-icd-loader`.
Verify with `vainfo`, `vdpauinfo`.

### 3.8 Autostart

XDG `.desktop` autostart files (installed by nm-applet, blueman, keyring, etc.)
are **not** started by i3 on their own.

| Option | Notes |
|---|---|
| **`dex -a -s /etc/xdg/autostart/:~/.config/autostart/`** | runs XDG autostart entries |
| `xdg-autostart-generator` (systemd) | `systemctl --user start xdg-desktop-autostart.target` |
| plain `exec --no-startup-id` lines | explicit, no surprises, more maintenance |
| `systemd --user` units bound to `graphical-session.target` | restart-on-crash, proper ordering, log in journal |

---

## 4. Toolkit theming & consistency

i3 has no settings daemon, so **nothing applies your theme unless you install one**.

### 4.1 The settings daemon

| Option | Notes |
|---|---|
| **`xsettingsd`** | tiny, reads `~/.config/xsettingsd/xsettingsd.conf`, live-reload with `killall -HUP xsettingsd` |
| `gnome-settings-daemon` (xsettings plugin only) | heavier, pulls GNOME |
| `dconf`/`gsettings` alone | GTK4 reads gsettings directly; GTK3 needs xsettings or the ini file |
| `xfsettingsd` | XFCE's, works standalone, also handles xrandr/xkb |

```conf
# ~/.config/xsettingsd/xsettingsd.conf
Net/ThemeName "Catppuccin-Mocha-Standard-Blue-Dark"
Net/IconThemeName "Papirus-Dark"
Gtk/CursorThemeName "Bibata-Modern-Ice"
Gtk/CursorThemeSize 24
Xft/DPI 98304
Xft/Antialias 1
Xft/RGBA "rgb"
```

### 4.2 GTK

- GTK2: `~/.gtkrc-2.0` · GTK3: `~/.config/gtk-3.0/settings.ini` · GTK4:
  `gsettings set org.gnome.desktop.interface gtk-theme ...` + `~/.config/gtk-4.0/`
- Config GUI: **`lxappearance`** (GTK2/3) / `nwg-look` (GTK3/4, works on X11
  too) / `gnome-tweaks`
- Themes: `catppuccin-gtk`, `adw-gtk3` (GTK3 that matches libadwaita),
  `gruvbox-gtk-theme`, `nordic-theme`, `orchis`, `colloid`, `materia`, `arc`
- GTK4/libadwaita apps ignore themes unless you use `adw-gtk3` +
  `GTK_THEME=` / gradience — accept that some apps stay Adwaita

### 4.3 Qt

- **`qt5ct`** + **`qt6ct`** — set `QT_QPA_PLATFORMTHEME=qt6ct`
- `kvantum` (+ `kvantum-theme-catppuccin` etc.) — SVG-based theming engine,
  set `QT_STYLE_OVERRIDE=kvantum`
- `qt5-styleplugins` / `qt6gtk2` — makes Qt follow your GTK theme instead
- `breeze` / `breeze-gtk` — if you go the KDE-consistency route

### 4.4 Icons & cursors

- Icons: **`papirus-icon-theme`**, `adwaita-icon-theme` (fallback — always
  install, apps reference its names), `breeze-icons`, `tela-icon-theme`,
  `numix-icon-theme`, `candy-icons`
- Cursors: **`bibata-cursor-theme`**, `capitaine-cursors`, `phinger-cursors`,
  `volantes-cursors`, `xcursor-themes` — set via `~/.icons/default/index.theme`
  **and** `XCURSOR_THEME` (root-window cursor needs `xsetroot -cursor_name left_ptr`)

### 4.5 Fonts

`fontconfig` + `noto-fonts` `noto-fonts-emoji` `noto-fonts-cjk`
`ttf-liberation` `ttf-dejavu` + a Nerd Font (`starship` `[have]` already pulls
one) + `ttf-ms-fonts`/`ttf-mscorefonts-installer` for docs.
Rendering tweaks: `/etc/fonts/conf.d/` symlinks (`10-sub-pixel-rgb.conf`,
`11-lcdfilter-default.conf`), `freetype2` with subpixel hinting.
Emoji fallback needs a `fontconfig` rule or emoji render as tofu in terminals.

### 4.6 HiDPI / scaling

X11 has no per-monitor scaling. Options: `Xft.dpi` in `~/.Xresources` (+
`xrdb -merge`), `GDK_SCALE`/`GDK_DPI_SCALE`, `QT_SCALE_FACTOR`/
`QT_AUTO_SCREEN_SCALE_FACTOR=1`, `xrandr --scale` (blurry), or `--dpi`.
Mixed-DPI multi-monitor is genuinely bad on X11 — this is the one real reason
to consider sway later.

### 4.7 Session env

```sh
# ~/.xprofile  (sourced by DMs and by ~/.xinitrc if you source it)
export XDG_CURRENT_DESKTOP=i3
export XDG_SESSION_TYPE=x11
export QT_QPA_PLATFORMTHEME=qt6ct
export QT_STYLE_OVERRIDE=kvantum
export XCURSOR_THEME=Bibata-Modern-Ice
export XCURSOR_SIZE=24
export GTK_THEME=Catppuccin-Mocha-Standard-Blue-Dark
export _JAVA_AWT_WM_NONREPARENTING=1   # JetBrains/Matlab/Java Swing → grey box without this
export _JAVA_OPTIONS='-Dawt.useSystemAAFontSettings=on -Dswing.aatext=true'
export MOZ_USE_XINPUT2=1               # smooth/pixel scrolling in Firefox
export SAL_USE_VCLPLUGIN=gtk3          # LibreOffice looks native
export ELECTRON_TRAMPOLINE=1           # (see §9 for Electron flags)
export EDITOR=micro VISUAL=micro
```

---

## 5. Input

| Need | Options |
|---|---|
| Touchpad/mouse | `xf86-input-libinput` (+ `/etc/X11/xorg.conf.d/30-touchpad.conf`), `libinput-gestures` or `fusuma` for swipe gestures, `touchegg` |
| Keyboard layout | `setxkbmap`, `xkb-switch`, **`kbdd`** (per-window layout memory), `xkeyboard-config` |
| Input method (CJK/Cyrillic/emoji) | **`fcitx5`** (+ `fcitx5-gtk` `fcitx5-qt` `fcitx5-configtool` + engines: `fcitx5-mozc`, `fcitx5-chinese-addons`, `fcitx5-hangul`) or `ibus` (+ `ibus-anthy`…) — needs `GTK_IM_MODULE=fcitx QT_IM_MODULE=fcitx XMODIFIERS=@im=fcitx` |
| Numlock at boot | `numlockx` |
| Key remapping | `xcape` (dual-role keys), `keyd` (kernel-level, works everywhere), `xmodmap`, `interception-tools`, `kmonad` |
| Hotkey daemon | i3 bindsym is enough; `sxhkd` if you want WM-independent bindings |
| Emoji picker | `rofimoji`, `ibus-emoji`, `gnome-characters`, `splatmoji` |

---

## 6. Audio

| Component | Options |
|---|---|
| Server | **`pipewire`** `[have]` + `wireplumber` + `pipewire-pulse` + `pipewire-alsa` + `pipewire-jack` (or legacy `pulseaudio` `[have]` — never both) |
| Mixer GUI | **`pavucontrol`** ← *missing from your repo*; `pulsemixer` (TUI), `ncpamixer`, `alsamixer`, `pwvucontrol` (native PW) |
| Patchbay | `qpwgraph` `[have]`, `helvum`, `carla`, `catia` |
| EQ / effects | `easyeffects` `[have]`, `pulseeffects` (legacy), `calf`, `lsp-plugins` |
| Volume keys | `pactl set-sink-volume @DEFAULT_SINK@ +5%`, `wpctl set-volume`, `amixer`, `pamixer` (nicest CLI), `volumeicon`/`pasystray` (tray) |
| Codecs/BT audio | `libfreeaptx`, `libldac`, `sbc`, `pipewire-audio`; enable in `wireplumber` bluez config |
| Utils | `alsa-utils` (`alsactl`, `speaker-test`), `sof-firmware` (modern Intel laptops — **no sound at all without it**), `alsa-firmware` |
| MPRIS control | `playerctl` (+ polybar module), `mpris-proxy` (BT headset buttons) |

---

## 7. Network, Bluetooth, hardware services

### 7.1 Network

- **`networkmanager`** `[have]` + **`network-manager-applet`** (`nm-applet` — the
  tray icon; without it no wifi/VPN password prompts in the GUI)
- TUI/CLI: `nmtui`, `nmcli`; alternatives: `iwd` (lighter, can back NM),
  `connman`+`cmst`, `wpa_supplicant`+`wpa_cli`, `netctl`
- VPN plugins: `networkmanager-openvpn`, `-openconnect`, `-wireguard` (built-in),
  `-l2tp`, `-strongswan`; standalone `wireguard-tools`, `amnezia-vpn` `[have]`
- DNS: `systemd-resolved` / `dnsmasq` / `dnscrypt-proxy`; `openresolv`
- Firewall: `ufw`+`gufw`, `firewalld`, `nftables` rules, `opensnitch` (app-level GUI)
- Sharing/discovery: `avahi` + `nss-mdns` (`.local` hostnames, network printers),
  `samba`, `nfs-utils`, `localsend`, `kdeconnect`+`indicator-kdeconnect`

### 7.2 Bluetooth

`bluez` + `bluez-utils` + **`blueman`** (`blueman-applet` in tray) or
`bluetuith`/`bluetui` (TUI) or `bluedevil` (KDE). Enable `bluetooth.service`,
plus `bluez-obex` for file transfer.

### 7.3 Power & thermals

- **`tlp`** (+ `tlp-rdw`) *or* `power-profiles-daemon` (never both) or `auto-cpufreq`
- `upower` (battery info for bars/notifications), `acpid`, `thermald` (Intel),
  `cpupower`, `powertop`
- Laptop lid/idle: `systemd-logind` (`/etc/systemd/logind.conf`) or `elogind`
- Battery alerts: polybar module + `dunstify`, or `batsignal`, `cbatticon`, `poweralertd`
- Hybrid GPU: `optimus-manager`, `envycontrol`, `nvidia-prime`, `switcheroo-control`

### 7.4 Backlight & sensors

`brightnessctl` (recommended) / `light` / `acpilight` / `xbacklight` (often
broken on modern drivers); `ddcutil` for external monitors over DDC/CI;
`lm_sensors` (`sensors-detect`) for temps in the bar.

### 7.5 Printing & scanning

`cups` `[have via printer]` + `cups-pdf` + `system-config-printer` (GUI) +
`avahi`/`nss-mdns` (driverless network printers) + `gutenprint`/`hplip`;
`sane` + `sane-airscan` + `simple-scan`/`xsane`/`skanlite`.

### 7.6 Storage & disks

`gnome-disks` (`udisks2` GUI) / `gparted` / `kde-partitionmanager`;
`ntfs-3g`, `exfatprogs`, `dosfstools`, `f2fs-tools`, `btrfs-progs`;
`smartmontools`; `ncdu` `[have]`/`baobab`/`dua-cli` for disk usage;
`timeshift`/`snapper`/`borg`+`vorta`/`restic` for backups.

---

## 8. File management & pickers

### 8.1 File managers

| Option | Notes |
|---|---|
| **`thunar`** (+ `thunar-volman` `thunar-archive-plugin` `thunar-media-tags-plugin` `tumbler`) | lightest full GTK FM, best i3 fit |
| `nautilus` `[have]` | GNOME; drags in tracker/gnome deps, but polished |
| `pcmanfm` / `pcmanfm-qt` | very light |
| `nemo` | Nautilus fork with more features, fewer GNOMEisms |
| `dolphin` (+ `kio-extras` `kdegraphics-thumbnailers`) | best-in-class features, Qt deps |
| `spacefm`, `worker`, `doublecmd` (dual-pane), `krusader` | niche/dual-pane |
| **`yazi`** `[have]` / `ranger` / `lf` / `nnn` / `vifm` / `mc` | TUI |

### 8.2 File pickers — what breaks and why

| App type | Dialog used | Requires |
|---|---|---|
| GTK3/GTK4 native | `GtkFileChooser` | `gvfs` (devices/network/trash sidebar), `xdg-user-dirs` (bookmarks) |
| Qt native | `QFileDialog` | `qt6ct` for theming; `QT_QPA_PLATFORMTHEME=gtk3` to get the GTK dialog instead |
| Flatpak / snap / Chromium / Electron / Firefox-with-portal | portal `FileChooser` | `xdg-desktop-portal` + `-gtk` + correct `XDG_CURRENT_DESKTOP` |
| KDE apps | `KFileWidget` | `kio` + `kio-extras`; falls back ugly without |
| Java/Swing | own AWT dialog | nothing, but needs `_JAVA_AWT_WM_NONREPARENTING=1` to render |

Force Firefox to use the portal picker: `widget.use-xdg-desktop-portal.file-picker = 1` in `about:config`.

### 8.3 Archives

`file-roller` (GTK) / `xarchiver` (light) / `ark` (Qt) / `peazip`, backed by
`p7zip` `unrar` `unzip` `[have via zip]` `zip` `tar` `xz` `zstd` `lzip` `cabextract`;
`atool`/`ouch` for CLI.

---

## 9. Applications (all X11-native, carry over from the Hyprland rice)

| Category | Options |
|---|---|
| Browser | `firefox` `[have]`, `zen-browser` `[have]`, `yandex-browser` `[have]`, `chromium`/`ungoogled-chromium`, `brave`, `librewolf`, `vivaldi`, `qutebrowser`, `nyxt` |
| Editor/IDE | `vscode-insiders` `[have]`, `kate` `[have]`, `micro` `[have]`, `neovim`/`lazyvim`, `helix`, `zed`, JetBrains via `jetbrains-toolbox`, `sublime-text` |
| Terminal | `ghostty` `[have]`, `wezterm` `[have]`, `alacritty`, `kitty`, `st`, `xterm` (keep as fallback!) |
| Multiplexer | `tmux`, `zellij`, `screen` |
| Media player | `celluloid` `[have]`/`mpv`, `vlc`, `haruna`, `smplayer` |
| Music | `mpd`+`ncmpcpp`/`rmpc`, `cmus`, `rhythmbox`, `strawberry`, `spotify`/`spotify-player`, `youtube-music` |
| Image viewer | `loupe`, `imv`, `nsxiv`/`sxiv`, `feh`, `eog`, `gwenview`, `qimgv` |
| Image edit | `gimp`, `krita`, `inkscape`, `pinta`, `darktable`, `rawtherapee`, `upscayl` |
| Video edit | `kdenlive`, `shotcut`, `davinci-resolve`, `losslesscut` |
| Office | `onlyoffice` `[have]`, `libreoffice-fresh`, `wps-office`; PDF: `zathura`+`zathura-pdf-mupdf`, `evince`, `okular`, `sioyek`, `xournalpp` (annotate) |
| Notes | `obsidian`, `logseq`, `joplin`, `standard-notes`, `zim` |
| Chat | `discord` `[have]`/`vesktop`, `telegram` `[have]`, `element`, `slack`, `signal`, `whatsapp-for-linux` |
| Mail | `thunderbird`, `evolution`, `geary`, `aerc`/`neomutt` (TUI) |
| Torrent | `qbittorrent` `[have]`, `transmission-gtk`, `deluge`, `fragments` |
| Screen record | `obs-studio` `[have]`, `simplescreenrecorder`, `peek` |
| Virtualization | `virt-manager`+`libvirt`+`qemu`, `virtualbox`, `gnome-boxes`, `distrobox`+`podman`, `docker` `[have]`, `waydroid` (Wayland-only — use `anbox`/`android-x86` VM on X11) |
| Gaming | `steam` `[have]` (+32-bit libs, `pacman-multilib` `[have]`), `lutris`, `heroic-games-launcher`, `bottles`, `wine`+`winetricks`, `proton-ge-custom`, `gamemode`, `mangohud`, `gamescope`, `curseforge` `[have]` |
| System info | `fastfetch` `[have]`, `inxi` `[have]`, `btop` `[have]`, `htop` `[have]`, `nvtop`, `mission-center`, `gnome-system-monitor` |
| Sync/cloud | `nextcloud-client`, `syncthing`+`syncthing-gtk`, `rclone`+`rclone-browser`, `megasync`, `insync` |
| Password mgr | `keepassxc`, `bitwarden`, `1password`, `pass`+`rofi-pass` |
| Colors/design | `gpick`, `gcolor3`, `figma-linux`, `pencil` |

### 9.1 Electron/Chromium notes on X11

Native X11 works out of the box (unlike Wayland). If you hit blurry HiDPI or
flicker: `--force-device-scale-factor=1.5`, `--disable-gpu-compositing`, or
`--use-gl=desktop`. Put per-app flags in `~/.config/<app>-flags.conf` or edit
the `.desktop` file. Tray icons need an SNI host — polybar's `tray` module or
`i3bar`'s `tray_output` handles it.

---

## 10. Flatpak / alternative package sources

`flatpak` `[have]` + `flathub` remote + `xdg-desktop-portal-gtk` (mandatory —
file dialogs break otherwise) + `flatpak-xdg-utils`.
Theming Flatpaks: `flatpak install org.gtk.Gtk3theme.<Theme>` and
`flatpak override --user --filesystem=xdg-config/gtk-3.0:ro --env=GTK_THEME=<Theme>`.
GUI manager: `gnome-software`/`warehouse`/`flatseal` (permissions — very useful).
Others: `appimagelauncher` (+ `fuse2`), `snapd`, `nix`, `homebrew`, `distrobox`.

---

## 11. i3 config essentials

```conf
# --- look
gaps inner 8
gaps outer 4
smart_borders on
default_border pixel 2
font pango:JetBrainsMono Nerd Font 10
client.focused #89b4fa #89b4fa #1e1e2e #f5c2e7 #89b4fa

# --- session glue
exec --no-startup-id dbus-update-activation-environment --systemd --all
exec --no-startup-id /usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1
exec --no-startup-id picom --daemon
exec --no-startup-id dunst
exec --no-startup-id polybar main
exec --no-startup-id feh --bg-fill ~/.local/share/wallpapers/current
exec --no-startup-id nm-applet
exec --no-startup-id blueman-applet
exec --no-startup-id udiskie --tray
exec --no-startup-id copyq
exec --no-startup-id xss-lock --transfer-sleep-lock -- i3lock -n -c 1e1e2e
exec --no-startup-id redshift -l 55.75:37.61
exec --no-startup-id dex -a -s /etc/xdg/autostart/
exec --no-startup-id numlockx on
exec --no-startup-id autorandr --change

# --- keys
bindsym $mod+d exec --no-startup-id rofi -show drun
bindsym $mod+Tab exec --no-startup-id rofi -show window
bindsym $mod+v exec --no-startup-id copyq toggle
bindsym $mod+Return exec ghostty
bindsym Print exec --no-startup-id flameshot gui
bindsym XF86AudioRaiseVolume exec --no-startup-id pamixer -i 5
bindsym XF86MonBrightnessUp exec --no-startup-id brightnessctl set +5%

# --- rules (the ones everyone eventually needs)
for_window [window_role="pop-up"] floating enable
for_window [window_role="bubble"] floating enable
for_window [window_type="dialog"] floating enable
for_window [class="Pavucontrol|Blueman-manager|Nm-connection-editor"] floating enable
for_window [class="steam" title="Friends List"] floating enable
for_window [title="Picture-in-Picture"] floating enable, sticky enable
```

Useful extras: `i3-resurrect` / `i3-save-tree` + `append_layout` (session
restore), `autotiling` (dwindle-like split direction), `i3-layout-manager`,
`i3ipc-python` for custom scripts, `i3-swallow`/`devour` (terminal swallowing).

---

## 12. Known X11 gotchas (the "why is this broken" list)

1. **Java/Swing apps show a grey box** → `_JAVA_AWT_WM_NONREPARENTING=1`
2. **GUI root prompts do nothing** → no polkit agent running (§3.1)
3. **Flatpak/Chromium file dialog is broken or blank** → portal not installed or
   `XDG_CURRENT_DESKTOP` unset (§3.2)
4. **Copied text vanishes when the source app closes** → no clipboard manager (§2.1)
5. **"Move to Trash" errors** → no `gvfs`
6. **Theme applies to some apps only** → no `xsettingsd`/settings daemon (§4.1)
7. **Qt apps look ancient** → `QT_QPA_PLATFORMTHEME` unset (§4.3)
8. **Tearing during video** → no compositor / vsync off (§1.3)
9. **Tray icons missing** → bar's tray module disabled, or two bars fighting for
   the systray owner selection
10. **No sound on a new Intel laptop** → missing `sof-firmware`
11. **Blank icons for WebP/AVIF** → missing gdk-pixbuf loaders (§3.6)
12. **Screen never locks on suspend** → no `xss-lock --transfer-sleep-lock`
13. **Mixed-DPI monitors look wrong** → X11 limitation; pick one scale or move to sway
14. **Screensaver kicks in during video** → apps need `xdg-screensaver`/idle inhibit;
    `xset s off -dpms` as a blunt fix, or `caffeine-ng`

---

## 13. Repo implications (os-rice)

- `modules/wayland.sh` is Wayland-only → needs an `xorg.sh` sibling (X server +
  X utils + GTK/Qt runtime libs + portals).
- `modules/foot.sh` `[wl]` cannot be listed in an i3 rice — use `ghostty`/`wezterm`.
- Wayland-only modules with no i3 use: `hyprland`, `hyprpaper`, `hyprlock`,
  `hypridle`, `hyprpicker`, `hyprcursor`, `waybar`, `wofi`, `mako`, `wleave`,
  `wlogout`, `gtklock`, `swaylock`, `waylock`, `cliphist`, `nwg-displays`,
  `luminance`, `helvum` (works on X11 actually), `waydroid`.
- Carry over unchanged: `git-base`, `openssh`, `zip`, `networkmanager`, `dkms`,
  `cpu-microcodes`, `gpu-drivers`, `starship`, `zsh`, `micro`, `htop`, `btop`,
  `fastfetch`, `yazi`, `pipewire`, `qpwgraph`, `easyeffects`, `printer`, `kate`,
  `nautilus`, `celluloid`, `firefox`, `vscode-insiders`, `discord`, `telegram`,
  `obs-studio`, `qbittorrent`, `amnezia-vpn`, `steam`, `sddm`, `flatpak`, `docker`.
- New modules needed (~20): `xorg`, `picom`, `polybar`, `rofi`, `dunst`,
  `i3lock` (i3lock-color + xss-lock + xidlehook), `feh`, `flameshot`, `copyq`,
  `arandr` (+autorandr), `redshift`, `xdg-portals`, `polkit-agent`, `gvfs`
  (+udisks2/udiskie), `keyring`, `theming` (xsettingsd + lxappearance + qt6ct +
  kvantum + icons + cursors), `pavucontrol`, `blueman`, `brightnessctl`,
  `thunar`, `dex`.
- New rice: `rices/linux-i3-<theme>/rice.list` — manifest order still the
  dependency graph: `xorg` → `i3` → `picom` → bar/launcher/notify → glue → apps.
