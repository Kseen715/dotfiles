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
.\os-rice\windows-rice\rice.ps1                    # install the default rice, default theme
.\os-rice\windows-rice\rice.ps1 -Ask                # confirm each config overwrite interactively
.\os-rice\windows-rice\rice.ps1 -Module wezterm     # install/reinstall just one module
.\os-rice\windows-rice\rice.ps1 -Save               # pull live configs back into the repo

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
      theme.list            metadata (mirrors ../themes/<name>/theme.list, no template engine yet -- see the file)
      config/oh-my-posh/      the theme's literal per-app files (its "90-*" layer)
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
    theme.ps1                   Get-ThemeConfig: theme file if the theme ships one, else $null
  rice.ps1                the runner: reads a rice.list, dispatches each module
  osr.ps1                 thin CLI front end over rice.ps1 (install / save / module / list)
```

Each module's *dotfiles-owned* base config still lives beside its app at the
repo root — `../../wezterm/.wezterm.lua`, `../../PowerShell7-profile/` — same
as Linux's `modules/wezterm.sh` reading from `$OSR_DOTFILES/wezterm/`. Only
the *theme-owned* layer (a look, not a structure) lives under `themes/`; see
`themes/osr-rice/theme.list` for why `wezterm-theme.toml` specifically stays
at the dotfiles level rather than being duplicated into the theme folder.

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
2. Add `modules/<name>.ps1` with `Install-<Name>`/`Save-<Name>` functions
   calling `Install-RiceConfig`/`Save-RiceConfig` (`src/config.ps1`) — dotfiles
   config from the repo root, theme config via `Get-ThemeConfig` (`src/theme.ps1`)
   if the app has a look worth swapping.
3. Add the module name to `rice.ps1`'s `$ModuleFns` map and to
   `rices/default/rice.list`.

## Adding a theme

New `themes/<name>/theme.list` + `themes/<name>/config/<app>/<file>` for
whichever apps this theme actually overrides — an app with no override for a
theme just keeps using its dotfiles default (`Get-ThemeConfig` returns
`$null`, the module falls back). Run with `-Theme <name>`.
