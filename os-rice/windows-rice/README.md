# windows-rice

The WezTerm + oh-my-posh + PowerShell 7 rice on Windows. Windows' own tree,
deliberately outside the POSIX `os-rice/lib` abstraction (see
[`../DESIGN.md`](../DESIGN.md): "different package model, different language
... do NOT force it into this abstraction") — but shaped exactly like the
Linux side: `lib` (`src/`) + `modules/` + `rices/` + `themes/`, one runner
that knows the whole rice. Unrelated to
[`../windows-11-x86_64`](../windows-11-x86_64) (OS debloat/tweaks) — no
elevation needed here.

```powershell
# From a checkout of this repo:
.\os-rice\windows-rice\rice.ps1                       # install the default rice, theme xin (see Themes below)
.\os-rice\windows-rice\rice.ps1 -Theme nord            # ...or any other real Linux theme
.\os-rice\windows-rice\rice.ps1 -Ask                   # confirm each config overwrite interactively
.\os-rice\windows-rice\rice.ps1 -Module wezterm        # install/reinstall just one module
.\os-rice\windows-rice\rice.ps1 -Save                  # pull live dotfiles-owned configs back into the repo

# or the CLI front end (mirrors ../osr):
.\os-rice\windows-rice\osr.ps1 install
.\os-rice\windows-rice\osr.ps1 save
.\os-rice\windows-rice\osr.ps1 module wezterm
.\os-rice\windows-rice\osr.ps1 list
```

Rerunning is safe: every package install checks whether its binary is already
on `PATH` first (a no-op if so), and a config copy either overwrites silently
(default — the repo is the source of truth for a dotfiles rice) or, with
`-Ask`, prompts per file/directory before touching anything that already
exists.

## Layout

```text
windows-rice/
  windows.map            logical name -> real id per manager (mirrors ../lib/pkgmap/*.map)
  rices/
    default/rice.list      which modules this rice installs (mirrors ../rices/<name>/rice.list)
  themes/
    osr-rice/
      theme.list            a REAL palette (mirrors ../themes/<name>/theme.list exactly -- see Themes below)
      config/oh-my-posh/      the one app with no .tmpl: its literal theme file (the escape hatch, see Themes)
  modules/                one file per app, ONE copy each (mirrors ../modules/*.sh)
    pwsh.ps1                Install-Pwsh / Save-Pwsh
    wezterm.ps1              Install-Wezterm / Save-Wezterm
    oh-my-posh.ps1            Install-OhMyPosh / Save-OhMyPosh
    fastfetch.ps1              Install-Fastfetch / Save-Fastfetch
  src/                    shared lib
    common.ps1              echo helpers + Update-SessionEnvironment (ASCII-only output, see below)
    pkg.ps1                  windows.map parsing + scoop -> choco -> winget dispatch
    fonts.ps1                 Nerd Font install (package manager, else GitHub release)
    config.ps1                 shared file/dir copy engine (Install-RiceConfig / Save-RiceConfig)
    theme.ps1                   theme.list parsing + {{role}} template rendering + Install-ThemeLayer
  rice.ps1                the runner: reads a rice.list, dispatches each module
  osr.ps1                 thin CLI front end over rice.ps1 (install / save / module / list)
```

Each module's *dotfiles-owned* base config still lives beside its app at the
repo root — `../../wezterm/.wezterm.lua`, `../../PowerShell7-profile/` — same
as Linux's `modules/wezterm.sh` reading from `$OSR_DOTFILES/wezterm/`. The
*theme-owned* layer (a look, not a structure) is rendered dynamically — see
Themes below.

## Themes: rendered dynamically from the real Linux templates

`src/theme.ps1` is a PowerShell port of `../lib/theme.sh`'s `_osr_theme_sed` /
`render_theme_template` — not a Windows reimplementation of the *idea*, the
same mechanism against the same files. `-Theme <name>` resolves, in order:

1. `themes/<name>/config/<app>/<file>` — a literal Windows-local file (the
   same escape hatch Linux themes have for a look that isn't a palette
   substitution; `oh-my-posh` always takes this branch, since a `.omp.json`
   theme has no `.tmpl` on either side).
2. `themes/<name>/theme.list` (Windows-local) or `../themes/<name>/theme.list`
   (a **real Linux theme** — `nord`, `gruvbox`, `catppuccin`, `glass`,
   `rosemary`, `xin`) — whichever exists, rendering `../<app>/<file>.tmpl`
   against it: the exact template the Linux rices render, substituting
   `{{role}}`/`{{role_rgb}}`/`{{role_dec}}`/`{{role_sgr}}` from real `color:`
   lines, same as `_osr_theme_sed`.
3. Neither exists → `$null`, and the module falls back to its own unthemed
   dotfiles default (`wezterm.ps1` falls back to the literal
   `../../wezterm/wezterm-theme.toml`; `fastfetch.ps1` just leaves fastfetch's
   built-in default in place).

This is what "dynamic" means here concretely: `.\rice.ps1 -Theme nord` renders
WezTerm's and fastfetch's real `.tmpl` files against nord's actual
`../themes/nord/theme.list` — not a hand-copied approximation of nord that
can silently drift from what Linux ships. `themes/osr-rice/theme.list` is
just one more theme.list by the same rules: a Windows-native palette,
interchangeable with a Linux one as far as the renderer is concerned — it's
just no longer `rice.ps1`'s own default, `xin` (a real Linux theme) is. It
also remains the only theme with a Windows-local oh-my-posh prompt
(`config/oh-my-posh/`); `modules/oh-my-posh.ps1` falls back to it, with a
warning, under any `-Theme` that ships none of its own.

**A rendered theme layer can't be `-Save`d** the way dotfiles-owned config
can — there's no sensible "installed file → repo" direction for something
computed from a template. `Save-Wezterm`/`Save-Fastfetch` say so and only
save the dotfiles-owned half; edit the `.tmpl` or the theme's `theme.list` to
change a look.

## Why ASCII-only output (`src/common.ps1`)

`../DESIGN.md` requires ASCII-only program output on the Linux side because a
UTF-8-no-BOM script read on a non-UTF-8 locale mis-renders. The Windows side
hit the same bug class concretely: the original emoji-laden `install.ps1`
scripts (`[✨]`, `[❌]`, `[❓]`), read via `powershell.exe -File` on a
non-Latin-1 codepage (e.g. a Cyrillic Windows install), decoded through the
wrong codepage badly enough to desync execution — a file-count check that
provably evaluated `True` still fell through to the `False` branch, because
the runtime wasn't parsing the bytes the editor showed. Every message here is
ASCII for the same reason it is on the POSIX side: it decodes identically
under any codepage, so the bug class doesn't exist.

## A resolved-path lesson (`modules/pwsh.ps1`)

The pwsh profile install used to assemble its target as
`$HOME\Documents\PowerShell\...`. That silently breaks whenever Documents is
redirected (OneDrive Known Folder Move, a manual folder-redirection policy,
Documents living on a different drive) — the file installs fine, pwsh just
never looks there. `Resolve-PwshProfilePath` asks the installed `pwsh` binary
for its own `$PROFILE.CurrentUserCurrentHost` instead of reconstructing the
path by hand, so it's correct under any redirection. The general lesson: when
an app can tell you its own path, ask it — don't rebuild the path from parts
that can individually drift.

## Adding an app to the rice

1. Add a row to `windows.map` with that app's id per manager (check what
   `scoop search` / `choco search` / `winget search` report — not every app is
   in every one).
2. Add `modules/<name>.ps1` with `Install-<Name>`/`Save-<Name>` functions.
   Dotfiles-owned config: `Copy-ConfigEntry`/`Install-RiceConfig`
   (`src/config.ps1`) from the repo root. A themed look: does the app already
   have a Linux `<app>/<file>.tmpl`? Use `Install-ThemeLayer` (`src/theme.ps1`)
   and it renders for free under any theme, Windows or Linux. No template on
   either side (like oh-my-posh)? `Get-ThemeConfig` for the literal-file-only
   lookup.
3. Add the module name to `rice.ps1`'s `$ModuleFns` map and to
   `rices/default/rice.list`.

## Adding a theme

A new Windows-native theme is just `themes/<name>/theme.list` with `color:`
lines for whatever roles the templates you care about actually use (an
app renders with the roles it finds; a role the theme omits is a warning, not
a failure — `Expand-ThemeTemplate` prints which one) plus, optionally,
`themes/<name>/config/<app>/<file>` for an app with no `.tmpl` or a look
that isn't a palette substitution. Run with `-Theme <name>`.

Nothing else to do for a theme that already exists on the Linux side —
`-Theme nord`/`gruvbox`/`catppuccin`/`glass`/`rosemary`/`xin` already works,
today, with no Windows-side file at all.
