//! End-to-end rendering: a themes directory on disk, through the catalogue, the
//! model, the layout and the rasteriser, to pixels.
//!
//! The assertions are about colour and placement rather than an exact image.
//! A committed golden PNG would be the stricter test and the wrong one here:
//! the output contains shaped text, so it changes with the font available on
//! the machine, and the test would fail on every box but the one that made it.
//! What must not change is that a theme's own colours end up on screen where
//! the layout says they should.

use std::fs;
use std::path::{Path, PathBuf};

use proteus::app::App;
use proteus::config::{Config, Style};
use proteus::images::ImageStore;
use proteus::model::Mode;
use proteus::render::cpu::CpuRenderer;
use proteus::scene::Color;
use proteus::text::TextEngine;
use proteus::ui::{build_scene, Layout};

fn tmpdir(tag: &str) -> PathBuf {
    let d = std::env::temp_dir().join(format!(
        "proteus-render-{tag}-{}-{:?}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    ));
    fs::create_dir_all(&d).unwrap();
    d
}

fn write(p: &Path, s: &str) {
    fs::create_dir_all(p.parent().unwrap()).unwrap();
    fs::write(p, s).unwrap();
}

/// Two themes with unmistakable, mutually distinct palettes, so any colour
/// found on screen can only have come from one of them.
fn fixture() -> PathBuf {
    let d = tmpdir("fix");
    write(
        &d.join("themes/aaa-red/theme.list"),
        "display: Red Theme\n\
         description: the first one\n\
         color: background  #200000\n\
         color: surface #400000\n\
         color: foreground  #ffdddd\n\
         color: text_dim    #a06060\n\
         color: accent  #ff0000\n",
    );
    write(
        &d.join("themes/bbb-green/theme.list"),
        "display: Green Theme\n\
         description: the second one\n\
         session: wayland\n\
         color: background  #002000\n\
         color: surface #004000\n\
         color: foreground  #ddffdd\n\
         color: text_dim    #60a060\n\
         color: accent  #00ff00\n",
    );
    d
}

struct Rendered {
    r: CpuRenderer,
    layout: Layout,
}

fn render(app: &App, mode: Mode, cursor: usize) -> Rendered {
    let style = Style {
        width: 800.0,
        height: 400.0,
        ..app.config.style.clone()
    };
    let mut text = TextEngine::new(None);
    let layout = Layout::compute(style.width, style.height, &style, &text);
    let mut model = app.model.clone_for_render();
    model.set_rows_visible(layout.rows_visible());
    if model.mode != mode {
        model.switch_mode();
    }
    model.move_to(cursor);

    let mut images = ImageStore::new(512);
    let scene = build_scene(&model, &style, &layout, &mut images, &mut text);
    let mut r = CpuRenderer::new(style.width as u32, style.height as u32).unwrap();
    r.draw(&scene, &images, &mut text);
    Rendered { r, layout }
}

fn app_for(dir: &Path) -> App {
    let (cfg, err) = Config::parse(&format!(
        "[sources]\nthemes_dir = \"{}\"\nwallpaper_dirs = []\nstate_file = \"{}\"\n",
        dir.join("themes").display(),
        dir.join("state").display()
    ));
    assert!(err.is_none(), "{err:?}");
    App::load(cfg, Vec::new())
}

/// How close two colours are, 0 = identical.
fn dist(a: Color, b: Color) -> i32 {
    (a.r as i32 - b.r as i32).abs()
        + (a.g as i32 - b.g as i32).abs()
        + (a.b as i32 - b.b as i32).abs()
}

#[test]
fn the_window_is_painted_in_the_selected_themes_background() {
    let d = fixture();
    let app = app_for(&d);
    assert_eq!(app.model.themes.len(), 2);

    // Cursor on the red theme: the window plate must be red-ish.
    let out = render(&app, Mode::Themes, 0);
    let bg = out.r.pixel(400, 380); // below the list, on the plate
    assert!(
        dist(bg, Color::rgba(0x20, 0, 0, 255)) < 40,
        "expected the red theme's background, got {bg:?}"
    );

    // Move to the green theme and the whole window restyles - this is the live
    // preview the picker is for.
    let out = render(&app, Mode::Themes, 1);
    let bg = out.r.pixel(400, 380);
    assert!(
        dist(bg, Color::rgba(0, 0x20, 0, 255)) < 40,
        "expected the green theme's background, got {bg:?}"
    );

    fs::remove_dir_all(&d).ok();
}

#[test]
fn the_selected_row_is_marked_with_the_accent() {
    let d = fixture();
    let app = app_for(&d);
    let out = render(&app, Mode::Themes, 0);

    // The selection is a bar on the shelf under the focused card. Sweep the
    // shelf band for it rather than guessing an exact pixel: the card's position
    // depends on the anchor, which depends on how many themes the fixture has.
    let l = out.layout;
    let anchor = l.anchor(0.0, 2);
    let card = l.card_rect(0.0 - anchor, 1.0);
    let band_y = (card.bottom() + l.shelf * 0.35) as u32;
    let mut found = false;
    for y in band_y..(band_y + 4).min(l.strip.bottom() as u32) {
        for x in (card.x.max(0.0) as u32)..((card.right() + l.skew) as u32) {
            let c = out.r.pixel(x, y);
            if c.r > 180 && c.g < 80 && c.b < 80 {
                found = true;
            }
        }
    }
    assert!(
        found,
        "the selected card should be marked with an accent bar"
    );

    fs::remove_dir_all(&d).ok();
}

#[test]
fn a_theme_without_a_wallpaper_still_gets_a_preview_made_of_its_colours() {
    let d = fixture();
    let app = app_for(&d);
    let out = render(&app, Mode::Themes, 0);

    // Somewhere on the selected card there must be the theme's accent, because
    // a theme with no wallpaper gets a preview built from its palette.
    let p = out.layout.strip;
    let mut found_accent = false;
    let mut y = p.y as u32 + 4;
    while y < (p.bottom() as u32).saturating_sub(4) {
        let mut x = p.x as u32 + 4;
        while x < (p.right() as u32).saturating_sub(4) {
            let c = out.r.pixel(x, y);
            if c.r > 200 && c.g < 60 && c.b < 60 {
                found_accent = true;
            }
            x += 3;
        }
        y += 3;
    }
    assert!(
        found_accent,
        "the generated preview should show the theme's accent colour"
    );

    fs::remove_dir_all(&d).ok();
}

#[test]
fn the_selected_items_name_is_drawn_in_the_header() {
    let d = fixture();
    let app = app_for(&d);
    let out = render(&app, Mode::Themes, 0);

    // The selected item's name is drawn in the header, in the theme's fg.
    let head = out.layout.header;
    let mut ink = 0;
    for y in head.y as u32..head.bottom() as u32 {
        for x in (head.x as u32)..(head.right() as u32) {
            let c = out.r.pixel(x, y);
            // The fg of both fixtures is near-white.
            if c.r > 200 && c.g > 180 && c.b > 180 {
                ink += 1;
            }
        }
    }
    assert!(
        ink > 30,
        "expected the selected theme's name in the header, found {ink} lit pixels"
    );

    fs::remove_dir_all(&d).ok();
}

#[test]
fn the_frame_is_opaque_inside_and_transparent_at_the_corners() {
    let d = fixture();
    let app = app_for(&d);
    let out = render(&app, Mode::Themes, 0);

    // The rounded plate leaves the very corner unpainted, which is what lets a
    // compositor show the desktop through it.
    let corner = out.r.pixel(0, 0);
    assert!(
        corner.a < 128,
        "the corner should be transparent, got {corner:?}"
    );

    // ...while the middle is fully opaque.
    let middle = out.r.pixel(400, 200);
    assert_eq!(middle.a, 255, "the body of the window must be opaque");

    fs::remove_dir_all(&d).ok();
}

#[test]
fn wallpaper_mode_renders_an_empty_library_without_panicking() {
    let d = fixture();
    let app = app_for(&d);
    // The fixture ships no wallpapers at all: the picker must still draw.
    let out = render(&app, Mode::Wallpapers, 0);
    let bg = out.r.pixel(400, 380);
    assert_eq!(
        bg.a, 255,
        "the window is still painted with an empty library"
    );
    fs::remove_dir_all(&d).ok();
}

#[test]
fn rendering_is_deterministic() {
    let d = fixture();
    let app = app_for(&d);
    // The same inputs must give the same pixels; a picker that shimmers between
    // frames would make every other assertion here flaky.
    let a = render(&app, Mode::Themes, 1);
    let b = render(&app, Mode::Themes, 1);
    for (x, y) in [(10, 10), (400, 200), (799, 399), (123, 45)] {
        assert_eq!(
            a.r.pixel(x, y),
            b.r.pixel(x, y),
            "pixel ({x},{y}) differs between runs"
        );
    }
    fs::remove_dir_all(&d).ok();
}
