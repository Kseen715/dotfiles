---
title: Proteus
type: readme
tags:
  - kind/readme
  - topic/theming
  - topic/gui
  - lang/rust
  - os/linux
---

# Proteus

A theme and wallpaper picker that opens in the middle of the screen, shows you
what each theme looks like, and applies the one you choose.

Named for the shape-shifting sea god, because that is the job: it changes what
everything looks like.

```
┌──────────────────────────────────────────────────────────────────┐
│  theme  filter themes...                                         │
│                                                                  │
│  Nord                                                          • │
│  Arctic, north-bluish palette                                    │
│  ▬▬ ▬▬ ▬▬ ▬▬ ▬▬                                                  │
│                                                                  │
│    ╱▔▔▔╱▔▔▔╱▔▔▔▔╱▔▔▔╱▔▔▔╱                                        │
│   ╱   ╱   ╱    ╱   ╱   ╱     cards overhang, selected in front   │
│  ╱▁▁▁╱▁▁▁╱▁▁▁▁╱▁▁▁╱▁▁▁╱                                          │
│           ▔▔▔▔                                                   │
│  4/6   ← → browse  •  Tab: wallpapers  •  Enter: apply  •  Esc   │
└──────────────────────────────────────────────────────────────────┘
```


## Why this exists

Every launcher-shaped tool is tied to one display server: rofi is X11 (the
Wayland port is a separate fork), wofi/fuzzel/tofi/anyrun are Wayland. Every
wallpaper GUI is a wallpaper GUI. Nothing shows you a *theme* with its wallpaper
as the preview and then rewires your configs.

Proteus is one binary that runs on both, draws identically on both, and calls a
command you configure to do the applying.

## What it does

- **One binary, X11 and Wayland.** Native on each: `wlr-layer-shell` on Wayland
  (Hyprland, sway), override-redirect on X11 (i3, bspwm) — a real overlay, not a
  window the WM tiles. Falls back to an ordinary window on compositors without
  layer-shell (GNOME, KDE).
- **A shelf of wallpapers, not a list of names.** Previews are leaning portrait
  cards that overhang each other like a fanned deck, with the selected one
  growing into focus in front — nothing ever covers it. A wallpaper is a portrait
  of a screen, and "which of these" is the decision being made. Themes with no
  wallpaper get a card generated from their own palette, so none is ever blank.
- **It wears the theme you are looking at.** Moving the cursor restyles the
  picker in that theme's colours — the fastest way to know what you are picking.
- **Wallpapers too.** `Tab` switches to a wallpaper browser over the current
  theme's images plus your library.
- **Fuzzy filter**, keyboard and mouse, wrap-around navigation.
- **An endless strip.** The cards are a ring: step past the last and the first
  slides in behind it, in the direction you were already going. There is no jump
  back to the start and no end to bump into.
- **Smooth scrolling.** The strip slides and cards grow and fade with distance
  from the selection, on a frame clock that runs only while something is moving —
  an idle picker measures **0 CPU ticks per second**.
- **Bounded memory.** ~18 MB idle. Decoded previews are held in a 25 MB LRU and
  thumbnails are cached on disk, so a library of hundreds of wallpapers costs the
  same as a library of six.
- **Runs on old and new hardware.** Three presentation paths, probed in order:
  wgpu (Vulkan, Metal, DX12, GL 3.3+), then **OpenGL 2.1 / GLES 2.0** for
  hardware wgpu will not touch, then straight to the display server. All three
  show identical pixels.
- **No build-time system dependencies.** Pure-Rust X11 and Wayland protocol
  handling; libxkbcommon and libwayland are `dlopen`ed at runtime, so the binary
  builds on a machine that has neither.

## Install

```sh
cargo install --locked --path .
```

Or, inside this dotfiles repo, as an os-rice module:

```sh
osr module proteus
```

## Use

```sh
proteus                     # open the picker
proteus --list              # print the themes it found
proteus --screenshot x.png  # render one frame headlessly (no display needed)
proteus --print-only        # print the command Enter would run
proteus --renderer gl       # force the OpenGL 2.1 path
proteus --renderer cpu      # force plain software presentation
proteus --window toplevel   # an ordinary window instead of an overlay
```

| Key | |
|---|---|
| `←` `→` `↑` `↓` | move along the strip (endless) |
| `PgUp` `PgDn` | page |
| `Home` `End` | first / last (by the shorter way round) |
| type | filter |
| `Tab` | themes ⇄ wallpapers |
| `Enter` | apply |
| `Esc` | clear the filter, then close |
| click | apply that card |

Bound in this repo to `$mod+Shift+d` (i3) and `SUPER+SHIFT+D` (Hyprland).

## Configuration

`~/.config/proteus/proteus.toml`. Every key is optional; see the commented
[`proteus.toml`](proteus.toml) for the full set with defaults.

```toml
[sources]
themes_dir = "~/projects/dotfiles/os-rice/themes"
wallpaper_dirs = ["~/Pictures/Wallpapers"]

[actions]
# `{}` is the theme name or the wallpaper path. This is an argv, not a shell
# line: a file named "; rm -rf ~ .png" is a file name and nothing else.
apply_theme = ["osr", "theme", "{}"]
apply_wallpaper = ["osr", "wallpaper", "{}"]

[style]
width = 1000.0
rows = 5               # cards visible across the strip
radius = 12.0
follow_theme = true    # take colours from the theme under the cursor
animate = true         # slide the strip instead of jumping

[cache]
memory_mb = 25         # decoded previews held in RAM (LRU)
disk_mb = 200          # thumbnails kept in ~/.cache/proteus/thumbs

[behavior]
window = "overlay"     # or "toplevel"
renderer = "auto"      # auto | wgpu | gl | cpu
```

`PROTEUS_DEBUG=1` prints which renderer and adapter were chosen.

A config that fails to parse is reported and then ignored: the picker still
opens with defaults, because a theme switcher you cannot open is a theme
switcher you cannot use to fix your config.

## Using it without os-rice

Nothing above the `[actions]` table knows what os-rice is. Point `themes_dir` at
any directory of `<name>/theme.list` files and `apply_theme` at any script:

```
mythemes/
  nord/
    theme.list
    wallpapers/*.png
```

```
display: Nord
description: Arctic, north-bluish palette
polarity: dark              # dark | light
session: any                # any | x11 | wayland
color: bg      #2e3440
color: surface #3b4252
color: fg      #d8dee9
color: dim     #4c566a
color: accent  #88c0d0
```

The format is deliberately not TOML — it is the same `key: value` shape os-rice
uses everywhere, so a POSIX shell can read it with `while read` and no parser.
One consequence worth knowing: since a palette value *is* a `#`, a comment is
only `#` at the start of a line or a hash with whitespace on **both** sides.

## Design

```
catalog ─┐                                          ┌─→ wgpu ──→ swapchain
config ──┼─→ model ─┬─→ ui ──→ scene ──→ cpu raster ─┼─→ gl 2.1 ─→ swapchain
fuzzy ───┘   anim ──┘         (tiny-skia)           └─→ direct ──→ X11 / Wayland
```

Everything decidable without a display server is: `catalog`, `fuzzy`, `model`,
`anim`, `scene` and `ui` are pure and unit-tested, and `--screenshot` renders the
real frame headlessly. That is why the test suite runs anywhere.

**One rasteriser, three ways to present it.** The scene is drawn by tiny-skia
and the GPU paths upload the finished frame. Turning the scene into GPU geometry
would mean a second implementation of clipping, antialiasing and glyph placement
that has to agree with the first pixel-for-pixel — three times over, for three
backends, on the drivers least able to be tested. Uploading the frame keeps every
backend pixel-identical by construction, so the golden tests cover all of them,
and it is what makes a GL 2.1 path affordable at all: a textured quad needs no
extension any 2004 GPU lacks.

That only works because the frame is cheap. Images are cached **composed at
their final on-screen size** — premultiplied, scaled, sheared to the card's lean,
alpha and all — so a frame is a handful of blits rather than a rescale of every
card. That took the frame from **10.4 ms to 1.3 ms** at 1000×460, which is what
makes 60fps scrolling possible on hardware far slower than the machine it was
written on. See [`src/render/gpu.rs`](src/render/gpu.rs) and
[`src/render/gl.rs`](src/render/gl.rs).

**The strip is a ring, and that is a model decision, not a drawing trick.** The
selection keeps an *unbounded* position which the view wraps onto the list. A
wrapped position cannot express "one step past the last card" — it can only say
"back at the first", which is exactly the jump this replaced. Keeping the two
apart is what lets the motion be continuous while the selection stays correct,
and it is why clicking names a card on the ring rather than an item in a list:
on a short ring the same theme is on screen twice, and the copy you clicked is
the one that should come to the middle.

**Memory is bounded, not merely small.** Decoded previews live in a 25 MB LRU;
past that the least-recently-seen are dropped and re-read from a thumbnail cache
under `~/.cache/proteus/thumbs`, itself capped and pruned oldest-first. Without
the bound, browsing a few hundred wallpapers at ~2.5 MB each would climb toward
half a gigabyte and never come back down — a cache indistinguishable from a leak.
Image ids stay valid across eviction, so a scene holding one never draws the
wrong picture; it draws nothing.

## Tests

```sh
cargo test
```

163 unit tests plus 7 end-to-end rendering tests. The window tests open a real
window when `DISPLAY`/`WAYLAND_DISPLAY` are set and skip themselves when not, so
the suite is green headless and meaningful on a desktop.

```sh
cargo run --release --example frame_cost -- ../os-rice/themes   # frame budget
cargo run --release --example anim_frames -- ../os-rice/themes /tmp  # motion
```

## License

MIT.
