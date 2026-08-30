---
title: Archived decisions
type: archive
status: frozen
updated: 2026-08-28
tags:
  - kind/archive
  - topic/dotfiles
  - topic/os-rice
  - topic/windows
---

# Archived decisions

> [!info] This file is history, not documentation
> Every entry here has already happened. It records *why* a shape was chosen
> and what it replaced, so the reasoning survives without cluttering the live
> docs. For what the repo does **today**, read [[os-rice/DESIGN|DESIGN]] and
> [[PLAN_UNIVERSAL]].

> [!warning] Do not treat entries as instructions
> Paths, file names and counts here were correct when written and are not
> maintained. Trees named below (`windows-rice/`, `windows-11-x86_64/`,
> `linux-arch-x86_64-hyprland-glass/`, `linux-debian-x86_64-kde-gruvbox/`)
> are deleted. Recoverable from git history only.

---

## Part 1 — os-rice (POSIX side)

### A1

**The copy-paste state that triggered the redesign**

*Status:* resolved · *Replaced by:* [[os-rice/DESIGN#1. Package abstraction + one-to-many table|DESIGN §1]]

Before the harness existed, each distro had its own tree.

| symptom | detail |
| --- | --- |
| `install-module.sh` | byte-identical across debian/rhel/arch except `apt update` vs `pacman -Sy` |
| `common.sh` | duplicated per distro; ~600-line grab-bag (apt repos + brew + cargo + git) |
| `zsh.sh` | ~95% identical in all three copies; only the package line differed |
| `install_or_update_zsh_plugin` | defined inside the zsh module, three times |
| drift, already | arch `zsh.sh` had `trace "git -C ..."` (whole command quoted as one arg — broken) and had silently dropped every `check_error` |

**Insight that drove everything after:** the only thing that actually varies
per distro in most modules is the package manager. Abstract it once, detect
once, write each module once.

### A2

**"No compiled C binary (for now)"**

*Status:* superseded for the harness, still binding for `osr`'s self-bootstrap block
*Replaced by:* [[os-rice/DESIGN#D-3. The C harness|DESIGN — the C harness]]

Original ruling, and it was about bootstrap primitives, not speed:

- a barebone box lacking `curl` almost always has `wget` or busybox;
- if a primitive is truly missing everywhere the answer is **static busybox**, not hand-rolled C;
- installer wall-clock is `apt`/`curl`/network, so C saves nothing measurable and breaks self-bootstrap.

What changed: the harness itself became C (`osr.c` + `lib/*.c` linked by
`nob.c` into `build/osr`), one translation unit per shell file it replaced.
The reasoning above still holds exactly where it was aimed — **the
self-bootstrap block at the top of `osr` stays pure sh and compiles nothing**,
because it runs before a toolchain is a given. Past that point the tool assumes
a C compiler, so that block installs one alongside git before it hands off.
(It was `bootstrap.sh` until it merged into `osr`: same code, same ruling, one
fewer entry point.)

Files removed outright by that port, not shrunk: `test/run.sh`,
`lib/state.sh`, and the build shim. `state.sh` existed only for its
`as_user tee` write, which `osr state set` now escalates itself.

One divergence was accepted rather than hidden: an `install.sh` option missing
its operand used to be `${2:?--user needs a name}`, whose diagnostic and exit
status came from the shell; it is now an ordinary `[ERROR]` line, exit 1.

### A3

**MVP scope and acceptance**

*Status:* closed — the repo is past MVP

The thesis was proved on the smallest slice: `lib/{log,ui,pkg,detect}.sh` +
two pkgmaps, one shared `install.sh`, `zsh.sh` installing on arch and debian
from one file, group-by-method dispatch on `starship=script:` +
`paru=source:`, the `.zshrc` split behind a marker-managed loader,
`osr switch` swapping only `90-theme` + wallpaper, and a podman double-run
matrix on arch/debian/alpine.

Still deliberately out of scope: `repo:` / `tarball:` / `brew:` / `flatpak:`
providers, and `osr prune`.

### A4

**Gap list G1–G10, read out of the pre-migration rices**

*Status:* closed except G1 · *Mechanisms live in:* [[os-rice/DESIGN]]

| gap | what it was | where it landed |
| --- | --- | --- |
| G1 | widen providers: `repo:` `tarball:` `brew:` `flatpak:` | **still open** — out of MVP |
| G2 | never reinstall a user-held/pinned/ignored package | idempotency contract, DESIGN §2 |
| G3 | `systemctl` is not portable (OpenRC, runit) | `enable_service`/`disable_service`, DESIGN §8 |
| G4 | `github_latest` duplicated in go/zig/ghostty | one helper in `lib/net.sh` |
| G5 | program-data vendored as config (`.oh-my-zsh`, 640 files) | install method, not a config layer |
| G6 | install method varies by distro *release* (ghostty: native on noble, source on jammy) | `name@facet`, DESIGN §1a |
| G7 | system config path varies by distro family (`/etc/default` vs `/etc/conf.d` vs `/etc/sysconfig`) | `OSR_ETC_DEFAULT`, DESIGN §5a |
| G8 | artifact-fetching providers are arch-specific | `OSR_ARCH`/`OSR_ARCH_DEB` + `name@arch` |
| G9 | a rice could not declare preconditions | `require:` + `lib/preflight.sh`, DESIGN §10 |
| G10 | "Vulkan actually initializes" is unprovable pre-install | cheap `gpu:present` gate + a functional probe module, DESIGN §10 |

A fourth rice, `linux-debian-x86_64-kde-gruvbox`, was deleted rather than
migrated: a dead 8k-file vendored-theme dump, which is the anti-pattern G5
argues against.

---

## Part 2 — the universal / Windows C core

Numbering is the original decision-log numbering, kept so old references still
resolve.

### D1

**Rejected: bootstrap a toolchain on the target machine**

Windows ships no compiler, some editions have no winget to fetch one, and a
bootstrap tool needing a toolchain on a fresh machine is a circular
dependency. Same conclusion A2 reached on the Linux side: the fix is not
building on the target, it is **not needing to**.

### D2

**Considered and rejected: Go or Rust as the one cross-compiled language**

Right *shape* (compile once, ship a binary), wrong *reach* once XP and obscure
architectures are requirements:

- Go 1.10 (2018) was the last release running on XP/Vista; newer needs an unofficial frozen fork.
- Rust dropped XP in the same era.
- Both curate their target lists; obscure embedded silicon is reached far more often by an existing GCC backend plus a prebuilt static cross-compiler (musl.cc, buildroot, OpenWrt SDKs).

### D3

**Decided: C, scoped to a minimal-runtime subset — **still live****

The XP blocker is not compiler availability but *runtime library* drift:
current mingw-w64/LLVM defaults to `_WIN32_WINNT=0x601`, GCC 16's `libstdc++`
needs `GetDynamicTimeZoneInformation` (Vista+), `winpthread` needs
`GetTickCount64` (Vista+), and current toolchains link UCRT rather than
`msvcrt.dll`. That hit C on the same timeline as Go and Rust — an
industry-baseline move, not a language gap.

Rule: target `_WIN32_WINNT=0x0501`, link `msvcrt.dll`, never link
`winpthread` (raw `CreateThread`), never touch the C++ stdlib. Prior art
checked before committing: [Building Principia for Windows XP](https://voxelmanip.se/2026/06/28/building-principia-for-windows-xp/).

### D4

**Rule: written for a human to read — **still live****

No macro tricks, no golfed one-liners. Named variables over inline
expressions, early returns over nesting, a short comment wherever the *why* is
not obvious. This matters more here than elsewhere because the tier exists for
old compilers and unfamiliar readers: code hard for a person to read is
usually also what an old compiler rejects.

### D5

**Decided: interleave C sources next to the file each ports — **still live****

`lib/net.c` beside `lib/net.sh`, `install.c` beside `install.sh`, rather than
a segregated `universal-core/` tree. Reasons: direct sh-to-C comparison is one
`ls` apart; module-by-module porting stays a per-file decision.

*Superseded sub-point:* the original third reason was reuse of
`windows-rice/windows.map` in place. That tree was later deleted (D8) and the
map moved to `os-rice/windows.map`.

### D6

**Decided: build with `nob.h`, not a Makefile — **still live****

`make` turned out to be a separate install on a fresh Windows box, on top of
the `gcc` the project needs anyway; `nob.h` needs nothing beyond that
compiler. Bootstrap once (`mkdir build && gcc -o build/nob.exe nob.c`), then
`build\nob.exe` rebuilds itself whenever `nob.c` changes.

`nob.c`/`nob.h` are build-time tooling run on the dev host, so they are
ordinary C99 — this does **not** loosen D3's C89/XP floor for product code.

> [!note] A `Makefile` still exists
> `os-rice/Makefile` is a developer convenience shim only. Every target
> bootstraps nob and forwards to it. Nothing in the repo calls it.

### D7

**Decided: port windows-rice's own 4 modules, not a generic framework**

fastfetch, wezterm, pwsh, oh-my-posh — ported function-for-function rather
than auto-generated. Superseded in framing only by D8, which made these the
whole Windows story rather than a fallback tier.

### D8

**Decided: ingest `windows-rice/` into the C core, then delete it**

*Status:* done — `windows-rice/` no longer exists

D1–D7 treated the C core as a fallback while plain-PowerShell `windows-rice/`
stayed primary. Once D7 had ported its modules function-for-function, the two
were the same four-module rice, one compiled and tested, one not — the exact
drift-prone duplication the repo avoids everywhere else.

| piece | moved to |
| --- | --- |
| `windows-rice/windows.map` | `os-rice/windows.map` |
| the `osr-rice` theme | `os-rice/themes/osr-rice/` — renders through the same `lib/theme_render.c` as every Linux theme |
| `Install-Scoop` (pkg.ps1) | `osr_winpkg_ensure_manager` |
| `Update-SessionEnvironment` (common.ps1) | `osr_winpkg_refresh_env` — re-reads Machine + User env from the registry after each install |
| `ui.sh`'s `run_step` spinner | `osr_run_step` in `lib/ui.c` |

Deliberate permanent gaps, not oversights:

- `fonts.ps1`'s manual GitHub-zip download-and-register fallback — reachable only when neither scoop nor choco exists; a ZIP decompressor in C89 is disproportionate.
- `config.ps1`'s `-Ask` confirm-before-overwrite — the C core always overwrites, which was `config.ps1`'s own default.
- `rice.ps1`'s `-Save` (pull a live config back into the repo) — `install.sh` never had the concept, so porting it would be a new feature.

`osr_run_step` differs from the sh original on purpose: it returns the exit
code instead of calling the fatal `osr_error()`, because one fatal path would
have broken the non-fatal manager fallback that existed at the time.

### D9

**Decided: drop `install.exe`'s `--apply` dry-run gate**

*Status:* done — the flag no longer exists

`install.c` originally required `--apply` before installing anything;
`install.sh` and `./osr` have no such concept. In practice it was a silent
trap: `osr.ps1 module wezterm` printed a clean `[INFO] would install...` and
did nothing. Removed, along with `osr.ps1`'s `-Apply`. `--theme-only` was
unaffected — it always applied.

### D10

**`windows.map` pins one manager per package**

*Status:* refined by D11, which is the live rule

The map used to list every manager carrying a package and try them in
scoop → choco → winget order. That is a trust bug: the three are independent
namespaces, so `foo` in one is not `foo` in another, and the manager a project
does *not* publish to is exactly where the name is still free for someone
else. The fallback turned "the intended manager is absent" into "install
whatever the next namespace has under that name", with no signal.

Which manager a row names follows that project's own install page: winget for
PowerShell (Microsoft's recommendation), scoop for fastfetch (its README leads
with it), and where upstream ranks nothing the tie goes to the
publisher-qualified id, since `Starship.Starship` can only be claimed by
starship while a bare `starship` cannot.

### D11

**One provider per row; a missing manager is installed, never substituted — **still live****

See [[PLAN_UNIVERSAL#Package resolution]] for the current description.

### D12

**One file per module, holding every OS it runs on — **still live****

The port started with `modules/*.c` for Windows and `modules/linux/*.c` beside
it, so `fastfetch` was two files doing the same job with no way to see one
from the other. Now a module is exactly `modules/<name>.c`, Windows behind
`#ifdef _WIN32`, POSIX after the `#else`, both exporting the same
`osrm_<name>` — safe because `nob.c` never compiles both.

This is D5's reasoning ("keep the files you have to compare one `ls` apart")
applied to the two C tiers themselves.

---

## Part 3 — deleted trees

| tree | fate |
| --- | --- |
| `linux-arch-x86_64-hyprland-glass/` | ~30 bash scripts, each re-implementing logging/sudo/`pacman -S`/chown → [[os-rice/rices/arch-hyprland-glass/arch-hyprland-glass\|rices/arch-hyprland-glass]] + shared `modules/`. Pre-migration tree at commit `63bbfd9`. |
| `linux-debian-x86_64-kde-gruvbox/` | deleted, not migrated (see A4) |
| `windows-rice/` | ingested into the C core (D8) |
| `windows-11-x86_64/` | ~25 `.ps1` files → `modules/win-*.c` + `lib/wintweak.c`. The twelve `reg-*.ps1` differed only in a key/value/default and the six `disable-*.ps1` only in a service name, so they are two tables now. |
