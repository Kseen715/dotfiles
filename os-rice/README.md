# os-rice

A DRY, declarative, POSIX-portable installer for unix-like apps, configs, and
whole rices. One module written **once** installs across package managers; a
rice is a plain list of what to install. See [DESIGN.md](DESIGN.md) for the full
rationale.

> Status: **past MVP**. The harness (`lib/`, `install.sh`, `osr`) plus ~70
> modules are complete and pass the idempotency matrix on apt/apk/pacman. Every
> legacy per-distro tree is gone: the last one,
> `linux-arch-x86_64-hyprland-glass/`, is now `rices/arch-hyprland-glass/` +
> `modules/*.sh`. `windows-11-x86_64/` is untouched and still its own thing.

## Layout

```text
os-rice/
  lib/                 shared POSIX-sh library (single copy)
    log.sh  ui.sh      logging; colors + spinner + step progress
    detect.sh          OSR_DISTRO / OSR_PKG / OSR_INIT, detected once
    user.sh            OSR_USER model: as_user/as_root, ensure_*, backup_copy
    pkg.sh             pkg_install/installed/refresh/remove + provider dispatch
    net.sh git.sh      download + github_latest; git repo / oh-my-zsh helpers
    service.sh         enable_service/disable_service (systemd/openrc/runit/sysv)
    config.sh          layered config: seed_once / install_layer / loader block
    pkgmap/            logical name -> real package(s), per manager
    servicemap         logical service -> real unit (only where they differ)
  modules/             ONE copy each, POSIX, distro-agnostic (zsh.sh)
  rices/<name>/        rice.list manifest + config/ (90-theme, starship) + wallpapers/
  install.sh           the shared runner
  osr                  front-end CLI (install / switch / list)
  bootstrap.sh         barebone entry: find downloader, clone repo, hand off
  test/                lint + hermetic unit tests + docker idempotency matrix
```

## Usage

```sh
# From a checkout of this repo:
os-rice/osr install gruvbox            # install a rice for the invoking user
os-rice/osr install --user alice nord  # rice a specific account (user-for-user)
os-rice/osr switch nord                # move to a rice: packages accrete, only
                                       # rice-owned 90-* config + wallpaper swap
os-rice/osr list

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
| `void-i3-rosemuted` | i3/X11 desktop in a muted rose dark palette, validated on Void |
| `xin` `catppuccin` `gruvbox` `nord` | shell/CLI rices (no DE) |

The i3 desktop is documented in two files: [`i3-sugg.md`](i3-sugg.md) is the
distro-agnostic component checklist (what an X11 desktop needs and why), and
[`i3-void-packages.md`](i3-void-packages.md) maps every one of those components
to its real Void `xbps` package — including the ones Void does not ship.

## Testing

```sh
sh os-rice/test/run.sh            # fast: POSIX lint + hermetic unit tests
sh os-rice/test/matrix.sh gruvbox # docker/podman double-run idempotency matrix
OSR_TEST_IMAGES="alpine:latest" sh os-rice/test/matrix.sh   # one image
```

CI runs the fast gate on every push and the idempotency matrix across
debian/alpine/arch/fedora (`.github/workflows/os-rice-ci.yml`).

## Not yet migrated (Out of MVP)

Still deliberately out of scope (see DESIGN "MVP scope"): the
`repo:`/`tarball:`/`brew:`/`flatpak:` providers, `osr prune`, and Windows —
`windows-11-x86_64/` remains its own PowerShell tree.

The legacy per-distro bash trees are done. What their migration cannot claim is
*verification*: modules that need a GPU, a display, a real kernel or a
hypervisor are correct-by-construction and unit-tested, but only a real machine
or a QEMU boot exercises them end to end (DESIGN §9). See
[rices/arch-hyprland-glass/README.md](rices/arch-hyprland-glass/README.md) for
the list.
