//! Windowing: one window, two display servers.
//!
//! Neither backend goes through a toolkit. That is not purism — it is the only
//! way to get the overlay behaviour a rofi-style picker needs: a Wayland
//! layer-shell surface and an X11 override-redirect window are both outside
//! what a portable windowing crate exposes.
//!
//! Both backends produce the same [`Event`]s and accept the same pixels, so
//! everything above this module is written once.

pub mod keys;
pub mod wayland;
pub mod x11;
pub mod xkb;

pub use keys::{Key, Mods};

/// What the window reports back to the application.
#[derive(Debug, Clone, PartialEq)]
pub enum Event {
    /// A key was pressed.
    Key(Key, Mods),
    /// The surface changed size; the argument is physical pixels.
    Resize(u32, u32),
    /// A redraw is needed.
    Redraw,
    /// The pointer moved to this position, in logical pixels.
    PointerMotion(f32, f32),
    /// The primary button was pressed at this position.
    PointerPress(f32, f32),
    /// Scroll, in lines (positive is down).
    Scroll(f32),
    /// The window should close (compositor asked, or focus was lost).
    Close,
}
