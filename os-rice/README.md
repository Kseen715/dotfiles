---
title: os-rice
type: readme
status: past-MVP
updated: 2026-08-28
tags:
  - kind/readme
  - topic/os-rice
  - topic/theming
  - lang/sh
  - lang/c
  - os/linux
---

# os-rice

A DRY, declarative, POSIX-portable installer for unix-like apps, configs and
whole rices. One module written **once** installs across package managers; a
rice is a plain list of what to install.

**Read next:** [[os-rice/DESIGN|DESIGN]] (the rationale) ·
[[PLAN_UNIVERSAL]] (the Windows/legacy C core) ·
[[os-rice/modules/WINDOWS|the win- modules]] · [[archive-decisions]]

> [!important] Where this is going: the backbone becomes C
> The shell tier works, but it is not the destination. The libs, the runner
> and the front end are being rewritten as C translation units linked into one
> binary (`build/osr`) — the shape the Windows core already has. Eight units
> are done; `lib/pkg.sh` is the blocker for the rest, since its providers are
> what pin most modules to the shell tier. Score and order:
> [[os-rice/DESIGN#13. The port, and what is left of it|DESIGN 13]].

> [!success] Status: past MVP
> The harness (`lib/`, `install.sh`, `osr`, `build/osr`) plus **115 shell
> modules and 11 C modules** pass the idempotency matrix on apt/apk/pacman.
> Every legacy per-distro tree is gone, Windows included — it is now a C core
> (`install.c`, `lib/win*.c`, `modules/win-*.c`), not a PowerShell tree.

---

## Layout

```text
os-rice/
  osr.c                the POSIX core: one binary (build/osr) that nob.c
                       links from the lib/ units, the same way install.c +
                       lib/*.c make the Windows core
  lib/
    ui.sh log.sh       the shell-callable surface: run_step's fork,
    user.sh            error()'s exit, as_user/as_root, and the shims that
    detect.sh theme.sh eval the facts the core prints, for modules to read
    ui.c log.c state.c what those shims call - one unit per <name>.sh:
    user.c detect.c    `osr ui`, `osr log`, `osr state`, `osr user`,
    theme.c install.c  `osr detect`, `osr theme`, `osr install`,
    testrun.c          `osr test-run`. state.c owns its writes outright:
                       there is no state.sh
    module.h/.c        the API a POSIX module written in C may call
    modules.c          the registry of those modules (`osr module`)
    common.h/.c        buffer, printf %b, the log line
    cmds.h             one declaration per command entry point
    render.c           the {{role}} template renderer, shared with theme.c
    undervolt.c        `osr undervolt cpu` (DESIGN 12)
    uv/                backend.h + generic_opp.c + journal.c
    benchmark.c        `osr benchmark cpu|sensors` (DESIGN 12)
    bench/             cpu.c power.c util.c
    winui.c winstate.c the WINDOWS core's own ui/state, beside winpkg/
    winpkg.c winbin.c  winbin/wintweak/elevate - unrelated to the units
    wintweak.c         above, and never built into the POSIX binary
    elevate.c
    pkg.sh             pkg_install/installed/refresh/remove + providers
    build.sh           the source: builders (~1250 lines of them)
    net.sh git.sh      download + github_latest; repo / oh-my-zsh helpers
    service.sh         enable_service/disable_service, 4 init systems
    config.sh          seed_once / install_layer / loader block / templates
    apply.sh           theme-only apply: the hotkey path, verbs stubbed
    reload.sh          tell the running apps to re-read their config
    preflight.sh       the require: predicates
    fonts.sh gnome.sh migrate.sh
    pkgmap/            logical name -> real package(s), per manager
    servicemap         logical service -> real unit, where they differ
  modules/             115 POSIX shell modules + 11 C ones. ONE file per
    <name>.sh          module, never one per OS: a module both systems can
    <name>.c           have holds both branches behind #ifdef _WIN32
    win-*.c            the Windows OS passes (see WINDOWS.md)
    win-data/          data files those passes carry
  rices/<name>/        rice.list: which PACKAGES, and which themes
  themes/<name>/       theme.list + config/ (the 90-* layers) + wallpapers/
  install.sh           the shared runner: sources the libs and the modules
  wallpaper.sh         set/query the wallpaper of the current theme
  osr                  front-end CLI
  osr.ps1 / osr.bat    the Windows front end, mirroring it
  bootstrap.sh         barebone entry: find downloader, clone, hand off
  nob.c                the build script (a C program, not a Makefile)
  test/                lint + hermetic unit tests + docker matrix
    ref/               frozen sh implementations the C ports are diffed at
../proteus/            the GUI picker (standalone Rust crate, X11+Wayland)
```

> [!note] `Makefile` is a dev convenience shim
> `nob.c` is the build. Nothing in the tool calls make.

---

## Usage

```sh
os-rice/osr install gruvbox            # rice for the invoking user
os-rice/osr install --user alice nord  # rice a specific account
os-rice/osr switch nord                # packages accrete; only 90-* config
                                       # and the wallpaper swap
os-rice/osr list
```

Themes are separate from rices ([[os-rice/DESIGN#6a. Themes are objects, not a folder inside a rice|DESIGN 6a]]): any theme applies onto any
rice, in about a second, with no packages, no network and no sudo. **This is
the one to bind to a hotkey.**

```sh
os-rice/osr theme                      # print the theme in use
os-rice/osr theme nord                 # apply, then reload the running apps
os-rice/osr themes
os-rice/osr wallpaper ~/pic.png        # remembered per theme
os-rice/osr wallpaper --next
proteus                                # the GUI picker (X11 + Wayland)
```

```sh
os-rice/osr module <name>              # one module, nothing else
os-rice/osr undervolt cpu probe        # never writes anything
build/osr benchmark cpu                # core command; ./osr has no verb for it
```

On a barebone box with no clone yet:

```sh
curl -fsSL https://raw.githubusercontent.com/Kseen715/dotfiles/main/os-rice/bootstrap.sh | sh -s -- gruvbox
```

`--verbose` streams command output instead of spinners — automatic when stdout
is not a TTY, so piping to a logfile stays clean.

---

## How it works

- **Package method, not just name.** A `pkgmap` row's RHS may carry a provider tag (`script:`, `source:`, `cargo:`, `aur:`). `pkg_install` expands logical names, installs the native batch in one call, then dispatches tagged rows — each provider owning its own idempotency probe. Untagged names pass through unchanged, so the common case needs no row at all.
- **Every module declares its session.** Line one is `# session: x11`, `wayland` or `x11+wayland`, enforced by `test/lint.sh`. That makes "can this rice move to X11?" a grep: `grep -l '^# session: wayland' os-rice/modules/*.sh`.
- **Service name, per init.** `servicemap` rows may carry `@<init>`, most specific wins — one `enable_service bluetooth` reaches `bluetooth.service` on systemd and `/etc/sv/bluetoothd` on runit. No module branches on the init system.
- **Idempotent by contract.** Run a rice 100x and it converges; a second run is all `[ok] skipped`, zero errors.
- **Config layered by ownership.** `00-env` (user, seeded once), `10-*`/`20-*`/`30-*` (dotfiles, overwritten), `90-theme` (rice, swapped), `99-local` (machine, never touched). `~/.zshrc` is a thin loader owning only a marked block.

---

## Writing a module

A module installs one thing. It is either a POSIX shell script under
`modules/` or a C translation unit `modules/<name>.c`. A rice names the module
either way; `install.sh` asks `osr module has <name>` and the core runs it when
it owns it. **C is the target tier, and shell is legacy** — every `.sh` module
carries a `# legacy: sh` marker enforced by `test/lint.sh`. See
[[os-rice/DESIGN#11a. Every `.sh` module is legacy|DESIGN 11a]] and
[[os-rice/DESIGN#13. The port, and what is left of it|DESIGN 13]].

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

**Ported so far:** `flameshot`, `docker`, `fastfetch`, `tcc`, and `helpers`
(POSIX), plus `wezterm`, `pwsh`, `oh-my-posh` and the three `win-*` passes
(Windows). Each shell original is frozen under `test/ref/`, and
`test/unit/module_c_parity.sh` runs both against stubbed package tooling and
diffs every command they issue. `helpers.c` never had a `.sh` form, so its
scenario asserts behaviour directly instead.

> [!warning] Provider-tagged packages block a port
> `cargo:`, `script:`, `aur:` and `source:` are still `lib/pkg.sh`'s job. A C
> module needing one fails loudly and should stay a script for now —
> `modules/i3lock.sh` is the canonical example.

---

## Adding to a rice

- **An app/module:** one line in `rices/<name>/rice.list` (that count is the progress-bar denominator).
- **A package that differs per distro:** one row in `lib/pkgmap/<mgr>.map`.
- **A new rice:** `rices/<name>/` with a `rice.list`, a `config/zsh/90-theme.zsh`, a `config/starship.palette.toml` (colors only) and `wallpapers/`.
- **A theme's colors:** `themes/<name>/theme.list`. Templates live beside each app's dotfiles, one per app — never one per theme.

---

## Desktops

| rice | what it is |
| --- | --- |
| [[os-rice/rices/arch-hyprland-glass/arch-hyprland-glass\|arch-hyprland-glass]] | Wayland/Hyprland "glass" desktop, Arch + systemd only |
| [[os-rice/rices/i3-rosemary/i3-rosemary\|i3-rosemary]] | i3/X11 in a muted rose palette, Void + Debian/Ubuntu |
| [[os-rice/rices/gruvbox/gruvbox\|gruvbox]], `xin`, `catppuccin`, `nord` | shell/CLI rices, no DE |

The i3 desktop has two reference notes: [[os-rice/i3-sugg|i3-sugg]] (what an
X11 desktop needs, and why) and [[os-rice/i3-void-packages|i3-void-packages]]
(each component mapped to its real Void `xbps` package).

---

## Testing

```sh
sh os-rice/osr test                # POSIX lint + hermetic unit tests
sh os-rice/test/matrix.sh gruvbox  # docker/podman double-run idempotency
OSR_TEST_IMAGES="alpine:latest" sh os-rice/test/matrix.sh
```

CI runs the fast gate on every push and the matrix across
debian/alpine/arch/fedora (`.github/workflows/os-rice-ci.yml`).

> [!warning] What the migration cannot claim is *verification*
> Modules needing a GPU, a display, a real kernel or a hypervisor are
> correct-by-construction and unit-tested, but only a real machine or a QEMU
> boot exercises them end to end
> ([[os-rice/DESIGN#9. Testing — containers plus QEMU, no real machine|DESIGN 9]]).

---

## Still out of scope

The `repo:` / `tarball:` / `brew:` / `flatpak:` providers, and `osr prune`
(package removal on rice switch — switching is additive on purpose). See
[[archive-decisions#A3]].
