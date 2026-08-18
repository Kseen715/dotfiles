# os-rice

A DRY, declarative, POSIX-portable installer for unix-like apps, configs, and
whole rices. One module written **once** installs across package managers; a
rice is a plain list of what to install. See [DESIGN.md](DESIGN.md) for the full
rationale.

> Status: **past MVP**. The harness (`lib/`, `install.sh`, `osr`) plus ~70
> modules are complete and pass the idempotency matrix on apt/apk/pacman. Every
> legacy per-distro tree is gone: the last one,
> `linux-arch-x86_64-hyprland-glass/`, is now `rices/arch-hyprland-glass/` +
> `modules/*.sh`. The Windows side is gone from sh entirely: the old
> `windows-11-x86_64/` PowerShell tree now lives in the C core as
> `modules/win-*.c` + `lib/wintweak.c` (see [PLAN_UNIVERSAL.md](../PLAN_UNIVERSAL.md)).

## Layout

```text
os-rice/
  osr.c                the POSIX harness core: one binary (build/osr) that
                       nob.c links from the lib/ units below, the same
                       way install.c + lib/*.c make the Windows core
  lib/                 the shell-callable surface + the core's units
    ui.sh  log.sh      run_step's fork, error()'s exit, the palette
    user.sh            as_user/as_root and the writes that go through them
    detect.sh  theme.sh   eval the facts the core prints, for modules to read
    ui.c  log.c  state.c  user.c  detect.c  theme.c  install.c  testrun.c
                       what those shims call, one unit per <name>.sh:
                       `osr ui`, `osr log`, `osr state`, `osr user`,
                       `osr detect`, `osr theme`, `osr install`, `osr test-run`.
                       state.c owns its writes outright - there is no state.sh
    module.h  module.c  the API a Linux module written in C may call
    modules.c          the registry of those modules (`osr module`)
    common.h/.c        what the units share (buffer, printf %b, log line)
    cmds.h             one declaration per command entry point
    winui.* winstate.* the WINDOWS core's own ui/state (its win* family, next
                       to winpkg/winbin/wintweak) - unrelated to the units above
    pkg.sh             pkg_install/installed/refresh/remove + provider dispatch
    net.sh git.sh      download + github_latest; git repo / oh-my-zsh helpers
    service.sh         enable_service/disable_service (systemd/openrc/runit/sysv)
    config.sh          layered config: seed_once / install_layer / loader block
    apply.sh           theme-only apply: the hotkey path, mutating verbs stubbed
    reload.sh          tell the running apps to re-read their new config
    pkgmap/            logical name -> real package(s), per manager
    servicemap         logical service -> real unit (only where they differ)
  modules/             ONE copy each, POSIX, distro-agnostic (zsh.sh)
    <name>.c           modules written in C instead - same rice.list entry,
                       registered in lib/modules.c (see "Writing a module").
                       One file per module, never one per OS: a module both
                       systems can have holds both branches behind #ifdef
                       _WIN32 (fastfetch.c), a Windows-only one is all
                       Windows branch (wezterm.c, the win-* OS passes)
    win-data/          data files the win-* passes carry (see WINDOWS.md)
  rices/<name>/        rice.list manifest: which PACKAGES, and which themes
  themes/<name>/       theme.list + config/ (the 90-* layers) + wallpapers/
  install.sh           the shared runner: sources the libs and the modules
  wallpaper.sh         set/query the wallpaper of the current theme
  osr                  front-end CLI (install / switch / theme / wallpaper / list)
  bootstrap.sh         barebone entry: find downloader, clone repo, hand off
  test/                lint + hermetic unit tests + docker idempotency matrix
    ref/               frozen sh implementations the C ports are diffed against
../proteus/            the GUI picker (standalone Rust crate, X11 + Wayland)
```

## Writing a module

A module installs one thing. It can be a POSIX shell script under `modules/`
(what the ~116 existing ones are) or a C translation unit `modules/<name>.c`.
A rice manifest names the module either way; `install.sh` asks `osr module has
<name>` and, when the core owns it, runs it there instead of sourcing the
script.

There is exactly one C file per module, never one per OS. A module only Linux
can have is all POSIX code; a module both systems can have carries the Windows
implementation in the same file behind `#ifdef _WIN32`, where it is a row in
`modules.c`'s dispatch instead (`modules/fastfetch.c` is both at once).

```c
/* modules/flameshot.c */
#include "../lib/module.h"

int osrm_flameshot(void) {
    static const char *const pkgs[] = { "flameshot", "maim", "slop", "xclip", NULL };
    return osr_pkg_install_step("Installing screenshot tools", pkgs);
}
```

Then one row in `lib/modules.c` (name, session marker, function) and one line
in `nob.c`'s `posix_srcs`. Everything a module may call is `lib/module.h`:
packages (`osr_pkg_install`, resolved through `lib/pkgmap/` exactly as
`pkg_install` does), steps (`osr_run_step` for a command, `osr_step` for a
function of your own — the thing the shell tier could not do), services,
`as_root`/`as_user` execs, the config-file primitives, and the detected facts.

Three are ported already (`flameshot`, `docker`, `fastfetch`); their shell originals are
frozen under `test/ref/` and `test/unit/module_c_parity.sh` runs both against
stubbed package tooling and diffs every command they issue. Provider-tagged
packages (`cargo:`, `script:`, `aur:`, `source:`) are still `lib/pkg.sh`'s job
— a C module that needs one fails loudly and should stay a script for now.

## Usage

```sh
# From a checkout of this repo:
os-rice/osr install gruvbox            # install a rice for the invoking user
os-rice/osr install --user alice nord  # rice a specific account (user-for-user)
os-rice/osr switch nord                # move to a rice: packages accrete, only
                                       # theme-owned 90-* config + wallpaper swap
os-rice/osr list

# Themes are separate from rices (DESIGN 6a): any theme applies onto any rice,
# in about a second, with no packages, no network and no sudo. This is the one
# to bind to a hotkey.
os-rice/osr theme                      # print the theme in use
os-rice/osr theme nord                 # apply a theme and reload the running apps
os-rice/osr themes                     # list available themes
os-rice/osr wallpaper ~/pic.png        # set this theme's wallpaper (remembered per theme)
os-rice/osr wallpaper --next           # cycle to the next wallpaper
proteus                                # the GUI picker (../proteus), X11 + Wayland

# On a barebone box (no clone yet):
curl -fsSL https://raw.githubusercontent.com/Kseen715/dotfiles/main/os-rice/bootstrap.sh | sh -s -- gruvbox
```

Add `--verbose` to stream command output instead of spinners (also automatic
when stdout is not a TTY, so piping to a logfile stays clean).

## How it works

- **Package method, not just name.** A `pkgmap` row's RHS may carry a provider
  tag (`script:`, `source:`; `cargo:`/`aur:`/… reserved). `pkg_install` expands
  logical names, installs the native batch first, then dispatches tagged rows —
  each provider owns its own idempotency probe. Untagged names pass through
  unchanged, so the common case needs no map row.
- **Every module declares its session.** The first line of each module is
  `# session: x11`, `# session: wayland` or `# session: x11+wayland`, enforced by
  `test/lint.sh`. CLI/system modules are display-server agnostic and so carry
  `x11+wayland`. This is what makes "can this rice move to X11?" a grep instead
  of a reading exercise:
  `grep -l '^# session: wayland' os-rice/modules/*.sh`.
- **Service name, per init.** `servicemap` rows may carry an `@<init>`
  qualifier and the most specific match wins, so one `enable_service bluetooth`
  reaches `bluetooth.service` on systemd and `/etc/sv/bluetoothd` on runit — no
  module ever branches on the init system.
- **Idempotent by contract.** Run a rice 100× and it converges; a second run is
  all `✔ skipped`, zero errors. Guards (`pkg_installed`, `ensure_line`,
  `ensure_block`, `backup_copy`, guard-style PATH) replace raw mutation.
- **Config layered by ownership.** `~/.config/osr/zsh/rc.d/` holds `00-env`
  (user, seeded once), `10-omz`/`20-aliases` (dotfiles, overwritten), `90-theme`
  (rice, swapped on switch), `99-local` (machine, never touched). `~/.zshrc` is a
  thin loader owning only a marked block.

## Adding to a rice

- **An app/module:** add one line to `rices/<name>/rice.list` (the module count
  is the progress-bar denominator).
- **A package that differs per distro:** add a row to `lib/pkgmap/<mgr>.map`.
- **A new rice:** new `rices/<name>/` with a `rice.list`, a
  `config/zsh/90-theme.zsh`, a `config/starship.palette.toml` (colors only — the
  prompt structure is the shared `starship/starship.toml` base), and `wallpapers/`.
- **A rice's prompt colors:** edit `rices/<name>/config/starship.palette.toml`
  (`accent`/`success`/`error`/`secondary`). The shared prompt layout/symbols live
  once in the dotfiles base `starship/starship.toml`; os-rice composes the two.

## Desktops

| rice | what it is |
| ---- | ---------- |
| `arch-hyprland-glass` | Wayland/Hyprland "glass" desktop, Arch + systemd only |
| `i3-rosemary` | i3/X11 desktop in a muted rose dark palette, validated on Void |
| `xin` `catppuccin` `gruvbox` `nord` | shell/CLI rices (no DE) |

The i3 desktop is documented in two files: [`i3-sugg.md`](i3-sugg.md) is the
distro-agnostic component checklist (what an X11 desktop needs and why), and
[`i3-void-packages.md`](i3-void-packages.md) maps every one of those components
to its real Void `xbps` package — including the ones Void does not ship.

## Testing

```sh
sh os-rice/osr test              # fast: POSIX lint + hermetic unit tests
sh os-rice/test/matrix.sh gruvbox # docker/podman double-run idempotency matrix
OSR_TEST_IMAGES="alpine:latest" sh os-rice/test/matrix.sh   # one image
```

CI runs the fast gate on every push and the idempotency matrix across
debian/alpine/arch/fedora (`.github/workflows/os-rice-ci.yml`).

## Not yet migrated (Out of MVP)

Still deliberately out of scope (see DESIGN "MVP scope"): the
`repo:`/`tarball:`/`brew:`/`flatpak:` providers and `osr prune`. Windows is no
longer on this list: it is a C core (`install.c`, `lib/*.c`, `modules/win-*.c`)
driven by `osr.ps1`, not a POSIX-sh target and not a PowerShell tree.

The legacy per-distro bash trees are done. What their migration cannot claim is
*verification*: modules that need a GPU, a display, a real kernel or a
hypervisor are correct-by-construction and unit-tested, but only a real machine
or a QEMU boot exercises them end to end (DESIGN §9). See
[rices/arch-hyprland-glass/README.md](rices/arch-hyprland-glass/README.md) for
the list.
