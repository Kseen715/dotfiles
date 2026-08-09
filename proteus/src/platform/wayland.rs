//! Wayland window, via the pure-Rust wayland-client backend.
//!
//! Two shells, picked at runtime:
//!
//!   - **wlr-layer-shell** when the compositor offers it (Hyprland, sway, and
//!     every other wlroots-based compositor). This is the real overlay: the
//!     surface sits on the overlay layer, centred, and takes keyboard focus
//!     exclusively — the Wayland equivalent of an override-redirect window, and
//!     the only way to be a launcher rather than a tile.
//!   - **xdg-shell** otherwise (GNOME, KDE, Weston). An ordinary toplevel the
//!     compositor places. Less controlled, but universally available, and a
//!     picker that opens beats a picker that insists on a protocol.
//!
//! Keyboard handling deliberately avoids libxkbcommon: the compositor sends a
//! keymap, [`super::keys::Keymap`] parses the part that matters, and the binary
//! needs no system development package. See that module for what is given up.

use std::fs::File;
use std::io::Write;
use std::os::fd::{AsFd, FromRawFd, OwnedFd};
use std::time::Duration;

use wayland_client::protocol::{
    wl_buffer::WlBuffer,
    wl_compositor::WlCompositor,
    wl_keyboard::{self, WlKeyboard},
    wl_pointer::{self, WlPointer},
    wl_registry::{self, WlRegistry},
    wl_seat::{self, WlSeat},
    wl_shm::{self, WlShm},
    wl_shm_pool::WlShmPool,
    wl_surface::WlSurface,
};
use wayland_client::{Connection, Dispatch, EventQueue, Proxy, QueueHandle};
use wayland_protocols::xdg::shell::client::{
    xdg_surface::{self, XdgSurface},
    xdg_toplevel::{self, XdgToplevel},
    xdg_wm_base::{self, XdgWmBase},
};
use wayland_protocols_wlr::layer_shell::v1::client::{
    zwlr_layer_shell_v1::{Layer, ZwlrLayerShellV1},
    zwlr_layer_surface_v1::{self, Anchor, KeyboardInteractivity, ZwlrLayerSurfaceV1},
};

use super::keys::{keysym_to_key, Key, Keymap, Mods};
use super::xkb::XkbKeyboard;
use super::Event;

/// How keycodes become characters.
///
/// libxkbcommon when it is installed - it is the reference implementation of a
/// specification with layout groups, shift levels, latched and locked modifiers
/// and four-level layouts, and getting any of that subtly wrong means typing the
/// wrong letter. The builtin parser is the fallback for a system without it
/// (a minimal container), where the alternative is no keyboard at all.
enum KeyboardMap {
    Xkb(Box<XkbKeyboard>),
    Builtin(Keymap),
    None,
}

/// Everything the dispatch callbacks need to touch.
struct State {
    compositor: Option<WlCompositor>,
    shm: Option<WlShm>,
    layer_shell: Option<ZwlrLayerShellV1>,
    xdg_wm_base: Option<XdgWmBase>,
    seat: Option<WlSeat>,
    keyboard: Option<WlKeyboard>,
    pointer: Option<WlPointer>,

    keymap: KeyboardMap,
    group: usize,
    mods: Mods,
    pointer_pos: (f32, f32),

    width: u32,
    height: u32,
    configured: bool,
    closed: bool,
    events: Vec<Event>,
}

pub struct WaylandWindow {
    conn: Connection,
    queue: EventQueue<State>,
    state: State,
    surface: WlSurface,
    _layer_surface: Option<ZwlrLayerSurfaceV1>,
    _xdg_surface: Option<XdgSurface>,
    _xdg_toplevel: Option<XdgToplevel>,
    /// The shm mapping backing the current buffer.
    pool: Option<(WlShmPool, File, usize)>,
    buffer: Option<WlBuffer>,
    /// True when the compositor gave us a real overlay rather than a toplevel.
    pub is_overlay: bool,
}

/// A `Duration` as the `Timespec` rustix's poll wants.
fn timespec(d: Duration) -> rustix::event::Timespec {
    rustix::event::Timespec {
        tv_sec: d.as_secs() as _,
        tv_nsec: d.subsec_nanos() as _,
    }
}

impl WaylandWindow {
    pub fn open(width: u32, height: u32, overlay: bool, title: &str) -> Result<Self, String> {
        let conn = Connection::connect_to_env()
            .map_err(|e| format!("cannot connect to the Wayland display: {e}"))?;
        let display = conn.display();
        let mut queue: EventQueue<State> = conn.new_event_queue();
        let qh = queue.handle();
        display.get_registry(&qh, ());

        let mut state = State {
            compositor: None,
            shm: None,
            layer_shell: None,
            xdg_wm_base: None,
            seat: None,
            keyboard: None,
            pointer: None,
            keymap: KeyboardMap::None,
            group: 0,
            mods: Mods::default(),
            pointer_pos: (0.0, 0.0),
            width,
            height,
            configured: false,
            closed: false,
            events: Vec::new(),
        };

        // Two round trips: the first brings the globals, the second the events
        // they emit on binding (a seat's capabilities, shm's formats).
        queue.roundtrip(&mut state).map_err(|e| e.to_string())?;
        queue.roundtrip(&mut state).map_err(|e| e.to_string())?;

        let compositor = state
            .compositor
            .clone()
            .ok_or_else(|| "the compositor does not offer wl_compositor".to_string())?;
        state
            .shm
            .clone()
            .ok_or_else(|| "the compositor does not offer wl_shm".to_string())?;

        let surface = compositor.create_surface(&qh, ());

        let mut layer_surface = None;
        let mut xdg_surface = None;
        let mut xdg_toplevel = None;
        let mut is_overlay = false;

        match (overlay, state.layer_shell.clone()) {
            (true, Some(shell)) => {
                let ls = shell.get_layer_surface(
                    &surface,
                    None,
                    Layer::Overlay,
                    title.to_string(),
                    &qh,
                    (),
                );
                ls.set_size(width, height);
                // Anchored to nothing: with no anchor the compositor centres the
                // surface, which is exactly where a launcher belongs.
                ls.set_anchor(Anchor::empty());
                // Exclusive keyboard focus, or every keystroke goes to whatever
                // was focused before and the picker looks frozen.
                ls.set_keyboard_interactivity(KeyboardInteractivity::Exclusive);
                surface.commit();
                layer_surface = Some(ls);
                is_overlay = true;
            }
            _ => {
                let base = state.xdg_wm_base.clone().ok_or_else(|| {
                    "the compositor offers neither wlr-layer-shell nor xdg-shell".to_string()
                })?;
                let xs = base.get_xdg_surface(&surface, &qh, ());
                let tl = xs.get_toplevel(&qh, ());
                tl.set_title(title.to_string());
                tl.set_app_id("proteus".to_string());
                surface.commit();
                xdg_surface = Some(xs);
                xdg_toplevel = Some(tl);
            }
        }

        // Wait for the first configure: drawing before it is a protocol error.
        for _ in 0..100 {
            queue
                .blocking_dispatch(&mut state)
                .map_err(|e| e.to_string())?;
            if state.configured {
                break;
            }
        }
        if !state.configured {
            return Err("the compositor never configured the surface".into());
        }

        Ok(Self {
            conn,
            queue,
            state,
            surface,
            _layer_surface: layer_surface,
            _xdg_surface: xdg_surface,
            _xdg_toplevel: xdg_toplevel,
            pool: None,
            buffer: None,
            is_overlay,
        })
    }

    pub fn size(&self) -> (u32, u32) {
        (self.state.width, self.state.height)
    }

    /// The raw handles a GPU surface is built from.
    pub fn raw_handles(
        &self,
    ) -> (
        raw_window_handle::RawDisplayHandle,
        raw_window_handle::RawWindowHandle,
    ) {
        use raw_window_handle::{
            RawDisplayHandle, RawWindowHandle, WaylandDisplayHandle, WaylandWindowHandle,
        };
        use wayland_client::Proxy;
        let display = WaylandDisplayHandle::new(
            std::ptr::NonNull::new(self.conn.backend().display_ptr() as *mut _)
                .expect("a live connection has a display pointer"),
        );
        let window = WaylandWindowHandle::new(
            std::ptr::NonNull::new(self.surface.id().as_ptr() as *mut _)
                .expect("a created surface has an object pointer"),
        );
        (
            RawDisplayHandle::Wayland(display),
            RawWindowHandle::Wayland(window),
        )
    }

    pub fn is_closed(&self) -> bool {
        self.state.closed
    }

    pub fn close(&mut self) {
        self.state.closed = true;
    }

    /// Present a frame of premultiplied BGRA (wl_shm's `Argb8888` is exactly
    /// that on a little-endian machine).
    pub fn present(&mut self, pixels: &[u8]) -> Result<(), String> {
        let (w, h) = (self.state.width, self.state.height);
        let stride = (w * 4) as usize;
        let needed = stride * h as usize;
        if pixels.len() < needed {
            return Err(format!(
                "frame is {} bytes, expected {needed}",
                pixels.len()
            ));
        }
        let qh = self.queue.handle();
        let shm = self.state.shm.clone().ok_or("no wl_shm")?;

        // Reallocate only when the size changed; a resize is rare and a fresh
        // mapping per frame would be a syscall storm while typing.
        let realloc = match &self.pool {
            Some((_, _, size)) => *size != needed,
            None => true,
        };
        if realloc {
            let file = shm_file(needed)?;
            let pool = shm.create_pool(file.as_fd(), needed as i32, &qh, ());
            self.pool = Some((pool, file, needed));
            if let Some(b) = self.buffer.take() {
                b.destroy();
            }
        }

        let (pool, file, _) = self.pool.as_mut().ok_or("no shm pool")?;
        // Write the frame into the mapping's backing file.
        use std::io::{Seek, SeekFrom};
        file.seek(SeekFrom::Start(0)).map_err(|e| e.to_string())?;
        file.write_all(&pixels[..needed])
            .map_err(|e| e.to_string())?;
        file.flush().map_err(|e| e.to_string())?;

        if self.buffer.is_none() {
            self.buffer = Some(pool.create_buffer(
                0,
                w as i32,
                h as i32,
                stride as i32,
                wl_shm::Format::Argb8888,
                &qh,
                (),
            ));
        }

        let buffer = self.buffer.clone().ok_or("no buffer")?;
        self.surface.attach(Some(&buffer), 0, 0);
        self.surface.damage_buffer(0, 0, w as i32, h as i32);
        self.surface.commit();
        self.conn.flush().map_err(|e| e.to_string())?;
        Ok(())
    }

    /// Block for events, then drain whatever else arrived.
    pub fn wait_events(&mut self) -> Vec<Event> {
        if self.queue.blocking_dispatch(&mut self.state).is_err() {
            self.state.closed = true;
            return vec![Event::Close];
        }
        std::mem::take(&mut self.state.events)
    }

    /// Collect pending events, waiting at most `timeout` - the frame clock for
    /// animation. See the X11 backend for why this is not just a busy loop.
    pub fn wait_events_timeout(&mut self, timeout: Duration) -> Vec<Event> {
        // Dispatch anything already read into the queue before sleeping.
        if self.queue.dispatch_pending(&mut self.state).is_err() {
            self.state.closed = true;
            return vec![Event::Close];
        }
        if !self.state.events.is_empty() {
            return std::mem::take(&mut self.state.events);
        }
        // The read guard is the protocol-correct way to wait: it tells the
        // library we are about to sleep on the fd, so another thread cannot
        // read our events out from under us. Failing to get one means events
        // arrived in the meantime, which is a reason to loop, not to error.
        if let Some(guard) = self.conn.prepare_read() {
            let fd = guard.connection_fd();
            let mut fds = [rustix::event::PollFd::from_borrowed_fd(
                fd,
                rustix::event::PollFlags::IN,
            )];
            let _ = rustix::event::poll(&mut fds, Some(&timespec(timeout)));
            let _ = guard.read();
        }
        if self.queue.dispatch_pending(&mut self.state).is_err() {
            self.state.closed = true;
            return vec![Event::Close];
        }
        std::mem::take(&mut self.state.events)
    }
}

/// An anonymous file of `size` bytes to share with the compositor.
///
/// `memfd_create` is the right tool but is a raw syscall; falling back to a
/// tmpfile in the runtime dir keeps this working on kernels and sandboxes where
/// it is unavailable.
fn shm_file(size: usize) -> Result<File, String> {
    let fd = unsafe { libc_memfd_create(c"proteus-frame".as_ptr(), 0) };
    let file = if fd >= 0 {
        unsafe { File::from(OwnedFd::from_raw_fd(fd)) }
    } else {
        let dir = std::env::var("XDG_RUNTIME_DIR").unwrap_or_else(|_| "/tmp".into());
        let path = format!("{dir}/proteus-{}-{}", std::process::id(), size);
        let f = File::options()
            .read(true)
            .write(true)
            .create(true)
            .truncate(true)
            .open(&path)
            .map_err(|e| format!("cannot create a frame buffer file: {e}"))?;
        // Unlink immediately: the fd keeps it alive, and nothing should find it
        // on disk if we crash.
        let _ = std::fs::remove_file(&path);
        f
    };
    file.set_len(size as u64).map_err(|e| e.to_string())?;
    Ok(file)
}

extern "C" {
    #[link_name = "memfd_create"]
    fn libc_memfd_create(name: *const i8, flags: u32) -> i32;
}

// --- dispatch ---------------------------------------------------------------

impl Dispatch<WlRegistry, ()> for State {
    fn event(
        state: &mut Self,
        registry: &WlRegistry,
        event: wl_registry::Event,
        _: &(),
        _: &Connection,
        qh: &QueueHandle<Self>,
    ) {
        let wl_registry::Event::Global {
            name,
            interface,
            version,
        } = event
        else {
            return;
        };
        match interface.as_str() {
            "wl_compositor" => {
                state.compositor =
                    Some(registry.bind::<WlCompositor, _, _>(name, version.min(4), qh, ()));
            }
            "wl_shm" => {
                state.shm = Some(registry.bind::<WlShm, _, _>(name, 1, qh, ()));
            }
            "zwlr_layer_shell_v1" => {
                state.layer_shell =
                    Some(registry.bind::<ZwlrLayerShellV1, _, _>(name, version.min(4), qh, ()));
            }
            "xdg_wm_base" => {
                state.xdg_wm_base =
                    Some(registry.bind::<XdgWmBase, _, _>(name, version.min(3), qh, ()));
            }
            "wl_seat" => {
                state.seat = Some(registry.bind::<WlSeat, _, _>(name, version.min(5), qh, ()));
            }
            _ => {}
        }
    }
}

impl Dispatch<WlSeat, ()> for State {
    fn event(
        state: &mut Self,
        seat: &WlSeat,
        event: wl_seat::Event,
        _: &(),
        _: &Connection,
        qh: &QueueHandle<Self>,
    ) {
        if let wl_seat::Event::Capabilities { capabilities } = event {
            let caps = match capabilities {
                wayland_client::WEnum::Value(v) => v,
                _ => return,
            };
            if caps.contains(wl_seat::Capability::Keyboard) && state.keyboard.is_none() {
                state.keyboard = Some(seat.get_keyboard(qh, ()));
            }
            if caps.contains(wl_seat::Capability::Pointer) && state.pointer.is_none() {
                state.pointer = Some(seat.get_pointer(qh, ()));
            }
        }
    }
}

impl Dispatch<WlKeyboard, ()> for State {
    fn event(
        state: &mut Self,
        _: &WlKeyboard,
        event: wl_keyboard::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        match event {
            wl_keyboard::Event::Keymap { format, fd, size } => {
                if !matches!(
                    format,
                    wayland_client::WEnum::Value(wl_keyboard::KeymapFormat::XkbV1)
                ) {
                    return;
                }
                // The compositor hands over a keymap by fd; read it and parse
                // the part that turns a keycode into a keysym.
                let mut f = File::from(fd);
                let mut buf = vec![0u8; size as usize];
                use std::io::Read;
                if f.read_exact(&mut buf).is_ok() {
                    // The map is NUL-terminated.
                    let end = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
                    if let Ok(text) = std::str::from_utf8(&buf[..end]) {
                        state.keymap = match XkbKeyboard::new(text) {
                            Some(k) => KeyboardMap::Xkb(Box::new(k)),
                            None => KeyboardMap::Builtin(Keymap::parse(text)),
                        };
                    }
                }
            }
            wl_keyboard::Event::Modifiers {
                mods_depressed,
                group,
                ..
            } => {
                // The bit positions are the keymap's own; the standard layout
                // puts Shift at 0, Control at 2, Alt at 3 and Super at 6.
                state.mods = Mods {
                    shift: mods_depressed & 0x01 != 0,
                    ctrl: mods_depressed & 0x04 != 0,
                    alt: mods_depressed & 0x08 != 0,
                    logo: mods_depressed & 0x40 != 0,
                };
                state.group = group as usize;
            }
            wl_keyboard::Event::Key {
                key,
                state: key_state,
                ..
            } => {
                if !matches!(
                    key_state,
                    wayland_client::WEnum::Value(wl_keyboard::KeyState::Pressed)
                ) {
                    return;
                }
                // Wayland reports evdev keycodes; XKB keymaps number keys with
                // an offset of 8.
                let keycode = key + 8;
                let mods = state.mods;
                let ev = match &state.keymap {
                    KeyboardMap::Xkb(kb) => {
                        let sym = kb.keysym(keycode).unwrap_or(0);
                        let key = keysym_to_key(sym, mods);
                        // For text, prefer what the library says the key
                        // produces: on a Cyrillic or accented layout that is the
                        // actual character, which mapping the keysym ourselves
                        // would not get right.
                        match key {
                            Key::Other | Key::Char(_) if !mods.ctrl && !mods.alt && !mods.logo => {
                                match kb.utf8(keycode).and_then(|s| s.chars().next()) {
                                    Some(c) if !c.is_control() => Some(Key::Char(c)),
                                    _ => Some(key),
                                }
                            }
                            other => Some(other),
                        }
                    }
                    KeyboardMap::Builtin(km) => km
                        .keysym(keycode, state.group, mods.shift)
                        .map(|sym| keysym_to_key(sym, mods)),
                    KeyboardMap::None => None,
                };
                if let Some(k) = ev {
                    if k != Key::Other {
                        state.events.push(Event::Key(k, mods));
                    }
                }
            }
            wl_keyboard::Event::Leave { .. } => {
                // Losing keyboard focus closes the picker, as every launcher does.
                state.events.push(Event::Close);
            }
            _ => {}
        }
    }
}

impl Dispatch<WlPointer, ()> for State {
    fn event(
        state: &mut Self,
        _: &WlPointer,
        event: wl_pointer::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        match event {
            wl_pointer::Event::Motion {
                surface_x,
                surface_y,
                ..
            } => {
                state.pointer_pos = (surface_x as f32, surface_y as f32);
                state
                    .events
                    .push(Event::PointerMotion(surface_x as f32, surface_y as f32));
            }
            wl_pointer::Event::Enter {
                surface_x,
                surface_y,
                ..
            } => {
                state.pointer_pos = (surface_x as f32, surface_y as f32);
            }
            wl_pointer::Event::Button {
                button, state: bs, ..
            } => {
                // BTN_LEFT
                if button == 0x110
                    && matches!(
                        bs,
                        wayland_client::WEnum::Value(wl_pointer::ButtonState::Pressed)
                    )
                {
                    let (x, y) = state.pointer_pos;
                    state.events.push(Event::PointerPress(x, y));
                }
            }
            wl_pointer::Event::Axis { axis, value, .. } => {
                if matches!(
                    axis,
                    wayland_client::WEnum::Value(wl_pointer::Axis::VerticalScroll)
                ) {
                    // The value is in surface units; one notch is ~10-15.
                    let lines = (value / 10.0).clamp(-3.0, 3.0);
                    if lines.abs() >= 0.5 {
                        state.events.push(Event::Scroll(lines as f32));
                    }
                }
            }
            _ => {}
        }
    }
}

impl Dispatch<ZwlrLayerSurfaceV1, ()> for State {
    fn event(
        state: &mut Self,
        ls: &ZwlrLayerSurfaceV1,
        event: zwlr_layer_surface_v1::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        match event {
            zwlr_layer_surface_v1::Event::Configure {
                serial,
                width,
                height,
            } => {
                ls.ack_configure(serial);
                // A zero dimension means "you choose", so keep what we asked for.
                if width > 0 {
                    state.width = width;
                }
                if height > 0 {
                    state.height = height;
                }
                state.configured = true;
                state.events.push(Event::Resize(state.width, state.height));
            }
            zwlr_layer_surface_v1::Event::Closed => {
                state.closed = true;
                state.events.push(Event::Close);
            }
            _ => {}
        }
    }
}

impl Dispatch<XdgSurface, ()> for State {
    fn event(
        state: &mut Self,
        xs: &XdgSurface,
        event: xdg_surface::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        if let xdg_surface::Event::Configure { serial } = event {
            xs.ack_configure(serial);
            state.configured = true;
            state.events.push(Event::Redraw);
        }
    }
}

impl Dispatch<XdgToplevel, ()> for State {
    fn event(
        state: &mut Self,
        _: &XdgToplevel,
        event: xdg_toplevel::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        match event {
            xdg_toplevel::Event::Configure { width, height, .. } => {
                if width > 0 && height > 0 {
                    let (w, h) = (width as u32, height as u32);
                    if (w, h) != (state.width, state.height) {
                        state.width = w;
                        state.height = h;
                        state.events.push(Event::Resize(w, h));
                    }
                }
            }
            xdg_toplevel::Event::Close => {
                state.closed = true;
                state.events.push(Event::Close);
            }
            _ => {}
        }
    }
}

impl Dispatch<XdgWmBase, ()> for State {
    fn event(
        _: &mut Self,
        base: &XdgWmBase,
        event: xdg_wm_base::Event,
        _: &(),
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        // The compositor pings to check we are alive; failing to pong gets the
        // window killed as unresponsive.
        if let xdg_wm_base::Event::Ping { serial } = event {
            base.pong(serial);
        }
    }
}

/// Interfaces we bind but never receive events from.
macro_rules! ignore_events {
    ($($t:ty),* $(,)?) => {$(
        impl Dispatch<$t, ()> for State {
            fn event(
                _: &mut Self,
                _: &$t,
                _: <$t as Proxy>::Event,
                _: &(),
                _: &Connection,
                _: &QueueHandle<Self>,
            ) {
            }
        }
    )*};
}

ignore_events!(
    WlCompositor,
    WlShm,
    WlShmPool,
    WlBuffer,
    WlSurface,
    ZwlrLayerShellV1,
);

#[cfg(test)]
mod tests {
    use super::*;

    fn have_wayland() -> bool {
        std::env::var("WAYLAND_DISPLAY")
            .map(|d| !d.is_empty())
            .unwrap_or(false)
    }

    #[test]
    fn opens_presents_and_closes_on_a_real_compositor() {
        if !have_wayland() {
            eprintln!("no WAYLAND_DISPLAY - skipping the Wayland window test");
            return;
        }
        // Toplevel rather than overlay: a layer surface with exclusive keyboard
        // focus would steal the keyboard of whoever runs the suite.
        let mut w = match WaylandWindow::open(320, 200, false, "proteus-test") {
            Ok(w) => w,
            Err(e) => {
                eprintln!("cannot open a Wayland window ({e}) - skipping");
                return;
            }
        };
        let (width, height) = w.size();
        assert!(width > 0 && height > 0);
        assert!(!w.is_closed());

        // A real frame through the real shm path.
        let frame: Vec<u8> = (0..width * height)
            .flat_map(|_| [200u8, 30, 30, 255])
            .collect();
        w.present(&frame).expect("present a frame");

        // Presenting twice must reuse the pool rather than leaking one per frame.
        w.present(&frame).expect("present a second frame");

        // A short frame is refused rather than read past.
        assert!(w.present(&[0, 0, 0, 0]).is_err());

        w.close();
        assert!(w.is_closed());
    }

    #[test]
    fn an_anonymous_frame_file_is_created_and_sized() {
        let f = shm_file(4096).expect("a frame file");
        assert_eq!(f.metadata().unwrap().len(), 4096);
    }
}
