# os-rice — Design

A DRY, declarative, POSIX-portable installer for custom unix-like apps, configs,
and whole rices.

## Problem Statement

How might I describe a whole rice (apps + modules + configs) as one readable
list, and install it re-runnably on any unix-like distro, without pasting the
same module three times per package manager?

---

## Current state (what triggered the redesign)

The module system is a good idea — `install-module.sh foo,bar` sourcing
`modules/foo.sh` is simple and works. Logging / `trace` / `check_error` are
solid. Per-rice folders as self-contained bundles is the right mental model.

The problem is it's **copy-paste, not DRY**:

- `install-module.sh` is **byte-identical** across debian/rhel/arch except one
  line (`apt update` vs `pacman -Sy`) and which `detect-*.sh` it sources.
- `common.sh` is duplicated per-distro — a ~600-line grab-bag mixing apt repo
  helpers, brew, cargo, and git-clone logic in one file.
- `zsh.sh` is **~95% identical** across all three distros. The only real
  difference is the package line: `install_pkg_apt` / `install_pkg_dnf` /
  `pacman -S`. Everything else (oh-my-zsh, plugins, config copy, chsh) is pasted
  three times.
- `install_or_update_zsh_plugin` is defined *inside* the zsh module and
  duplicated in all 3 copies. It belongs in a shared lib.
- Copy-paste drift already produced bugs: the arch `zsh.sh` has
  `trace "git -C ..."` (quotes the whole command as one arg — broken) and
  silently dropped all its `check_error` calls.

**Core insight:** the only thing that actually varies per-distro in most modules
is the package manager. Abstract it behind one interface, detect the distro once,
and write each module **once**.

---

## Target layout

```
os-rice/
  lib/                 # single copy (was per-distro common.sh), POSIX sh
    log.sh             # info / warn / error / check_error
    ui.sh              # colors, run_step, _spin, step counter, tty detection
    pkg.sh             # pkg_install / pkg_installed / pkg_refresh / pkg_add_repo / pkg_remove
    detect.sh          # sets OSR_DISTRO, OSR_PKG, OSR_INIT, OSR_GPU, OSR_VIRT, OSR_USER once
    service.sh         # enable_service / disable_service, dispatch on OSR_INIT (§8)
    git.sh             # install_or_update_git_repo, zsh-plugin helper, etc.
    net.sh             # download, github_latest version resolution (§7 G4)
    user.sh            # OSR_USER resolution, as_user, ensure_line, ensure_block, backup_copy
    pkgmap/
      apt.map          # logical package name -> real package(s), one-to-many
      dnf.map
      pacman.map
      apk.map
      xbps.map
    servicemap         # logical service name -> real unit(s), only where they differ (§8)
  modules/             # ONE copy each, POSIX sh, distro-agnostic
    zsh.sh             # calls pkg_install, not install_pkg_apt
    ...
  rices/
    arch-hyprland-glass/
      rice.list        # the readable "what to install" description
      config/          # this rice's own configs
      wallpapers/
  install.sh           # single shared runner: install.sh <rice>
```

Distro variance lives in exactly one place (`pkg.sh` + `pkgmap/` + `detect.sh`),
not smeared across every module. New distro = teach `pkg.sh` its verbs and add a
`pkgmap`; most modules light up for free.

> Windows (`windows-11-x86_64`) stays its own PowerShell world — different
> package model, different language. Do **not** force it into this abstraction.

---

## Decisions

### Shell target: POSIX sh everywhere

Runs on Alpine / busybox `ash` out of the box, no bash required — the honest
answer for "max compatible with barebone systems." Cost: rewrite bash-isms
(`[[ ]]` → `[ ]`, arrays → space-lists or `while read`). Validate every module
under `dash` / busybox `ash`, not just bash.

### No compiled C binary (for now)

The C idea was about bootstrap primitives ("compile a helper binary to not
download some tools"), not speed. But:

- A barebone box that lacks `curl` almost always has `wget` or busybox —
  `bootstrap.sh` should detect whatever's present.
- If a primitive is *truly* missing everywhere, the answer is **static busybox**
  (one battle-tested binary giving sh/wget/tar/sha256), not hand-rolled C.
- Installer wall-clock is `apt`/`curl`/network, not shell parsing — C saves
  nothing measurable and breaks self-bootstrap (needs a toolchain on target).

**Trigger to revisit:** a concrete bootstrap need that busybox genuinely can't
fill. Until then, custom C never enters.

### Rices are declarative manifests, not folders of scripts

The headline lever. A rice = a plain list of modules/apps + configs to copy,
with `#` comments, parsed with `while read` (no TOML/YAML parser — un-POSIX,
un-lazy). The module count in the list *is* the progress-bar denominator.

### A package has a *method*, not just a *name*

`pkgmap` name→name(s) only covers the case where the same package manager has a
different *name* per distro. It has no answer for the case where the *install
method itself* varies: AUR-build on Arch, `apt` on Debian, `cargo` where no
package exists, `curl | sh` for starship, a from-source build for paru/amnezia.
The repo already proves this — `vscode-insiders.sh` shells out to `$AUR_HELPER`,
and `build-paru.sh` / `build-amneziavpn-client.sh` are standalone.

So a `pkgmap` row's RHS may carry an optional **provider tag**. No tag = native
package manager (the common, zero-effort case is untouched). The resolver
expands names, **groups by method, and dispatches each group** — native rows
still batch into one install call. Each provider owns its own idempotency probe.

### Config is layered by ownership, not one dotfile

A dotfile is not one thing. `.zshrc` today jams together PATH/toolchain env
(machine-specific), aliases and functions (personal, rice-independent), and
theme/prompt (rice-specific) in one blob — so `cp -f .zshrc` destroys any env
the user added on that machine. The same wound exists in every monolithic DE
config (`hyprland.conf`, `waybar`, `foot`).

Split **every** config along ownership layers with distinct lifecycles, and let
os-rice write **only what it owns**:

| layer      | owner          | overwrite policy                 | rice-scoped? |
|------------|----------------|----------------------------------|--------------|
| `00-env`   | user / machine | seeded once if absent, then kept | no           |
| `10-*`     | dotfiles repo  | overwrite on update              | no           |
| `20-*`     | dotfiles repo  | overwrite on update              | no           |
| `90-theme` | rice           | **swapped** on rice switch       | **yes**      |
| `99-local` | machine        | gitignored, never touched        | no           |

This is what makes rice-switching non-destructive: the user's env and aliases
are structurally out of os-rice's reach.

---

## 1. Package abstraction + one-to-many table

One map file per package manager, logical name → real package(s). No entry =
pass the name through unchanged (the common case stays zero-effort). Only
packages that *actually differ* need a row — don't pre-fill identity mappings.

```
# lib/pkgmap/apt.map          # lib/pkgmap/dnf.map
zsh = zsh                     zsh = zsh
neovim = neovim               neovim = neovim
build = build-essential       build = gcc gcc-c++ make      # <- one-to-many
dev-headers = libssl-dev      dev-headers = openssl-devel pkgconf
```

```sh
# lib/pkg.sh
pkg_install() {                     # pkg_install build zsh
  set -- $(_pkgmap "$@")            # expand logical -> real, one-to-many
  case "$OSR_PKG" in
    apt)    apt-get install -y "$@" ;;
    dnf)    dnf install -y "$@" ;;
    pacman) pacman -S --needed --noconfirm "$@" ;;
    apk)    apk add "$@" ;;
    xbps)   xbps-install -y "$@" ;;
  esac
}

_pkgmap() {                         # POSIX: while/for + grep, no assoc arrays
  for name; do
    line=$(grep "^$name[[:space:]]*=" "$OSR_LIB/pkgmap/$OSR_PKG.map" 2>/dev/null)
    if [ -n "$line" ]; then
      printf '%s ' "${line#*= }"    # RHS may be several packages
    else
      printf '%s ' "$name"          # no mapping -> use name as-is
    fi
  done
}
```

Five verbs cover ~everything: `pkg_install`, `pkg_installed`, `pkg_refresh`,
`pkg_add_repo`, `pkg_remove`. Modules say `pkg_install build`; the table absorbs
every distro's splitting.

> Open question: package-*name* differences — central `pkgmap` table (chosen)
> vs inline `case` per module. Central table wins: one place, only for packages
> that differ.

---

## 2. Re-runnable / idempotent — a hard contract

**Rule: a module may be run 100× and converge, never error on the 2nd run.**

Idempotency comes from guard helpers every module uses instead of raw mutation:

```sh
pkg_installed zsh || pkg_install zsh              # skip if present (dnf/apt need this; pacman --needed is built-in)
ensure_line "$rc" 'eval "$(starship init sh)"'    # grep -q before append — safe on rerun
backup_copy "$src" "$dst"                          # copies to .bak once, then overwrites; rerun-safe

# only chsh if not already the login shell
[ "$(getent passwd "$u" | cut -d: -f7)" = "$(command -v zsh)" ] \
  || chsh -s "$(command -v zsh)" "$u"
```

The existing "already installed, skipping" checks already lean this way — this
makes it law and gives it a toolbox.

---

## 3. Fancy logs — spinners + step progress, no fake byte bars

Colors and spinners are achievable in POSIX. A real **byte-level progress bar is
not** — `apt`/`pacman` own their stdout and don't report parseable progress.
Fighting that is a time sink for a jittery result. So:

- **Step progress** (honest, easy, looks pro): `[03/12] zsh ✔` — module count is
  known from the manifest. This is the progress bar that actually works.
- **Spinner** wraps long *silent* steps (clone, download, build): run in the
  background, capture output to a per-run logfile, animate `⠋⠙⠹…` on `\r`, print
  `✔`/`�’` on completion. On failure, dump the log tail.
- **Auto-degrade:** everything keys off `[ -t 1 ]`. TTY → spinners + hidden
  output. Piped / CI / `--verbose` → plain streamed lines, no escape junk in
  logs. This is what makes it both fancy *and* re-runnable-into-a-logfile.

```sh
# lib/ui.sh
run_step() {                         # run_step "Cloning oh-my-zsh" git clone ...
  desc=$1; shift
  if [ -t 1 ] && [ -z "$OSR_VERBOSE" ]; then
    ( "$@" ) >>"$OSR_LOG" 2>&1 & pid=$!
    _spin "$pid" "$desc"
    wait "$pid" || { tail -n 20 "$OSR_LOG"; error "$desc failed"; }
    printf '\r%b✔%b %s\n' "$GREEN" "$NC" "$desc"
  else
    info "$desc"; "$@" || error "$desc failed"   # non-tty: current trace behavior
  fi
}
```

`run_step` replaces `trace` at call sites — same ergonomics, gains the spinner.
`trace` becomes the non-tty branch.

> Honest caveat: spinner + hidden output means a hang shows a spinner, not the
> tool's live output. Mitigate with `--verbose` and the on-failure log dump —
> the standard tradeoff (what `brew`, `paru`, etc. do).

---

## 4. Provider-tagged install methods

A `pkgmap` row's RHS gets an optional `method:` prefix. No prefix = native
package manager. This keeps the common case zero-effort and only tags the rows
that genuinely install a different way.

```
# any.map (shared) / per-distro maps
zsh      = zsh                                    # native (default)
build    = gcc gcc-c++ make                       # native, one-to-many
starship = script:https://starship.rs/install.sh  # curl | sh
ripgrep  = cargo:ripgrep                           # cargo where no pkg exists
vscode   = aur:visual-studio-code-insiders-bin     # arch.map only
paru     = source:build-paru                       # from-source build fn
```

`pkg_install` stops being "expand names into one `case`". It becomes
**expand → group by method → dispatch each group**; native rows still batch
into a single `apt-get install` / `pacman -S`:

```sh
pkg_install() {                    # pkg_install zsh starship paru
  native="" ; for spec in $(_pkgmap "$@"); do
    case "$spec" in
      cargo:*)  _via_cargo  "${spec#cargo:}"  ;;
      script:*) _via_script "${spec#script:}" ;;
      aur:*)    _via_aur    "${spec#aur:}"    ;;
      source:*) _via_source "${spec#source:}" ;;
      *)        native="$native $spec"        ;;   # collect, batch below
    esac
  done
  [ -n "$native" ] && _via_native $native
}
```

**Each provider owns its idempotency probe** — the tag drives the skip check,
not just the install, or the re-run contract (§2) breaks:

| method   | install            | `pkg_installed` probe                |
|----------|--------------------|--------------------------------------|
| native   | apt / dnf / pacman | `dpkg -s` / `rpm -q` / `pacman -Q`   |
| cargo    | `cargo install`    | `cargo install --list \| grep -q`    |
| script   | `curl … \| sh`     | `command -v <bin>`                   |
| aur      | `$AUR_HELPER -S`   | `pacman -Q`                          |
| source   | build fn in `lib/` | `command -v <bin>` \|\| marker file  |

**Prerequisites are ordinary manifest lines, in order** — `rust` before any
`cargo:` row, `paru` before any `aur:` row. Manifest order *is* the dependency
graph. No auto-resolved DAG (that's the plugin framework already in Not Doing).

> Decided: `script:` (curl | sh) installers are **not** pinned to a checksum —
> accept upstream drift for the convenience. Revisit only if a drifting
> installer actually burns a run.

---

## 5. Config layering — every config, not just zsh

The ownership table (see Decisions) is realized as a **drop-in dir sourced in
lexical order**, plus **managed marker blocks** wherever a single file is
unavoidable. This applies to *all* configs now, not just the shell — DE configs
get the same split via their native include mechanisms.

```
~/.config/osr/zsh/rc.d/
  00-env.zsh      user / per-machine   seeded once if absent, then never touched
  10-aliases.zsh  dotfiles             overwrite on update, rice-independent
  20-func.zsh     dotfiles             overwrite on update
  90-theme.zsh    rice-owned           swapped on rice switch (prompt/omz theme)
  99-local.zsh    per-machine          gitignored, never touched
```

The shipped `~/.zshrc` is a **thin loader** that sources `rc.d/*.zsh`. Where the
target file must stay singular (the user already has their own `~/.zshrc`), inject
only a marked block and rewrite **only** between the markers:

```sh
# >>> os-rice:loader >>>   (managed — edits between markers are overwritten)
for f in "$HOME"/.config/osr/zsh/rc.d/*.zsh; do . "$f"; done
# <<< os-rice:loader <<<
```

`ensure_block` (sibling to `ensure_line`) owns the marked region. The same
pattern layers DE configs through their own includes:

```
config/hypr/hyprland.conf     source = ./00-env.conf    # user monitors/input
                              source = ./90-theme.conf   # rice: colors, decoration
config/waybar/                config.jsonc + 90-theme.css (rice-owned)
config/foot/                  foot.ini includes foot-colors.ini (rice-owned)
```

**Two seeding rules matter:**

- `00-env` / `99-local` are **seeded once if absent**, then never rewritten —
  os-rice reads them via the loader but treats them as user territory.
- Config idempotency: PATH/env mutation in `00-env` is **guard-style**, never a
  blind append (`case ":$PATH:" in *":$d:"*) ;; *) PATH="$d:$PATH" ;; esac`), so
  a re-run never duplicates entries.

---

## 6. Rice switching — additive for packages, replace for owned config

Moving A → B must be cheaper and safer than a reinstall. The asymmetry is the
whole trick: **packages accrete, rice-owned config layers get replaced.**

```sh
osr switch <rice>:
  install manifest(rice)        # install missing pkgs; NEVER uninstall
  swap 90-theme.* layers        # replace rice-owned shell theme
  relink config/{hypr,waybar,…} # replace rice-owned DE config (their 90-* only)
  set wallpaper
```

Untouched by a switch: `00-env`, `10-aliases`, `20-func`, `99-local`, and every
installed toolchain. Old rice's packages linger (disk cruft is the accepted
cost); an opt-in `osr prune <rice>` may come later but removal is out of scope.

---

## Manifest format

Plain list, `#` comments, parsed with `while read`:

```
# rices/arch-hyprland-glass/rice.list
base
zsh
hyprland
waybar
firefox
config: hypr waybar foot      # copy these config dirs from the rice's config/
```

`install.sh arch-hyprland-glass` reads the list, runs each module, copies each
config. Adding an app to a rice = add one line. New rice = new list, reuse every
module.

---

## 7. Observed install style + gaps (read from the existing rices)

A pass over the Linux rices (arch = 83 `.sh`, mature; debian/rhel = thin
skeletons) to pin the design to what actually exists, not what's imagined. A
fourth rice, `linux-debian-x86_64-kde-gruvbox`, was **deleted** — it was a dead
8k-file vendored-theme dump, and its wholesale-vendoring anti-pattern is exactly
what G5 below argues against.

### What the rices already do (ground truth to preserve)

- **Privilege model — `DELEVATED_USER`.** `sudo -v` once upfront + a keep-alive
  loop; run system installs as root but **drop to the target user**
  (`sudo -u "$DELEVATED_USER" …`) for user-space (cargo, oh-my-zsh, dotfiles),
  then `chown -R` back. Home resolved via `getent`, default `root`. This is in
  ~every module and *must* become a `lib/user.sh` primitive (`as_user`), not be
  hand-rolled — the dropped-`check_error` arch drift bug came from hand-rolling.
- **Idempotency is already the de-facto style** — "already installed, skipping"
  guards, git repos updated-or-cloned with remote-URL check + dirty-reset. §2
  is codifying existing behavior, not inventing it.
- **Native installs already respect user holds/pins** — `install_pkg_apt`
  filters `dpkg --get-selections | grep hold` **and** negative-priority apt
  preferences; `install_pkg_brew` honors `--pinned`; cargo reads
  `~/.cargo/ignore`. The naive `pkg_install` in §1 silently drops this.
- **Detectors already factored** — `detect-{cpu,gpu,virt,hwaccel,aur-helper}.sh`.
  `lib/detect.sh` must **absorb all five**, not just distro/pkg.

### The real provider palette (wider than §4's five tags)

- **native** + hold/pin filtering — *all rices* — ✅ (add pin-respect, see gaps)
- **`aur:`** (paru/yay via detect) — *arch apps, 15 files* — ✅ covered
- **`cargo:`** (`--locked`, run as user) — *debian/rhel starship, serie* — ✅ covered
- **`script:`** (curl \| sh, oh-my-zsh sed-patched) — *debian zsh* — ✅ covered
- **`brew:`** — *debian common.sh* — ➕ add tag
- **`repo:`** (add 3rd-party repo + GPG key, then native) — *debian gh/docker* — ➕ **missing**
- **`tarball:`** (fetch release binary → `/usr/local`) — *debian go, zig* — ➕ **missing**
- **`source:` with a bootstrapped build toolchain** — *debian ghostty (fetches matching zig first, then builds)* — ⚠️ §4's `source:` is too simple
- **`flatpak:`** — *arch flatpack, hyprcursor* — ➕ add tag

### Gaps the design must close

- **G1 — widen §4 providers:** add `repo:`, `tarball:`, `brew:`, `flatpak:`;
  let `source:` declare a build-toolchain prerequisite (ghostty→zig) as an
  ordinary earlier manifest line.
- **G2 — respect holds/pins in the idempotency contract (§2):** never reinstall
  or override a user-held/pinned/ignored package. This *is* "don't destroy
  user-defined state" applied to packages, not just config.
- **G3 — service management isn't POSIX/systemd-portable → resolved with
  `enable_service` / `disable_service`.** Every rice uses `systemctl enable
  --now` (NetworkManager, sshd, sddm, cups, smb, waydroid, vmtoolsd) — but the
  Alpine/busybox target runs **OpenRC/runit**, no `systemctl`. **Decided:** a
  universal `enable_service` / `disable_service` verb pair in `lib/service.sh`
  dispatches on the detected init, idempotently (see §8).
- **G4 — `github_latest` version resolution is duplicated** (go, zig, ghostty
  each re-query `api.github.com/.../tags`). One helper in `lib/net.sh`.
- **G5 — program-data vs config.** `.oh-my-zsh` was copied wholesale (640
  vendored files in the now-deleted kde rice) and *also* script-installed in
  debian — same tool, two methods, drift. It's an **installed program** (belongs
  to `script:`/`git:` install, one method), not config; only `.zshrc` +
  `starship.toml` are the config layer (§5). Split the two so a rice never
  vendors 640 files.

### The migration wound, concretely

Config today is `cp -f "$REPO/zsh/.zshrc" "$HOME/.zshrc"` + `chown -R` — the
exact blob-clobber §5 replaces. The `.zshrc` I profiled mixes toolchain PATHs
(cargo/go/brew/cuda/nvm), aliases, a `y()` yazi function, and starship/omz theme
in one file. That single file is the §5 split's first and best test case.

---

## 8. Init & privilege — universal services + target-user install

Two "who/what am I acting on" abstractions the modules currently hand-roll.

### Universal service control

`lib/service.sh` gives two idempotent verbs that work on any init — no module
ever calls `systemctl` directly again:

```sh
enable_service NetworkManager     # enable + start now, idempotent
disable_service cups              # stop + disable, idempotent
```

Dispatch on `OSR_INIT` (added to `detect.sh`); check current state before acting:

- **systemd** — enable: `systemctl enable --now` · disable: `systemctl disable --now`
- **OpenRC** (Alpine) — enable: `rc-update add … default` + `rc-service … start` · disable: `rc-update del …` + stop
- **runit** (Void) — enable: `ln -s /etc/sv/… /var/service/` · disable: `rm /var/service/…`
- **sysvinit** (fallback) — enable: `update-rc.d … enable` + `service … start` · disable: `update-rc.d … disable`

Service *names* differ per init (`NetworkManager` vs `networkmanager`) → a
`servicemap` echoing `pkgmap`, rows only for names that actually differ.

### Target-user model: root-for-root **or** user-for-user

Generalize today's `DELEVATED_USER` into **`OSR_USER` = the account being riced**,
with two symmetric modes:

- **root-for-root** — run as root, `OSR_USER=root`, `HOME=/root`; dotfiles/configs
  land in root's home; no privilege drop.
- **user-for-user** — `OSR_USER=<name>`; user-space work (cargo, oh-my-zsh,
  dotfiles, `chsh`, flatpak-user) runs **as that user**; only the native package
  step escalates to root.

Resolution order: `--user <name>` > `$SUDO_USER` (when invoked via sudo) >
current `$USER` > `root`. The key inversion for user-mode: **root is the
exception, not the default** — most providers (`cargo:`, `brew:`, `flatpak:`
user, `git:` clones, config layering) need no root at all; `as_user` is the
default wrapper and escalation is opt-in per step.

```sh
as_user() { [ "$(id -un)" = "$OSR_USER" ] && "$@" || sudo -u "$OSR_USER" "$@"; }
```

Modules call `as_user cargo install …`; they never hand-roll `sudo -u` + a
`chown -R` afterthought (the source of the arch drift bug).

---

## 9. Testing harness — no real machine (containers + QEMU)

Yes: **podman/docker cover ~everything except the GPU/DE**, QEMU covers the rest.
Split by what each layer can actually exercise:

- **install logic, `pkgmap`, idempotency, POSIX-sh under `dash`/`ash`, 5-distro
  matrix** → **podman/docker** (`archlinux`, `debian:stable-slim`, `alpine`,
  `fedora`, `void`): fast, ephemeral, CI-friendly; covers 5 pkg managers + 3 inits.
- **user-for-user mode (§8)** → **rootless podman**: runs as a non-root uid →
  exercises no-root install for free, surfaces bad root assumptions.
- **`enable_service` dispatch** → **PATH-mocked** `systemctl`/`rc-update`/`sv`:
  assert the right command per `OSR_INIT`; real init is painful in a container.
- **full DE / hyprland / sddm / GPU / wallpaper** → **QEMU VM**: needs a real
  kernel, display, GPU — impossible in a container.

Concretely:

- **Matrix = the idempotency test.** For each image:
  `podman run --rm -v "$PWD":/os-rice img sh -c 'install.sh <rice> && install.sh <rice>'`
  — the **double run is** §2's "second run all skipped, zero errors" acceptance.
- **POSIX lint in CI:** every `lib/`+`modules/` file through `dash -n` and
  `shellcheck -s sh` — catches the bash-isms the POSIX decision commits to killing.
- **Service tests without real services:** fake `systemctl`/`rc-update`/`sv` on
  `PATH` that log their args; assert `enable_service foo` emits the right call for
  each `OSR_INIT`. Real start only in an occasional systemd/OpenRC full image.
- **QEMU only for the DE smoke test:** one heavy, *manual/nightly* job — boot an
  Arch VM, run the hyprland rice, screenshot. Not per-commit.

**Recommendation: podman-first** (rootless matches user-mode, no daemon), docker
as fallback, QEMU reserved for the DE/service smoke test. Container matrix +
`dash -n` + shellcheck run per-commit; QEMU stays manual.

---

## Module example (target)

`zsh.sh` written **once**, POSIX, distro-agnostic — compare to the three pasted
copies today:

```sh
# modules/zsh.sh
run_step "Installing zsh + tools" pkg_install zsh curl lsd starship

install_omz                                        # shared helper in lib/git.sh
install_zsh_plugin zsh-autosuggestions     https://github.com/zsh-users/zsh-autosuggestions
install_zsh_plugin zsh-syntax-highlighting https://github.com/zsh-users/zsh-syntax-highlighting

backup_copy "$DOTFILES/zsh/.zshrc"          "$HOME_U/.zshrc"
backup_copy "$DOTFILES/starship/starship.toml" "$HOME_U/.config/starship.toml"

[ "$(getent passwd "$U" | cut -d: -f7)" = "$(command -v zsh)" ] \
  || chsh -s "$(command -v zsh)" "$U"
```

---

## MVP scope

Prove the whole thesis end to end on the smallest slice:

**In:**
- `lib/{log,ui,pkg,detect}.sh` + `lib/pkgmap/{apt,pacman}.map`
- one shared `install.sh` runner
- migrate `zsh.sh` to POSIX + `pkg_*` + `run_step`
- make it install on **arch and debian from the same module file**
- **prove group-by-method dispatch** on two non-native rows: `starship=script:`
  and `paru=source:` install correctly alongside native `zsh` in one
  `pkg_install` call, each with its own idempotency probe (§4)
- **split this `.zshrc`** into `rc.d/{00-env,10-aliases,90-theme}.zsh` behind a
  marker-managed loader via `ensure_block`; env seeded-once, guard-style PATH (§5)
- **`osr switch <rice>`** that swaps `90-theme` + wallpaper only, proving the
  additive/replace asymmetry (§6)
- **podman idempotency test** on arch + debian + alpine images: double-run
  `install.sh` is green, `dash -n` + shellcheck clean (§9)

**Out of MVP:** rewriting all ~40 arch modules; `cargo:`/`flatpak:` providers;
layering DE configs beyond zsh; `osr prune`; the auto-dependency resolver; the
bootstrap binary; Windows.

**Acceptance:**
- [ ] The 5-verb `pkg_*` abstraction covers `zsh` with only package *names*
      differing (handled by `pkgmap`).
- [ ] `pkg_install zsh starship paru` resolves to native-batched `zsh`,
      `script:` starship, and `source:` paru in one call.
- [ ] Modules run under `dash` / busybox `ash`, not just bash.
- [ ] `bootstrap.sh` finds a downloader (curl || wget || busybox wget) on
      minimal Alpine + minimal Debian; no compiled binary needed.
- [ ] Running the rice **twice** → second run is all `✔ skipped`, zero errors,
      and `$PATH` has **no duplicated entries**.
- [ ] `osr switch other-rice` changes the prompt + wallpaper while leaving
      `00-env`, aliases, and every installed package untouched.
- [ ] Piping install to a file yields clean plain-text logs (no escape junk).

---

## Not Doing (and Why)

- **Custom C helper binary** — busybox-static covers missing primitives;
  hand-rolled C reinvents it. Revisit only on a concrete gap busybox can't fill.
- **C rewrite for speed** — wall-clock is `apt`/`curl`/network, not shell. Saves
  nothing measurable and breaks self-bootstrap.
- **Byte-level per-package progress bars** — package managers won't feed them;
  step progress + spinners deliver the same "feels alive" for a fraction of the
  code.
- **TOML/YAML manifests** — need a parser, un-POSIX, un-lazy. A newline list with
  `#` comments is more readable *and* free to parse.
- **Merging Windows in** — different package model and language; a shared
  abstraction there is negative value.
- **Per-commit full-VM (QEMU) CI** — too slow for the inner loop; containers
  carry the per-commit matrix, QEMU is a manual/nightly DE smoke test (§9).
- **A plugin/hook framework** — YAGNI. `use module` = source a file. Add
  structure when a second contributor actually needs it.
- **Auto-resolved provider dependency DAG** — manifest order is the graph (`rust`
  before `cargo:`, `paru` before `aur:`). A DAG is the plugin framework by
  another name. YAGNI.
- **Package removal on rice switch** — additive-only; removal is un-idempotent
  and risky. A separate opt-in `osr prune <rice>` may come later.
- **Provider fallback chains** (native → cargo → source) — non-deterministic and
  hard to make idempotent. Explicit per-distro tags are predictable; revisit only
  if hand-maintaining N maps hurts.
- **Pinning `curl | sh` installers** — accept upstream drift for the convenience;
  revisit only if a drifting installer burns a run.

---

## Open Questions

- Idempotency as the hard contract for `install.sh <rice>` — confirmed yes; make
  it a documented rule and lint modules against raw mutation where practical.
- `pkgmap` rows only for packages that differ (chosen) vs exhaustive maps —
  keep minimal.
- Layer taxonomy beyond `00/10/20/90/99` — is a two-digit numeric prefix enough
  ordering headroom for every config, or will some need sub-layers?
