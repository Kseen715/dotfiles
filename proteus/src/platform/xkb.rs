//! Wayland keyboard state, through libxkbcommon.
//!
//! A Wayland compositor hands the client an XKB keymap and expects it to resolve
//! keycodes itself. Doing that properly means honouring layout groups, shift
//! levels, modifier consumption, latched and locked modifiers, and layouts that
//! reach four levels deep — which is a lot of specification to reimplement, and
//! reimplementing it is how a picker ends up typing the wrong letter on a
//! Russian or Neo layout.
//!
//! So libxkbcommon does it. It is loaded with `dlopen` rather than linked, so
//! the binary still builds anywhere and still runs on a system that has not got
//! it — the caller falls back to [`super::keys::Keymap`] there, which handles
//! the common single-group case. Every Wayland desktop ships libxkbcommon (the
//! compositors themselves need it), so the fallback is for minimal containers,
//! not for real machines.

use std::ffi::CString;
use std::os::raw::c_char;
use std::ptr::NonNull;

use xkbcommon_dl::{
    xkb_context, xkb_context_flags, xkb_keymap, xkb_keymap_compile_flags, xkb_state,
    xkb_state_component, XkbCommon,
};

/// The dlopen'd library, or `None` when it is not installed.
///
/// Loaded once: `dlopen` is not cheap and a picker opens on a key press.
fn lib() -> Option<&'static XkbCommon> {
    use std::sync::OnceLock;
    static LIB: OnceLock<Option<XkbCommon>> = OnceLock::new();
    LIB.get_or_init(|| {
        // SAFETY: dlopen of a well-known system library by soname. The symbols
        // dlib resolves are the ones declared in xkbcommon-dl's binding, which
        // matches the library's stable ABI. The versioned soname is tried first
        // because the unversioned one only exists with a -dev package.
        unsafe {
            XkbCommon::open("libxkbcommon.so.0")
                .or_else(|_| XkbCommon::open("libxkbcommon.so"))
                .ok()
        }
    })
    .as_ref()
}

/// Whether libxkbcommon is available on this system.
pub fn available() -> bool {
    lib().is_some()
}

/// A compiled keymap plus the state that tracks modifiers and layout.
pub struct XkbKeyboard {
    context: NonNull<xkb_context>,
    keymap: NonNull<xkb_keymap>,
    state: NonNull<xkb_state>,
}

impl XkbKeyboard {
    /// Compile the keymap text a compositor sent. `None` when libxkbcommon is
    /// absent or the keymap does not compile.
    pub fn new(keymap_text: &str) -> Option<Self> {
        let lib = lib()?;
        let text = CString::new(keymap_text).ok()?;
        // SAFETY: every pointer below is checked before use, and each object is
        // owned by this struct and freed exactly once in Drop.
        unsafe {
            let context = NonNull::new((lib.xkb_context_new)(
                xkb_context_flags::XKB_CONTEXT_NO_FLAGS,
            ))?;
            let keymap = match NonNull::new((lib.xkb_keymap_new_from_string)(
                context.as_ptr(),
                text.as_ptr() as *const c_char,
                xkbcommon_dl::xkb_keymap_format::XKB_KEYMAP_FORMAT_TEXT_V1,
                xkb_keymap_compile_flags::XKB_KEYMAP_COMPILE_NO_FLAGS,
            )) {
                Some(k) => k,
                None => {
                    (lib.xkb_context_unref)(context.as_ptr());
                    return None;
                }
            };
            let state = match NonNull::new((lib.xkb_state_new)(keymap.as_ptr())) {
                Some(s) => s,
                None => {
                    (lib.xkb_keymap_unref)(keymap.as_ptr());
                    (lib.xkb_context_unref)(context.as_ptr());
                    return None;
                }
            };
            Some(Self {
                context,
                keymap,
                state,
            })
        }
    }

    /// Feed the modifier state the compositor reported.
    ///
    /// All four masks matter: depressed is the obvious one, but latched and
    /// locked are how Caps Lock and sticky keys work, and skipping them makes
    /// Caps Lock silently do nothing.
    pub fn update_modifiers(&mut self, depressed: u32, latched: u32, locked: u32, group: u32) {
        let Some(lib) = lib() else { return };
        // SAFETY: `state` is a live object owned by self.
        unsafe {
            (lib.xkb_state_update_mask)(
                self.state.as_ptr(),
                depressed,
                latched,
                locked,
                0,
                0,
                group,
            );
        }
    }

    /// The keysym a keycode produces in the current state.
    ///
    /// The keycode is the Wayland one (evdev + 8), which is what XKB numbers
    /// keys by.
    pub fn keysym(&self, keycode: u32) -> Option<u32> {
        let lib = lib()?;
        // SAFETY: as above.
        let sym = unsafe { (lib.xkb_state_key_get_one_sym)(self.state.as_ptr(), keycode) };
        (sym != 0).then_some(sym)
    }

    /// The text a keycode produces, honouring the layout and modifiers.
    ///
    /// Preferred over mapping the keysym ourselves: this is what makes a
    /// Cyrillic or accented layout type the right character rather than the
    /// keysym's Latin name.
    pub fn utf8(&self, keycode: u32) -> Option<String> {
        let lib = lib()?;
        // SAFETY: the buffer is sized from the length the library reports, and
        // the second call writes at most that many bytes plus a NUL.
        unsafe {
            let len =
                (lib.xkb_state_key_get_utf8)(self.state.as_ptr(), keycode, std::ptr::null_mut(), 0);
            if len <= 0 {
                return None;
            }
            let mut buf = vec![0u8; len as usize + 1];
            (lib.xkb_state_key_get_utf8)(
                self.state.as_ptr(),
                keycode,
                buf.as_mut_ptr() as *mut c_char,
                buf.len(),
            );
            buf.truncate(len as usize);
            String::from_utf8(buf).ok().filter(|s| !s.is_empty())
        }
    }

    /// Whether a named modifier is active right now.
    pub fn mod_active(&self, name: &str) -> bool {
        let Some(lib) = lib() else { return false };
        let Ok(cname) = CString::new(name) else {
            return false;
        };
        // SAFETY: `cname` outlives the call.
        unsafe {
            (lib.xkb_state_mod_name_is_active)(
                self.state.as_ptr(),
                cname.as_ptr() as *const c_char,
                xkb_state_component::XKB_STATE_MODS_EFFECTIVE,
            ) > 0
        }
    }
}

impl Drop for XkbKeyboard {
    fn drop(&mut self) {
        let Some(lib) = lib() else { return };
        // SAFETY: each object is freed once, in the reverse of creation order.
        unsafe {
            (lib.xkb_state_unref)(self.state.as_ptr());
            (lib.xkb_keymap_unref)(self.keymap.as_ptr());
            (lib.xkb_context_unref)(self.context.as_ptr());
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A minimal but real keymap, in the format a compositor sends.
    const KEYMAP: &str = r#"xkb_keymap {
xkb_keycodes "test" {
    minimum = 8;
    maximum = 255;
    <ESC> = 9;
    <AD01> = 24;
    <RTRN> = 36;
};
xkb_types "test" { include "basic" };
xkb_compat "test" { include "basic" };
xkb_symbols "test" {
    key <ESC> { [ Escape ] };
    key <AD01> { [ q, Q ] };
    key <RTRN> { [ Return ] };
};
};
"#;

    #[test]
    fn compiles_a_keymap_when_the_library_is_present() {
        if !available() {
            eprintln!("libxkbcommon not installed - skipping (the builtin parser covers this)");
            return;
        }
        let kb = XkbKeyboard::new(KEYMAP).expect("a valid keymap should compile");
        assert_eq!(kb.keysym(24), Some(0x71), "keycode 24 is 'q'");
        assert_eq!(kb.utf8(24).as_deref(), Some("q"));
        assert_eq!(kb.keysym(9), Some(0xff1b), "keycode 9 is Escape");
        // Escape produces a control character, not text worth inserting.
        assert_eq!(kb.keysym(36), Some(0xff0d), "keycode 36 is Return");
    }

    #[test]
    fn rejects_a_keymap_that_does_not_compile() {
        if !available() {
            eprintln!("libxkbcommon not installed - skipping");
            return;
        }
        assert!(XkbKeyboard::new("this is not a keymap").is_none());
        assert!(XkbKeyboard::new("").is_none());
    }

    #[test]
    fn shift_selects_the_second_level() {
        if !available() {
            eprintln!("libxkbcommon not installed - skipping");
            return;
        }
        let mut kb = XkbKeyboard::new(KEYMAP).expect("keymap compiles");
        assert_eq!(kb.utf8(24).as_deref(), Some("q"));
        // Shift is modifier index 0 in the standard set, so mask bit 0.
        kb.update_modifiers(1, 0, 0, 0);
        assert_eq!(
            kb.utf8(24).as_deref(),
            Some("Q"),
            "Shift must reach level 2"
        );
        kb.update_modifiers(0, 0, 0, 0);
        assert_eq!(
            kb.utf8(24).as_deref(),
            Some("q"),
            "and releasing it goes back"
        );
    }

    #[test]
    fn availability_is_a_question_not_a_crash() {
        // Whatever the answer, asking must not panic - this runs on machines
        // with and without the library.
        let _ = available();
    }
}
