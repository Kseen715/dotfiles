# gruvbox rice

A shell + KDE/Plasma gruvbox desktop. Originally the standalone
`linux-debian-x86_64-kde-gruvbox` bundle (Debian 12 / KDE Plasma / KWin), now a
declarative os-rice rice — install it with:

```sh
osr install gruvbox          # or: ./install.sh gruvbox
```

<details open><summary><b>Inspired by:</b></summary>

  - [Reddit - Shaggy96Fi](https://www.reddit.com/r/unixporn/comments/lydglv/kdeplasma_easy_on_the_eyes_gruvbox_theme/)
  - [Reddit - 8KCoffeeWizard](https://www.reddit.com/r/unixporn/comments/y72zlv/kde_kde_rice_without_blur_real/)
</details>

## What this rice is

- **DE / WM:** KDE Plasma / KWin
- **Terminal:** foot (os-rice module) or Konsole
- **Shell:** zsh + oh-my-zsh + [Starship](https://starship.rs/) (the `zsh` module)
- **Fonts:** [Fira Code Nerd](https://github.com/ryanoasis/nerd-fonts/releases/download/v3.2.0/FiraCode.zip) / [Iosevka](https://typeof.net/Iosevka/)
- **Color scheme:** Gruvbox-Dark-B-LB
- **Icons:** Gruvbox

The `zsh` module installs the shell, prompt and layered rc.d config; the
rice-owned starship + `90-theme.zsh` set the gruvbox prompt (swapped on
`osr switch`).

## What os-rice vendors vs. what you download

Following the config-vs-program-data split (DESIGN G5), this rice vendors only
small **local theme config**; anything installable or downloadable is left to be
fetched, not committed:

**Vendored here (rice-owned config):**

- `config/gtk-4.0/`, `config/fontconfig/`, `config/xsettingsd/` — GTK theme
  selection, font rendering, and xsettings for GTK apps under KDE. Applied to
  `~/.config` automatically via the manifest `config:` directive.
- `kde-theme/color-schemes/GruvboxColors.colors` — KDE color scheme.
- `kde-theme/konsole/` — Konsole gruvbox colorscheme + profile.
- `kde-theme/gtkrc-2.0` — GTK2 rc (copy to `~/.gtkrc-2.0`).

The `kde-theme/` files are applied manually (see below) — full KDE/Plasma
theming is a desktop concern outside the container-tested install path.

**Download separately (not vendored — installed packs / large binaries):**

- **Global Theme (Plasma look-and-feel, aurorae, desktop theme):** [store.kde.org/p/1327723](https://store.kde.org/p/1327723)
- **Login Screen (SDDM):** [store.kde.org/p/1214121](https://store.kde.org/p/1214121)
- **Screen-locking wallpapers:** [store.kde.org/p/1069729](https://store.kde.org/p/1069729)
- **Konsole theme:** [store.kde.org/p/1327725](https://store.kde.org/p/1327725)
- **Gruvbox icon theme:** install from your distro or its upstream project.
- **Wallpaper:** [gruvbox_forest-4.png](https://raw.githubusercontent.com/D3Ext/aesthetic-wallpapers/main/images/gruvbox_forest-4.png)

## Applying the KDE color theme manually

```sh
# color scheme + konsole (KDE reads these from ~/.local/share)
mkdir -p ~/.local/share/color-schemes ~/.local/share/konsole
cp kde-theme/color-schemes/GruvboxColors.colors ~/.local/share/color-schemes/
cp kde-theme/konsole/*                            ~/.local/share/konsole/
cp kde-theme/gtkrc-2.0                             ~/.gtkrc-2.0
```

Then in System Settings: Colors → *Gruvbox*, and apply the downloaded Global
Theme. Konsole → Settings → pick the *Gruvbox* colorscheme / *Profiel* profile.
