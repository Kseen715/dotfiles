---
title: os-rice — Design
type: design
status: past-MVP
updated: 2026-08-28
tags:
  - kind/design
  - topic/os-rice
  - topic/theming
  - topic/hardware
  - lang/sh
  - lang/c
  - os/linux
---

# os-rice — Design

A DRY, declarative, POSIX-portable installer for unix-like apps, configs, and
whole rices.

> [!important] Direction: the backbone becomes C
> The shell tier works and is not going anywhere tomorrow, but it is **not
> the destination**. Every part of the backbone — the libs, the runner, the
> front end, and eventually the ~115 modules — is being rewritten as C
> translation units linked into one binary (`build/osr`), the same shape the
> Windows core already has. Sections [[#D-3. The C harness|D-3]],
> [[#D-4. A module may be a C unit instead of a script|D-4]] and
> [[#13. The port, and what is left of it|13]] are the plan and the score.

> [!abstract] Problem
> Describe a whole rice (apps + modules + configs) as one readable list, and
> install it re-runnably on any unix-like distro, without pasting the same
> module once per package manager.

**Related:** [[os-rice/README|os-rice/README]] · [[PLAN_UNIVERSAL]] ·
[[archive-decisions]] · [[proteus/README|Proteus]]

---

## Core principle

> Distro variance lives in exactly one place — `pkg.sh` + `pkgmap/` +
> `detect.sh` — never smeared across modules.

A new distro means teaching `pkg.sh` its verbs and adding a `pkgmap`. Most
modules light up for free.

> [!note] Windows is its own world
> Different package model, different language. It is **not** PowerShell any
> more either: it is the C core (`install.c`, `lib/win*.c`, `modules/win-*.c`,
> `windows.map`). See [[PLAN_UNIVERSAL]]. Do not force it into this
> abstraction.

---

## Decisions in force

### D-1. POSIX sh everywhere (shell tier)

Runs on Alpine / busybox `ash` out of the box, no bash. Cost: no `[[ ]]`, no
arrays — space-lists or `while read`. Every module is validated under `dash`
and busybox `ash`, not just bash.

### D-2. CLI output is ASCII-only

Every byte written to the terminal is 7-bit ASCII. No Unicode spinners, box
drawing, em-dashes or checkmark glyphs.

| purpose | bytes |
| --- | --- |
| spinner frames | `-\|/` |
| success | `[ok]` |
| failure | `[!!]` |
| separators | `\|` `-` |

**Why:** busybox on fresh Alpine, a serial console, `LANG=C`, or a minimal
`TERM` mangle non-ASCII into mojibake. ANSI SGR color is still ASCII bytes and
stays gated on `[ -t 1 ]`.

> [!info] Scope
> This governs **program output**, not this document. Enforced in CI by a
> non-ASCII grep over `lib/` + `modules/` string literals.

### D-3. The C harness

The POSIX side is C, arranged like the Windows core: `nob.c` links `osr.c` (a
command dispatcher) with one translation unit per file it replaced into a
single binary, `build/osr`.

| shell file | unit | command | what moved |
| --- | --- | --- | --- |
| `lib/ui.sh` | `lib/ui.c` | `osr ui` | palette, live step window, step counter |
| `lib/log.sh` | `lib/log.c` | `osr log` | the five log lines |
| `lib/state.sh` | `lib/state.c` | `osr state` | all of it — **file removed** |
| `lib/user.sh` | `lib/user.c` | `osr user` | user model, login shells, file primitives |
| `lib/detect.sh` | `lib/detect.c` | `osr detect` | every distro and hardware probe |
| `lib/theme.sh` | `lib/theme.c` | `osr theme` | manifests, palette, the `{{key}}` script |
| `install.sh` | `lib/install.c` | `osr install` | help, listings, option loop, manifest, report |
| `test/run.sh` | `lib/testrun.c` | `osr test-run` | the suite runner |

Five of the ported units keep a shell shim in `lib/` (`ui.sh`, `log.sh`,
`user.sh`, `detect.sh`, `theme.sh`). They are **not** implementations — they are the
shell-callable surface, which cannot go away while ~115 modules are shell
scripts:

- `run_step`'s arguments are shell **functions** (`pkg_install`, `as_root`), so only a shell can fork them;
- `error` must `exit` the running shell;
- `as_user`/`as_root` are command prefixes wrapping `sudo`;
- `osr_detect`/`osr_resolve_theme` set the variables modules branch on — the core prints assignments, the shim `eval`s them;
- `install.sh` **sources** each module.

> [!important] `bootstrap.sh` compiles nothing
> It runs before a toolchain is a given and stays pure sh. Past that point the
> tool assumes a C compiler. See [[archive-decisions#A2|A2]].

**Contract:** byte-for-byte identical output. The sh version is frozen under
`test/ref/` and diffed against the C one by `test/unit/*_c_parity.sh` — 317
checks over the eight units. Exactly one divergence is accepted and asserted
rather than hidden (see [[archive-decisions#A2|A2]]).

### D-4. A module may be a C unit instead of a script

`modules/<name>.c` registered in `lib/modules.c` against `lib/module.h`, with
`modules/*.sh` unchanged beside them. `install.sh` asks `osr module has
`<name>` per manifest entry, so a `rice.list` never says which tier it wants
and a module can move between them without touching a rice.

**One file, not one per OS.** `modules/fastfetch.c` holds both
implementations — Windows behind `#ifdef _WIN32` (dispatched from
`modules.c`), POSIX after the `#else` (registered in `lib/modules.c`) — both
exporting `osrm_fastfetch`, because only one is ever compiled. `nob.c` gives
the object to whichever core the host builds.

What C buys is not speed:

- **`osr_step` forks a function of the program.** `run_step` could only fork a *shell* function — the single reason `lib/ui.sh` still exists.
- **A module is a translation unit, not a sourced fragment.** A `.sh` module runs inside the installer's shell and can clobber any lib variable.
- **The contract is a header**, not "whatever happens to be defined by the time we source you".
- **It is checkable.** `test/unit/module_c_parity.sh` runs the frozen `.sh` and the C module side by side under stubbed package tooling and diffs what they did to the box.

What C costs is the property the module system was built for: a module as one
readable POSIX script anyone can copy and edit. Modules are the **last** stage of the port, not the first: the backbone has to
be able to carry them (providers included) before a shell module can move
without losing behaviour. Until then a port happens when a module is being
touched anyway, and preferably one whose logic is real rather than three lines
of package install.

> [!warning] Providers are not ported
> `osr_pkg_install` covers the **native** path only and hands a provider row
> back to `lib/pkg.sh`. `source:` in particular dispatches into
> `lib/build.sh` (~1250 lines of downloaders and builders). That one call is
> the only place the C tier calls back into sh.

### D-5. Rices are declarative manifests

A rice is a plain list of modules/apps + configs, `#` comments, parsed with
`while read`. No TOML/YAML — that needs a parser, which is un-POSIX. The
module count in the list **is** the progress-bar denominator.

### D-6. A package has a *method*, not just a *name*

`pkgmap` name→name(s) only covers "same manager, different name". It has no
answer for "the install *method* itself varies": AUR on Arch, `apt` on Debian,
`cargo` where no package exists, `curl | sh` for starship, from-source for
paru. So a row's RHS may carry a **provider tag**. No tag = native manager.

### D-7. Config is layered by ownership

| layer | owner | overwrite policy | rice-scoped? |
| --- | --- | --- | --- |
| `00-env` | user / machine | seeded once if absent, then kept | no |
| `10-*` `20-*` `30-*` | dotfiles repo | overwrite on update | no |
| `90-theme` | rice | **swapped** on rice switch | **yes** |
| `99-local` | machine | gitignored, never touched | no |

This is what makes rice-switching non-destructive: the user's env and aliases
are structurally out of os-rice's reach.

---

## 1. Package abstraction + one-to-many table

One map file per package manager, logical name → real package(s). No entry =
pass the name through unchanged, so the common case is zero-effort. Only
packages that *actually differ* get a row.

```
# lib/pkgmap/apt.map          # lib/pkgmap/dnf.map
zsh = zsh                     zsh = zsh
build = build-essential       build = gcc gcc-c++ make      # one-to-many
dev-headers = libssl-dev      dev-headers = openssl-devel pkgconf
```

```sh
# lib/pkg.sh -- distro variance is exactly this one case statement.
_via_native() {                     # _via_native build-essential zsh
  case "$OSR_PKG" in
    apt)    apt-get install -y "$@" ;;
    dnf)    dnf install -y "$@" ;;
    pacman) pacman -S --needed --noconfirm "$@" ;;
    apk)    apk add "$@" ;;
    xbps)   xbps-install -y "$@" ;;
  esac
}
```

Five verbs cover ~everything: `pkg_install`, `pkg_installed`, `pkg_refresh`,
`pkg_add_repo`, `pkg_remove`. Modules only ever say `pkg_install build`.

### 1a. Facet qualifiers — `name@facet`, most specific wins

`OSR_PKG` is not the only axis of variance. Two more bite in practice:

- **Release version.** `ghostty` is native on Ubuntu noble (24.04) but must build from source on jammy (22.04).
- **CPU arch.** Native managers resolve arch themselves; this bites artifact-fetching providers naming a specific asset (`go1.22.linux-amd64.tar.gz`).

Both are one mechanism: a map key may carry an `@facet`, and the resolver
checks most specific first, falling back to the bare name.

```
ghostty            = source:build-ghostty     # default for apt
ghostty@noble      = ghostty                  # 24.04 ships it
# jammy has no row -> falls through to source:

go                 = tarball:https://go.dev/dl/go1.22.linux-${OSR_ARCH_DEB}.tar.gz
zig@aarch64        = source:build-zig         # no prebuilt for arm64
```

**Resolution ladder:**

| key form | axis | match |
| --- | --- | --- |
| `name@trixie` | codename | exact |
| `name@3.21.3` | version_id | exact |
| `name@3.21` | version_id | dotted prefix, longest first |
| `name@<=3.21` | version_id | comparison `<` `<=` `>` `>=`, first matching row wins |
| `name@x86_64` | arch | exact |
| `name` | — | the bare row |

Prefixes are **components**, not string prefixes: `name@3.21` covers 3.21.x
and never 3.210. Comparisons are component-wise and numeric, missing
components count as 0, and each component keeps its leading digits only —
which is what makes `24.04`, `15-SP5` and `3.24_alpha` comparable at all. A
hand-rolled `_ver_cmp`, not `sort -V`, because it must run identically in
`lib/module.c`.

> [!warning] Boundary that follows from that
> `3.22.1` is **greater** than `3.22`. A bound meant to cover a whole series
> is `<3.23`, not `<=3.22`. Ranges have no specificity order between them, so
> **file order is the tie-break: write the tightest bound first.**

**Free properties:** the §2 idempotency probe follows automatically, since the
tag drives the probe and a qualified row resolves to a different tag per
release/arch. A version that flips to `source:` needs a toolchain — that stays
**out of `rice.list`**; the build function declares its own prerequisite.

**Remaining ceiling:** no ranges on codename (unordered by nature) and none on
arch. Arch keeps two naming schemes (`OSR_ARCH`, `OSR_ARCH_DEB`); add a third
alias var only when a third upstream convention forces it.

---

## 2. Idempotency — a hard contract

> [!important] Rule
> A module may be run 100× and converge. It never errors on the 2nd run.

Idempotency comes from guard helpers used instead of raw mutation:

```sh
pkg_installed zsh || pkg_install zsh              # pacman --needed is built-in
ensure_line "$rc" 'eval "$(starship init sh)"'    # grep -q before append
backup_copy "$src" "$dst"                         # .bak once, then overwrite

osr_shell_is "$u" "$(command -v zsh)" || osr_set_login_shell "$u" "$(command -v zsh)"
```

`osr_set_login_shell` exists because `chsh` is not a given: busybox images have
no such applet and minimal Fedora leaves it in `util-linux-user`, so a
chsh-only module quietly leaves those boxes on `/bin/sh`. It registers the
shell in `/etc/shells`, then tries `chsh` → `usermod` → a direct `/etc/passwd`
rewrite, **verifying the passwd entry after each** rather than trusting an
exit code.

Holds and pins are part of the contract: never reinstall or override a
user-held, pinned or ignored package. That is "don't destroy user-defined
state" applied to packages.

---

## 3. Progress output — two-level steps, no fake byte bars

A real byte-level progress bar is impossible: `apt`/`pacman` own their stdout
and report no parseable progress. So:

**Two-level step progress:** `[03/12] zsh | 4/6 [ok]`

| level | source | exactness |
| --- | --- | --- |
| outer `[03/12]` | module count from the manifest | exact, pre-known |
| inner `4/6` | `grep -c '^[[:space:]]*run_step' modules/zsh.sh` | locally exact per module |

- **A skipped branch counts as finished.** Executed `run_step`s tick +1; guarded ones that do not fire do not. At module end `step_finish_module` fast-forwards the inner counter to its total, so it always reaches `6/6` and skipped work renders `[ok] skipped`. The jump `4/6 -> 6/6` is the honest signal a guard skipped work, not a stall.
- **Why not an exact global pre-count?** Conditionals and loops mean the true total is unknowable without running, and a dry-run pre-pass counts guarded steps the real run then skips — so "exact" drifts anyway.
- **Lint rule this depends on:** a raw `git clone`/`curl` outside `run_step` is invisible to the count. CI greps for install-ish verbs outside `run_step`.

**Spinner** wraps long silent steps: run in background, capture output to a
per-run logfile, animate `-\|/` on `\r`, print `[ok]`/`[!!]`, dump the log
tail on failure.

**Auto-degrade** keys off `[ -t 1 ]`. TTY → spinners + hidden output. Piped /
CI / `--verbose` → plain streamed lines, no escape junk in logs.

> [!note] Honest caveat
> Hidden output means a hang shows a spinner, not the tool's live output.
> Mitigated by `--verbose` and the on-failure log dump — the same tradeoff
> `brew` and `paru` make.

The painting lives in `osr ui` (`lib/ui.c`); `lib/ui.sh` keeps only the fork,
because a step's argv is a shell function.

---

## 4. Provider-tagged install methods

```
zsh      = zsh                                    # native (default)
build    = gcc gcc-c++ make                       # native, one-to-many
starship = script:https://starship.rs/install.sh  # curl | sh
ripgrep  = cargo:ripgrep                          # where no package exists
vscode   = aur:visual-studio-code-insiders-bin    # arch.map only
paru     = source:build-paru                      # from-source build fn
```

`pkg_install` is **expand → group by method → dispatch each group**; native
rows still batch into one call:

```sh
pkg_install() {                    # pkg_install zsh starship paru
  native="" ; for spec in $(_pkgmap "$@"); do
    case "$spec" in
      cargo:*)  _via_cargo  "${spec#cargo:}"  ;;
      script:*) _via_script "${spec#script:}" ;;
      aur:*)    _via_aur    "${spec#aur:}"    ;;
      source:*) _via_source "${spec#source:}" ;;
      *)        native="$native $spec"        ;;
    esac
  done
  [ -n "$native" ] && _via_native $native
}
```

**Each provider owns its idempotency probe** — the tag drives the skip check,
not just the install, or the §2 contract breaks.

| method | install | `pkg_installed` probe |
| --- | --- | --- |
| native | apt / dnf / pacman | `dpkg -s` / `rpm -q` / `pacman -Q` |
| cargo | `cargo install` | `cargo install --list \| grep -q` |
| script | `curl … \| sh` | `command -v <bin>` |
| aur | `$AUR_HELPER -S` | `pacman -Q` |
| source | build fn in `lib/build.sh` | `command -v <bin>` \|\| marker file |

**Prerequisites are ordinary manifest lines, in order** — `rust` before any
`cargo:` row, `paru` before any `aur:` row. Manifest order **is** the
dependency graph.

> [!note] `script:` installers are not checksum-pinned
> Upstream drift is accepted for the convenience. Revisit only if a drifting
> installer actually burns a run.

---

## 5. Config layering — every config, not just zsh

A drop-in dir sourced in lexical order, plus managed marker blocks wherever a
single file is unavoidable.

```
~/.config/osr/zsh/rc.d/
  00-env.zsh      user / per-machine   seeded once if absent, never touched after
  10-aliases.zsh  dotfiles             overwrite on update, rice-independent
  20-func.zsh     dotfiles             overwrite on update
  30-tools.zsh    dotfiles             guarded tool init (lazy nvm, shared
                                       ssh-agent) that must reach new machines,
                                       so it cannot live in 99-local
  90-theme.zsh    rice-owned           swapped on rice switch
  99-local.zsh    per-machine          gitignored, never touched
```

The shipped `~/.zshrc` is a thin loader. Where the target file must stay
singular, inject only a marked block:

```sh
# >>> os-rice:loader >>>   (managed -- edits between markers are overwritten)
for f in "$HOME"/.config/osr/zsh/rc.d/*.zsh; do . "$f"; done
# <<< os-rice:loader <<<
```

`ensure_block` owns the marked region. DE configs layer through their own
include mechanisms:

| app | mechanism |
| --- | --- |
| hyprland | `source = ./00-env.conf` + `source = ./90-theme.conf` |
| waybar | `config.jsonc` + `90-theme.css` (rice-owned) |
| foot | `foot.ini` includes `foot-colors.ini` |
| btop | `btop.conf` selects `themes/rice.theme` |

**Two seeding rules:**

1. `00-env` / `99-local` are seeded once if absent, then never rewritten.
2. PATH/env mutation in `00-env` is **guard-style**, never a blind append, so a re-run never duplicates entries.

### 5a. System config paths vary by distro *family*

The variance is a narrow set of system config: `/etc/default/foo` (Debian) vs
`/etc/conf.d/foo` (OpenRC/Alpine) vs `/etc/sysconfig/foo` (RHEL). Because it
is family-wide, a per-package "confmap" is the wrong shape — it would be rows
of identical values. Resolve the base dir **once in `detect.sh`** as
`OSR_ETC_DEFAULT`; modules write to `"$OSR_ETC_DEFAULT/foo"`, never a literal.

- **Prefer a drop-in file over editing the package's own config** — extends §5's "write only what it owns" from `$HOME` to system config.
- The rare per-package *and* per-distro oddball gets an inline `case "$OSR_DISTRO"` in that one module. Path divergence is rare, so inline is correct.

---

## 6. Rice switching — additive for packages, replace for owned config

```sh
osr switch <rice>:
  install manifest(rice)        # install missing pkgs; NEVER uninstall
  swap 90-theme.* layers        # replace rice-owned shell theme
  relink config/{hypr,waybar,...} # replace rice-owned DE config (their 90-* only)
  set wallpaper
```

Untouched: `00-env`, `10-aliases`, `20-func`, `99-local`, and every installed
toolchain. Old packages linger — disk cruft is the accepted cost.

### 6a. Themes are objects, not a folder inside a rice

A switch is still a full manifest run: package managers, source builds,
services, network. Minutes, and a sudo ticket. Nobody binds that to a key, so
a rice got chosen once and never changed — the opposite of having six.

| thing | is | lives in |
| --- | --- | --- |
| **rice** | a set of PACKAGES | `rices/<name>/rice.list` |
| **theme** | a set of APPEARANCE LAYERS | `themes/<name>/` |

```
themes/<name>/
  theme.list      metadata + palette, same `key: value` shape as rice.list
  config/         the 90-* layers, one dir per app
  wallpapers/     0..n images
```

Any theme applies onto any rice. Layers for apps a rice never installed land
in `~/.config` and are simply never read — which is why this is safe.

```sh
osr theme <name>:
  run ONLY the modules carrying a theme layer, with every install/build/
  download/service verb neutralized (lib/apply.sh)
  apply the theme's whole-dir `config:` entries
  set the wallpaper (per-theme choice, remembered)
  reload the running apps (lib/reload.sh)
```

**The engine is the same modules, not a second copy of the mapping.** A module
already knows where gruvbox's `config/dunst/90-theme.conf` belongs; a
declarative theme manifest would duplicate that and then drift. So a theme
apply sources the same module files with `pkg_install`, `enable_service`,
`provide_*`, `osr_install_nerd_font` and friends replaced by no-ops. The
neutralized set is **derived from the libs** rather than listed, so a provider
added to `lib/build.sh` tomorrow is inert here the day it is written.

Three properties make it hotkey-safe:

1. **No package manager, no network, no sudo prompt.** `as_root` degrades to a no-op unless a ticket already exists — a key press has no terminal to type a password into.
2. **Narrowed by the installed rice.** `~/.config/osr/state` records which manifest was installed, so a theme apply runs that rice's ~20 modules, not all 39 that could paint something.
3. **Never fatal.** A failing module costs its own layer and a warning; the desktop is not left half-painted.

`rice.list` gains `theme:` (installed with the rice) and `themes:` (the set
the picker offers). Both are advisory.

### 6b. A theme is a palette, not a directory of app configs

Owning themes used to be quadratic: every theme kept a full config file per
app, so a seventh theme meant seven more files and an eighth app meant editing
six themes. Six themes × seven shared apps was 31 files saying the same thing
in six palettes — and they drifted.

```
<app>/<file>.tmpl           ONE template per app, beside that app's dotfiles
themes/<name>/theme.list    the palette that fills it in
```

A template is the app's real config with `{{role}}` where a color goes.
`render_theme_template` (`lib/config.sh`) substitutes every `color:` role,
every single-valued meta field, and `{{THEME}}`. The whole engine is one
generated sed script (`_osr_theme_sed`) — no template language, same reason
`theme.list` is not TOML.

**The palette vocabulary** (spelled out, because it is the API a template is
written against):

| group | roles |
| --- | --- |
| the window | `background` `foreground` `cursor` `selection_background` `selection_foreground` `background_opacity` `background_blur` |
| the 16 ANSI slots | `ansi_black` .. `ansi_white`, `ansi_bright_black` .. `ansi_bright_white` |
| TUI chrome | `text_primary` `text_metadata` `text_muted` `panel_background` `border` `highlight` `accent_red` .. `accent_cyan` `box_cpu` `box_memory` `box_network` `box_process` `gradient_mid` `gradient_peak` |
| semantic | `surface` `text_dim` `accent` `success` `error` `warning` `prompt_secondary` |

`background_opacity` and `background_blur` are palette values for the same
reason the colors are: how translucent a terminal is, is part of how the theme
looks. They used to sit in each terminal's base config, where four terminals
had drifted to four different numbers (ghostty 0.85, wezterm 0.9, foot 0.7,
alacritty 0.7). Now glass is 0.7 everywhere because glass says so.

**Three spellings per color**, because the configs os-rice owns write a color
three ways:

| form | shape | consumers |
| --- | --- | --- |
| `{{role}}` | `#rrggbb` | GTK, Xresources, most TUIs |
| `{{role_rgb}}` | bare `rrggbb` | foot |
| `{{role_dec}}` | `r,g,b` | KDE color schemes, Konsole, CSS `rgba()` |

Non-color fields substitute the same way: `gtk_theme`, `icon_theme`,
`cursor_theme`, `cursor_size`, `ui_font`, `mono_font`, `gnome_accent`. That
collapsed five files repeating the same six toolkit names (`gtk-2.0/gtkrc`,
GTK3 and GTK4 `settings.ini`, `xsettingsd.conf`, the cursor `index.theme`)
into one template each. `modules/theming.sh` no longer parses those names back
out of the file it just wrote to feed `gsettings`; it reads `theme.list`.

The chrome group is why this is a vocabulary and not just a color list. A
full-screen app paints furniture the ANSI slots have no name for, and *which*
intensity it accents with is a design choice: most themes accent with the
bright set, rosemary accents with the normal one because it is deliberately
muted. `accent_green` lets the theme answer.

> [!tip] Escape hatch
> A theme can still ship a literal file, and it wins. `osr_theme_source <app> <name>` returns `themes/<t>/config/<app>/<name>` when it exists and a rendered template otherwise. That is what made the migration incremental rather than a flag day. None of the shared apps need it today.

Where a theme looked bespoke, the honest fix was a role: glass's blur became `background_blur`, rosemary's muted look became the `accent_*` group.

yazi was the clearest case. A flavor there is a **directory** — `flavor.toml`
plus the `tmtheme.xml` colouring file previews — so the tree carried five
vendored flavors, ~4400 lines, and only four themes were painted. Both files
are palette maps with a 600-line icon table attached, so both are templates
now: every theme gets a flavor named after itself. The `yazi_flavor` field
retired with them — the flavor is `{{THEME}}`, so selector and flavor cannot
disagree.

A theme's `config/` now holds only what is genuinely single-theme: glass's
Hyprland/waybar/sddm tree, rosemary's i3/polybar/GTK tree. No other theme has a copy to drift from.

Modules go through `install_theme_layer <app> <name> <dst>`, which returns
non-zero when the theme has neither a literal nor a template — the module's
cue to fall back to its dotfiles default. Because the helpers no longer name
`$OSR_THEME_DIR` directly, `osr_theme_modules` greps for them too
(`OSR_THEME_MARKERS`) when deciding which modules carry a theme layer.

The GUI half is **[[proteus/README|Proteus]]**, a standalone Rust crate. It
reads `theme.list` directly and shells out to `osr theme`, so neither half
depends on the other's internals.

---

## 7. Manifest format

```
# rices/arch-hyprland-glass/rice.list
require: arch:x86_64
base
zsh
hyprland
waybar
firefox
config: hypr waybar foot      # copy these config dirs from the rice's config/
```

Plain list, `#` comments, `while read`. Adding an app = one line. New rice =
new list, reuse every module.

---

## 8. Init and privilege

### Universal service control

`lib/service.sh` gives two idempotent verbs. No module calls `systemctl`
directly.

```sh
enable_service NetworkManager     # enable + start now, idempotent
disable_service cups              # stop + disable, idempotent
```

Dispatch on `OSR_INIT`, checking current state before acting:

| init | enable | disable |
| --- | --- | --- |
| systemd | `systemctl enable --now` | `systemctl disable --now` |
| OpenRC (Alpine) | `rc-update add … default` + `rc-service … start` | `rc-update del …` + stop |
| runit (Void) | `ln -s /etc/sv/… /var/service/` | `rm /var/service/…` |
| sysvinit | `update-rc.d … enable` + `service … start` | `update-rc.d … disable` |

Service *names* differ per init (`NetworkManager` vs `networkmanager`) → a
`servicemap` echoing `pkgmap`, rows only where names actually differ.

### Target-user model: root-for-root or user-for-user

`OSR_USER` is the account being riced.

| mode | behavior |
| --- | --- |
| **root-for-root** | run as root, `OSR_USER=root`, `HOME=/root`, no privilege drop |
| **user-for-user** | user-space work (cargo, oh-my-zsh, dotfiles, `chsh`, flatpak-user) runs as that user; only the native package step escalates |

Resolution order: `--user <name>` > `$SUDO_USER` > current `$USER` > `root`.

> [!important] The key inversion
> **Root is the exception, not the default.** Most providers need no root at
> all; `as_user` is the default wrapper and escalation is opt-in per step.

```sh
as_user() { [ "$(id -un)" = "$OSR_USER" ] && "$@" || sudo -u "$OSR_USER" "$@"; }
```

Modules call `as_user cargo install …` — never a hand-rolled `sudo -u` plus a
`chown -R` afterthought, which is where the historical drift bug came from.

---

## 9. Testing — containers plus QEMU, no real machine

| what it exercises | how |
| --- | --- |
| install logic, `pkgmap`, idempotency, POSIX-sh under `dash`/`ash`, 5-distro matrix | podman/docker (`archlinux`, `debian:stable-slim`, `alpine`, `fedora`, `void`) |
| user-for-user mode | rootless podman — a non-root uid surfaces bad root assumptions for free |
| `enable_service` dispatch | PATH-mocked `systemctl`/`rc-update`/`sv` that log their args |
| full DE / hyprland / sddm / GPU / wallpaper | QEMU VM — needs a real kernel, display, GPU |

- **The matrix is the idempotency test.** `podman run --rm -v "$PWD":/os-rice img sh -c 'install.sh <rice> && install.sh <rice>'` — the double run **is** §2's acceptance.
- **POSIX lint in CI:** every `lib/` + `modules/` file through `dash -n` and `shellcheck -s sh`.
- **QEMU is manual/nightly**, never per-commit: boot an Arch VM, run the hyprland rice, screenshot.

**Podman-first** (rootless matches user-mode, no daemon), docker as fallback.

---

## 10. Rice preconditions — declare, fail before mutation

### Tier 1 — `require:` (cheap, declarative, pre-mutation)

Parsed by the runner and checked before step 1, exiting non-zero **before
anything is written**.

```
require: arch:x86_64          # fail on arm instead of 404ing a tarball
require: init:systemd
require: gpu:present
```

| predicate | check |
| --- | --- |
| `arch:<m>` | `uname -m` (or `OSR_ARCH_DEB`) |
| `init:<i>` | `OSR_INIT` |
| `distro:<d>` / `release:<c>` | `OSR_DISTRO` / `OSR_CODENAME` |
| `cmd:<bin>` | `command -v` |
| `gpu:present` | `/dev/dri/renderD*` or a GPU in `lspci` |

The value half may list alternatives with `|` —
`require: distro:void|debian|ubuntu` — and holds when any branch does. Two
`require:` lines already mean AND, so `|` is the only combinator needed.

`lib/preflight.sh` dispatches each. Preflight runs on `osr switch` too — you
cannot switch into a rice the hardware cannot run.

### Tier 2 — functional capability probe, as an early module

"GPU present" is cheap; "Vulkan actually initializes" is not — the tool that
proves it is itself a package you have not installed, and a true probe
**touches the system**, colliding with "fail before mutation". So it is an
early module (`modules/gpu-vulkan.sh`), not a predicate:

```
detect GPU  ->  install minimal driver + loader + prober (mesa, vulkan-loader, vulkan-tools)
            ->  headless probe: vulkaninfo --summary (or vkcube --c 1)
            ->  parse for a REAL hardware device, reject software fallback (llvmpipe/lavapipe)
            ->  initializes: proceed | software-only or error: hard-fail with the driver diagnostic
```

It stays honest: the probe installs only its **own** prerequisites, never
rice-specific packages. `require: gpu:vulkan` is sugar meaning "run the
`gpu-vulkan` probe early and hard".

---

## 11. Two module tiers — and only one is the target

`install.sh` asks `osr module has <name>` and runs whichever exists, so a
`rice.list` never says which tier a module belongs to. That is what makes the
migration incremental rather than a rewrite. The C tier is the target; see
[[#D-4. A module may be a C unit instead of a script]] for what it buys.

### 11a. Every `.sh` module is legacy

All 115 shell modules carry a marker beside `# session:` and `# themable:`:

```sh
# session: x11
# themable: yes
# legacy: sh  -- port to C (modules/<name>.c + lib/modules.c); see DESIGN 11a
```

Enforced by `test/lint.sh`, for the same reason `# session:` is: a marker only
usually present answers no question. `grep -c '^# legacy:' modules/*.sh` is
the remaining-work count, and it only goes down.

> [!note] "Legacy" means "should be C", not "is broken"
> A legacy module is fully supported and a rice may depend on it. What it may
> not be is the pattern a **new** module copies.

Ports happen when a module is being touched anyway — that is how `flameshot`,
`docker`, `fastfetch` and `tcc` moved. One blocker is worth knowing up front:
`osr_pkg_install` implements the **native** path only, so a module whose
packages resolve to `cargo:`/`source:`/`script:`/`aur:` must stay shell until
those providers are ported. It fails loudly rather than installing wrongly.
`modules/i3lock.sh` (`cargo:xidlehook`, `source:provide_betterlockscreen`) is
the canonical example.

A brand-new module goes straight to C. `modules/helpers.c` is the first that
never had a `.sh` form and therefore has no `test/ref` twin; its scenario in
`module_c_parity.sh` asserts behaviour directly.

---

## 12. Hardware commands — `undervolt` and `benchmark`

Two commands that are not ricing at all. They live in the same binary because
it is already the thing that knows the machine.

### `osr undervolt cpu <verb>`

CPU voltage offsets, by hand or found automatically. The loop it exists to
automate:

```
undervolt -> stress -> crashed/errored ? back off : record, go deeper
          -> repeat -> long soak at the safe value
```

By hand that is an evening per machine, and it leaves no record of where you
were when the box locked up. The pieces exist (intel-undervolt, ryzen_smu,
stress-ng); what does not is anything that runs the loop, decides what
"stable" means, and **survives the machine dying mid-test**.

| verb | state |
| --- | --- |
| `probe` | implemented — what this machine exposes; **never writes anything** |
| `status` `set` `reset` | not yet |
| `test` | not yet (needs `lib/uv/stress.c`) |
| `auto` `resume` | not yet (needs `lib/uv/search.c`) |
| `apply` `enable-boot` `disable-boot` | not yet |

> [!tip] Start with `probe`
> On most machines the answer is that firmware has voltage control locked,
> and that is worth knowing before anything else. `probe` works on a machine
> with no voltage control at all, and its answer decides whether the rest of
> the command has anything to do.

| unit | responsibility | state |
| --- | --- | --- |
| `lib/uv/backend.h` | four verbs per vendor — all hardware knowledge behind it | done |
| `lib/uv/generic_opp.c` | the only backend today; last in the table and always claims | done |
| `lib/uv/journal.c` | crash-safe record of what was applied when | done |
| `lib/uv/stress.c` | the tiered validator | planned |
| `lib/uv/search.c` | vendor-neutral descent: coarse step, back off, refine, subtract a margin, soak | planned |
| `lib/undervolt.c` | argument parsing and reports. No hardware. | done |

Vendor backends (`amd_smu`, `intel_msr`, `arm_dt`) are what `backend.h` exists
for; none is written yet, and `uv_detect` walks the table most-specific-first,
so adding one is a row, not a refactor.

> [!important] The journal is load-bearing, not bookkeeping
> An undervolt one step too far does not return an error — it hard-locks the
> box between one instruction and the next, and the next thing that runs is
> the BIOS. So the search's memory cannot live in the search's process.
>
> ```
> append TRY <what we are about to apply>   <- fsync, file AND directory
> apply it
> run the test
> append OK (or FAIL) <the same thing>      <- fsync
> ```
>
> A `TRY` with no verdict after it is a machine that died mid-test.
> Distinguishing "died" from "still running" is what the boot id is for: a
> dangling `TRY` from a **previous** boot is a crash, one from **this** boot
> is an interrupted run (^C, killed). Those need opposite responses — back off
> versus carry on — and nothing else can tell them apart.
>
> The directory fsync matters for the same reason: a freshly created journal
> whose directory entry never reached the platter reads as "no journal at
> all" after the power cycle, which is exactly the case being survived.

Analysis is a pure function over a record array (`uv_journal_analyze`),
separate from file I/O.

### `osr benchmark cpu` / `osr benchmark sensors`

Throughput, power, thermals, clocks — `lib/benchmark.c` over
`lib/bench/{cpu,power,util}.c`. Its own command rather than a private corner
of the undervolt code, because "what is this machine actually doing" is worth
asking on its own, and the undervolt perf gate then consumes these numbers
instead of growing a second, subtly different measurement.

Flags: `--seconds`, `--json`, `--verbose`, `--save <file>`, `--compare <file>`,
`--install-deps`.

**The headline number for undervolting is ops per watt, not ops/s.**
Throughput barely moves when an undervolt works; what changes is the power
needed to reach it, and therefore how long the part holds boost.

Power has no portable interface on Linux, so it is layered exactly like
`lib/uv/backend.h`: try each source, take the first that answers, and if none
does, **say so rather than invent a number**.

| source | kind | caveats |
| --- | --- | --- |
| RAPL | energy counter (uJ) | accurate, cannot miss a spike between samples; wraps, and root-only on most kernels since PLATYPUS |
| hwmon | instantaneous (uW) | must be sampled and averaged; whatever happens between samples is lost |
| SMU | package power from the Ryzen SMU PM table | best on Granite Ridge |
| battery | discharge rate | laptops only, and only on battery |

That difference is why the workload runs as a child process while the parent
polls — an instantaneous source needs sampling throughout, and the polling
loop is also where peak temperature and peak clock come from.

> [!warning] Reachability
> `benchmark` is a core command (`build/osr benchmark cpu`). The `./osr` front
> end does not list it as a verb; `undervolt` it does.

---

## 13. The port, and what is left of it

The target is one binary. Nothing about the design changes to get there — the
libs keep their responsibilities, the manifests keep their format, the module
API is `lib/module.h`. What changes is the language the backbone is written in.

### Done

| unit | replaced | note |
| --- | --- | --- |
| `lib/ui.c` `log.c` `user.c` `detect.c` `theme.c` | `lib/*.sh` | the `.sh` file survives as a shim only (see [[#D-3. The C harness\|D-3]]) |
| `lib/state.c` | `lib/state.sh` | file removed outright |
| `lib/install.c` | `install.sh`'s option loop, help, listings, manifest, report | `install.sh` remains only because it **sources** modules |
| `lib/testrun.c` | `test/run.sh` | removed outright |
| `lib/render.c` | the `{{role}}` sed script | shared with `theme.c` |
| `lib/manifest.c` `theme_list.c` `theme_render.c` `config_copy.c` `net.c` | — | written for the Windows core, already portable |
| `lib/fetch.c` `git.c` | `lib/net.sh` `git.sh` | the shell tier's downloader and the git/oh-my-zsh helpers; `lib/net.c` stays the landing spot for a future native fetch backend |
| `lib/service.c` | `lib/service.sh` | both verbs on all four inits, and the servicemap facet lookup they share |
| `lib/preflight.c` `nerdfont.c` | `lib/preflight.sh` `fonts.sh` | the require: predicates and the Nerd Font install; `nerdfont.c` is so named because `lib/fonts.c` is the Windows core's |
| `lib/gnome.c` `migrate.c` | `lib/gnome.sh` `migrate.sh` | GNOME session detection, freeing a chord off the Shell keys and registering a custom keybinding; and the migrations that patch a seeded, user-owned layer in place. `migrate_replace` takes the old and the new region as text rather than the names of two functions that print them — the fork per region bought a C caller nothing |

### Remaining, in the order the dependencies force

| what | lines of sh | why it is where it is in the queue |
| --- | --- | --- |
| `lib/pkg.sh` | 551 | **the blocker.** `osr_pkg_install` covers the native path only; every provider (`cargo:` `script:` `aur:` `source:`) still lands back in sh, and that is what pins most modules to the shell tier |
| `lib/build.sh` | 1387 | the `source:` builders, **in progress**: `lib/build.c` holds the registry, the tarball primitive and the seven prebuilt-binary builders (`gh` `btop` `lsd` `fzf` `fastfetch` ×2, `lsd_deb`); a name it does not know still runs in sh, one row at a time. The Windows half of this shape already exists as `provide/<name>.c` + `provide_module.c`, so the pattern is settled — this is volume, not design |
| `lib/config.sh` | 576 | layering, templates, `ensure_block`, the Mozilla/JSON composers. **In progress**: `lib/config.c` holds the seeded layers, the owned blocks (one composition, shared with `user.c`), the JSON/starship composers, the foot and Alacritty version adapters, `apply_config`, the Mozilla layer and the whole wallpaper family (resolve, install, record, set, library, pick) |
| `lib/apply.sh` `reload.sh` | 294 | theme-only apply and the reload table. Depend on the module tier's shape, so late |
| `install.sh` `wallpaper.sh` `osr` | 458 | the runner and the front end. Last: `install.sh` cannot stop being sh while it sources shell modules |
| `modules/*.sh` | 115 files | see [[#11a. Every `.sh` module is legacy\|11a]] |

> [!note] `bootstrap.sh` is not on this list
> It runs before a compiler is a given, and stays sh forever.

### The rule the port runs under

> Byte-for-byte identical output, asserted, or it is not a port.

Every unit's shell original is frozen under `test/ref/` and diffed by
`test/unit/*_c_parity.sh` — 317 checks over the eight done so far, and exactly
one accepted divergence, asserted rather than hidden. A port that cannot be
diffed this way (`helpers.c`, which never had a `.sh` form) asserts its
behaviour directly instead.

`grep -c '^# legacy:' modules/*.sh` and the table above are the remaining-work
count. Both only go down.

---

## Not doing (and why)

| not doing | why |
| --- | --- |
| C rewrite for speed | wall-clock is `apt`/`curl`/network, not shell |
| byte-level per-package progress bars | package managers will not feed them; step progress delivers the same for a fraction of the code |
| TOML/YAML manifests | needs a parser, un-POSIX; a newline list with `#` is more readable *and* free to parse |
| merging Windows in | different package model and language; a shared abstraction is negative value |
| per-commit full-VM (QEMU) CI | too slow for the inner loop; containers carry the per-commit matrix |
| a plugin/hook framework | YAGNI. `use module` = source a file. Add structure when a second contributor needs it |
| auto-resolved provider dependency DAG | manifest order is the graph. A DAG is the plugin framework by another name |
| package removal on rice switch | additive-only; removal is un-idempotent and risky. An opt-in `osr prune` may come later |
| provider fallback chains (native → cargo → source) | non-deterministic and hard to make idempotent. Explicit per-distro tags are predictable |
| pinning `curl \| sh` installers | accept upstream drift for the convenience |

---

## Open questions

- Is a two-digit numeric prefix (`00/10/20/90/99`) enough ordering headroom for every config, or will some need sub-layers?
- G1 (the `repo:` / `tarball:` / `brew:` / `flatpak:` providers) is still open. Which, if any, is worth adding before the C tier can consume providers at all?
