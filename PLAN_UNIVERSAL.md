---
title: PLAN_UNIVERSAL — the compiled core
type: plan
status: phase-1-in-progress
updated: 2026-08-28
tags:
  - kind/plan
  - topic/os-rice
  - topic/windows
  - lang/c
  - os/windows
---

# PLAN_UNIVERSAL — a compiled core for the OSes shell/PowerShell can't reach

**Related:** [[os-rice/DESIGN|os-rice/DESIGN]] ·
[[os-rice/modules/WINDOWS|the win- modules]] · [[archive-decisions]] ·
[[README|vault index]]

> [!info] Where the decision log went
> The twelve decisions this plan was argued out in (D1–D12) live in
> [[archive-decisions#Part 2 — the universal / Windows C core|the decision log]], with their original
> numbering. Everything below is the state today.

---

## Problem statement

[[os-rice/DESIGN|os-rice]] covers every OS with a real shell and a real
package manager — and is itself being rewritten into C
([[os-rice/DESIGN#13. The port, and what is left of it|DESIGN 13]]), which is
why this core and that one are converging on the same shape rather than two. Two targets fall outside what a shell can reach at all:

- **Legacy Windows** — Windows 7 (PowerShell 2.0 built in, no winget) and Windows XP (no guaranteed PowerShell, no TLS 1.2 without patching, no package manager, ever).
- **Obscure / bare embedded Linux** — a kernel, maybe a `/bin/sh`, no `apt`/`pacman`/`apk`.

Neither is reachable by writing more shell: the runtime those languages need
is not reliably there. This plan is the **third tier** — a small, compiled,
dependency-free core built from one shared C source tree.

> [!important] Windows is no longer an "optional extra tier"
> The plain-PowerShell tree `windows-rice/` was ported into this C core and
> deleted. The C core is now the **only** Windows rice this repo ships. See
> [[archive-decisions#D8|D8]].

---

## Scope

**Does:** parse `rice.list` / `theme.list` (the same `key: value` format
`lib/theme.sh` reads on Linux and `lib/theme_list.c` reads here), branch on
OS/arch, dispatch installs to whatever is actually present, render theme
templates with the same `{{role}}` substitution as the sh renderer.

**Does not:** reimplement the ~115 Linux `modules/*.sh`, the DE/session logic,
or anything assuming a Linux desktop. XP and bare boards get a minimal rice —
shell config, prompt, a few CLI tools, a theme. No Hyprland, no GPU probing.

Two carve-outs, both finite and both **ported, not invented**:

| carve-out | what | why it is not a slippery slope |
| --- | --- | --- |
| 4 Windows app modules (fastfetch, wezterm, pwsh, oh-my-posh) | `modules.c` | the already-finite set `windows-rice/` had |
| 4 Windows OS passes (`win-tweaks`, `win-update`, `win-debloat`, `win-winutil`) | `modules/win-*.c` over `lib/wintweak.c` | the ingest of `windows-11-x86_64/`'s ~25 `.ps1` files, which did exactly three things |

A **fifth** app module would need a person to write it, like the first four —
not a framework to generate it.

---

## Package resolution

`windows.map` is the Windows counterpart of `lib/pkgmap/`, read by
`lib/winpkg.c`. Same shape (`name[@facet] = <provider>`, most specific key
wins), one rule of its own:

> [!important] One provider per row, never substituted
> A row names exactly **one** installer. If it is not there, install *it*
> first (`osr_winpkg_ensure_manager`, elevating once if that needs admin) —
> never silently install the package from a different manager.
>
> Two managers' `wezterm` are not the same artifact: different install root,
> different shim, different update path. Substituting one for the other is
> how a "successful" install ends up invisible to everything downstream.
> Ties between candidate ids break toward the **publisher-qualified** one.

| provider form | meaning |
| --- | --- |
| `scoop:<id>` / `choco:<id>` / `winget:<id>` | that manager, that id |
| `source:<builder>` | a builder in `provide/<name>.c` |
| `script:<url>` | a vendor installer script |

`provide_module.c` is a metapacket: it `#include`s every `provide/<name>.c` so
they are one translation unit, and holds the name→function registry. It is the
C port of `lib/build.sh`'s role, not of its contents.

Builders are written against `lib/winbin.c`: resolve a direct URL or a
`gh:owner/repo:pattern` release asset, fetch, unzip, locate an exe, place it,
extend PATH, run an msi/setup installer or a vendor script.

**Elevation is one-shot.** `lib/elevate.c` relaunches the run under `runas`,
carrying `--user-home` across the boundary the way `sudo` carries `$SUDO_USER`
— because the elevated process has a different `%USERPROFILE%` and would
otherwise rice the Administrator account.

---

## Source layout

C sources live **interleaved** inside `os-rice/`, beside the `.sh`/`.ps1` file
each one ports, rather than in a separate tree ([[archive-decisions#D5|D5]]).

```
os-rice/
  windows.map           logical name -> the ONE provider that installs it
  provide_module.c/.h   the source: builder registry (metapacket)
  provide/<name>.c      one builder per package (wezterm.c: the arm64 build)
  install.c             CLI entry, C port of install.sh: rice.list ->
                        packages + modules; always installs for real (no
                        dry-run gate); --theme-only --theme <n>; --module <n>
  wallpaper.c           standalone program, C port of wallpaper.sh
  modules.c / .h        the finite Windows module set + its dispatch
  modules/<name>.c      ONE file per module, never one per OS: Windows behind
                        #ifdef _WIN32, POSIX after #else, both exporting the
                        same osrm_<name> -- only one is ever compiled
  modules/win-*.c       the OS-tweak group (see WINDOWS.md)
  modules/win-data/     ooshutup10.cfg, winutils.json -- data, not code
  themes/osr-rice/      the Windows-native palette, rendered by the same
                        theme_render.c path as every other theme
  lib/
    net.c               URL/header parsing (portable) + WinInet fetch;
                        the #else branch is a documented stub
    winpkg.c            windows.map lookup + ensure_manager + env refresh
    winbin.c            the toolkit builders are written against
    elevate.c           one-shot UAC elevation
    manifest.c          rice.list parser (factored out for unit testing)
    theme_list.c        theme.list parser -- the one parser of that format
    theme_render.c      {{role}}/_rgb/_dec/_sgr substitution + layer chain
    config_copy.c       ~ expansion + bounded file copy
    fonts.c             Nerd Font install (registry check + scoop/choco)
    wintweak.c          registry DWORDs + service control, straight Win32
    wallpaper.c         theme wallpaper library + SystemParametersInfo
    state.c  ui.c       state file; status lines + step counter + run_step
  test/
    c_test.h            C89 assertion micro-framework
    unit_c/*.c          net_parse, winpkg, manifest, theme_render,
                        config_copy, wintweak
    fixtures/           synthetic theme.list/.tmpl fixtures
  nob.c / nob.h         build script + vendored build library
  build/                every binary and nothing else; git-ignored whole
  osr.ps1 / osr.bat     the Windows front end, mirroring ./osr in full
```

One `#ifdef _WIN32` / `#else` pair per platform difference — no runtime OS
detection where compile-time will do, since each target is a separate build.

> [!note] `toolchains/` does not exist yet
> Phase 0 is unstarted. See [[#Open questions]].

---

## Build

`nob.c` replaces the Makefile the first version of this plan used
([[archive-decisions#D6|D6]]): the build is a C program, so the only tool a build
needs is the compiler already required to build the product.

```sh
mkdir build && gcc -o build/nob.exe nob.c    # once
build\nob.exe          # build/install.exe + build/wallpaper.exe
build\nob.exe test
build\nob.exe clean
```

### Toolchain matrix

| Target              | Toolchain                                                               | `_WIN32_WINNT` / libc                                  | Notes                                             |
| ------------------- | ----------------------------------------------------------------------- | ------------------------------------------------------ | ------------------------------------------------- |
| Win10/11            | current MSYS2 mingw-w64 or MSVC                                         | UCRT                                                   | no constraints; this is the shipping Windows rice |
| Win7                | current mingw-w64                                                       | `0x0601`, UCRT or msvcrt                               | works today, no patching                          |
| WinXP               | **own pinned/patched mingw-w64**, Dockerized                            | `0x0501`, `msvcrt.dll`, no `winpthread`, no C++ stdlib | the one genuinely bespoke toolchain — unstarted   |
| Linux, common arch  | `musl-gcc`, static                                                      | musl                                                   | x86_64/aarch64/armv7, any current musl toolchain  |
| Linux, obscure arch | prebuilt cross-compiler from musl.cc / buildroot, fetched **on demand** | musl                                                   | stood up only against a named real board          |

### Distribution

The intended handoff is `bootstrap.sh`-shaped: a tiny native stub picks and
execs the right prebuilt binary, never doing installer logic itself.

> [!warning] Not what `osr.ps1` does today
> `osr.ps1`/`osr.bat` build `install.exe` locally from source via `nob.c` when
> it is missing. They fetch no prebuilt release binary — there is no release
> feed to fetch from yet.

---

## Status

| phase | state |
| --- | --- |
| **Phase 1 — minimal core** | the shape is proven end to end: both manifest parsers, the theme renderer, the package/provider path, elevation, the 8 Windows modules, the unit suite. Remaining: the byte-diff-against-sh checkpoint and a human pass on the fallback branches. |
| **Phase 0 — XP toolchain** | not started |
| **Phase 2 — XP end to end** | not started |
| **Phase 3 — obscure Linux arch** | not started, and deliberately unplanned |

Phase 1 detail worth keeping: `theme_render_test.c` renders a **real**
template (`wezterm/wezterm-theme.toml.tmpl`) against a **real** theme
(`themes/nord`), not only a synthetic fixture; `wintweak_test.c` asserts every
row of `win-tweaks.c`'s two tables, because the tables *are* the port.

---

## Open questions

> [!question] Phases 0, 2 and 3 are open questions, not a roadmap
> They had task templates with acceptance criteria; those criteria had
> already gone stale (they said `make TARGET=...`, and `nob` replaced make).
> Rather than maintain a plan for work with no start date, the questions that
> would actually gate starting it:
>
> - **Is there a real, named device** driving the obscure-arch requirement, or is it a "should be possible" goal? Phase 3 stays unstarted until one exists — pre-building toolchains for hardware nobody uses is an unbounded task list.
> - **Does XP need to be maintained**, re-tested per release, or is "proven once, frozen" acceptable? This decides whether the pinned Docker image needs periodic revalidation or can be archived after one success. It is worth answering *before* sinking Phase 0's cost, not after.
> - **Where does an XP-minimal `rice.list` live** — a new `rices/xp-minimal/`, or an optional "minimal subset" existing rices grow?
> - **Does the `#else` stub in `net.c` get a native Linux port**, and does anything need it before a bare-board target exists?

---

## Risks

| Risk | Impact | Mitigation |
| --- | --- | --- |
| XP toolchain silently bitrots | High — the tier stops building with no warning | Docker-image the pin; rebuild only deliberately, never `apt upgrade` inside it |
| No real XP hardware for verification | Medium — false confidence from emulation | QEMU VM is the baseline; real hardware is a bonus |
| C core and sh renderer drift on theme output | Medium — a theme looks different per tier | the renderer's acceptance is a byte-diff against sh output, not "looks right" |
| `fetch_and_place` has no uninstall/upgrade story | Low | scope this tier install-only, matching the repo's additive model |

---

## Not doing (and why)

| not doing | why |
| --- | --- |
| porting the ~115 Linux modules or the DE stack to this tier | XP and bare boards get shell + prompt + theme. Out of scope by definition of the target |
| pre-building cross-toolchains for unnamed architectures | unbounded task list for hardware nobody is using |
| replacing `os-rice/` with this core | POSIX sh is the right tool wherever a real shell exists. This fills the gap it structurally cannot reach — Windows was the one place that changed, see [[archive-decisions#D8|D8]] |
| a general plugin framework mirroring `modules/` | YAGNI at this scope: a handful of CLI tools and a theme, not ~115 apps |
| committing to per-release XP support before it is proven once | the maintenance cost is real and should be weighed against actual usage |
