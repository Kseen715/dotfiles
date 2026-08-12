# PLAN_UNIVERSAL — a compiled core for the OSes shell/PowerShell can't reach

## Problem statement

`os-rice/` (POSIX sh, mainstream Linux distros) and `os-rice/windows-rice/`
(PowerShell 7, Windows 10/11) already cover "ricing every OS" for anything
with a real shell and a real package manager — see
[`os-rice/DESIGN.md`](os-rice/DESIGN.md) and
[`os-rice/windows-rice/README.md`](os-rice/windows-rice/README.md). That
split is correct and **stays as-is**: this plan does not touch either tree.

Two targets fall outside what a shell can reach at all:

- **Legacy Windows** — Windows 7 (PowerShell 2.0 built in, no winget) and
  Windows XP (no PowerShell guaranteed, no TLS 1.2 without patching, no
  package manager, ever).
- **Obscure/bare embedded Linux** — boards where there's no distro, no
  `apt`/`pacman`/`apk`, and not even a guaranteed rich shell — just a kernel,
  maybe a `/bin/sh`, maybe not.

Neither can be reached by writing more shell or more PowerShell — the runtime
those languages need isn't reliably there. This plan adds a **third tier**:
a small, compiled, dependency-free core, built once per target from one
shared C source tree, that these two environments run directly.

---

## Decision log

**1. Rejected: bootstrap a scripting/compiled language on the target machine
(the original "compile a C helper on-device" idea).**
Windows has no compiler out of the box, some editions have no winget to
fetch one, and a bootstrap tool that itself needs a toolchain on a fresh
machine is a circular dependency. Same conclusion `os-rice/DESIGN.md`
"No compiled C binary (for now)" reached for the Linux side: the fix isn't
building on the target, it's **not needing to**.

**2. Considered: Go or Rust as the single orchestration language, cross-compiled
centrally, shipped as static binaries (`GOOS=windows go build`, etc.), mirroring
how this repo already ships Proteus and installs via `cargo-binstall`
(prebuilt binaries, never compiled on-target — see
[`cargo/cargo-binstall-shim`](cargo/cargo-binstall-shim)).**
This is the right *shape* (compile once, ship a binary, target never needs a
toolchain) but the wrong *reach* once XP and obscure architectures are real
requirements:
- Go 1.10 (2018) was the last release that runs on Windows XP/Vista; anything
  newer requires an unofficial frozen fork.
- Rust dropped XP around the same era.
- Both curate their target list; obscure embedded silicon (DSPs, uncommon
  embedded cores) is reached far more often by an existing GCC backend and a
  prebuilt static cross-compiler (musl.cc, buildroot, OpenWrt SDKs) than by
  Go's or Rust's officially-supported target sets.

**3. Decided: C, but scoped narrowly — not "C solves it," a specific minimal-runtime
subset of C does.**
The actual XP blocker isn't compiler availability (trivial for x86), it's that
every modern toolchain's *runtime library* has drifted its floor to Vista/Win7:
current mingw-w64/LLVM defaults to `_WIN32_WINNT=0x601`, GCC 16's `libstdc++`
needs `GetDynamicTimeZoneInformation` (Vista+), `winpthread` needs
`GetTickCount64` (Vista+), and current toolchains link UCRT, not `msvcrt.dll`.
This hit C toolchains too, on the same timeline as Go and Rust — it is not a
C-vs-modern-language gap, it's a "the whole industry moved its baseline"
gap. The fix: target `_WIN32_WINNT=0x0501`, link `msvcrt.dll`, never link
`winpthread` (use raw `CreateThread`), never touch C++ stdlib. A precedent
exists and was verified before committing to this:
[Building Principia for Windows XP](https://voxelmanip.se/2026/06/28/building-principia-for-windows-xp/)
— someone doing exactly this in 2026 with a deliberately pinned/patched
mingw-w64. `go-backports` (an unofficial frozen Go fork for XP) is the
equivalent-effort alternative in Go, with less prior art to build on.

**Net effect:** this is not "rewrite everything in C." It is one small,
narrow-scope C core (install dispatch + theme rendering, nothing else) built
by three different pinned toolchains for three different reach targets, kept
as the *fallback* tier under `os-rice/` and `windows-rice/`, which remain the
primary path for everything that already has a real shell.

---

## Architecture

### Scope: what the C core does and does not do

**Does:** parse a `rice.list`/`theme.list` (same plain `key: value` format
`os-rice/lib/theme.sh` and `windows-rice/src/theme.ps1` already read — see
[`os-rice/DESIGN.md` §6b](os-rice/DESIGN.md)), branch on OS/arch, dispatch
package installs to whatever's actually present (`winget` if found, else a
direct download; `apt`/`pacman`/`apk` if found, else nothing — legacy/bare
targets often have no package manager, only "fetch and place a file"),
render theme templates with the same `{{role}}` substitution algorithm as
the sh/PowerShell renderers.

**Does not:** reimplement the ~70 os-rice modules, the DE/session logic, or
anything that assumes a desktop. XP and bare embedded boards get a minimal
rice: shell config, a prompt, a few CLI tools, a theme. No Hyprland, no
Wayland, no GPU probing.

### Source layout

```
os-rice/universal-core/
  src/
    main.c            entry: parse argv, dispatch to install/theme/render
    os_windows.c       #ifdef _WIN32 branch: winget/direct-download install,
                        registry/env, Win32 threads only (no winpthread)
    os_linux.c         #ifdef __linux__ branch: apt/pacman/apk probe + native
                        fallback (fetch a static tarball, extract, place)
    manifest.c         rice.list / theme.list parser — hand-rolled, mirrors
                        the `while read` logic in lib/theme.sh line-for-line
    theme.c            {{role}}/{{role_rgb}}/{{role_dec}} substitution,
                        ports _osr_theme_sed's algorithm, not its shell
    net.c              minimal HTTP(S) fetch — WinInet on Windows,
                        libcurl-or-raw-sockets on Linux; degrades to
                        "no network, local-only" on XP (see toolchain matrix)
  Makefile             one Makefile, target selected by env (see below)
  toolchains/          Dockerfiles for the pinned/patched builders (XP tier)
```

One `#ifdef _WIN32` / `#else` pair per platform difference — no runtime OS
detection where compile-time is available, since each target is already a
separate build:

```c
#ifdef _WIN32
  #include <windows.h>
  static int install_pkg(const char *name) {
      if (has_winget()) return run_cmd("winget", "install", "-e", "--id", win_map(name), NULL);
      return fetch_and_place(name);           /* XP/Win7-without-winget path */
  }
#else
  static int install_pkg(const char *name) {
      switch (detect_pkg_manager()) {
          case PKG_APT:    return run_cmd("apt-get", "install", "-y", name, NULL);
          case PKG_PACMAN: return run_cmd("pacman", "-S", "--noconfirm", name, NULL);
          case PKG_NONE:   return fetch_and_place(name);   /* bare embedded path */
      }
  }
#endif
```

### Toolchain matrix

| Target | Toolchain | `_WIN32_WINNT` / libc | Notes |
|---|---|---|---|
| Win10/11 | current MSYS2 mingw-w64 or MSVC | UCRT | no constraints, could also just call `windows-rice/` instead — this tier exists only if a rice needs the same C core for consistency |
| Win7 | current mingw-w64 | `0x0601`, UCRT or msvcrt | works today, no patching |
| WinXP | **own pinned/patched mingw-w64**, Dockerized (`toolchains/xp.Dockerfile`) | `0x0501`, `msvcrt.dll`, no `winpthread`, no C++ stdlib | the one genuinely custom toolchain in this plan |
| Linux, common arch | `musl-gcc`, static | musl | x86_64/aarch64/armv7 covered by any current distro's musl toolchain |
| Linux, obscure arch | prebuilt cross-compiler from musl.cc / buildroot, fetched per-arch **on demand**, not pre-vendored | musl | only stood up when a real target actually needs it — don't pre-build 20 toolchains for archs nobody's asked for |

The XP toolchain is the only piece of infrastructure that's genuinely
bespoke and needs to be built once and pinned (frozen in a Docker image so
an upstream MSYS2 update can't silently break it), following the approach in
the Principia writeup cited above. Every other row uses an off-the-shelf,
actively-maintained toolchain.

### Distribution

Same `bootstrap.sh`-style handoff as the existing systems: a tiny native
stub (`.bat` one-liner on Windows, `sh` one-liner on Linux) fetches the
correct prebuilt binary for the detected target and execs it. The stub never
does installer logic itself — it only picks and runs a binary, same as
`cargo-binstall` already does for Rust packages in this repo.

---

## Task breakdown

### Phase 0 — Toolchain infrastructure (blocks everything else)

- [ ] **Task 0.1: Pin and Dockerize the XP-capable mingw-w64 build**
  - Acceptance: `docker run osr-xp-toolchain gcc -o hello.exe hello.c` produces
    an `.exe` that boots on a real or emulated XP SP3 install and prints
    output.
  - Verification: manual boot test in a QEMU XP VM (no CI runner for this —
    same "manual/nightly, not per-commit" precedent as `os-rice/DESIGN.md`
    §9's QEMU DE smoke test).
  - Dependencies: none.
  - Files: `os-rice/universal-core/toolchains/xp.Dockerfile`.
  - Scope: Medium.

- [ ] **Task 0.2: musl-gcc build path for common Linux architectures**
  - Acceptance: `make TARGET=linux-x86_64-musl` and `make TARGET=linux-aarch64-musl`
    both produce statically-linked binaries with `ldd` reporting "not a
    dynamic executable."
  - Verification: automated, runs in normal CI.
  - Dependencies: none.
  - Files: `os-rice/universal-core/Makefile`.
  - Scope: Small.

### Checkpoint: Phase 0

- [ ] XP toolchain produces a bootable binary (manual VM check).
- [ ] Musl static build is automated and green in CI.
- [ ] Human review before writing any real orchestration logic on top of this.

### Phase 1 — Minimal core (proves the shape end to end)

- [ ] **Task 1.1: Manifest parser (`manifest.c`)**
  - Acceptance: parses a real `rice.list`/`theme.list` from the existing
    Linux `os-rice/rices/` and `os-rice/themes/` trees byte-identically to
    what `lib/theme.sh`'s reader extracts (same keys, same values).
  - Verification: unit test comparing parsed output against a fixture
    generated by the existing shell parser.
  - Dependencies: none.
  - Files: `os-rice/universal-core/src/manifest.c`, a small test harness.
  - Scope: Small.

- [ ] **Task 1.2: Theme template renderer (`theme.c`)**
  - Acceptance: given one existing `.tmpl` file (e.g.
    `os-rice/btop/btop.theme.tmpl`) and a `theme.list` (e.g. `nord`),
    produces output identical to `render_theme_template` in `lib/theme.sh`.
  - Verification: diff against the sh-rendered output for all six existing
    themes.
  - Dependencies: Task 1.1.
  - Files: `os-rice/universal-core/src/theme.c`.
  - Scope: Medium.

- [ ] **Task 1.3: Windows branch — install dispatch + `fetch_and_place` fallback**
  - Acceptance: on a Win7 VM with no winget, `install_pkg("starship")`
    downloads and places the binary without error; on Win10/11 with winget
    present, it uses winget instead.
  - Verification: manual run on both a Win7 and Win10 VM.
  - Dependencies: Task 1.1.
  - Files: `os-rice/universal-core/src/os_windows.c`.
  - Scope: Medium.

- [ ] **Task 1.4: Linux branch — package-manager probe + bare fallback**
  - Acceptance: on a normal Debian container, uses `apt-get`; on a bare
    busybox rootfs with no package manager, falls through to
    `fetch_and_place`.
  - Verification: container test for both cases, added to
    `os-rice/test/matrix.sh`-style harness.
  - Dependencies: Task 1.1.
  - Files: `os-rice/universal-core/src/os_linux.c`.
  - Scope: Medium.

### Checkpoint: Phase 1

- [ ] Manifest + theme rendering match the existing shell/PowerShell output
      exactly, on real fixtures, not synthetic ones.
- [ ] Both OS branches build and run their happy + fallback paths.
- [ ] Human review: is the C core's behavior actually indistinguishable from
      what a shell-based install would have done, for the subset of modules
      it covers?

### Phase 2 — XP end-to-end

- [ ] **Task 2.1: Build the core with the pinned XP toolchain**
  - Acceptance: `make TARGET=windows-xp` produces an `.exe`; it launches on
    real/emulated XP without an immediate crash (the `winpthread`/UCRT
    failure mode this plan exists to avoid).
  - Verification: manual boot + smoke run on the XP VM from Task 0.1.
  - Dependencies: Phase 0, Phase 1.
  - Files: build config only.
  - Scope: Small.

- [ ] **Task 2.2: XP-safe network fetch**
  - Acceptance: `net.c`'s WinInet path either succeeds against a plain-HTTP
    mirror or fails with a clear, actionable message about XP's TLS
    limitation — never a silent hang or a generic crash.
  - Verification: manual test against both an HTTP and an HTTPS-only URL on
    the XP VM.
  - Dependencies: Task 2.1.
  - Files: `os-rice/universal-core/src/net.c`.
  - Scope: Small.

- [ ] **Task 2.3: Minimal XP rice — one real end-to-end install**
  - Acceptance: running the XP binary against a trimmed `rice.list`
    (shell config + one CLI tool + one theme) succeeds on real/emulated XP,
    producing a themed, working prompt.
  - Verification: manual, screenshotted, on the XP VM.
  - Dependencies: Task 2.2.
  - Files: a new minimal rice, e.g. `os-rice/universal-core/rices/xp-minimal/`.
  - Scope: Medium.

### Checkpoint: Phase 2 — XP go/no-go

- [ ] A real XP install works end to end on at least one theme.
- [ ] **Explicit human decision point:** is the toolchain-maintenance cost
      paid in Phase 0/2 worth keeping XP as a live, tested target going
      forward, or does it become "was proven possible once, not maintained
      per-release"? Record the answer here before Phase 3.

### Phase 3 — Obscure Linux architecture (only once a real target exists)

- [ ] **Task 3.1: Fetch-on-demand cross-toolchain for one concrete obscure arch**
  - Acceptance: given a specific real board/arch (name it when this task is
    actually started — do not pre-select one speculatively), `make
    TARGET=linux-<arch>-musl` builds using a musl.cc or buildroot
    cross-compiler fetched for that arch, producing a static binary that
    runs on real or emulated hardware for that arch.
  - Verification: manual run on the real board, or QEMU user-mode emulation
    if hardware isn't available.
  - Dependencies: Phase 1.
  - Files: `os-rice/universal-core/toolchains/`, `Makefile`.
  - Scope: Medium (per architecture).

> Deliberately not broken into more tasks yet — §"Not Doing" explains why
> pre-building for architectures nobody has actually asked for is waste.

### Checkpoint: Phase 3

- [ ] One obscure-arch target proven end to end.
- [ ] Human review before adding more archs — confirm each addition is
      driven by a real device, not speculative coverage.

---

## Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| XP toolchain silently bitrots (upstream mingw-w64/MSYS2 update breaks the pin) | High — XP tier stops building with no warning | Docker-image the pinned toolchain (Task 0.1); rebuild the image only deliberately, never `apt upgrade` inside it |
| No real XP hardware/VM available for verification | Medium — false confidence from emulation alone | QEMU XP VM is the baseline; treat any real hardware test as a bonus, not a requirement |
| Obscure-arch scope creep (building toolchains for archs nobody needs) | Medium — infinite, low-value task list | Phase 3 tasks are only created against a named, real target device (see Task 3.1's framing) |
| C core and shell/PowerShell renderers drift on theme output | Medium — a theme looks different on the C-core tier than everywhere else | Task 1.2's acceptance criterion is a byte-diff against the existing sh output, not "looks right" |
| `fetch_and_place` fallback has no package-manager-level uninstall/upgrade story | Low — matches os-rice's own "additive, no removal" model already (`DESIGN.md` §6) | Explicitly scope this tier to install-only, same as the rest of the repo already accepts |

---

## Not doing (and why)

- **Porting the ~70 existing Linux modules or the DE/Hyprland stack to this
  tier.** XP and bare embedded boards get a minimal shell+prompt+theme rice,
  not a desktop. Out of scope by definition of what these targets even are.
- **Pre-building cross-toolchains for architectures with no named target
  device.** Only stand up an obscure-arch toolchain against Task 3.1's
  concrete, real board — otherwise this becomes an unbounded task list for
  hardware nobody is using.
- **Replacing `os-rice/` or `windows-rice/` with this C core.** Both already
  work, are past MVP, and are the right tool wherever a real shell exists.
  This plan only fills the gap they structurally cannot reach.
- **A general plugin/module framework for the C core mirroring `os-rice/modules/`.**
  YAGNI at this scope — the C core covers a handful of CLI tools and a
  theme, not ~70 apps. Revisit only if a second contributor needs it (same
  standing rule as `os-rice/DESIGN.md`'s own "Not Doing" list).
- **Committing to long-term, per-release XP support before Phase 2's go/no-go
  checkpoint.** The toolchain-maintenance cost is real; the checkpoint exists
  specifically so that cost gets weighed against actual XP usage before more
  work is sunk into it.

---

## Open questions

- Is there a real, named device driving the obscure-Linux-architecture
  requirement, or is it a "should be possible" goal? Phase 3 stays unstarted
  until one exists (see Task 3.1).
- Does XP support need to survive as a **maintained, re-tested-per-release**
  target, or is "proven once, frozen" (Phase 2's checkpoint) acceptable? This
  changes whether Phase 0's Docker image needs periodic revalidation or can
  simply be archived once Phase 2 passes.
- Should the C core's `rice.list` entries live under
  `os-rice/universal-core/rices/`, or should existing rices grow an optional
  "minimal subset" the C core can also read, to avoid a fourth manifest tree
  to keep in sync?
