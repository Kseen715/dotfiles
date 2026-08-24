# i3-rosemary

A full i3/X11 desktop in a muted rose dark palette, on **Void (xbps)** and on
**Debian/Ubuntu (apt)**. Every difference between the two lives in
`lib/pkgmap/{xbps,apt}.map`; no module branches on the distro.

```sh
./osr install i3-rosemary           # full install
./osr switch  i3-rosemary           # from another rice (packages accrete,
                                          # only the 90-* theme layers change)
./osr module  polybar --theme i3-rosemary   # one module, this rice's theme
```

## Where the look comes from

Base colors and the general shape (polybar over gaps, rofi grid, picom blur,
betterlockscreen) are derived from [dcindallas/dots-new][dots] — its Base16
Twilight bases pulled toward rose, plus the pink accent that setup uses for its
rofi selection. The upstream repo drives all of this through `flavours` at
runtime; here the colors are static files instead, because os-rice's config
model is layered ownership (§5), not a theme daemon.

[dots]: https://github.com/dcindallas/dots-new

## Palette

| role | hex | used for |
|---|---|---|
| bg | `#1c1f20` | window/bar background |
| bg-alt | `#24282a` | inputs, cards, tooltips |
| sel | `#34393b` | selection, inactive borders |
| muted | `#4e5456` | disabled text, dividers |
| fg-dim | `#b3a8ab` | secondary text |
| fg | `#e8dfe1` | body text |
| **accent** | **`#d98cae`** | focus, selection, rings, prompts |
| accent-dim | `#a86a84` | pressed/active accent |
| urgent | `#f15495` | urgent windows, critical notifications |
| red | `#e2655e` | errors |
| orange | `#d3877a` | warnings, temps |
| yellow | `#d9b48f` | backlight, modified |
| green | `#9fb08a` | ok, cpu |
| cyan | `#8fb3b0` | verifying, download |
| blue | `#8f95bd` | links, memory |

Exactly one saturated color (the accent) does the work; everything else is a
desaturated neutral. Keep it that way when adding modules.

## Layers this rice owns (§5/§6)

| file | lands at |
|---|---|
| `config/i3/90-theme.conf` | `~/.config/i3/config.d/90-theme.conf` |
| `config/polybar/colors.ini` | `~/.config/polybar/colors.ini` |
| `config/rofi/colors.rasi` | `~/.config/rofi/colors.rasi` |
| `config/dunst/90-theme.conf` | `~/.config/dunst/dunstrc.d/90-theme.conf` |
| `config/picom/90-theme.conf` | `~/.config/picom/90-theme.conf` |
| `config/xprofile/90-theme.sh` | `~/.config/xprofile.d/90-theme.sh` |
| `config/Xresources/colors` | appended to `~/.Xresources` |
| `config/gtk-{2,3,4}.0/*`, `xsettingsd/*`, `qt{5,6}ct/*` | `~/.config/...` |
| `config/betterlockscreen/*` | `~/.config/betterlockscreen/` |
| `config/lightdm/*` | `/etc/lightdm/` (root — the greeter has no home) |
| `config/{ghostty,wezterm,btop,serie}/*`, `starship.palette.toml`, `zsh/90-theme.zsh` | terminal + shell |
| `config/micro/{settings.json,rosemary.micro}` | merged over the dotfiles micro base |
| `config/evolution/gsettings.conf` | applied with `gsettings` (key-by-key) |
| `config/evolution/gtk.css` | `~/.local/share/themes/osr-evolution/gtk-3.0/` |
| `config/firefox/userChrome.css`, `config/thunderbird/userChrome.css` | into every Mozilla profile |
| `config/{zathura,mpv,vlc}/*` | document + media viewers |
| `config/fastfetch/config.jsonc` | truecolor, so a screenshot keeps the palette |
| `config/yazi/theme.toml` | selects `dotfiles/yazi/flavors/rosemary.yazi` |

Everything else in `~/.config` belongs to the dotfiles base layer or to you.
`99-local.conf` / `99-local.sh` are yours and are never rewritten.

## Before first login

1. Drop a wallpaper into `wallpapers/` (see the README there).
2. On Void: `xbps-install -S void-repo-nonfree void-repo-multilib` if you want
   `steam`. On Debian/Ubuntu: `dpkg --add-architecture i386` for the same reason.
3. Run `sensors-detect` once so polybar's temperature module has a zone.
4. Set your location in `redshift/redshift.conf` — the default is Moscow.
5. Save a monitor layout with `autorandr --save <name>`; the i3 config runs
   `autorandr --change` at startup.

## What makes it feel like a distro and not a bare WM

These are the pieces a desktop environment provides that i3 has none of, and
each one is a bug you would otherwise hit and not connect back to the WM:

| you press / click | what makes it work |
|---|---|
| volume / brightness keys | `~/.config/i3/scripts/osd.sh` — changes the level *and* draws the progress popup, redrawn in place. It probes for pamixer, then pactl, then wpctl, so the keys work on releases that package none of the first choice. |
| nothing, for ten minutes, mid-film | `xidlehook --not-when-audio --not-when-fullscreen`. `xautolock` (the fallback where there is no Rust toolchain) counts wall-clock idle and will blank the screen during a video. |
| Thunar → "Open Terminal Here" | `modules/helpers.c` — Thunar shells out to `exo-open --launch TerminalEmulator`, which resolves through `~/.config/xfce4/helpers.rc`. Nothing outside XFCE ever writes that file, so without it the menu entry is present and does nothing. |
| `$mod+Return` when ghostty is broken | `xterm`, installed by the same module purely as the escape hatch. |
| a GUI app asking for root | `polkit-agent` — without it every such action fails silently. |
| "Move to Trash" | `gvfs`. |
| plugging in a USB stick | `udiskie --tray` over `udisks2`. |
| a Flatpak's file dialog | `xdg-desktop-portal-gtk`, pinned in `i3-portals.conf` because i3 has no portal backend of its own. |
| your theme, in every app | `xsettingsd` — i3 has no settings daemon, which is why "the theme only works in some apps" is the most common i3 complaint. |

## Two config shapes worth knowing

Most layers are plain drop-in files. Two apps cannot do that and are handled by
composition instead (`lib/config.sh`):

- **One-JSON editors** (micro) have no include, so `compose_json_config` merges
  the rice's theme keys over the dotfiles base. The installed `settings.json` is
  *generated output* — edit the base or the rice fragment.
- **Mozilla apps** (Firefox, and Thunderbird if you swap it in) keep settings in a randomly-named
  profile directory, so `install_mozilla_layer` walks `profiles.ini` and writes
  `user.js` + `chrome/userChrome.css` into each one. An app that has never been
  launched has no profile yet: the module says so and succeeds — rerun it after
  the first start.

## Two apps that are deliberately different

- **Evolution** stores its look in GSettings, not a config file, and it is GTK3 —
  which has no per-application CSS selector. So the rice ships a *private* GTK
  theme (`~/.local/share/themes/osr-evolution/`) and a `.desktop` override that
  selects it with `GTK_THEME=`, instead of writing rules into
  `~/.config/gtk-3.0/gtk.css` where they would restyle every GTK app. The
  GSettings half is applied key-by-key and skips whatever the installed
  Evolution version does not have, so it survives upgrades in both directions.
  It is the rice's one mail client; `thunderbird` remains an available module.
- **VS Code** is installed and then left alone. It has its own Settings Sync and
  profiles, which write the same `settings.json` os-rice would own — two managers
  on one file means the last writer wins and the other silently loses edits.
  Pick the theme inside VS Code.

## Debian/Ubuntu notes

Nothing extra to do — the manifest is the same. Two things worth knowing:

1. `firefox` on **Ubuntu** is a snap stub, and the snap keeps its profile under
   `~/snap/firefox/`. `modules/firefox.sh` follows the profile rather than
   fighting the package, so the theme lands either way. Debian gets
   `firefox-esr`, which uses the classic profile root.
2. `require: distro:void|debian|ubuntu` is checked before anything is written.
   On any other distro the run exits having touched nothing, and says so.

## Known gaps, per target

| component | Void | Debian/Ubuntu |
|---|---|---|
| rose GTK/Kvantum theme | not packaged — Adwaita-dark + a `gtk.css` accent override | same |
| Bibata/Capitaine cursors | not packaged — `Vanilla-DMZ` is the stand-in | packaged, but the rice stays on the same stand-in for one look on both |
| `betterlockscreen` | packaged | built by `provide_betterlockscreen` (upstream script) |
| `autotiling` | packaged | packaged from trixie; built from the upstream script elsewhere |
| `xidlehook` (idle inhibits) | `cargo:` — `rust` is listed before `i3lock` | same |
| `xcolor`, `ouch` | packaged | `cargo:` |
| `rofimoji`, `rofi-calc`, `rofi-emoji` | packaged | not in the archive — skipped, fcitx5's own emoji picker remains |
| `libinput-gestures` (swipes) | packaged | not in the archive — skipped |
| `keyd` (kernel-level remap) | packaged | trixie only; `xcape` covers dual-role keys elsewhere |
| RAW thumbnails | `libopenraw-pixbuf-loader` | no `libopenraw` at all — RAW previews are lost |
| `discord`, `gradia` | Flatpak | Flatpak |
| `elogind` | installed (runit has no logind) | **not** installed — systemd provides it, and apt would remove `systemd-sysv` to make room |

See `../../i3-void-packages.md` for the Void component → package writeup, and
the third-pass block in `lib/pkgmap/apt.map` for the apt one — every row there
was checked against the real binary index for bullseye, bookworm, trixie, jammy
and noble.
