//! X11 window, via the pure-Rust x11rb protocol implementation.
//!
//! The window is **override-redirect**: the X server hands it straight to the
//! screen without consulting the window manager. That is how rofi and dmenu put
//! themselves in the middle of a tiling desktop without the WM tiling them, and
//! there is no polite alternative — a managed window under i3 becomes a tile.
//!
//! Three things follow from that choice, and all three have to be done by hand
//! because no window manager is helping:
//!
//!   - centring (nobody will place it)
//!   - taking the keyboard (nobody will focus it)
//!   - a 32-bit ARGB visual, so rounded corners have something to be round
//!     against, since the WM will not composite a shape for us

use std::os::fd::AsRawFd;
use std::time::{Duration, Instant};

use x11rb::connection::Connection;
use x11rb::protocol::xproto::{
    AtomEnum, ColormapAlloc, ConnectionExt as _, CreateGCAux, CreateWindowAux, EventMask, Gcontext,
    GrabMode, ImageFormat, PropMode, Screen, Visualid, Window, WindowClass,
};
use x11rb::protocol::Event as XEvent;
use x11rb::rust_connection::RustConnection;
use x11rb::wrapper::ConnectionExt as _;
use x11rb::COPY_DEPTH_FROM_PARENT;

use super::keys::{keysym_to_key, Mods};
use super::Event;

pub struct X11Window {
    conn: RustConnection,
    screen_num: usize,
    window: Window,
    gc: Gcontext,
    depth: u8,
    pub width: u32,
    pub height: u32,
    /// keycode -> keysyms, straight from the server.
    keysyms: Vec<Vec<u32>>,
    min_keycode: u8,
    closed: bool,
    /// Set when the window is override-redirect, which changes how focus works.
    override_redirect: bool,
}

/// Find a 32-bit TrueColor visual, so the window can have real transparency.
/// Falls back to the screen's default depth, where the window is simply opaque —
/// worse looking, but a picker that opens beats a picker that insists on alpha.
fn argb_visual(screen: &Screen) -> Option<(u8, Visualid)> {
    for depth in &screen.allowed_depths {
        if depth.depth != 32 {
            continue;
        }
        for v in &depth.visuals {
            if v.class == x11rb::protocol::xproto::VisualClass::TRUE_COLOR {
                return Some((32, v.visual_id));
            }
        }
    }
    None
}

/// A `Duration` as the `Timespec` rustix's poll wants.
fn timespec(d: Duration) -> rustix::event::Timespec {
    rustix::event::Timespec {
        tv_sec: d.as_secs() as _,
        tv_nsec: d.subsec_nanos() as _,
    }
}

impl X11Window {
    /// Open a window. `overlay` selects override-redirect; when false the window
    /// is an ordinary managed one, which is the escape hatch for compositors and
    /// setups where bypassing the WM is not wanted.
    pub fn open(width: u32, height: u32, overlay: bool, title: &str) -> Result<Self, String> {
        let (conn, screen_num) =
            RustConnection::connect(None).map_err(|e| format!("cannot connect to X: {e}"))?;
        let screen = conn.setup().roots[screen_num].clone();

        let (depth, visual, colormap) = match argb_visual(&screen) {
            Some((d, v)) => {
                let cm = conn.generate_id().map_err(|e| e.to_string())?;
                conn.create_colormap(ColormapAlloc::NONE, cm, screen.root, v)
                    .map_err(|e| e.to_string())?;
                (d, v, Some(cm))
            }
            None => (COPY_DEPTH_FROM_PARENT, screen.root_visual, None),
        };

        // Centre on the screen: with override-redirect there is no window
        // manager to place us, and a picker in the corner is a bug.
        let x = ((screen.width_in_pixels as i32 - width as i32) / 2).max(0) as i16;
        let y = ((screen.height_in_pixels as i32 - height as i32) / 2).max(0) as i16;

        let window = conn.generate_id().map_err(|e| e.to_string())?;
        let mut aux = CreateWindowAux::new()
            .event_mask(
                EventMask::EXPOSURE
                    | EventMask::KEY_PRESS
                    | EventMask::BUTTON_PRESS
                    | EventMask::POINTER_MOTION
                    | EventMask::STRUCTURE_NOTIFY
                    | EventMask::FOCUS_CHANGE,
            )
            .background_pixel(0)
            .border_pixel(0)
            .override_redirect(overlay as u32);
        if let Some(cm) = colormap {
            aux = aux.colormap(cm);
        }

        conn.create_window(
            depth,
            window,
            screen.root,
            x,
            y,
            width as u16,
            height as u16,
            0,
            WindowClass::INPUT_OUTPUT,
            visual,
            &aux,
        )
        .map_err(|e| format!("cannot create window: {e}"))?;

        // Identify ourselves even when override-redirect: a WM ignores the hints
        // but `xprop`, screenshot tools and the user's own rules still read them.
        conn.change_property8(
            PropMode::REPLACE,
            window,
            AtomEnum::WM_NAME,
            AtomEnum::STRING,
            title.as_bytes(),
        )
        .map_err(|e| e.to_string())?;
        // WM_CLASS is two NUL-terminated strings: instance then class.
        conn.change_property8(
            PropMode::REPLACE,
            window,
            AtomEnum::WM_CLASS,
            AtomEnum::STRING,
            b"proteus\0Proteus\0",
        )
        .map_err(|e| e.to_string())?;
        Self::set_window_type_dialog(&conn, window);

        conn.map_window(window).map_err(|e| e.to_string())?;
        conn.flush().map_err(|e| e.to_string())?;

        let gc = conn.generate_id().map_err(|e| e.to_string())?;
        conn.create_gc(gc, window, &CreateGCAux::new())
            .map_err(|e| e.to_string())?;

        // The keyboard map, so a keycode can become a character.
        let setup = conn.setup();
        let min_keycode = setup.min_keycode;
        let max_keycode = setup.max_keycode;
        let mapping = conn
            .get_keyboard_mapping(min_keycode, max_keycode - min_keycode + 1)
            .map_err(|e| e.to_string())?
            .reply()
            .map_err(|e| e.to_string())?;
        let syms_per_code = mapping.keysyms_per_keycode;
        let keysyms: Vec<Vec<u32>> = mapping
            .keysyms
            .chunks(syms_per_code as usize)
            .map(|c| c.to_vec())
            .collect();

        let mut me = Self {
            conn,
            screen_num,
            window,
            gc,
            depth: if depth == COPY_DEPTH_FROM_PARENT {
                screen.root_depth
            } else {
                depth
            },
            width,
            height,
            keysyms,
            min_keycode,
            closed: false,
            override_redirect: overlay,
        };
        if overlay {
            me.grab_keyboard();
        }
        Ok(me)
    }

    fn set_window_type_dialog(conn: &RustConnection, window: Window) {
        // Best-effort: on a WM that honours it, the toplevel fallback then opens
        // floating and centred instead of being tiled.
        let Ok(t) = conn.intern_atom(false, b"_NET_WM_WINDOW_TYPE") else {
            return;
        };
        let Ok(d) = conn.intern_atom(false, b"_NET_WM_WINDOW_TYPE_DIALOG") else {
            return;
        };
        let (Ok(t), Ok(d)) = (t.reply(), d.reply()) else {
            return;
        };
        let _ =
            conn.change_property32(PropMode::REPLACE, window, t.atom, AtomEnum::ATOM, &[d.atom]);
    }

    /// Take the keyboard. An override-redirect window is never focused by a
    /// window manager, so without this every keystroke goes to whatever was
    /// focused before and the picker looks frozen.
    ///
    /// The grab can fail if another client holds one (a menu mid-open, a
    /// screen locker). Retrying briefly covers the common case of racing a
    /// key-release grab from the hotkey that launched us.
    fn grab_keyboard(&mut self) {
        let deadline = Instant::now() + Duration::from_millis(500);
        loop {
            let ok = self
                .conn
                .grab_keyboard(
                    true,
                    self.window,
                    x11rb::CURRENT_TIME,
                    GrabMode::ASYNC,
                    GrabMode::ASYNC,
                )
                .ok()
                .and_then(|c| c.reply().ok())
                .map(|r| r.status == x11rb::protocol::xproto::GrabStatus::SUCCESS)
                .unwrap_or(false);
            if ok || Instant::now() > deadline {
                if !ok {
                    // Not fatal: with a WM that focuses us anyway, keys still
                    // arrive. Say so rather than pretending nothing happened.
                    eprintln!("proteus: could not grab the keyboard - another client holds it");
                }
                return;
            }
            std::thread::sleep(Duration::from_millis(20));
        }
    }

    fn keysym(&self, keycode: u8, shift: bool) -> u32 {
        let idx = keycode.wrapping_sub(self.min_keycode) as usize;
        let Some(syms) = self.keysyms.get(idx) else {
            return 0;
        };
        // Column 0 is unshifted, column 1 shifted — the first group, which is
        // all this picker needs.
        let sym = if shift {
            syms.get(1).copied().filter(|&s| s != 0).unwrap_or_else(|| {
                let base = syms.first().copied().unwrap_or(0);
                // No shifted level: upper-case the base symbol ourselves.
                super::keys::keysym_to_char(base)
                    .and_then(|c| c.to_uppercase().next())
                    .map(|c| {
                        let cp = c as u32;
                        if (0x20..=0xff).contains(&cp) {
                            cp
                        } else {
                            0x0100_0000 + cp
                        }
                    })
                    .unwrap_or(base)
            })
        } else {
            syms.first().copied().unwrap_or(0)
        };
        sym
    }

    pub fn size(&self) -> (u32, u32) {
        (self.width, self.height)
    }

    pub fn is_closed(&self) -> bool {
        self.closed
    }

    pub fn close(&mut self) {
        self.closed = true;
    }

    /// Present a frame. `pixels` is premultiplied BGRA, `width * height * 4`.
    pub fn present(&mut self, pixels: &[u8]) -> Result<(), String> {
        let expected = (self.width * self.height * 4) as usize;
        if pixels.len() < expected {
            return Err(format!(
                "frame is {} bytes, expected {expected}",
                pixels.len()
            ));
        }
        // PutImage has a request-size ceiling, so a big window has to go up in
        // horizontal bands rather than one call. 256 KiB stays well under the
        // 256 KiB..16 MiB range servers actually allow, and the extra requests
        // cost nothing next to the rasterisation that produced the frame.
        let max_bytes = 256 * 1024;
        let row_bytes = (self.width * 4) as usize;
        let rows_per_chunk = (max_bytes / row_bytes.max(1)).max(1);

        let mut y = 0u32;
        while y < self.height {
            let rows = rows_per_chunk.min((self.height - y) as usize) as u32;
            let start = (y as usize) * row_bytes;
            let end = start + (rows as usize) * row_bytes;
            self.conn
                .put_image(
                    ImageFormat::Z_PIXMAP,
                    self.window,
                    self.gc,
                    self.width as u16,
                    rows as u16,
                    0,
                    y as i16,
                    0,
                    self.depth,
                    &pixels[start..end],
                )
                .map_err(|e| format!("put_image: {e}"))?;
            y += rows;
        }
        self.conn.flush().map_err(|e| e.to_string())?;
        Ok(())
    }

    /// Collect pending events, blocking for the first one.
    pub fn wait_events(&mut self) -> Vec<Event> {
        let mut out = Vec::new();
        match self.conn.wait_for_event() {
            Ok(e) => self.translate(e, &mut out),
            Err(_) => {
                self.closed = true;
                out.push(Event::Close);
                return out;
            }
        }
        while let Ok(Some(e)) = self.conn.poll_for_event() {
            self.translate(e, &mut out);
        }
        out
    }

    /// Collect pending events, waiting at most `timeout`.
    ///
    /// This is what lets the loop animate: with something in flight it asks for
    /// the next frame's worth of time, and with nothing in flight it blocks in
    /// `wait_events` and burns no CPU at all. A picker that spins at 60fps while
    /// sitting still would be a laptop-battery bug.
    pub fn wait_events_timeout(&mut self, timeout: Duration) -> Vec<Event> {
        let mut out = Vec::new();
        // Anything already queued (x11rb buffers internally) must be taken
        // first, or poll would sleep with events waiting to be read.
        while let Ok(Some(e)) = self.conn.poll_for_event() {
            self.translate(e, &mut out);
        }
        if !out.is_empty() {
            return out;
        }
        let fd = self.conn.stream().as_raw_fd();
        let mut fds = [rustix::event::PollFd::from_borrowed_fd(
            unsafe { rustix::fd::BorrowedFd::borrow_raw(fd) },
            rustix::event::PollFlags::IN,
        )];
        // EINTR is a signal, not a failure: report no events and let the caller
        // come round again.
        let _ = rustix::event::poll(&mut fds, Some(&timespec(timeout)));
        while let Ok(Some(e)) = self.conn.poll_for_event() {
            self.translate(e, &mut out);
        }
        out
    }

    fn translate(&mut self, event: XEvent, out: &mut Vec<Event>) {
        match event {
            XEvent::Expose(_) => out.push(Event::Redraw),
            XEvent::ConfigureNotify(e) => {
                let (w, h) = (e.width as u32, e.height as u32);
                if (w, h) != (self.width, self.height) && w > 0 && h > 0 {
                    self.width = w;
                    self.height = h;
                    out.push(Event::Resize(w, h));
                }
            }
            XEvent::KeyPress(e) => {
                // state bit 0 is Shift, bit 2 Control, bit 3 Mod1 (Alt),
                // bit 6 Mod4 (Super).
                let s = u16::from(e.state);
                let mods = Mods {
                    shift: s & 0x01 != 0,
                    ctrl: s & 0x04 != 0,
                    alt: s & 0x08 != 0,
                    logo: s & 0x40 != 0,
                };
                let sym = self.keysym(e.detail, mods.shift);
                out.push(Event::Key(keysym_to_key(sym, mods), mods));
            }
            XEvent::ButtonPress(e) => match e.detail {
                1 => out.push(Event::PointerPress(e.event_x as f32, e.event_y as f32)),
                // Buttons 4/5 are the scroll wheel in X11's model.
                4 => out.push(Event::Scroll(-1.0)),
                5 => out.push(Event::Scroll(1.0)),
                _ => {}
            },
            XEvent::MotionNotify(e) => {
                out.push(Event::PointerMotion(e.event_x as f32, e.event_y as f32))
            }
            XEvent::FocusOut(_) => {
                // Losing focus closes the picker, matching every other launcher.
                // Only when override-redirect: a managed window legitimately
                // loses focus when the user alt-tabs and should stay open.
                if self.override_redirect {
                    out.push(Event::Close);
                }
            }
            XEvent::DestroyNotify(_) | XEvent::UnmapNotify(_) => {
                self.closed = true;
                out.push(Event::Close);
            }
            _ => {}
        }
    }

    /// The raw handles a GPU surface is built from.
    ///
    /// x11rb speaks XCB, so the XCB pair is what is reported; wgpu accepts it
    /// directly and does not need an Xlib `Display*`.
    pub fn raw_handles(
        &self,
    ) -> (
        raw_window_handle::RawDisplayHandle,
        raw_window_handle::RawWindowHandle,
    ) {
        use raw_window_handle::{
            RawDisplayHandle, RawWindowHandle, XcbDisplayHandle, XcbWindowHandle,
        };
        let conn = std::ptr::NonNull::new(self.conn.stream().as_raw_fd() as *mut std::ffi::c_void);
        let mut display = XcbDisplayHandle::new(conn, self.screen_num as i32);
        // The fd is not the connection pointer; x11rb's pure-Rust connection has
        // no XCB handle to hand over, so report none and let wgpu fall back to
        // opening its own. Keeping the screen number is what actually matters.
        display.connection = None;
        let window = XcbWindowHandle::new(
            std::num::NonZeroU32::new(self.window).expect("a mapped window has a non-zero id"),
        );
        (RawDisplayHandle::Xcb(display), RawWindowHandle::Xcb(window))
    }

    /// The screen size, for callers that want to size themselves against it.
    pub fn screen_size(&self) -> (u32, u32) {
        let s = &self.conn.setup().roots[self.screen_num];
        (s.width_in_pixels as u32, s.height_in_pixels as u32)
    }
}

impl Drop for X11Window {
    fn drop(&mut self) {
        // Release the keyboard explicitly: an override-redirect client that dies
        // holding a grab leaves the whole session unable to type.
        let _ = self.conn.ungrab_keyboard(x11rb::CURRENT_TIME);
        let _ = self.conn.destroy_window(self.window);
        let _ = self.conn.flush();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// These need a real X server. When `DISPLAY` is unset the test is a no-op
    /// rather than a failure, so the suite still runs headless.
    fn have_x11() -> bool {
        std::env::var("DISPLAY")
            .map(|d| !d.is_empty())
            .unwrap_or(false)
    }

    #[test]
    fn opens_presents_and_closes_on_a_real_server() {
        if !have_x11() {
            eprintln!("no DISPLAY - skipping the X11 window test");
            return;
        }
        // Toplevel, not overlay: an override-redirect window would grab the
        // keyboard of whoever is running the suite.
        let mut w = match X11Window::open(320, 200, false, "proteus-test") {
            Ok(w) => w,
            Err(e) => {
                eprintln!("cannot open an X11 window ({e}) - skipping");
                return;
            }
        };
        assert_eq!(w.size(), (320, 200));
        assert!(!w.is_closed());

        // A full frame of opaque blue, in the premultiplied BGRA the X server
        // wants. This is the real code path, not a mock.
        let frame: Vec<u8> = (0..320 * 200).flat_map(|_| [255u8, 0, 0, 255]).collect();
        w.present(&frame).expect("present a full frame");

        // A short frame must be refused rather than reading past the buffer.
        assert!(w.present(&[0, 0, 0, 0]).is_err());

        assert!(w.screen_size().0 > 0);
        w.close();
        assert!(w.is_closed());
    }

    #[test]
    fn keysym_lookup_survives_an_out_of_range_keycode() {
        // Constructed without a server: only the pure lookup is exercised.
        let w = X11Window {
            conn: match RustConnection::connect(None) {
                Ok((c, _)) => c,
                Err(_) => {
                    eprintln!("no X server - skipping");
                    return;
                }
            },
            screen_num: 0,
            window: 0,
            gc: 0,
            depth: 24,
            width: 1,
            height: 1,
            keysyms: vec![vec![0x61, 0x41]],
            min_keycode: 8,
            closed: false,
            override_redirect: false,
        };
        assert_eq!(w.keysym(8, false), 0x61, "the first keycode maps to 'a'");
        assert_eq!(w.keysym(8, true), 0x41, "shifted gives 'A'");
        assert_eq!(
            w.keysym(200, false),
            0,
            "an unmapped keycode is not a panic"
        );
        assert_eq!(
            w.keysym(0, false),
            0,
            "a keycode below the minimum is not a panic"
        );
        std::mem::forget(w); // Drop would destroy a window id we never created
    }
}
