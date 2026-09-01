---
title: os-rice
type: readme
status: past-MVP
updated: 2026-08-30
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

> [!success] The backbone is C
> The port is finished. Every lib, every module, the runner and the test
> runner are C translation units. The default `build/osr` links everything
> into one static module host; optional `build/osr-runtime` compiles and loads
> one module when it is first used. **`osr`, `install.sh` and `wallpaper.sh`
> are the only `.sh` files the harness contains**, and the last two are
> two-line shims over the binary. See
> [[os-rice/DESIGN#13. The port, and what it left behind|DESIGN 13]].

> [!success] Status: past MVP
> The harness (`lib/`, `install.sh`, `osr`, `build/osr`) plus **120 C
> modules** pass the idempotency matrix on apt/apk/pacman. Every legacy
> per-distro tree is gone, Windows included — it is now a C core
> (`install.c`, `lib/win*.c`, `modules/win-*.c`), not a PowerShell tree.

---

## Layout

```text
os-rice/
  osr.c                the POSIX core: one binary (build/osr) that nob.c
                       links from the lib/ units, the same way install.c +
                       lib/*.c make the Windows core
  lib/
    ui.c log.c state.c one unit per shell file that used to be here:
    user.c detect.c    `osr ui`, `osr log`, `osr state`, `osr user`,
    theme.c install.c  `osr detect`, `osr theme`, `osr install`,
    testrun.c          `osr test-run`
    module.h/.c        the API a POSIX module written in C may call
    module_runtime.c   compile/cache/load backend for build/osr-runtime
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
    pkg.c              pkg_install/installed/refresh/remove + providers
    build.c            the source: builders (26 of them)
    fetch.c git.c      download + github_latest; repo / oh-my-zsh helpers
    service.c          enable_service/disable_service, 4 init systems
    config.c           seed_once / install_layer / loader block / templates
    apply.c            theme-only apply: the hotkey path, verbs neutralized
    reload.c           tell the running apps to re-read their config
    preflight.c        the require: predicates
    nerdfont.c gnome.c migrate.c
    pkgmap/            logical name -> real package(s), per manager
    servicemap         logical service -> real unit, where they differ
  modules/             120 POSIX modules, all C. ONE file per module, never
    <name>.c           one per OS: a module both systems can have holds
                       both branches behind #ifdef _WIN32
    win-*.c            the Windows OS passes (see WINDOWS.md)
    win-data/          data files those passes carry
  rices/<name>/        rice.list: which PACKAGES, and which themes
  themes/<name>/       theme.list + config/ (the 90-* layers) + wallpapers/
  install.sh           a two-line shim: exec ./osr install "$@"
  wallpaper.sh         set/query the wallpaper of the current theme
  osr                  front-end CLI, and the `curl | sh` barebone entry
  osr.ps1 / osr.bat    the Windows front end, mirroring it
  nob.c                the build script (a C program, not a Makefile)
  test/                lint + hermetic unit tests + docker matrix
    unit_c/            every test: what each unit must DO, stated by name
    harness.h/.c       the sandbox they are written against
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
curl -fsSL https://raw.githubusercontent.com/Kseen715/dotfiles/main/os-rice/osr | sh -s -- install gruvbox
```

`osr` piped from curl has no checkout around it, so it installs git and a C
compiler, clones the repo to `$OSR_DEST` (default
`~/.local/share/os-rice-dotfiles`) and re-execs itself from there with the same
arguments. Run from inside a checkout it does none of that. To see what a box
is missing without touching it:

```sh
curl -fsSL https://raw.githubusercontent.com/Kseen715/dotfiles/main/os-rice/osr | sh -s -- --check
```

`--verbose` streams command output instead of spinners — automatic when stdout
is not a TTY, so piping to a logfile stays clean.

---

## Build modes

The default remains a full executable with every POSIX module linked in. It can
be deployed and run without a compiler:

```sh
mkdir -p build
cc -o build/nob nob.c
./build/nob static       # build/osr; same as ./build/nob with no argument
```

The runtime host keeps the same module registry and API but leaves module
objects out of the executable. On first use it compiles only the requested
`modules/<name>.c`, stores the shared object in the user cache, loads it, and
calls its existing `osrm_<name>` entry point:

```sh
./build/nob runtime      # build/osr-runtime
./build/nob both         # both outputs
OSR_C_MODULE_BACKEND=runtime ./osr module flameshot
```

Runtime objects live under
`${XDG_CACHE_HOME:-$HOME/.cache}/os-rice/modules/abi-1/`. The cache key includes
the module source, public module headers, compiler command, runtime ABI, OS, and
architecture, so relevant changes compile a new object. Set `OSR_MODULE_CC` to
override the module compiler; it falls back to `CC`, then the first available of
`cc`, `gcc`, `clang`, and `tcc`. Compiler text is split into argv and executed directly, never through `sh -c`.

Runtime mode is currently POSIX/Linux-only and needs a working compiler when an
uncached module is first used. It is runtime compilation, not adaptive JIT:
modules compile on demand, not from execution profiling. Module source remains
trusted installer code and runs with the same privileges as a statically linked
module; dynamic loading is not a sandbox. Windows and compiler-free deployments
use the static output.

---

## Supported compiler targets

### Working

| Compiler | OS | Arch | Notes | Compilation time |
|---|---|---|---|---|
| [tcc](https://bellard.org/tcc/) 0.9.27 | GNU Linux | x86_64 | Ladder 1 priority | 1.18s |
| [drh/lcc](https://github.com/drh/lcc) | GNU Linux | i386(x86) | - | ~2.11x |
| [pcc](http://pcc.ludd.ltu.se/) 1.2.0.DEVEL 20220331 | GNU Linux | x86_64 | - | ~3.56x |
| clang 21.1.8 | GNU Linux | x86_64 | Ladder 2 priority | ~9.99x |
| gcc 15.2.0 | GNU Linux | x86_64 | Ladder 3 priority | ~10.75x |
| gcc 15.2.0 `-m32` | GNU Linux | i386(x86) | `CC="gcc -m32"`; needs `gcc-multilib` + `libc6-dev-i386` | ~11.47x |
| zig 0.14.1 cc clang 20.1.8 | GNU Linux | x86_64 | Ladder 4 priority | ~19.33x |

### In testing

| Compiler | OS | Arch | Notes |
|---|---|---|---|
| mingw-w64 | Windows | - | - |
| [OrangeC](https://github.com/LADSoft/OrangeC) | Windows | - | - |
| [CompCert](https://github.com/AbsInt/CompCert) | - | - | - |
| [arocc](https://github.com/Vexu/arocc) | - | - | - |
| [SmallerC](https://github.com/alexfru/SmallerC) | - | - | - |
| [shecc](https://github.com/sysprog21/shecc) | - | - | - |
| [Cuik](https://github.com/RealNeGate/Cuik) | - | - | - |
| [Artfuscator](https://github.com/JuliaPoo/Artfuscator) | - | - | - |
| [amacc](https://github.com/jserv/amacc) | - | - | - |
| [lacc](https://github.com/larmel/lacc) | - | - | - |
| [cproc](https://github.com/michaelforney/cproc) | - | - | - |
| [xcc](https://github.com/tyfkda/xcc) | - | - | - |

### Not working

| Compiler | OS | Arch | Notes |
|---|---|---|---|
| [chibicc](https://github.com/rui314/chibicc) | GNU Linux | - | C11 compiler that searches /usr/include but not the compiler-private directory where stddef.h actually lives on a glibc host, and it cannot parse GCC's own stdarg.h |
| [faucc](https://github.com/FAU-AS-MOS/FAUcc) | GNU Linux | - | 16/32-bit only; `cc1` predates host's glibc headers - it rejects `-std=c89`, has no `__builtin_bswap*`/`__builtin_expect`, and cannot even parse a cast inside an integer constant expression (valid C89, but glibc's `fd_set` uses it), so every TU that includes a system header dies in `cc1`. Not fixable by adding multilib. `nob` now drives it with `-b i386`; the 32-bit target itself builds via `CC="gcc -m32"` |
| [bcc](https://github.com/realchonk/bcc) | GNU Linux | - | Does not have libc implementation |
| [sdcc](https://sdcc.sourceforge.net/) | GNU Linux | - | Targets only microprocessors |
| [wrecc](https://github.com/PhilippRados/wrecc) | - | - | Unfinished |
| [ts-c-compiler](https://github.com/Mati365/ts-c-compiler) | - | - | Unfinished; support only 16 bit x86 |

## How it works

- **Package method, not just name.** A `pkgmap` row's RHS may carry a provider tag (`script:`, `source:`, `cargo:`, `aur:`). `pkg_install` expands logical names, installs the native batch in one call, then dispatches tagged rows — each provider owning its own idempotency probe. Untagged names pass through unchanged, so the common case needs no row at all.
- **Every module declares its session.** Its `lib/modules.c` registry row is `x11`, `wayland`, or `x11+wayland`, so session compatibility is metadata rather than source inspection.
- **Service name, per init.** `servicemap` rows may carry `@<init>`, most specific wins — one `enable_service bluetooth` reaches `bluetooth.service` on systemd and `/etc/sv/bluetoothd` on runit. No module branches on the init system.
- **Idempotent by contract.** Run a rice 100x and it converges; a second run is all `[ok] skipped`, zero errors.
- **Config layered by ownership.** `00-env` (user, seeded once), `10-*`/`20-*`/`30-*` (dotfiles, overwritten), `90-theme` (rice, swapped), `99-local` (machine, never touched). `~/.zshrc` is a thin loader owning only a marked block.

---

## Writing a module

A module installs one thing: a C translation unit `modules/<name>.c`,
registered in `lib/modules.c`. All are C. A `modules/<name>.sh` still runs
if one appears — a rice never says which tier it wanted — but it gets no
os-rice verbs, because those are C functions now. See
[[os-rice/DESIGN#11a. Every `.sh` module is legacy|DESIGN 11a]] and
[[os-rice/DESIGN#13. The port, and what it left behind|DESIGN 13]].

```c
/* modules/flameshot.c */
#include "../lib/module.h"

int osrm_flameshot(void) {
    static const char *const pkgs[] = { "flameshot", "maim", "slop", "xclip", NULL };
    return osr_pkg_install_step("Installing screenshot tools", pkgs);
}
```

Then add one row in `lib/modules.c` (name, session marker, themable flag,
function) and one line in `nob.c`'s POSIX module source list. Static builds link
that source into `build/osr`; runtime builds use the registry row as their
allowlist and compile the source only when requested. Everything a module may
call is `lib/module.h`: packages (`osr_pkg_install`, resolved through
`lib/pkgmap/` exactly as `pkg_install` does), steps (`osr_run_step` for a
function of your own — the thing the shell tier could not do), services,
`as_root`/`as_user` execs, the config-file primitives, and the detected facts.

**Ported:** every one of them. What each must do is stated in
`test/unit_c/modules_test.c` and its neighbours, by name, against a sandbox
whose `$PATH` is a directory of logging stubs — the argv log a module produces
IS what it did to the box.

> [!note] Every provider is the core's
> `cargo:`, `script:`, `aur:` and `source:` all resolve inside `lib/pkg.c`, and
> every `source:` row in `lib/pkgmap/` names a builder in `lib/build.c`. A row
> naming one that does not exist is fatal rather than silently installing
> nothing.

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
sh os-rice/osr test                # lint + the hermetic C test suite
sh os-rice/test/matrix.sh gruvbox  # docker/podman double-run idempotency
OSR_TEST_IMAGES="alpine:latest" sh os-rice/test/matrix.sh
```

Every test is a C behaviour test under `test/unit_c/`: **1617 named assertions
in 40 binaries**, each stating what a unit must DO rather than diffing it
against a recording. A test links no lib object — it drives `build/osr` as a
subprocess inside a sandbox whose `$PATH` is a directory of stubs that log
their own argv, so it survives the unit under it being renamed or split, and so
"what did this do to the box" is a literal, complete list of commands. See
`test/harness.h` for the sandbox and
[[os-rice/DESIGN#13. The port, and what it left behind|DESIGN 13]] for why the
suite is written this way rather than as a diff against the shell tier.

> [!tip] Writing an expectation
> `OSR_TEST_DUMP=1 ./build/nob test` prints the whole actual command log on a
> failed comparison instead of only the first line that differed — which is
> what you want while writing a new expectation, and not what you want in CI.

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
