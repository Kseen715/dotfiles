---
title: os-rice
type: readme
status: past-MVP
updated: 2026-09-03
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
> per-distro tree is gone, Windows included - and Windows is no longer a
> second C core either: `build/osr.exe` is built from the same `osr.c`, the
> same `lib/` units and the same `modules/` files as `build/osr`, with the
> differences inside those units under one `#ifdef` each. See
> [[os-rice/DESIGN#13a. The two cores became one|DESIGN 13a]].

---

## Layout

```text
os-rice/
  osr.c                the core: ONE binary per host (build/osr,
                       build/osr.exe) that nob.c links from the lib/ units.
                       Same file, same units, both systems
  lib/                 a unit per subsystem. Where the two systems answer a
                       question differently, BOTH answers are in the file
                       that owns it, under one #ifdef -- never a second file
    ui.c log.c state.c one unit per shell file that used to be here:
    user.c detect.c    `osr ui`, `osr log`, `osr state`, `osr user`,
    theme.c install.c  `osr detect`, `osr theme`, `osr install`,
    testrun.c          `osr test-run`
    module.h/.c        the API a module written in C may call, and its two
                       bodies (no sudo and no fork on the Windows side)
    module_runtime.c   compile/cache/load backend for build/osr-runtime
    modules.c          the registry of those modules (`osr module`)
    common.h/.c        buffer, printf %b, the log line, the path helpers,
                       and the handful of questions only a kernel answers
    cmds.h             one declaration per command entry point
    render.c           the {{role}} template renderer, shared with theme.c
    undervolt.c        `osr undervolt cpu` (DESIGN 12)
    uv/                backend.h + generic_opp.c + journal.c
    benchmark.c        `osr benchmark cpu|sensors` (DESIGN 12)
    bench/             cpu.c power.c util.c
    elevate.c          one privilege prompt per run: sudo -v, or the UAC
                       relaunch that stands in for it
    pkg.c              pkg_install/installed/refresh/remove + providers:
                       native/script/cargo/aur/source, and scoop/choco/winget
    build.c            the source: builders (26 of them) + the artifact
                       toolkit a Windows builder assembles out of
    fetch.c git.c      download + github_latest (curl/wget, or WinINet);
                       repo / oh-my-zsh helpers
    service.c          enable_service/disable_service, 4 init systems + SCM
    config.c           seed_once / install_layer / loader block / templates
    apply.c            theme-only apply: the hotkey path, verbs neutralized
    reload.c           tell the running apps to re-read their config
    preflight.c        the require: predicates
    fonts.c gnome.c migrate.c wallpaper.c
    pkgmap/            logical name -> real package(s), per manager --
                       apt/dnf/pacman/apk/xbps/portage/any, and windows
    servicemap/        logical service -> real unit, per init, where they differ
  modules/             ONE file per module, never one per OS, and ONE
    <name>.c           function: int osrm_<name>(void). A module both
                       systems have differs inside its body, if at all
    win-*.c            the Windows OS passes (see WINDOWS.md)
    win-data/          data files those passes carry
  rices/<name>/        rice.list: which PACKAGES, and which themes
  themes/<name>/       theme.list + config/ (the 90-* layers) + wallpapers/
  install.sh           a two-line shim: exec ./osr install "$@"
  wallpaper.sh         set/query the wallpaper of the current theme
  osr                  front-end CLI, and the `curl | sh` barebone entry
  osr.ps1 / osr.bat    the Windows front end: bootstrap the build, then hand
                       the command to build/osr.exe
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

`--verbose` streams command output instead of spinners - automatic when stdout
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

GCC is the reference compiler, but the harness builds and tests against TCC, GCC and Clang. Other compilers are treated as rudimentary, and are not expected to pass the test suite.
All compilers are tested with `nob -t` (synchronous, no parallelism, with time measurement).

Lower x = faster. The ratio is against **that host's** gcc.

#### x86_64

| Compiler | OS | Notes | Compilation time |
| --- | --- | --- | --- |
| [tcc](https://bellard.org/tcc/) 0.9.27 | GNU Linux | - | 0.072x |
| [lacc](https://github.com/larmel/lacc) | GNU Linux | `osr module lacc`. C89 by design, own assembler and ELF writer. Needs the preprocessor patch the module ships | 0.190x |
| [cproc](https://github.com/michaelforney/cproc) + [QBE](https://c9x.me/compile/) | GNU Linux | `osr module cproc`. Accepts every flag `nob` emits, `-std=c89` included; QBE is built into the same prefix. QBE's arm64 backend does not carry over: on aarch64 it compiles a hello world but every os-rice unit dies in the kernel headers - `/usr/include/aarch64-linux-gnu/asm/sigcontext.h:81:2: error: no type in struct member declaration` | 0.340x |
| [pcc](http://pcc.ludd.ltu.se/) 1.2.0.DEVEL 20220331 | GNU Linux | - | 0.381x |
| clang 21.1.8 | GNU Linux | - | 0.810x |
| gcc 15.2.0 | GNU Linux | - | **BASELINE** |
| zig 0.14.1 cc clang 20.1.8 | GNU Linux | - | 1.854x |

#### x86 / i386

| Compiler | OS |  Notes | Compilation time |
| --- | --- | --- | --- |
| [drh/lcc](https://github.com/drh/lcc) | GNU Linux | `osr module lcc` | _16.234s_ |
| gcc 15.2.0 `-m32` | GNU Linux | `CC="gcc -m32"`; needs `gcc-multilib` + `libc6-dev-i386` | **BASELINE** |

#### aarch64 / arm64 / armv8

| Compiler | OS | Notes | Compilation time |
| --- | --- | --- | --- |
| [tcc](https://bellard.org/tcc/) 0.9.27 | GNU Linux | - | 0.083x |
| gcc 13.3.0 | GNU Linux | - | **BASELINE** |

### In testing

| Compiler | OS | Arch | Notes |
| --- | --- | --- | --- |
| [xcc](https://github.com/tyfkda/xcc) | GNU Linux | x86_64, aarch64, riscv64, wasm | `osr module xcc` Ships its own libc instead of using the host headers, and it has no `<dirent.h>`: `Cannot open file: <dirent.h>`. The driver locates `cc1`/`cpp`/`as`/`ld` relative to `argv[0]`, so the module installs an exec wrapper rather than a symlink. Its aarch64 backend was checked on the Pi: the module builds, the hello world passes, and the tree still stops at the same `<dirent.h>` |
| [SmallerC](https://github.com/alexfru/SmallerC) | GNU Linux, DOS, Windows | i386(x86), 16-bit x86 | `osr module smallerc` 32-bit only, own libc, no `<dirent.h>`, and the driver rejects `-std=c89`, `-O2` and `-pedantic`. Its prefix is compiled in (`-DPATH_PREFIX`), so `make` and `make install` get the same one, or every compile ends in `smlrpp: not found` |
| [shecc](https://github.com/sysprog21/shecc) | GNU Linux | ARMv7-A, RV32IM | `osr module shecc` No x86-64 backend at all, so on an x86 box it is a cross compiler whose output cannot run. On the aarch64 Pi its ARMv7 output _does_ run, directly on the kernel's 32-bit compat layer and with no qemu - after a `chmod +x`, which shecc does not do to its own output. It still cannot build the tree: it ignores host headers in favour of its own libc, so any TU with `#include <stdio.h>` aborts (SIGABRT, no diagnostic printed at all), and every os-rice unit does. Only the stage-0 compiler is installed either way: `make` also builds the self-hosted stage 1 and 2 and needs `qemu-arm` for them (`Warning: failed to build the stage 1 and stage 2 compilers due to missing qemu-arm`) |
| [arocc](https://github.com/Vexu/arocc) | GNU Linux | x86_64 | `osr module arocc` Front end only so far: even a hello world ends at `fatal error: TODO CodeGen.genVar`. It tracks Zig master and needs 0.17.0-dev or newer to build - on Zig 0.14 the build stops at `error: no field named 'debug' in enum 'builtin.OptimizeMode'` |
| [Cuik](https://github.com/RealNeGate/Cuik) | GNU Linux | x86_64 | `osr module cuik` Alpha. No `-std` switch and it rejects the warning flags `nob` emits; its preprocessor expands the predefined `linux` macro inside a header name, so `#include <linux/limits.h>` becomes `couldn't find file: 1/limits.h`. The build itself needs LuaJIT specifically (Lua 5.4 rejects `build.lua`'s `0x...u` suffixes and its `unpack`), plus ninja, nasm, clang and lld - the `cuik-build-deps` pkgmap row |
| mingw-w64 | Windows | - | Windows PE target; cannot be judged from a Linux box |
| [OrangeC](https://github.com/LADSoft/OrangeC) | Windows | - | Windows PE target; same |
| [Artfuscator](https://github.com/JuliaPoo/Artfuscator) | GNU Linux | i386(x86) | An LLVM obfuscating backend rather than a compiler in its own right |
| [CompCert](https://github.com/AbsInt/CompCert) | GNU Linux | x86_64 | No distro package, and the GitHub releases carry no binaries, so there is nothing to install short of the Coq/opam source build. Note also the INRIA license: free for research and evaluation, paid for commercial use |
| [amacc](https://github.com/jserv/amacc) | GNU Linux | ARM32 | `osr module amacc` ARM-only JIT: it compiles a C subset and runs it in-process, and the driver itself is a 32-bit ARM binary, so the module installs the `arm-cross` row (`gcc-arm-linux-gnueabihf` + `qemu-user`, both demanded by upstream's `mk/arm.mk` before it will build). On the aarch64 Pi the driver runs natively on the 32-bit compat layer once `libc6:armhf` is present, and the module's wrapper falls back to `qemu-arm -L /usr/arm-linux-gnueabihf` where it does not - which is also how an x86_64 host gets it. JIT mode passes upstream's 27 tests; its ELF-output mode does not survive here (bus error natively, `Inconsistency detected by ld.so: rtld.c: 1280` under qemu-arm). It cannot build os-rice: the C subset is far short of the tree, and `-c` is not one of its flags (`usage: amacc [-s] [-o object] file`) |

### Not working

| Compiler | OS | Arch | Notes |
| --- | --- | --- | --- |
| [chibicc](https://github.com/rui314/chibicc) | GNU Linux | - | C11 compiler that searches /usr/include but not the compiler-private directory where stddef.h actually lives on a glibc host, and it cannot parse GCC's own stdarg.h |
| [faucc](https://github.com/FAU-AS-MOS/FAUcc) | GNU Linux | - | 16/32-bit only; `cc1` predates host's glibc headers - it rejects `-std=c89`, has no `__builtin_bswap*`/`__builtin_expect`, and cannot even parse a cast inside an integer constant expression (valid C89, but glibc's `fd_set` uses it), so every TU that includes a system header dies in `cc1`. Not fixable by adding multilib. `nob` now drives it with `-b i386`; the 32-bit target itself builds via `CC="gcc -m32"` |
| [bcc](https://github.com/realchonk/bcc) | GNU Linux | - | Does not have libc implementation |
| [sdcc](https://sdcc.sourceforge.net/) | GNU Linux | - | Targets only microprocessors |
| [wrecc](https://github.com/PhilippRados/wrecc) | - | - | Unfinished |
| [ts-c-compiler](https://github.com/Mati365/ts-c-compiler) | - | - | Unfinished; support only 16 bit x86 |

## How it works

- **Package method, not just name.** A `pkgmap` row's RHS may carry a provider tag (`script:`, `source:`, `cargo:`, `aur:`). `pkg_install` expands logical names, installs the native batch in one call, then dispatches tagged rows - each provider owning its own idempotency probe. Untagged names pass through unchanged, so the common case needs no row at all.
- **Every module declares its session.** Its `lib/modules.c` registry row is `x11`, `wayland`, or `x11+wayland`, so session compatibility is metadata rather than source inspection.
- **Service name, per init.** `servicemap/` is a file per init (`<init>.map`, then `any.map`), most specific wins - one `enable_service bluetooth` reaches `bluetooth.service` on systemd and `/etc/sv/bluetoothd` on runit. No module branches on the init system.
- **Idempotent by contract.** Run a rice 100x and it converges; a second run is all `[ok] skipped`, zero errors.
- **Config layered by ownership.** `00-env` (user, seeded once), `10-*`/`20-*`/`30-*` (dotfiles, overwritten), `90-theme` (rice, swapped), `99-local` (machine, never touched). `~/.zshrc` is a thin loader owning only a marked block.

---

## Writing a module

A module installs one thing: a C translation unit `modules/<name>.c`,
registered in `lib/modules.c`. All are C. A `modules/<name>.sh` still runs
if one appears - a rice never says which tier it wanted - but it gets no
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
function of your own - the thing the shell tier could not do), services,
`as_root`/`as_user` execs, the config-file primitives, and the detected facts.

**Ported:** every one of them. What each must do is stated in
`test/unit_c/modules_test.c` and its neighbours, by name, against a sandbox
whose `$PATH` is a directory of logging stubs - the argv log a module produces
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
- **A theme's colors:** `themes/<name>/theme.list`. Templates live beside each app's dotfiles, one per app - never one per theme.

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
against a recording. A test links no lib object - it drives `build/osr` as a
subprocess inside a sandbox whose `$PATH` is a directory of stubs that log
their own argv, so it survives the unit under it being renamed or split, and so
"what did this do to the box" is a literal, complete list of commands. See
`test/harness.h` for the sandbox and
[[os-rice/DESIGN#13. The port, and what it left behind|DESIGN 13]] for why the
suite is written this way rather than as a diff against the shell tier.

> [!tip] Writing an expectation
> `OSR_TEST_DUMP=1 ./build/nob test` prints the whole actual command log on a
> failed comparison instead of only the first line that differed - which is
> what you want while writing a new expectation, and not what you want in CI.

CI runs the fast gate on every push and the matrix across
debian/alpine/arch/fedora (`.github/workflows/os-rice-ci.yml`).

> [!warning] What the migration cannot claim is *verification*
> Modules needing a GPU, a display, a real kernel or a hypervisor are
> correct-by-construction and unit-tested, but only a real machine or a QEMU
> boot exercises them end to end
> ([[os-rice/DESIGN#9. Testing - containers plus QEMU, no real machine|DESIGN 9]]).

---

## Still out of scope

The `repo:` / `tarball:` / `brew:` / `flatpak:` providers, and `osr prune`
(package removal on rice switch - switching is additive on purpose). See
[[archive-decisions#A3]].
