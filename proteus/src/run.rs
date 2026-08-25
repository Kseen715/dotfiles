//! The event loop: keys in, frames out, actions run.
//!
//! Written once against [`platform::Event`], so X11 and Wayland share it. The
//! interesting decisions here are about *when* to redraw and *when* to close;
//! everything about what a key means already lives in [`crate::model`].

use std::time::{Duration, Instant};

use crate::anim;
use crate::app::App;
use crate::config::{RendererChoice, WindowMode};
use crate::images::ImageStore;
use crate::model::Action;
use crate::platform::{Event, Key};
use crate::render::cpu::CpuRenderer;
use crate::text::TextEngine;
use crate::ui::{build_scene, Layout};

/// Translate an input event into a model action, mutating the model as needed.
///
/// Split out from the loop so every key binding is testable without a window.
pub fn handle_event(app: &mut App, layout: &Layout, event: &Event) -> Action {
    match event {
        Event::Key(key, mods) => match key {
            Key::Escape => app.model.escape(),
            Key::Enter => app.model.activate(),
            // Left/Right are the axis the strip actually runs along. Up/Down
            // stay bound to the same thing rather than being dropped: on a
            // horizontal shelf they have no meaning of their own, and a key that
            // does nothing is worse than a key that does the obvious thing.
            Key::Left | Key::Up => {
                app.model.move_by(-1);
                Action::Redraw
            }
            Key::Right | Key::Down => {
                app.model.move_by(1);
                Action::Redraw
            }
            Key::PageUp => {
                app.model.page_by(-1);
                Action::Redraw
            }
            Key::PageDown => {
                app.model.page_by(1);
                Action::Redraw
            }
            Key::Home => {
                app.model.move_to(0);
                Action::Redraw
            }
            Key::End => {
                let last = app.model.visible.len().saturating_sub(1);
                app.model.move_to(last);
                Action::Redraw
            }
            Key::Tab | Key::ShiftTab => {
                app.model.switch_mode();
                Action::Redraw
            }
            Key::Backspace => {
                app.model.pop_char();
                Action::Redraw
            }
            Key::Char(c) => {
                // Control characters would be invisible in the filter and would
                // silently match nothing.
                if c.is_control() {
                    Action::None
                } else {
                    app.model.push_char(*c);
                    Action::Redraw
                }
            }
            Key::Delete | Key::Other => {
                let _ = mods;
                Action::None
            }
        },
        Event::Scroll(lines) => {
            app.model.move_by(*lines as isize);
            Action::Redraw
        }
        Event::PointerMotion(_, _) => {
            // Hovering deliberately does NOT move the selection.
            //
            // It did while the picker was a static vertical list, where it was
            // the obvious nicety. On a strip that slides it is a feedback loop:
            // selecting under the pointer scrolls the strip, which puts a
            // different card under that same pointer, which selects again. The
            // cursor moves on keys and on clicks - both of which the user meant.
            Action::None
        }
        Event::PointerPress(x, y) => {
            // The click names a CARD on the ring, not an item in a list: on a
            // short ring the same theme appears more than once, and the one that
            // should slide to the middle is the one under the pointer.
            match layout.card_at(*x, *y, app.model.cursor_offset()) {
                Some(v) => match app.model.item_at_virtual(v) {
                    Some(_) => {
                        app.model.move_to_virtual(v);
                        app.model.activate()
                    }
                    None => Action::None,
                },
                // A click outside the strip is a click outside the picker.
                None => Action::Quit,
            }
        }
        Event::Resize(_, _) | Event::Redraw => Action::Redraw,
        Event::Close => Action::Quit,
    }
}

/// Outcome of a loop iteration, so the driver knows what to do next.
#[derive(Debug, PartialEq, Eq)]
pub enum Step {
    Continue,
    Redraw,
    Quit,
}

/// Apply an action's side effects. Returns what the loop should do next.
pub fn dispatch(app: &App, action: Action) -> Step {
    match action {
        Action::None => Step::Continue,
        Action::Redraw => Step::Redraw,
        Action::Quit => Step::Quit,
        Action::ApplyTheme(_) | Action::ApplyWallpaper(_) => {
            if let Err(e) = app.run(&action) {
                // Report and stay open: closing on failure hides the reason.
                eprintln!("proteus: {e}");
                return Step::Redraw;
            }
            if app.config.behavior.close_on_apply {
                Step::Quit
            } else {
                Step::Redraw
            }
        }
    }
}

/// Build the preview cache from the config, pruning the disk cache first.
///
/// Pruning here rather than on exit keeps it honest: a picker is killed far more
/// often than it is closed politely, so cleanup that runs at shutdown mostly
/// does not run.
pub fn new_image_store(config: &crate::config::Config) -> ImageStore {
    let dir = config.cache.dir();
    if let Some(d) = &dir {
        crate::images::prune_cache_dir(d, config.cache.disk_bytes());
    }
    ImageStore::new(1024)
        .with_budget(config.cache.memory_bytes())
        .with_disk_cache(dir)
}

/// A window, whichever display server produced it.
///
/// An enum rather than a trait object: there are exactly two, they are chosen
/// once at startup, and a `dyn` boundary in the frame path would buy nothing.
enum Window {
    Wayland(Box<crate::platform::wayland::WaylandWindow>),
    X11(Box<crate::platform::x11::X11Window>),
}

impl Window {
    fn raw_handles(
        &self,
    ) -> (
        raw_window_handle::RawDisplayHandle,
        raw_window_handle::RawWindowHandle,
    ) {
        match self {
            Window::Wayland(w) => w.raw_handles(),
            Window::X11(w) => w.raw_handles(),
        }
    }

    fn size(&self) -> (u32, u32) {
        match self {
            Window::Wayland(w) => w.size(),
            Window::X11(w) => w.size(),
        }
    }

    fn present(&mut self, pixels: &[u8]) -> Result<(), String> {
        match self {
            Window::Wayland(w) => w.present(pixels),
            Window::X11(w) => w.present(pixels),
        }
    }

    fn wait_events(&mut self) -> Vec<Event> {
        match self {
            Window::Wayland(w) => w.wait_events(),
            Window::X11(w) => w.wait_events(),
        }
    }

    fn wait_events_timeout(&mut self, timeout: std::time::Duration) -> Vec<Event> {
        match self {
            Window::Wayland(w) => w.wait_events_timeout(timeout),
            Window::X11(w) => w.wait_events_timeout(timeout),
        }
    }

    fn is_closed(&self) -> bool {
        match self {
            Window::Wayland(w) => w.is_closed(),
            Window::X11(w) => w.is_closed(),
        }
    }
}

/// Open a window on whichever display server is running.
///
/// Wayland is tried first when `WAYLAND_DISPLAY` is set, because a Wayland
/// session usually also exports `DISPLAY` for XWayland — going through XWayland
/// there would work but would give up the layer-shell overlay and pick up
/// XWayland's scaling quirks. If that fails and X11 is available, fall back
/// rather than exiting: a broken protocol is not a reason to show nothing.
fn open_window(width: u32, height: u32, overlay: bool) -> Result<Window, String> {
    let have_wayland = std::env::var("WAYLAND_DISPLAY")
        .map(|v| !v.is_empty())
        .unwrap_or(false);
    let have_x11 = std::env::var("DISPLAY")
        .map(|v| !v.is_empty())
        .unwrap_or(false);

    let mut first_error = None;
    if have_wayland {
        match crate::platform::wayland::WaylandWindow::open(width, height, overlay, "proteus") {
            Ok(w) => return Ok(Window::Wayland(Box::new(w))),
            Err(e) => first_error = Some(format!("wayland: {e}")),
        }
    }
    if have_x11 {
        match crate::platform::x11::X11Window::open(width, height, overlay, "proteus") {
            Ok(w) => {
                if let Some(e) = &first_error {
                    eprintln!("proteus: {e} - falling back to X11");
                }
                return Ok(Window::X11(Box::new(w)));
            }
            Err(e) => {
                return Err(match first_error {
                    Some(w) => format!("{w}; x11: {e}"),
                    None => format!("x11: {e}"),
                })
            }
        }
    }
    Err(first_error.unwrap_or_else(|| {
        "no display server (neither WAYLAND_DISPLAY nor DISPLAY is set)".to_string()
    }))
}

/// How a finished frame reaches the screen.
///
/// The scene is always rasterised by the CPU renderer — see `render::gpu` for
/// why — so this only decides how those pixels are handed over: through the
/// GPU's swapchain, or straight to the display server.
enum Present {
    /// wgpu: Vulkan, Metal, DX12, or GL 3.3+/GLES 3.
    Gpu(Box<crate::render::gpu::GpuPresenter>),
    /// OpenGL 2.1 / GLES 2.0, for hardware wgpu will not touch.
    Gl(Box<crate::render::gl::GlPresenter>),
    /// Straight to the display server, no GPU involved.
    Direct,
}

/// Pick a presentation path, honouring the config and falling back in order.
///
/// `auto` walks wgpu -> GL 2.1 -> direct and takes the first that initialises,
/// quietly: a machine without Vulkan is exactly what the next rung exists for,
/// and it is not something to interrupt the user about. An explicit
/// `--renderer wgpu` that fails IS reported, because the user asked for
/// something specific and got something else.
fn choose_present(window: &Window, width: u32, height: u32, choice: RendererChoice) -> Present {
    let debug = std::env::var("PROTEUS_DEBUG").is_ok();
    if choice == RendererChoice::Cpu {
        return Present::Direct;
    }
    let (display, handle) = window.raw_handles();

    if matches!(choice, RendererChoice::Auto | RendererChoice::Wgpu) {
        // catch_unwind, because "this rung is unavailable" does not always come
        // back as an Err. wgpu-hal asserts its way out rather than returning:
        // on an X11 machine whose Vulkan ICD cannot be used, surface creation
        // hits `.expect("Pointer to X-Server is not set")` inside
        // wgpu-hal/src/vulkan/instance.rs and the process dies. The Err arm
        // below never runs, so `auto` never reaches GL - which is precisely the
        // machine the lower rungs exist for, and it takes the picker down
        // instead of degrading. A panic here means the same thing an Err does.
        //
        // The hook is swapped out for the attempt so `auto` stays quiet: the
        // default hook would print a panic message and a backtrace note for
        // what is, at this rung, an expected outcome.
        let prev_hook = std::panic::take_hook();
        std::panic::set_hook(Box::new(|_| {}));
        let attempt = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            // SAFETY: `window` outlives the presenter - both are dropped at the
            // end of `run`, the window last.
            let target = unsafe { crate::render::gpu::SurfaceTarget::new(display, handle) };
            crate::render::gpu::GpuPresenter::new(target, width, height)
        }));
        std::panic::set_hook(prev_hook);

        match attempt {
            Ok(Ok(p)) => {
                if debug {
                    eprintln!("proteus: wgpu {} via {}", p.adapter_name, p.backend);
                }
                return Present::Gpu(Box::new(p));
            }
            Ok(Err(e)) => {
                if choice == RendererChoice::Wgpu {
                    eprintln!("proteus: {e} - falling back");
                } else if debug {
                    eprintln!("proteus: wgpu unavailable ({e}), trying GL");
                }
            }
            Err(_) => {
                if choice == RendererChoice::Wgpu {
                    eprintln!("proteus: the wgpu backend panicked - falling back");
                } else if debug {
                    eprintln!("proteus: wgpu panicked during init, trying GL");
                }
            }
        }
    }

    if matches!(choice, RendererChoice::Auto | RendererChoice::Gl) {
        // SAFETY: as above.
        match unsafe { crate::render::gl::GlPresenter::new(display, handle, width, height) } {
            Ok(p) => {
                if debug {
                    eprintln!("proteus: GL {} ({})", p.renderer_name, p.version);
                }
                return Present::Gl(Box::new(p));
            }
            Err(e) => {
                if choice == RendererChoice::Gl {
                    eprintln!("proteus: {e} - falling back to CPU presentation");
                } else if debug {
                    eprintln!("proteus: GL unavailable ({e}), using CPU presentation");
                }
            }
        }
    }

    Present::Direct
}

/// Run the picker against a real window.
pub fn run(mut app: App) -> Result<(), String> {
    let style = app.config.style.clone();
    let mut text = TextEngine::new(style.font.clone());
    let overlay = app.config.behavior.window == WindowMode::Overlay;

    let mut window = open_window(style.width as u32, style.height as u32, overlay)?;
    let (mut width, mut height) = window.size();

    let mut present = choose_present(&window, width, height, app.config.behavior.renderer);
    let mut renderer =
        CpuRenderer::new(width, height).ok_or_else(|| "cannot allocate a frame".to_string())?;
    let mut images = new_image_store(&app.config);

    let mut layout = Layout::compute(width as f32, height as f32, &style, &text);
    app.model.set_view(layout.rows_visible(), layout.advance);
    app.model
        .set_animation(style.animate, style.animation_speed);

    // Draw before waiting for the first event: an empty window while the user
    // waits for input looks like a hang.
    let mut needs_draw = true;
    let mut last_frame = Instant::now();
    loop {
        if needs_draw {
            layout = Layout::compute(width as f32, height as f32, &style, &text);
            app.model.set_view(layout.rows_visible(), layout.advance);
            let scene = build_scene(&app.model, &style, &layout, &mut images, &mut text);
            renderer.resize(width, height);
            renderer.draw(&scene, &images, &mut text);
            // A GPU failure mid-run drops to direct presentation rather than
            // killing the picker: the frame is already rasterised, and the
            // display server can always take it.
            let failed = match &mut present {
                Present::Gpu(gpu) => {
                    gpu.resize(width, height);
                    gpu.present(renderer.rgba(), width, height).err()
                }
                Present::Gl(gl) => {
                    gl.resize(width, height);
                    gl.present(renderer.rgba(), width, height).err()
                }
                Present::Direct => {
                    window.present(renderer.bgra_premultiplied())?;
                    None
                }
            };
            if let Some(e) = failed {
                eprintln!("proteus: {e} - switching to CPU presentation");
                present = Present::Direct;
                window.present(renderer.bgra_premultiplied())?;
            }
            needs_draw = false;
        }

        // Two waiting modes. While something is moving, wake on a frame clock so
        // the animation advances; otherwise block indefinitely and use no CPU at
        // all. A picker that spun at 60fps while sitting still would be a
        // laptop-battery bug, and one that only redrew on input could not
        // animate.
        let was_animating = app.model.animating();
        let events = if was_animating {
            window.wait_events_timeout(anim::FRAME)
        } else {
            window.wait_events()
        };

        // Elapsed time only counts when something WAS animating. After an idle
        // block this is minutes, and feeding that to the animation would advance
        // the very first keypress almost to its destination - the one press that
        // would then not appear to animate at all.
        let now = Instant::now();
        let dt = if was_animating {
            now.saturating_duration_since(last_frame)
        } else {
            Duration::ZERO
        };
        last_frame = now;

        for event in events {
            if let Event::Resize(w, h) = event {
                if w > 0 && h > 0 && (w, h) != (width, height) {
                    width = w;
                    height = h;
                }
                needs_draw = true;
                continue;
            }
            let action = handle_event(&mut app, &layout, &event);
            match dispatch(&app, action) {
                Step::Quit => return Ok(()),
                Step::Redraw => needs_draw = true,
                Step::Continue => {}
            }
        }

        // Advance by the time that actually passed, not by the frame we hoped
        // for: the wait may return early on an event or late under load, and
        // using the nominal 16ms either way would make the motion speed up and
        // slow down with input.
        //
        // Ticked after the events, so a target a key just set is animated from
        // where the list is now rather than from where it was a frame ago.
        if app.model.tick(dt) {
            needs_draw = true;
        }

        if window.is_closed() {
            return Ok(());
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::catalog::{Palette, Session, Theme};
    use crate::config::Config;
    use crate::model::{Mode, Model};
    use crate::platform::Mods;
    use crate::ui::Layout;
    use std::path::PathBuf;

    fn theme(name: &str) -> Theme {
        Theme {
            name: name.into(),
            display: name.into(),
            description: String::new(),
            session: Session::Any,
            polarity: "dark".into(),
            palette: Palette::default(),
            dir: PathBuf::from("/t").join(name),
            wallpapers: Vec::new(),
        }
    }

    fn app() -> App {
        let (config, _) = Config::parse("");
        let mut a = App {
            config,
            model: Model::new(
                vec![
                    theme("alpha"),
                    theme("beta"),
                    theme("gamma"),
                    theme("delta"),
                ],
                vec![PathBuf::from("/w/one.png"), PathBuf::from("/w/two.png")],
                Session::Any,
            ),
            warnings: Vec::new(),
        };
        a.model.set_rows_visible(4);
        a
    }

    fn layout() -> Layout {
        let style = crate::config::Style::default();
        Layout::compute(style.width, style.height, &style, &TextEngine::new(None))
    }

    fn key(k: Key) -> Event {
        Event::Key(k, Mods::default())
    }

    #[test]
    fn arrows_move_the_selection() {
        let mut a = app();
        let l = layout();
        assert_eq!(handle_event(&mut a, &l, &key(Key::Down)), Action::Redraw);
        assert_eq!(a.model.cursor, 1);
        handle_event(&mut a, &l, &key(Key::Up));
        assert_eq!(a.model.cursor, 0);
        handle_event(&mut a, &l, &key(Key::End));
        assert_eq!(a.model.cursor, 3);
        handle_event(&mut a, &l, &key(Key::Home));
        assert_eq!(a.model.cursor, 0);
    }

    #[test]
    fn typing_filters_and_backspace_undoes_it() {
        let mut a = app();
        let l = layout();
        handle_event(&mut a, &l, &key(Key::Char('b')));
        assert_eq!(a.model.query, "b");
        assert_eq!(a.model.visible.len(), 1, "only beta contains a 'b'");
        handle_event(&mut a, &l, &key(Key::Backspace));
        assert_eq!(a.model.query, "");
        assert_eq!(a.model.visible.len(), 4);
    }

    #[test]
    fn control_characters_are_not_typed_into_the_filter() {
        let mut a = app();
        let l = layout();
        assert_eq!(
            handle_event(&mut a, &l, &key(Key::Char('\u{1}'))),
            Action::None
        );
        assert!(a.model.query.is_empty());
    }

    #[test]
    fn enter_applies_and_escape_backs_out() {
        let mut a = app();
        let l = layout();
        assert_eq!(
            handle_event(&mut a, &l, &key(Key::Enter)),
            Action::ApplyTheme("alpha".into())
        );
        // Escape clears a filter first...
        handle_event(&mut a, &l, &key(Key::Char('b')));
        assert_eq!(handle_event(&mut a, &l, &key(Key::Escape)), Action::Redraw);
        // ...and quits once there is none.
        assert_eq!(handle_event(&mut a, &l, &key(Key::Escape)), Action::Quit);
    }

    #[test]
    fn tab_switches_mode_in_both_directions() {
        let mut a = app();
        let l = layout();
        handle_event(&mut a, &l, &key(Key::Tab));
        assert_eq!(a.model.mode, Mode::Wallpapers);
        handle_event(&mut a, &l, &key(Key::ShiftTab));
        assert_eq!(a.model.mode, Mode::Themes);
    }

    /// Where card `i` is actually drawn right now.
    fn card_centre(l: &Layout, a: &App, i: usize) -> (f32, f32) {
        let anchor = l.anchor(a.model.cursor_offset(), a.model.visible.len());
        let card = l.card_rect(i as f32 - anchor, 1.0);
        (card.x + card.w / 2.0, card.y + card.h / 2.0)
    }

    #[test]
    fn a_click_on_a_card_selects_and_applies_it() {
        let mut a = app();
        let l = layout();
        let (x, y) = card_centre(&l, &a, 2);
        let action = handle_event(&mut a, &l, &Event::PointerPress(x, y));
        assert_eq!(a.model.cursor, 2);
        assert_eq!(action, Action::ApplyTheme("gamma".into()));
    }

    #[test]
    fn a_click_outside_the_list_closes_the_picker() {
        let mut a = app();
        let l = layout();
        // The footer is not a row.
        let action = handle_event(
            &mut a,
            &l,
            &Event::PointerPress(l.footer.x, l.footer.y + 2.0),
        );
        assert_eq!(action, Action::Quit);
    }

    #[test]
    fn hovering_does_not_move_the_selection() {
        let mut a = app();
        let l = layout();
        let (x, y) = card_centre(&l, &a, 1);
        // Selecting on hover would scroll the strip, which would put a different
        // card under the same pointer, which would select again - a loop the
        // user cannot stop except by moving the mouse off the window.
        assert_eq!(
            handle_event(&mut a, &l, &Event::PointerMotion(x, y)),
            Action::None
        );
        assert_eq!(
            a.model.cursor, 0,
            "the selection stayed where the keys left it"
        );
        // ...and a mouse dragged across the window costs no redraws at all.
        for i in 0..20 {
            assert_eq!(
                handle_event(&mut a, &l, &Event::PointerMotion(i as f32 * 40.0, y)),
                Action::None
            );
        }
    }

    #[test]
    fn clicking_past_the_end_of_a_short_list_selects_nothing() {
        let mut a = app();
        let l = layout();
        for c in "alpha".chars() {
            a.model.push_char(c);
        }
        assert_eq!(a.model.visible.len(), 1);
        // A slot to the right of the only card is empty strip; clicking it must
        // not apply something that is not there.
        // The slot immediately right of the only card - still inside the strip,
        // but empty.
        let anchor = l.anchor(a.model.cursor_offset(), a.model.visible.len());
        let card = l.card_rect(1.0 - anchor, 1.0);
        let (x, y) = (card.x + card.w / 2.0, card.y + card.h / 2.0);
        assert!(
            l.strip.contains(x, y),
            "the test point must be inside the strip"
        );
        assert_eq!(
            handle_event(&mut a, &l, &Event::PointerPress(x, y)),
            Action::None
        );
        assert_eq!(a.model.cursor, 0);
    }

    #[test]
    fn scrolling_moves_the_selection() {
        let mut a = app();
        let l = layout();
        handle_event(&mut a, &l, &Event::Scroll(1.0));
        assert_eq!(a.model.cursor, 1);
        handle_event(&mut a, &l, &Event::Scroll(-1.0));
        assert_eq!(a.model.cursor, 0);
    }

    #[test]
    fn close_and_resize_do_the_obvious_thing() {
        let mut a = app();
        let l = layout();
        assert_eq!(handle_event(&mut a, &l, &Event::Close), Action::Quit);
        assert_eq!(
            handle_event(&mut a, &l, &Event::Resize(100, 100)),
            Action::Redraw
        );
        assert_eq!(handle_event(&mut a, &l, &Event::Redraw), Action::Redraw);
    }

    /// The frame clock's contract, which is easy to get wrong in a way that
    /// looks like "animation just does not work sometimes".
    ///
    /// After an idle block the elapsed wall time is however long the user sat
    /// there. Feeding that to the animation would advance the first keypress
    /// almost to its destination, so the one press after a pause would not
    /// appear to animate. `dt` therefore only counts while something WAS
    /// already moving.
    #[test]
    fn the_first_move_after_an_idle_wait_animates_from_the_start() {
        let mut a = app();
        let l = layout();
        a.model.set_view(4, 60.0);
        assert!(!a.model.animating(), "a fresh model is at rest");

        // The loop's rule: nothing was animating, so no time is credited.
        let dt = if a.model.animating() {
            Duration::from_secs(600)
        } else {
            Duration::ZERO
        };

        handle_event(&mut a, &l, &key(Key::Down));
        assert!(a.model.animating(), "moving starts an animation");
        let before = a.model.cursor_offset();
        a.model.tick(dt);
        let after = a.model.cursor_offset();

        assert!(
            (after - before).abs() < 0.01,
            "a ten-minute idle must not advance the animation (moved {} rows)",
            after - before
        );
        assert!(a.model.animating(), "and it is still in flight");

        // From there a real frame advances it a little, not all the way.
        a.model.tick(anim::FRAME);
        let stepped = a.model.cursor_offset();
        assert!(stepped > after, "a frame moves it");
        assert!(
            stepped < 1.0,
            "but not all the way to the next row: {stepped}"
        );
    }

    #[test]
    fn an_animation_settles_and_then_stops_asking_for_frames() {
        let mut a = app();
        let l = layout();
        a.model.set_view(4, 60.0);
        handle_event(&mut a, &l, &key(Key::Down));

        let mut frames = 0;
        while a.model.tick(anim::FRAME) {
            frames += 1;
            assert!(frames < 500, "the animation never settled");
        }
        assert!(!a.model.animating());
        // Once settled the loop blocks instead of spinning - that is what keeps
        // an open picker at zero CPU.
        assert!(
            !a.model.tick(anim::FRAME),
            "a settled model asks for no more frames"
        );
        assert!(
            (a.model.cursor_offset() - 1.0).abs() < 1e-3,
            "it landed on row 1"
        );
    }

    #[test]
    fn wrapping_around_the_ring_is_one_step_of_motion() {
        let mut a = app();
        let l = layout();
        a.model.set_view(4, 60.0);
        // Left from the first item lands on the last. It used to settle
        // instantly, on the reasoning that sliding the whole list past the eye
        // was noise - but on a ring it is not the whole list, it is one step
        // backwards, and animating it is what makes the strip endless.
        handle_event(&mut a, &l, &key(Key::Left));
        assert_eq!(a.model.cursor, 3, "wrapped onto the last item");
        assert!(a.model.animating(), "and it slides there");
        assert_eq!(a.model.virtual_cursor(), -1, "by one step, backwards");

        while a.model.tick(anim::FRAME) {}
        assert!(
            (a.model.cursor_offset() + 1.0).abs() < 1e-3,
            "it settles one step back, not three forward: {}",
            a.model.cursor_offset()
        );
    }

    #[test]
    fn dispatch_keeps_the_window_open_when_an_apply_fails() {
        let (config, _) = Config::parse("[actions]\napply_theme = [\"proteus-not-a-program\"]\n");
        let a = App {
            config,
            model: Model::new(vec![theme("alpha")], Vec::new(), Session::Any),
            warnings: Vec::new(),
        };
        // A failed apply must not close the picker - closing would hide the
        // error the user needs to see.
        assert_eq!(
            dispatch(&a, Action::ApplyTheme("alpha".into())),
            Step::Redraw
        );
    }

    #[test]
    fn dispatch_closes_on_success_only_when_configured() {
        let (config, _) = Config::parse("[actions]\napply_theme = [\"true\"]\n");
        let a = App {
            config,
            model: Model::new(vec![theme("alpha")], Vec::new(), Session::Any),
            warnings: Vec::new(),
        };
        assert_eq!(dispatch(&a, Action::ApplyTheme("alpha".into())), Step::Quit);

        let (config2, _) = Config::parse(
            "[actions]\napply_theme = [\"true\"]\n[behavior]\nclose_on_apply = false\n",
        );
        let b = App {
            config: config2,
            model: Model::new(vec![theme("alpha")], Vec::new(), Session::Any),
            warnings: Vec::new(),
        };
        assert_eq!(
            dispatch(&b, Action::ApplyTheme("alpha".into())),
            Step::Redraw
        );
    }
}
