# void-i3-rosemuted

A full i3/X11 desktop on Void Linux in a muted rose dark palette.

```sh
./osr install void-i3-rosemuted           # full install
./osr switch  void-i3-rosemuted           # from another rice (packages accrete,
                                          # only the 90-* theme layers change)
./osr module  polybar --theme void-i3-rosemuted   # one module, this rice's theme
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
| `config/micro/{settings.json,rosemuted.micro}` | merged over the dotfiles micro base |
| `config/evolution/gsettings.conf` | applied with `gsettings` (key-by-key) |
| `config/evolution/gtk.css` | `~/.local/share/themes/osr-evolution/gtk-3.0/` |
| `config/firefox/userChrome.css`, `config/thunderbird/userChrome.css` | into every Mozilla profile |
| `config/{zathura,mpv,vlc}/*` | document + media viewers |
| `config/fastfetch/config.jsonc` | truecolor, so a screenshot keeps the palette |
| `config/yazi/theme.toml` | selects `dotfiles/yazi/flavors/rosemuted.yazi` |

Everything else in `~/.config` belongs to the dotfiles base layer or to you.
`99-local.conf` / `99-local.sh` are yours and are never rewritten.

## Before first login

1. Drop a wallpaper into `wallpapers/` (see the README there).
2. `xbps-install -S void-repo-nonfree void-repo-multilib` if you want `steam`.
3. Run `sensors-detect` once so polybar's temperature module has a zone.
4. Set your location in `redshift/redshift.conf` — the default is Moscow.
5. Save a monitor layout with `autorandr --save <name>`; the i3 config runs
   `autorandr --change` at startup.

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

## Known gaps on Void

- No packaged rose GTK/Kvantum theme — the rice uses Adwaita-dark plus a
  `gtk.css` accent override rather than vendoring a theme.
- No Bibata/Capitaine cursors; `Vanilla-DMZ` is the packaged stand-in.
- `discord` and `gradia` are Flatpak-only.
- `xidlehook` needs a Rust toolchain; the rice uses `xautolock` + `xss-lock`.

See `../../i3-void-packages.md` for the full component → package writeup.
