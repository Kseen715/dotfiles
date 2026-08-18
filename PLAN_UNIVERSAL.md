# PLAN_UNIVERSAL — a compiled core for the OSes shell/PowerShell can't reach

## Problem statement

`os-rice/` (POSIX sh, mainstream Linux distros) already covers "ricing every
OS" for anything with a real shell and a real package manager — see
[`os-rice/DESIGN.md`](os-rice/DESIGN.md). This plan does not touch that tree.

**Windows is different from the original version of this plan.** Windows
used to have its own separate, plain-PowerShell tree (`os-rice/windows-rice/`)
alongside this C core, on the theory that an ordinary Win10/11 box didn't
need the C core at all. That tree has since been retired: its 4 modules,
its Windows-native `osr-rice` theme, its `windows.map`, and its
scoop-bootstrap/env-refresh logic were all ported into the C core, which is
now the *only* Windows rice implementation this repo ships — see decision 8
below for why and what changed.

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

**4. Rule: code here is written for a human to read, not to be clever.**
No macro tricks, no golfed one-liners, no "look how few lines" C. Every
function reads top-to-bottom as ordinary, explicit C: named variables over
inline expressions, early returns over nested conditionals, a short comment
wherever the *why* isn't obvious from the code itself (a WinAPI quirk, a
format this has to stay byte-identical with). This matters more here than in
most code, because the reason this tier exists at all is compilers and
readers older/less sophisticated than the norm elsewhere in this repo — code
that's hard for a person to read is also usually the code that's hardest for
an old compiler to accept and hardest for the next contributor porting a
Linux module into this tier to trust.

**5. Decided: interleave C sources into `os-rice/` next to the file each one
ports, not a separate `universal-core/` tree.**
The original plan below (§"Source layout") put every C file under
`os-rice/universal-core/src/`, kept deliberately apart from `os-rice/` and
`windows-rice/`. In practice, starting implementation made a different
layout the better fit: `lib/net.c` sits beside `lib/net.sh`, `lib/winpkg.c`
beside the `windows-rice/src/pkg.ps1` it ports, `install.c` at the same
level as `install.sh`. Reasons this won out over the segregated tree:

- **Direct sh-to-C comparison.** Reviewing whether `net.c` actually matches
  `net.sh`'s behavior is a lot easier when the two files are one `ls` apart,
  not two directory levels removed in an unrelated tree.
- **Incremental module-by-module porting stays incremental.** The stated
  end goal is working through the ~70 `os-rice/modules/*.sh` files one at a
  time and, where it makes sense, growing a native (non-stub) Linux branch
  in the matching `lib/*.c` file. That only stays a per-file decision if the
  C and sh files already live together — a segregated tree would need a
  wholesale migration first.
- **`windows.map`/`windows-rice/` reuse falls out for free.** At the time
  this decision was made, `lib/winpkg.c` read the *existing*
  `windows-rice/windows.map` file and ported the *existing*
  `windows-rice/src/pkg.ps1` dispatch logic rather than inventing a second
  package-name mapping — only possible because nothing was moved out of
  `windows-rice/` yet. **Superseded by decision 8**: `windows-rice/` was
  later ingested and deleted, and `windows.map` now lives at
  `os-rice/windows.map` directly — this bullet is kept as the historical
  reasoning for why the interleaved layout (decision 5 itself) was chosen,
  not as a current description of where the file lives.
This does not change decision 1-3's reasoning (C, narrow scope, XP floor via
`_WIN32_WINNT=0x0501`) or the "does not touch `os-rice/`'s *behavior*"
intent — `install.sh` and `lib/net.sh` are untouched and still the primary
path on Linux; only *where new files land* changed, and only because it
serves the same "later, continue consuming and rewriting Linux modules"
goal the original layout was also trying to serve. (`windows-rice/` itself
was untouched *at the time this decision was written* — see decision 8 for
what changed there since.)

**6. Decided: build with `nob.h` (vendored), not a Makefile.**
The first working version of this used a plain Makefile. Reconsidered
because `make` turned out to be a real, separate install on a fresh
Windows box (this session's own dev machine didn't have it — had to
`scoop install make`), on top of the `gcc` this project needs anyway;
`nob.h` needs nothing beyond the C compiler already required to build
`install.exe` itself. Mechanics: `nob.c` (`os-rice/nob.c`) is the actual
build script, `nob.h` (`os-rice/nob.h`, vendored verbatim, public domain,
<https://github.com/tsoding/nob.h>) is a single-header library it uses for
running compiler commands. Bootstrap once —
`mkdir build && gcc -o build/nob.exe nob.c` — and every run after that is
just `build\nob.exe`; nob.h's "Go Rebuild Urself" technology recompiles it
on the spot whenever `nob.c` changes, so nobody types that `gcc` line a
second time. Every binary `nob` produces — the programs, the test
binaries, the objects, and `nob` itself — lands under `os-rice/build/`,
never beside the sources, so the tree a developer reads holds only source
and one `.gitignore` line covers the output. `install.exe`/`wallpaper.exe`
are built knowing that: their default root (where `rices/`, `themes/`,
`modules/`, `windows.map` live) is the *parent* of the directory they sit
in, overridable with `--root`.
`nob.c`/`nob.h` are build-time tooling only (run on the dev/CI host, never
cross-compiled for a target), so — unlike `install.c`/`lib/*.c` — they're
written in ordinary C99, matching what `nob.h` itself requires; this does
not loosen decision 3's C89/XP-floor rule for the actual product code.
`osr.ps1` leans on this directly: it creates `build/` and bootstraps
`nob.exe` into it the same way if it isn't already there, so a fresh clone
needs nothing typed by hand at all, PowerShell included (see `osr.ps1`'s
own header comment).

**7. Decided: port windows-rice's own 4 modules (fastfetch, wezterm, pwsh,
oh-my-posh), not a generic module framework.**
Phase 1 initially covered package resolution only, with theme rendering and
module execution left as gaps. Both are now real: `modules.c` runs the same
finite module set `windows-rice/modules/*.ps1` already runs, ported
function-for-function (see `modules.c`'s header comment for the exact
mapping) — package install, Nerd Font install where needed, dotfiles-owned
config copy, and theme-rendered config, using `lib/theme_render.c` (a third
C port of the same `{{role}}` substitution `lib/theme.sh` and
`windows-rice/src/theme.ps1` already implement — see that file's own header
comment) and `lib/wallpaper.c` (the wallpaper half of `lib/config.sh`,
fresh-ported since `windows-rice` had nothing to port for it). This is
still not the "~70 Linux modules" the Scope section below rules out —
`windows-rice` already decided 4 modules is the realistic Windows set, and
this only ports what already existed. `install.exe` also gained
`--theme-only` (the hotkey-safe re-theme path, install.sh's own
`--theme-only`) and `--module` (install a named module with no rice,
install.sh's `--module`), and a standalone `wallpaper.exe`
(`wallpaper.sh`'s shape: separate from install, no packages, no modules
run). `osr.ps1`'s `theme`/`wallpaper`/`module`/`switch` verbs, previously
"not yet implemented," now dispatch to these for real.

**8. Decided: ingest `windows-rice/` fully into the C core, then retire it.**
Decisions 1-7 treated the C core as a *fallback* tier — `windows-rice/`
(plain PowerShell) stayed the primary Windows path, and this plan explicitly
said it "does not touch" that tree. That framing stopped making sense once
decision 7 had already ported `windows-rice/modules/*.ps1` function-for-
function: at that point the C core and `windows-rice/` were two copies of
the same four-module rice, one of them compiled and tested, the other not —
exactly the kind of drift-prone duplication this repo avoids everywhere
else (`DESIGN.md`'s own "distro variance lives in exactly one place"
principle, applied here to "Windows rice lives in exactly one place"). This
session finished the ingestion and deleted `windows-rice/`:
- **`windows.map`** moved to `os-rice/windows.map` (was
  `windows-rice/windows.map`); `lib/winpkg.c` and `install.c`/`modules.c`
  read it from there now.
- **The `osr-rice` theme** (a Windows-native palette plus the one
  Windows-only asset, `config/oh-my-posh/M365Princess++.omp.json`) moved to
  `os-rice/themes/osr-rice/`, alongside every Linux theme — it renders
  through the exact same `lib/theme_render.c` path as `nord`/`gruvbox`/etc.
  now, not a separate Windows-only theme root. `modules.c`'s
  `module_oh_my_posh` no longer needs a second `themes_root` parameter for
  this reason.
- **`Install-Scoop` (pkg.ps1) and `Update-SessionEnvironment` (common.ps1)**
  — the two pieces decision 7's port had left out — are now real:
  `lib/winpkg.c`'s `osr_winpkg_ensure_manager` (installs the one manager a
  `windows.map` row pins the package to, when it is absent -- scoop via
  `irm get.scoop.sh | iex`, choco and winget via their own documented
  installers, both of which require elevation; decision 10 replaced the
  original scoop-only bootstrap) and
  `osr_winpkg_refresh_env` (re-reads Machine + User environment variables,
  including PATH, from the registry after every install attempt, so a
  package a manager just installed is visible to the rest of the same
  `install.exe` run without a new shell).
- **`ui.sh`'s `run_step` spinner** is now ported too (`osr_run_step` in
  `lib/ui.c`) — a live-repainting status block (dimmed tail of the running
  command's own output, a spinner line last) that collapses to `[ok]`/`[!!]
  desc` on completion, off a TTY (piped output, or `OSR_VERBOSE` set) falls
  back to a single plain info line + streamed output, matching `ui.sh`'s
  own auto-degrade. `lib/winpkg.c`'s three package-manager `system()` calls
  now run through it. One deliberate behavior difference from the sh
  original: `osr_run_step` returns the exit code instead of calling
  `osr_error()` (fatal, process-exiting) on failure — see `ui.h`'s header
  comment for why forcing sh's "one fatal path" here would have broken the
  non-fatal scoop-then-choco-then-winget fallback `windows-rice/src/pkg.ps1`
  already established and this C core already mirrors.
- **What did NOT get ported, on purpose** (permanent gaps, not oversights):
  `fonts.ps1`'s manual GitHub-zip-download-and-register fallback (only
  reachable when neither scoop nor choco is present; writing a ZIP
  decompressor in C89 for that corner is disproportionate to the value —
  `lib/fonts.h`'s header comment already flagged this before decision 8);
  `config.ps1`'s `-Ask` interactive confirm-before-overwrite mode (the C
  core always overwrites, same default `config.ps1` itself already used);
  `rice.ps1`'s `-Save` direction (pulling a live installed config back into
  the repo) — `install.sh` never had this concept either, so porting it
  would be a new feature, not a port.

**9. Decided: drop `install.exe`'s dry-run-by-default `--apply` gate — it now
always executes, matching `install.sh`.**
`install.c` originally required an explicit `--apply` flag before a rice or
`--module` run would install anything; without it, every module printed a
"would install..." line and touched nothing. `install.sh` (and `osr`, its
Linux front end) has no equivalent concept at all — `osr install <rice>` and
`osr module <name>` always perform the install immediately, the same
"switch and install share one idempotent engine, nothing is destructive"
model the rest of `os-rice/DESIGN.md` relies on. The dry-run gate was never
a documented, deliberate deviation from that — it was just how `install.c`
happened to get written — and in practice it was a silent trap: running
`osr.ps1 module wezterm` printed a clean-looking `[INFO] would install...`
line with no error and no obvious next step, easy to mistake for "already
done" rather than "nothing happened, you needed `-Apply`." Removed
entirely: `run_one_module`/`run_rice`/`run_modules_direct` in `install.c` no
longer take a `do_apply` flag and always take the action; the `--apply` CLI
option is gone (an unknown option now, same as `install.sh` would treat any
flag it doesn't implement); `osr.ps1`'s `-Apply` switch on `install`/
`switch`/`module` is gone too, for the same reason `./osr` never had one.
`--theme-only` is unaffected — it already always applied regardless of this
flag, matching `install.sh --theme-only`. Net result: `osr.ps1 module
wezterm` (no flags) now does what `osr module wezterm` already did on
Linux — installs and themes it for real, on the first run, with no second
flag to discover.

**10. `windows.map` pins each package to exactly one package manager, and a
missing manager is bootstrapped rather than substituted.**
The file used to list every manager that carried a package
(`pwsh = scoop:pwsh choco:powershell-core winget:Microsoft.PowerShell`) and
`lib/winpkg.c` tried them in a fixed scoop -> choco -> winget order, using
whichever was already installed. That is a trust bug, not just a preference:
scoop, choco and winget are independent namespaces, so `foo` in one is not
`foo` in another, and the manager a project does *not* ship to is exactly
where its name is still free for someone else to publish under. The fallback
order turned "the intended manager is absent on this machine" into "install
whatever the next namespace happens to have under that name," with no signal
that a different publisher answered. Now each row names one manager
(`name[@facet] = mgr:id`), a row naming several is a map error
(`OSR_WINPKG_BAD_ROW`) rather than a chain, and if the pinned manager is
missing osr installs *that* manager: scoop's own per-user installer needs no
elevation, while choco's and winget's do, so from a non-elevated run those
print the exact command to run instead of half-installing. Which manager a
row names follows that project's own Windows install page (Microsoft calls
winget the recommended route for PowerShell; oh-my-posh's docs warn its choco
package is community-maintained; fastfetch's README leads with scoop, so that
row is scoop) — where upstream lists several without ranking them, the tie
goes to the publisher-qualified id, since `Starship.Starship` can only be
claimed by starship while a bare `starship` cannot. The `@facet` qualifiers
(`@24H2` release > `@11` version > `@arm64` arch > bare) are the same
most-specific-wins scheme `lib/pkg.sh` already resolves for Linux
(`os-rice/DESIGN.md` §1a), so per-release and per-arch divergence has a home
without reintroducing a fallback chain. Cost, accepted: a machine that has
only scoop now bootstraps winget (elevated) for the four winget-pinned rows,
where before it would have quietly installed scoop's packages instead.

**11. One provider per row -- a missing manager is installed, never
substituted -- and elevation is asked for once, up front.**
Decision 10 pinned each package to one manager and rejected rows that named
two. What it left open was what happens when that manager is not on the
machine, and since choco's and winget's installers both need Administrator,
"nothing installs" was a real outcome. The resolution keeps the one-provider
rule absolute and makes the named provider always obtainable.

`osr_winpkg_ensure_manager` installs a missing manager from its own vendor
installer: scoop from get.scoop.sh (per-user, no elevation), choco from
community.chocolatey.org/install.ps1, winget from asheroto/winget-install
(Microsoft's documented route for a machine without it is the Store, with no
supported command line). Because the named provider can always be made to
exist, the map needs no fallbacks at all -- which is what lets the format
stay at exactly one provider per row, the same shape `lib/pkgmap/*.map` has
always had on the Linux side. `parse_rhs` rejects a second provider token
whatever it is: two managers, two `source:` builders, or a manager paired
with a `source:`/`script:` provider are all `OSR_WINPKG_BAD_ROW`.

Elevation is a real path rather than a refusal, ported from what the Linux
side already does. `install.sh` runs `sudo -v` once at the top of a run so no
escalating step prompts mid-loop, and `lib/user.sh`'s `as_root` escalates
only the steps that need it. `lib/elevate.c` is that shape for UAC:
`install.c` asks `osr_winpkg_run_needs_admin` before doing any work and, if
some package's provider needs an elevated install, calls `osr_elevate_now` --
which relaunches this same run under the `runas` verb, waits for it, and
exits with its status. One prompt covers the whole run, because every later
`osr_is_admin()` inside the elevated child is simply true. Two details worth
keeping in view: UAC cannot inherit a console, so the elevated child
necessarily gets its own window (a property of Windows, not a choice); and
the child is passed `--user-home`, this port's `$SUDO_USER`, so the profile
being riced travels across the elevation boundary and configs still land in
the user's home rather than in whichever admin account answered the prompt --
exactly the job `as_user` does on Linux.

The remaining providers are the two Linux already has, ported rather than
invented. `script:<url>` fetches a vendor's own install script and runs it
(`irm <url> | iex`), the shape `lib/pkg.sh`'s `_via_script` has. And
`source:<builder>` names a C function in `provide_module.c`, exactly as
`source:provide_wezterm` in `lib/pkgmap/any.map` names a shell function in
`lib/build.sh`.

`source:` is the general escape hatch, and it is a function rather than a URL
on purpose: a URL can only say "download this", while a builder can resolve a
version, pick an asset per architecture, install its own build dependencies
*through this same map*, clone with submodules, run a compiler, and place
several binaries at the end. `provide_module.c` is a metapacket -- it
`#include`s each `provide/<name>.c`, so the whole set is one translation unit
and a new recipe costs no build plumbing -- and holds the registry mapping
the name a map row writes to the function, plus whether it needs
Administrator. `lib/winbin.c` is the toolkit those builders are written
against (resolve a GitHub release asset, fetch, unzip, locate an exe, place
it, extend the user's PATH, hand a file to msiexec), the counterpart of
`lib/build.sh`'s `_osr_install_tarball_bin` and friends.

The contract is copied from `_via_source` so both platforms behave alike:
idempotency belongs to the dispatcher (`osr_provide_run` probes the command
and skips a builder whose program is already present, so no builder writes
its own already-installed check), an unregistered builder name is reported as
a map error rather than silently skipped, and a failed build keeps its
checkout so a retry resumes instead of recompiling from scratch.

wezterm is the worked example, and gives the arm64 question a real answer.
Every Windows provider for it ships x64 only -- checked against
`wez.wezterm`'s winget installer manifest, which declares a single x64
installer -- and upstream publishes no Windows arm64 release asset at all, so
there is no artifact for any manager or URL to fetch. What upstream documents
for exactly that case is building it, so the map reads
`wezterm@x86_64 = winget:wez.wezterm` and
`wezterm@arm64 = source:provide_wezterm`, and `provide/wezterm.c` follows
upstream's own recipe (git clone with submodules, then
`cargo build --release`) after installing git, rustup and Strawberry Perl
through the map. There is deliberately no bare `wezterm` row: upstream has no
32-bit build either, so an x86 machine gets "no windows.map row for wezterm"
rather than a silently wrong answer.

The other four packages need no `@arch` row, and that is a checked claim
rather than an oversight: winget's manifests carry arm64 installers for
Microsoft.PowerShell, Starship.Starship and JanDeDobbeleer.OhMyPosh, and
scoop's fastfetch manifest has an arm64 URL, so all four resolve architecture
themselves. Putting a `source:` row in front of a manager that already has an
arm64 build would be a downgrade -- a builder's output is only ever
overwritten in place, never upgraded or removed by the thing that installed
it.

Resulting behaviour per package: the row's one provider is used. For a
manager row that means installing the manager first if it is missing
(elevating once when that needs it); for `source:`/`script:` it means running
the builder or the script. Nothing falls through to a second provider, and a
package manager is never substituted for another one at any step.

**12. Decided: one file per module, holding every OS it runs on, instead of
one folder per OS.**
The port started with `modules/*.c` for Windows and, once the POSIX core grew
a C module tier, `modules/linux/*.c` beside it — so `fastfetch` was two files
in two directories, doing the same job (install the package, paint the one
`config.jsonc` it reads) with no way to see one from the other. That is the
shape that lets two implementations of one module drift apart quietly: a fix
to the Linux fallback rule, or a new config layer, lands in one file and
nobody reading the other has any signal it happened.

So a module is now exactly `modules/<name>.c` on every OS, with the platform
split inside the file: the Windows implementation under `#ifdef _WIN32` (a row
in `modules.c`'s dispatch, called with `repo_root/themes_root/map_path/theme/
theme_only`), the POSIX one after the `#else` (a row in `lib/modules.c`'s
registry, called with no arguments). Both export the same `osrm_<name>`, which
is safe precisely because they are never compiled together — `nob.c` hands the
file to the Windows core's source list or the POSIX one, never both, and
`modules/src/common.h` declares the Windows signatures under the same guard.
A module only one system can have is simply a file whose other branch is
empty: `win-tweaks`/`win-update`/`win-debloat` (Windows-only, and the `win-`
name prefix rather than a `windows/` folder is what groups them now),
`docker`/`flameshot` (POSIX-only). Nothing about a module's *reach* is
expressed by where its file sits any more; it is expressed by which branches
that file has, which is the thing a reader has to check anyway.

This is decision 5's reasoning ("direct sh-to-C comparison" — keep the files
you have to compare one `ls` apart) applied to the two C tiers themselves.

**Net effect:** this is not "rewrite everything in C." It is one small,
narrow-scope C core (install dispatch + theme rendering, nothing else) built
by three different pinned toolchains for three different reach targets —
except on Windows specifically, where, after decision 8, it is no longer a
*fallback* tier but the one Windows rice implementation this repo ships.
`os-rice/` (POSIX sh) remains the primary path for Linux, untouched by any
of this.

---

## Architecture

### Scope: what the C core does and does not do

**Does:** parse a `rice.list`/`theme.list` (same plain `key: value` format
`os-rice/lib/theme.sh` reads on Linux and `lib/theme_list.c` reads on
Windows — see [`os-rice/DESIGN.md` §6b](os-rice/DESIGN.md)), branch on OS/arch, dispatch
package installs to whatever's actually present (`winget` if found, else a
direct download; `apt`/`pacman`/`apk` if found, else nothing — legacy/bare
targets often have no package manager, only "fetch and place a file"),
render theme templates with the same `{{role}}` substitution algorithm as
the sh/PowerShell renderers.

**Does not:** reimplement the ~70 Linux `os-rice/modules/*.sh`, the
DE/session logic, or anything that assumes a Linux desktop. XP and bare
embedded boards get a minimal rice: shell config, a prompt, a few CLI
tools, a theme. No Hyprland, no Wayland, no GPU probing.

**Exception, decided in decision 7, now the whole Windows story per
decision 8:** the C core runs the 4 Windows modules (fastfetch, wezterm,
pwsh, oh-my-posh) — `modules.c`. That is not the ~70-module tree this
section rules out; it is the same already-decided, already-finite Windows
module set the now-retired `windows-rice/` tree had, ported rather than
reinvented. No Linux module gets a C port under this rule; a *fifth* app
module would need a person to write it (like the first 4 were), not a
generic framework to auto-generate it (see "Not doing" below, which still
holds).

**Second exception, same shape:** the C core also runs 4 Windows *OS
passes* — `win-tweaks`, `win-update`, `win-debloat`, `win-winutil`
(`modules/win-*.c`, over `lib/wintweak.c`). These are the ingest of the
other retired PowerShell tree, `windows-11-x86_64/`, which was ~25 .ps1
files doing three things: write a registry DWORD, disable a service, run a
vendor debloat script. They are deliberately not app modules — no package,
no config, no theme layer — and deliberately not part of any rice: they
change the operating system, so they are asked for by name. The same
"already-decided, already-finite set, ported not reinvented" rule applies;
this does not open the door to a generic Windows-tweak framework.

### Source layout

**Revised from the original plan below** (see decision 5): C sources live
interleaved inside `os-rice/`, next to the `.sh`/`.ps1` file each one ports,
instead of in a separate `universal-core/` tree. What actually exists today:

```
os-rice/
  windows.map           logical package name -> the ONE provider that
                        installs it (`name[@facet] = <provider>`, provider
                        being scoop:/choco:/winget:<id>, source:<builder> or
                        script:<url>), read by lib/winpkg.c.
  provide_module.c/.h   source: builders -- the C port of lib/build.sh. A
                        metapacket: #includes every provide/<name>.c so they
                        are one translation unit, and holds the registry
                        mapping a row's builder name to the function.
  provide/<name>.c      one builder per package (provide/wezterm.c today:
                        the arm64 source build).
                        Ingested from the retired windows-rice/windows.map
                        (decision 8), reworked to one-manager-per-row +
                        @facet qualifiers (decision 10).
  install.c            CLI entry, C port of install.sh: rice.list -> package
                        + module resolution, always installs for real (no
                        dry-run gate, matching install.sh -- decision 9),
                        `--theme-only --theme <name>` (hotkey-safe re-theme,
                        no packages), `--module <name>...` (no rice)
  wallpaper.c           standalone program, C port of wallpaper.sh (show/
                        --list/--next/set) -- separate from install.exe on
                        purpose, same separation the sh originals have
  modules.c / modules.h the finite Windows module set (fastfetch, wezterm,
                        pwsh, oh-my-posh) -- see decision 7/8 and this
                        file's own header comment for the exact mapping
  modules/<name>.c      ONE file per module, never one per OS (decision 12):
                        the Windows implementation behind `#ifdef _WIN32`,
                        the POSIX one after the `#else`, both exporting the
                        same osrm_<name> because only one is ever compiled.
                        fastfetch.c is the file that has both today;
                        wezterm/pwsh/oh-my-posh are Windows-only so far,
                        docker/flameshot POSIX-only. modules/src/common.h
                        is what the Windows branches share.
  modules/win-*.c       the OS-tweak group, ingested from the also-retired
                        windows-11-x86_64/ ps1 tree (setup.ps1,
                        win-update.ps1, winutils.ps1, src/common.ps1 and 19
                        microscripts). NOT app modules -- no package, no
                        font, no config, no theme layer; each is one pass
                        over the operating system, which is why the win-
                        prefix (not a folder of their own) marks them and
                        why they are asked for by name rather than listed
                        in a rice.list:
                          win-tweaks.c   12 registry rows + 7 service rows
                                     as two declarative tables (the 12
                                     reg-*.ps1 and 6 disable-*.ps1 files
                                     differed only in a key, a value name or
                                     a service name); rows that tree carried
                                     but never applied are kept with
                                     enabled = 0 rather than dropped
                          win-update.c   trigger a Windows Update run:
                                     wuauclt for the older reach targets,
                                     usoclient where that is the live
                                     interface
                          win-debloat.c  the two third-party vendor scripts
                                     (Raphire's Win11Debloat, Chris Titus
                                     WinUtil), opt-in, over winbin's
                                     run_script
  modules/win-data/     the two non-code files that tree carried
                        (ooshutup10.cfg, winutils.json): saved profiles for
                        tools this repo does not drive
  themes/
    osr-rice/            a Windows-native palette (theme.list + the one
                        Windows-only asset, config/oh-my-posh/
                        M365Princess++.omp.json) -- ingested from the
                        retired windows-rice/themes/osr-rice/ (decision 8),
                        lives alongside nord/gruvbox/etc. now, renders
                        through the same lib/theme_render.c path as any of
                        them
  lib/
    net.c / net.h       C port of lib/net.sh: URL/header parsing (portable,
                        no OS dependency) + WinInet-backed fetch (Windows
                        only so far; the #else branch is a documented stub,
                        landing spot for a native Linux port later)
    winpkg.c / winpkg.h windows.map lookup (facet-qualified, most specific
                        key wins) + install through the row's single
                        provider, installing a missing manager from its
                        vendor installer first (osr_winpkg_ensure_manager,
                        elevating once when that needs admin; decision 11)
                        + registry env refresh (osr_winpkg_refresh_env) --
    winbin.c / winbin.h the toolkit source: builders are written against:
                        resolve a direct URL or a gh:owner/repo:pattern
                        release asset, fetch, unzip, locate an exe, place it,
                        extend the user's PATH, run an msi/setup installer or
                        a vendor script (decision 11) -- the Windows
                        counterpart of lib/build.sh's install helpers
    elevate.c/elevate.h one-shot UAC elevation: relaunch this run under
                        `runas`, carrying --user-home across the boundary
                        the way sudo carries $SUDO_USER. Port of
                        install.sh's sudo warm-up + lib/user.sh's as_root
                        (decision 11)
                        the full package half of the retired windows-rice/
                        src/pkg.ps1 + src/common.ps1 (decision 8). NOT a
                        port of lib/pkg.sh -- that's a different package
                        model (apt/pacman/apk), still POSIX-sh-only
    manifest.c / manifest.h  rice.list parser (the `theme:`/`themes:`/
                        `require:`/module-line format), factored out of
                        install.c for standalone unit testing. Does NOT
                        parse theme.list's own shape -- that's theme_list.c.
    theme_list.c / .h   theme.list parser (meta fields + `color:`/`config:`
                        multi-valued lines), C port of lib/theme.sh's
                        _osr_theme_lines/osr_theme_meta/osr_theme_color --
                        the one parser of this format now (decision 8)
    theme_render.c/.h   `{{role}}`/`{{role_rgb}}`/`{{role_dec}}`/
                        `{{role_sgr}}` template substitution + the literal-
                        file-then-render layer resolution chain, C port of
                        lib/theme.sh's _osr_theme_sed. Verified against a
                        real template (wezterm/wezterm-theme.toml.tmpl) +
                        real theme (themes/nord), not only a synthetic fixture.
    config_copy.c / .h  `~` expansion + bounded file copy (parent dirs
                        created as needed)
    fonts.c / fonts.h   Nerd Font install: registry "already installed"
                        check + scoop/choco dispatch (the manual GitHub-zip
                        fallback is a documented, permanent scope cut -- see
                        decision 8 and the file's own header comment)
    wintweak.c / .h     registry DWORD writes, service stop/start-type
                        control and recursive purges, straight through
                        Win32 (RegCreateKeyEx, OpenSCManager/
                        ChangeServiceConfig) rather than by spawning a
                        PowerShell. The mechanism half of the retired
                        windows-11-x86_64/src/common.ps1; the policy half
                        is modules/win-tweaks.c's tables
    wallpaper.c / .h    theme wallpaper library + apply +
                        SystemParametersInfo live-set, C port of the
                        wallpaper half of lib/config.sh (fresh port from sh,
                        windows-rice never had this)
    state.c / state.h   `%USERPROFILE%\.config\osr\state` key=value reader/
                        writer, C port of lib/state.sh
    ui.c / ui.h         colored `[INFO]`/`[WARN]`/`[ERROR]`/`[DONE]` status
                        lines + step counter + osr_run_step (the ui.sh
                        live-repainting spinner block, ported in full as of
                        decision 8 -- see ui.h's header comment for the one
                        deliberate behavior difference from the sh original)
  test/
    c_test.h            tiny C89 assertion micro-framework (ok/fail/finish,
                        same shape as test/lib.sh, nothing generated between
                        the two)
    unit_c/
      net_parse_test.c     lib/net.c's portable parsers
      winpkg_test.c        lib/winpkg.c against the real windows.map fixture
      manifest_test.c      lib/manifest.c against real rices/*/rice.list
      theme_render_test.c  lib/theme_list.c + theme_render.c: a synthetic
                        fixture matching test/unit/theme_template.sh's own,
                        plus a real template rendered against a real theme
      config_copy_test.c   lib/config_copy.c: ~ expansion + file copy
      wintweak_test.c      lib/wintweak.c's key parser + every row of
                        modules/win-tweaks.c's two tables. Touches
                        nothing: the verbs change the machine they run on,
                        but the tables ARE the port, so asserting them row
                        by row is what catches a setting lost in the ingest
    fixtures/            synthetic theme.list/.tmpl fixtures for
                        theme_render_test.c (mirrors test/unit/
                        theme_template.sh's own synthetic scenario)
  nob.c / nob.h         build script + vendored build-system library,
                        replaces the Makefile the first version of this
                        used (see decision 6).
                        `mkdir build && gcc -o build/nob.exe nob.c` once,
                        then `build\nob.exe` (builds build\install.exe +
                        build\wallpaper.exe) / `build\nob.exe test` /
                        `build\nob.exe clean`.
  build/                every binary, and nothing else: install.exe,
                        wallpaper.exe, nob.exe, test/ (the unit-test
                        binaries) and obj/ (the shared .o files). Written
                        by nob, git-ignored whole, never mixed in with the
                        sources.
  osr.ps1               the Windows front end -- the only one now that
                        windows-rice/ is retired (decision 8) -- mirrors
                        ./osr in full (install/switch/theme/wallpaper/module/
                        list/modules/test). Bootstraps build/nob.exe and
                        the binaries under build/ itself if they don't
                        exist yet.
  osr.bat               thin cmd.exe launcher for osr.ps1
```

`toolchains/` (Dockerfiles for pinned/patched builders) does not exist yet --
Task 0.1 is unstarted, see that task's updated status below.

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
| Win10/11 | current MSYS2 mingw-w64 or MSVC | UCRT | no constraints -- this is also the only Windows rice this repo ships now (decision 8), not an optional extra tier |
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

**Not yet what `osr.bat`/`osr.ps1` do today.** Those two files (see Source
layout) are the CLI entry point/dispatcher — they build `install.exe`
locally from source via `nob.c` if it isn't there yet, they don't fetch a
prebuilt release binary from anywhere. This section's "fetch the right
prebuilt binary" stub is a different, still-unbuilt piece: something to
revisit once there's an actual release feed of prebuilt binaries to fetch
from (parallel to how `cargo-binstall` needs published release artifacts
to point at).

---

## Task breakdown

### Phase 0 — Toolchain infrastructure (blocks everything else)

- [ ] **Task 0.1: Pin and Dockerize the XP-capable mingw-w64 build**
  - **Status: long-away-planned, not started.** Explicit call: the pinned/
    patched toolchain itself is real infrastructure work with no
    dependents ready for it yet, so it stays deferred. What *is* done
    ahead of it: `nob.c` already builds every C source with
    `-DWINVER=0x0501 -D_WIN32_WINNT=0x0501` on an ordinary current
    mingw-w64, and `lib/net.c`'s WinInet calls and `lib/winpkg.c`'s
    `system()`-based dispatch were written against that floor (no
    `winpthread`, no C++ stdlib, no API newer than XP) — so the source is
    ready for this toolchain the day it exists; it just hasn't been
    verified against the *actual* pinned/patched compiler or a real XP
    boot yet.
  - Acceptance: `docker run osr-xp-toolchain gcc -o hello.exe hello.c` produces
    an `.exe` that boots on a real or emulated XP SP3 install and prints
    output.
  - Verification: manual boot test in a QEMU XP VM (no CI runner for this —
    same "manual/nightly, not per-commit" precedent as `os-rice/DESIGN.md`
    §9's QEMU DE smoke test).
  - Dependencies: none.
  - Files: `os-rice/toolchains/xp.Dockerfile` (path updated per decision 5).
  - Scope: Medium.

- [ ] **Task 0.2: musl-gcc build path for common Linux architectures**
  - Acceptance: `make TARGET=linux-x86_64-musl` and `make TARGET=linux-aarch64-musl`
    both produce statically-linked binaries with `ldd` reporting "not a
    dynamic executable."
  - Verification: automated, runs in normal CI.
  - Dependencies: none.
  - Files: `os-rice/nob.c` (path/build-tool updated per decisions 5 and 6).
  - Scope: Small.

### Checkpoint: Phase 0

- [ ] XP toolchain produces a bootable binary (manual VM check).
- [ ] Musl static build is automated and green in CI.
- [ ] Human review before writing any real orchestration logic on top of this.

### Phase 1 — Minimal core (proves the shape end to end)

- [x] **Task 1.1a: `rice.list` half of the manifest parser — done.**
  `lib/manifest.c`'s `osr_parse_rice_list()` reads `theme:`/`themes:`/
  `require:`/module-line `rice.list` files, matching install.sh's own
  inline parser (comment stripping, whitespace trim, directive lines).
  Verified against the real `rices/nord/rice.list` and
  `rices/gruvbox/rice.list` fixtures in `test/unit_c/manifest_test.c`
  (13 assertions, all passing).
- [x] **Task 1.1b: `theme.list` half — done.** `lib/theme_list.c`'s
  `osr_load_theme_palette()` reads the single-valued meta fields
  (`display:`/`description:`/`polarity:`/`session:`/...) and the
  multi-valued `color:`/`config:` lines, including the narrower comment
  rule `_osr_theme_lines` needs (a `#rrggbb` value must never be read as a
  comment). Files: `os-rice/lib/theme_list.c`, `theme_list.h` (a separate
  parser, not an extension of `manifest.c` — the two formats' comment rules
  differ, see `theme_list.h`'s header comment).

- [x] **Task 1.2: Theme template renderer — done, as `theme_render.c`.**
  `osr_render_template()` implements the full `{{role}}`/`{{role_rgb}}`/
  `{{role_dec}}`/`{{role_sgr}}`/meta/`{{THEME}}` substitution
  (`_osr_theme_sed`'s algorithm) and `osr_theme_layer_source()` the
  literal-then-render resolution chain (`Get-ThemeSource`/
  `Install-ThemeLayer`).
  - Acceptance met: `test/unit_c/theme_render_test.c` renders a real
    template (`wezterm/wezterm-theme.toml.tmpl`) against a real theme
    (`themes/nord`) and asserts the substituted colors are present with
    nothing left unsubstituted, plus the exact synthetic scenario
    `test/unit/theme_template.sh` already proves against the sh
    implementation (same theme, same template, same expected output
    lines) — a human comparing the two sees the same behavior, ported.
  - Not run against all six themes/every `.tmpl` in one automated sweep
    the way `theme_template.sh`'s last section does (looping every real
    theme against every real template) — a real gap versus that test's
    full coverage, worth closing before relying on this for a theme this
    session hasn't hand-tested.
  - Files: `os-rice/lib/theme_render.c`, `theme_render.h` (path/name
    updated: not `theme.c`, since manifest/theme_list/theme_render ended
    up as three separate small files rather than one).

- [ ] **Task 1.3: Windows branch — install dispatch + `fetch_and_place` fallback**
  - **Status: partially done, scope narrower than originally written.**
    `lib/winpkg.c` implements the dispatch half against `windows.map`
    (one pinned manager per package, see decision 10) and
    `install.c` wires it up end to end (`install.exe <rice>` resolves a
    real `rice.list` and reports/installs each module's package). What
    decision 11 then added: the generic `fetch_and_place` fallback this
    task's title names, as the map's `bin:` token and `lib/winbin.c` --
    every row now carries a vendor-binary route, so a machine with none of
    the managers still installs, per-user and with no UAC prompt.

    Verified live on the dev machine: `gh:` asset resolution against the
    real GitHub API, the glob that picks the versioned asset, download,
    `Expand-Archive`, locating the executable inside the archive, and the
    HKCU PATH append (checked with a command absent from the machine, so
    the PATH entry is what made it resolve; %LOCALAPPDATA% was pointed at a
    scratch tree and the PATH value restored byte-for-byte afterwards).

    Still unverified: a real machine with NO package manager at all -- this
    dev box has scoop, choco and winget, so the manager bootstraps
    (`osr_winpkg_ensure_manager`), the elevation relaunch
    (`osr_elevate_now`), and the `scoop install`/`choco install`/
    `winget install` calls themselves have not been exercised in anger.
    The routing that chooses between them is unit-tested; the commands it
    chooses are not.
  - Acceptance: on a Win7 VM with no winget, `install_pkg("starship")`
    downloads and places the binary without error; on Win10/11 with winget
    present, it uses winget instead.
  - Verification: manual run on both a Win7 and Win10 VM.
  - Dependencies: Task 1.1a.
  - Files: `os-rice/lib/winpkg.c`, `os-rice/install.c` (paths updated per
    decision 5 — no separate `os_windows.c`; the platform branch lives
    inside `lib/net.c`'s `#ifdef _WIN32`, not a whole extra file, since
    Windows package dispatch (`windows.map`) and Linux package dispatch
    (`lib/pkg.sh`'s apt/pacman/apk) are different enough models that they
    were never going to share one `install_pkg()` body).
  - Scope: Medium.

- [ ] **Task 1.4: Linux branch — package-manager probe + bare fallback**
  - Acceptance: on a normal Debian container, uses `apt-get`; on a bare
    busybox rootfs with no package manager, falls through to
    `fetch_and_place`.
  - Verification: container test for both cases, added to
    `os-rice/test/matrix.sh`-style harness.
  - Dependencies: Task 1.1a.
  - Files: `os-rice/lib/net.c`'s `#else` branch (currently a documented
    stub returning "unsupported" — see decision 5; path updated, no
    separate `os_linux.c`).
  - Scope: Medium.

- [x] **Task 1.5: Windows module execution + wallpaper — done, beyond the
  original Phase 1 scope.** Not in the original task breakdown (the
  original Scope section ruled out module execution entirely); added this
  session once `windows-rice/modules/*.ps1` turned out to be a small,
  finite, already-decided set worth porting rather than a reason to defer
  further — see decision 7.
  - Acceptance: `install.exe <rice>` installs + themes fastfetch, wezterm,
    pwsh, and oh-my-posh for real (package + Nerd Font where needed +
    dotfiles config + theme-rendered config); `install.exe --theme-only
    --theme <name>` re-themes only what's already installed, no packages;
    `install.exe --module <name>...` installs module(s) without a rice;
    `wallpaper.exe` shows/lists/steps/sets the theme wallpaper. (Originally
    written against a `--apply` flag that gated real installs behind a
    dry-run default; that gate was removed in decision 9 so this now reads
    "always does it," matching `install.sh`.)
  - Verification: manual smoke test on this session's own dev machine —
    `install.exe --theme-only --theme nord` correctly themed the three
    modules actually installed there (fastfetch, wezterm, oh-my-posh),
    including reproducing oh-my-posh's real "no config for this theme,
    fall back to osr-rice" warning path byte-for-byte against
    `oh-my-posh.ps1`'s own logic. A real package-manager install (winget/
    scoop/choco actually installing something) is still unverified — every
    test so far either exercised the dry-run plan (pre-decision-9) or ran
    against tools already present, so the `scoop install`/`choco install`/
    `winget install` `system()` calls themselves have not been exercised in
    anger. `install.exe --module wezterm` was re-verified end to end after
    decision 9 (real build, real run, `[DONE] wezterm: themed as 'xin'`) on
    this dev machine, where wezterm was already installed — still not
    verified on a second machine or a clean VM.
  - Files: `os-rice/modules.c`, `modules.h`, `os-rice/wallpaper.c`,
    `os-rice/lib/wallpaper.c`, `wallpaper.h`.
  - Scope: Large (was the biggest single increment of this session).

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
  - Files: `os-rice/lib/net.c` (path updated per decision 5).
  - Scope: Small.

- [ ] **Task 2.3: Minimal XP rice — one real end-to-end install**
  - Acceptance: running the XP binary against a trimmed `rice.list`
    (shell config + one CLI tool + one theme) succeeds on real/emulated XP,
    producing a themed, working prompt.
  - Verification: manual, screenshotted, on the XP VM.
  - Dependencies: Task 2.2.
  - Files: a new minimal rice, e.g. `os-rice/rices/xp-minimal/` (path
    updated per decision 5).
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
  - Files: `os-rice/toolchains/`, `os-rice/nob.c` (path/build-tool updated
    per decisions 5 and 6).
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
  (This still holds after Task 1.5/decisions 7-8: the 4 modules `modules.c`
  ports are the Windows rice's own already-finite module set, not any of
  these ~70 Linux ones.)
- **Pre-building cross-toolchains for architectures with no named target
  device.** Only stand up an obscure-arch toolchain against Task 3.1's
  concrete, real board — otherwise this becomes an unbounded task list for
  hardware nobody is using.
- **Replacing `os-rice/` with this C core.** `os-rice/` (POSIX sh) already
  works, is past MVP, and is the right tool wherever a real shell exists —
  this plan only fills the gap it structurally cannot reach. (Windows is the
  one place this bullet no longer applies exactly as originally written:
  decision 8 *did* replace `windows-rice/`, a plain-PowerShell tree with no
  compiled core underneath, with this C core plus its `osr.ps1` front end —
  see that decision for why duplicating the same 4-module rice in two
  untested-against-each-other trees stopped being the right call. `os-rice/`
  itself is untouched.)
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
- Should an XP-minimal rice's `rice.list` live under a new `os-rice/rices/
  xp-minimal/`, or should existing rices grow an optional "minimal subset"
  the C core can also read, to avoid a fourth manifest tree to keep in
  sync? (Less pressing than when this was first written — decision 5's
  interleaved layout means there's no longer a *separate tree's* rices/ to
  duplicate into, just the question of a new directory under the existing
  one.)
