# i3wm on Void Linux — where every component in `i3-sugg.md` actually comes from

Companion to `i3-sugg.md` (the distro-agnostic checklist). This file answers the
*second* question: for Void specifically, what is the real `xbps` package name,
and what do you do for the handful of things Void does not ship.

**Verification method.** Every name in the "xbps package" column was checked
against the real repository index, not from memory:

```sh
curl -O https://repo-default.voidlinux.org/current/x86_64-repodata          # + nonfree/, multilib/
# index.plist inside is a zstd'd tar; the <key> entries are the package names
```

14 737 names in `current`, 68 in `nonfree`, 5 704 in `multilib`. Anything marked
**MISSING** is genuinely absent from all three as of this writing.

Void naming rules that explain most of the diffs:

- X11 client utilities are **not** prefixed. Arch's `xorg-xrandr` is just `xrandr`.
- GStreamer 1.x carries the API version: `gstreamer1`, `gst-plugins-good1`.
- A few packages keep upstream's capitalisation: `Thunar`, `CopyQ`, `MangoHud`.
- Metapackages exist and are convenient: `xorg`, `xorg-minimal`, `xorg-apps`,
  `xorg-fonts`, `xorg-input-drivers`, `xorg-video-drivers`.
- Void is **runit + elogind**, not systemd. Everything in `i3-sugg.md` §1.5 that
  says `systemctl --user` has no equivalent; see §S below.

---

## 1. X session core (§1 of i3-sugg)

| Component | xbps package | Notes |
|---|---|---|
| X server | `xorg-server` | `xorg` pulls server + apps + fonts + drivers; `xorg-minimal` is server + a couple of fonts |
| `startx` | `xinit` | **not** `xorg-xinit` |
| Auth cookies | `xauth` | needed by `startx` and by any DM |
| Core utils | `xrandr` `xset` `xsetroot` `xprop` `xev` `xkill` `xdpyinfo` `xinput` | each is its own package; `xorg-apps` is the metapackage for all of them |
| Window scripting | `xdotool` | |
| Keyboard | `xkeyboard-config` `setxkbmap` | `setxkbmap` is a separate package on Void |
| Per-window layout | `kbdd` `xkb-switch` | both packaged |
| Input driver | `xf86-input-libinput` | `xorg-input-drivers` is the metapackage |
| Video drivers | `mesa-dri` + `xf86-video-*` | `xorg-video-drivers` metapackage; `mesa-intel-dri`, `mesa-ati-dri`, `mesa-nouveau-dri` are split out |
| Fonts for X | `xorg-fonts` `font-misc-misc` | avoids the "no fixed font" startup failure on a bare server |
| WM | `i3` | 4.24+, gaps merged upstream — `i3-gaps` also exists but is redundant |
| WM companions | `i3status` `autotiling` `python3-i3ipc` | `i3-resurrect` is **MISSING** (pip only) |
| Compositor | `picom` | v13, has `--backend glx` + dual_kawase. The animation forks (ftlabs/pijulius) are **MISSING** — no AUR equivalent, you would package them yourself |
| Display manager | `lightdm` + `lightdm-gtk-greeter` | also `lightdm-slick-greeter`, `lightdm-mini-greeter`, `lightdm-webkit2-greeter`, and `sddm` |
| Session bus | `dbus` (+`dbus-x11`) | `dbus-elogind` is the alternate build; do **not** install both |
| Seat/session | `elogind` `seatd` | `polkit-elogind` is the matching polkit build |

`ly` / `emptty` / `greetd`: **MISSING** on Void — `lightdm` or `sddm` or plain
`startx`.

---

## 2. The Wayland→X11 swap table (§2)

| Legacy `[wl]` | X11 pick | xbps package |
|---|---|---|
| `waybar` | **polybar** | `polybar` (3.7.2) — also `yambar`, `xmobar`, `tint2`, `i3status`, `dmenu`-fed bars |
| `wofi` | **rofi** | `rofi` + `rofi-emoji` `rofi-calc` `rofi-pass` `rofi-rbw` `rofimoji`; also `dmenu`, `j4-dmenu-desktop` |
| `mako` | **dunst** | `dunst` |
| `hyprlock`/`swaylock` | **betterlockscreen** | `betterlockscreen` + `i3lock-color` + `xss-lock` |
| `hypridle` | **xautolock + xss-lock** | `xautolock`, `xss-lock`. **`xidlehook` is MISSING** → `cargo:xidlehook` (needs `rust` + xcb headers) if you want its `--not-when-audio` |
| `hyprpaper` | **feh** | `feh`; also `nitrogen`, `xwallpaper`, `hsetroot` |
| `hyprpicker` | **xcolor** | `xcolor`; also `gpick`, `gcolor3` |
| `cliphist` | **CopyQ** | `CopyQ` — capital C and Q. `greenclip` is **MISSING**; `clipmenu` is packaged |
| `wleave`/`wlogout` | rofi script | no package needed (ships in this rice as `rofi-powermenu.sh`) |
| `nwg-displays` | **arandr + autorandr** | `arandr`, `autorandr` |
| `luminance` | **redshift** | `redshift` |
| `hyprcursor` | XCursor | `xcursor-themes`, `xcursor-vanilla-dmz`, `xcursor-vanilla-dmz-aa`. **Bibata and Capitaine are MISSING** — download the release tarball into `~/.local/share/icons/` |
| `grim`+`slurp` | **flameshot** | `flameshot`; also `maim` + `slop`, `scrot`, `xfce4-screenshooter` |
| `wf-recorder` | **obs** | package is `obs`, not `obs-studio` |
| `foot` | **ghostty** | `ghostty` is native on Void; also `wezterm`, `alacritty`, `kitty`, `xterm` |
| `wl-clipboard` | **xclip** | `xclip`, `xsel` |

---

## 3. The invisible glue (§3)

### 3.1 Polkit

`polkit` (or `polkit-elogind`) + an agent: **`polkit-gnome`** — also `mate-polkit`,
`lxqt-policykit`, `polkit-kde-agent`, `xfce-polkit`, `pantheon-agent-polkit`.
On Void polkit is D-Bus-activated; there is no runit service to enable.

### 3.2 Portals

`xdg-desktop-portal` + `xdg-desktop-portal-gtk`. Both packaged. You still must
export `XDG_CURRENT_DESKTOP=i3` and pin the backend in
`~/.config/xdg-desktop-portal/i3-portals.conf` — the portal has no idea what i3 is.

### 3.3 Mounting / trash

| Need | xbps |
|---|---|
| VFS | `gvfs` + `gvfs-mtp` `gvfs-gphoto2` `gvfs-smb` `gvfs-afc` `gvfs-afp` `gvfs-cdda` `gvfs-goa` |
| **`gvfs-nfs`** | **does not exist** — Void ships no NFS gvfs backend; mount with `nfs-utils` instead |
| Block daemon | `udisks2` |
| Automount | `udiskie` |
| MTP | `gvfs-mtp`, `jmtpfs`, `simple-mtpfs`, `android-file-transfer-linux` (**not** `android-file-transfer`) |
| Network | `cifs-utils`, `nfs-utils`, **`fuse-sshfs`** (not `sshfs`), `rclone` |
| Trash | from `gvfs`; CLI `trash-cli` |
| Disk GUI | `gnome-disk-utility` (**not** `gnome-disks`), `gparted` |
| Filesystems | `ntfs-3g`, `exfatprogs`, `dosfstools`, `smartmontools` |

### 3.4 XDG basics

`xdg-user-dirs`, `xdg-user-dirs-gtk`, `shared-mime-info`, `desktop-file-utils`,
`xdg-utils` — all packaged under exactly those names.

### 3.5 Keyring

`gnome-keyring`, `libsecret`, `gcr` (and `gcr4`), `seahorse`. All packaged.
PAM wiring on Void goes in `/etc/pam.d/lightdm` and `/etc/pam.d/login`; Void's
PAM ships `pam_gnome_keyring.so` with `gnome-keyring`.

### 3.6 Thumbnails

`tumbler`, `ffmpegthumbnailer`, `poppler-glib`, `libgsf`, `libopenraw`,
`gnome-epub-thumbnailer`, `gdk-pixbuf` (**not** `gdk-pixbuf2`),
`webp-pixbuf-loader`, `librsvg`, `libheif`, `libavif`, `libjxl`.
**`raw-thumbnailer` is MISSING** → `libopenraw-pixbuf-loader` gives you RAW
previews through gdk-pixbuf instead.

### 3.7 Codecs

`gstreamer1` `gst-plugins-base1` `gst-plugins-good1` `gst-plugins-bad1`
`gst-plugins-ugly1` `gst-libav` + `ffmpeg`.
**`gst-plugin-pipewire` is MISSING as a package** — Void builds the PipeWire
GStreamer element into `pipewire` itself.

Hardware decode: `libva` + `libva-intel-driver` / `intel-media-driver` /
`mesa-vaapi` (this is Void's name for `libva-mesa-driver`) / `nvidia-vaapi-driver`;
`libvdpau` (+ `libvdpau-va-gl`, since Void ships no `mesa-vdpau`); `vulkan-loader`
(**not** `vulkan-icd-loader`) + `mesa-vulkan-radeon`/`-intel`/`-nouveau`.
Verify with `libva-utils` (`vainfo`) and `vdpauinfo`.

### 3.8 Autostart

`dex` is packaged. runit has no user-service concept, so
`graphical-session.target`-style supervision does not exist — use `exec` lines in
the i3 config (what this rice does) or a per-user `runsvdir`.

---

## 4. Theming (§4)

| Need | xbps |
|---|---|
| Settings daemon | `xsettingsd` |
| GTK config GUI | `lxappearance`, `nwg-look` |
| GTK theme | `gnome-themes-extra` (Adwaita-dark), `arc-theme`, `gtk-engine-murrine`. **`adw-gtk3`, Catppuccin, Materia are MISSING** — drop them in `~/.themes/` by hand |
| Qt | `qt5ct`, `qt6ct`, `kvantum`, `qt5-styleplugins` |
| Icons | `papirus-icon-theme`, `adwaita-icon-theme`, `hicolor-icon-theme` |
| Cursors | `xcursor-themes`, `xcursor-vanilla-dmz` (Bibata: **MISSING**, install by hand) |
| Fonts | `noto-fonts-ttf` `noto-fonts-emoji` `noto-fonts-cjk` `liberation-fonts-ttf` `dejavu-fonts-ttf` `font-awesome6` `nerd-fonts-symbols-ttf` (`nerd-fonts` / `nerd-fonts-ttf` / `nerd-fonts-otf` are the huge metapackages) |
| MS fonts | **MISSING** — not even in nonfree; use `liberation-fonts-ttf` |

After any font install: `xbps-reconfigure -f font-<name>` or `fc-cache -f`.

---

## 5. Input (§5)

| Need | xbps | note |
|---|---|---|
| Touchpad/mouse | `xf86-input-libinput` | |
| Gestures | `libinput-gestures` | needs the user in the `input` group. **`fusuma` is MISSING** (Ruby gem only) |
| Layout | `setxkbmap`, `xkeyboard-config`, `xkb-switch`, `kbdd` | `kbdd` = per-window layout memory |
| Dual-role keys | `xcape` (X11), `keyd` (kernel-level) | `interception-tools` and `kmonad` are also packaged — run **one** of the three, they fight over the same evdev grabs |
| Legacy remap | `xmodmap` | Void drops the `xorg-` prefix here too |
| Numlock | `numlockx` | |
| Pointer hiding | `unclutter`, `unclutter-xfixes`, `xbanish` | |
| Input method | `fcitx5` + `fcitx5-gtk` `fcitx5-qt` `fcitx5-configtool` | engines: `fcitx5-mozc` `fcitx5-chinese-addons` `fcitx5-hangul`; `ibus` + `ibus-anthy` are the alternative |
| Emoji | `rofimoji` | also `ibus` emoji table |

The three env vars (`GTK_IM_MODULE`/`QT_IM_MODULE`/`XMODIFIERS`) matter as much
as the packages — without them a toolkit falls back to raw XIM and shows no
candidate window, which reads exactly like "fcitx5 is broken".

## 6. Audio (§6)

`pipewire` + `wireplumber` + `alsa-utils` + `alsa-firmware` + `sof-firmware`.

| Need | xbps |
|---|---|
| Mixer GUI | `pavucontrol`, `pwvucontrol` (native PipeWire) |
| Mixer TUI/CLI | `pulsemixer`, `ncpamixer`, `pamixer`, `alsamixer` (in alsa-utils) |
| Patchbay / EQ | `qpwgraph`, `helvum`, `easyeffects` |
| MPRIS | `playerctl`; `mpris-proxy` (headset buttons) ships **inside `bluez`** |
| BT codecs | `sbc`, `libfreeaptx`, **`ldacBT`** (Void's name for `libldac`) |
| Tray volume | **`volumeicon`/`pasystray` MISSING** — `volctl` is packaged and is what the reference dotfiles use |

There is no `pipewire-audio` metapackage and no `gst-plugin-pipewire`: both are
inside `pipewire`. `wireplumber-elogind` exists as an alternative build for
elogind session tracking — install one or the other, never both.

## 7. Network, Bluetooth, power, printing (§7)

| Need | xbps | note |
|---|---|---|
| Network | `NetworkManager` + `network-manager-applet` | `nmtui`/`nmcli` are inside NetworkManager. Without the applet there is no GUI wifi/VPN password prompt |
| DNS | `dnsmasq`, `dnscrypt-proxy`, `openresolv` | |
| Firewall | `ufw` + `gufw` | |
| Discovery | `avahi` + `nss-mdns` | the NSS half is the one people skip: without an `mdns` entry in `/etc/nsswitch.conf` the printer appears in CUPS but its name never resolves |
| Sharing | `samba`, `nfs-utils`, `kdeconnect` | **`localsend` is MISSING** → Flatpak |
| Bluetooth | `bluez` + `bluez-obex` + `blueman` | also `bluetuith`, `bluetui` (TUI). Service is `/etc/sv/bluetoothd` |
| Power | `tlp` + `tlp-rdw`, `power-profiles-daemon` | never both — `modules/power.sh` removes the daemon before installing tlp. **`auto-cpufreq` MISSING** |
| Battery/thermal | `upower`, `acpid`, `thermald` (Intel), `cpupower`, `powertop`, `batsignal`, `cbatticon`, `poweralertd` | |
| Hybrid GPU | `switcheroo-control` | **`optimus-manager`, `envycontrol`, `nvidia-prime` all MISSING** |
| Backlight/sensors | `brightnessctl`, `light`, `xbacklight`, `ddcutil`, `lm_sensors` | |
| Printing | `cups` + `cups-pdf` + `system-config-printer` + `gutenprint` + `hplip` | `captdriver` (Canon CAPT) is **MISSING**; `modules/printer.sh` degrades to a warning instead of failing |
| Scanning | `sane` + `sane-airscan` + `simple-scan` | also `xsane`, `skanlite` |

## 7a. Disks and archives (§7.6, §8.3)

`ntfs-3g`, `exfatprogs`, `dosfstools`, `f2fs-tools`, `btrfs-progs`,
`smartmontools`; GUIs `gnome-disk-utility`, `gparted`, `baobab`.

Archives: `xarchiver`, `file-roller`, `engrampa`, backed by `p7zip`, `unrar`
(nonfree), `unzip`, `zip`, `tar`, `xz`, `zstd`, `lzip`, `cabextract`, and
`atool`/`ouch` for the CLI. **`peazip` is MISSING.**

---

## 8. Files / apps (§8–§9)

`Thunar` + `thunar-volman` + `thunar-archive-plugin` + `thunar-media-tags-plugin`;
`pcmanfm`, `nautilus`; archives `xarchiver`, `file-roller`, `engrampa`.

Apps that need something other than a plain `xbps-install <name>` — either a
different name, or a route that is not xbps at all:

| Wanted | Do this |
|---|---|
| `discord` | `flatpak install flathub com.discordapp.Discord` |
| `gradia` | `flatpak install flathub be.alexandervanhee.Gradia` |
| `MangoHud` | packaged as `MangoHud` (+ `MangoHud-mangoapp`) — capitalised |
| `obs-studio` | packaged as `obs` |
| `copyq` | packaged as `CopyQ` |
| `thunar` | packaged as `Thunar` |
| `bibata-cursor-theme` | GitHub release tarball → `~/.local/share/icons/` |
| `xidlehook` | `cargo install xidlehook` (needs `rust`, `libxcb-devel`) |
| `i3-resurrect` | `pip install --user i3-resurrect` |
| `onlyoffice` | `flatpak install flathub org.onlyoffice.desktopeditors` |
| `localsend` | `flatpak install flathub org.localsend.localsend_app` |
| `vscode-insiders` | AUR-only. Void ships **`vscode`** (Code - OSS) — `modules/vscode.sh` installs it and does **not** manage its config (Settings Sync owns that file) |
| `fusuma`, `auto-cpufreq`, `peazip` | not packaged; `libinput-gestures` / `tlp` / `xarchiver` are the packaged equivalents |
| `greenclip` | use `CopyQ` or `clipmenu` |

`telegram-desktop`, `evolution` (+ `evolution-data-server`, `evolution-ews` for
Exchange/Office365 — the usual reason an account cannot be added at all),
`thunderbird`, `qbittorrent`, `keepassxc`,
`syncthing`, `firefox`, `chromium`, `vlc`, `mpv`, `celluloid`, `zathura` +
`zathura-pdf-mupdf`, `nsxiv`, `imv`, `vscode`, `steam` (needs
`void-repo-nonfree` + `void-repo-multilib`), `lutris`, `gamemode`, `MangoHud`,
`flatpak`, `kdeconnect` — all packaged under exactly those names.

### 8.1 Firefox on a low-RAM machine

Nothing to install — it is prefs. `modules/firefox.sh` writes
`dotfiles/firefox/user.js` into every profile; the two knobs that actually move
the needle are `dom.ipc.processCount` (default 8, each process is a fixed
~80-150 MB floor even idle) and `browser.sessionhistory.max_total_viewers`
(fully-live back/forward DOM+JS heaps). `browser.tabs.unloadOnLowMemory` is the
third: it drops background tabs under memory pressure instead of letting the OOM
killer choose. user.js rather than prefs.js, because Firefox rewrites prefs.js on
exit and anything put there is lost.

---

## S. Services (runit, not systemd)

Void names its runit services differently from systemd units. Enable with
`ln -s /etc/sv/<name> /var/service/` — which is exactly what `enable_service`
already does for `OSR_INIT=runit`.

| logical | systemd unit | Void `/etc/sv/` |
|---|---|---|
| dbus | `dbus.service` | `dbus` |
| elogind | (built into systemd) | `elogind` |
| NetworkManager | `NetworkManager.service` | `NetworkManager` |
| lightdm | `lightdm.service` | `lightdm` |
| bluetooth | `bluetooth.service` | **`bluetoothd`** |
| cups | `cups.service` | **`cupsd`** |
| avahi | `avahi-daemon.service` | `avahi-daemon` |
| acpid | `acpid.service` | `acpid` |
| sshd | `sshd.service` | `sshd` |

The two that actually differ are handled by new `@runit` facet rows in
`lib/servicemap` (`bluetooth@runit = bluetoothd`, `cups@runit = cupsd`) — the same
"most specific match wins" trick `pkgmap` already uses, so no module grows an
init `case`.

`dbus` and `elogind` must be enabled **before** a graphical session, or polkit,
udisks automount, screen locking on lid-close and portals all fail silently.

---

## 9. What this produced in the repo

- `lib/pkgmap/xbps.map` — every row above that differs from the logical name.
- `lib/servicemap` + `lib/service.sh` — `@<init>` facet support, `bluetooth`/`cups`.
- `modules/` — 37 new distro-agnostic modules (`xorg`, `picom`, `polybar`,
  `rofi`, `dunst`, `i3lock`, `feh`, `flameshot`, `copyq`, `arandr`, `redshift`,
  `xdg`, `polkit-agent`, `gvfs`, `keyring`, `thumbnails`, `codecs`, `theming`,
  `lightdm`, `audio`, `blueman`, `brightnessctl`, `thunar`, `input`, `fcitx5`,
  `avahi`, `ufw`, `kdeconnect`, `dnscrypt`, `power`, `disks`, `archives`,
  `viewers`, `vlc`, `evolution`, `thunderbird`, `vscode`, plus rewritten `i3`, `firefox`,
  `micro` and `fastfetch`).
- Every module now declares `# session: x11 | wayland | x11+wayland` on its
  first line, enforced by `test/lint.sh`, so
  `grep -l '^# session: wayland' modules/*.sh` answers "what does moving this
  rice to X11 break" without opening 110 files.
- `rices/i3-rosemary/` — the rose-muted dark rice (palette derived from
  `dcindallas/dots-new`).
