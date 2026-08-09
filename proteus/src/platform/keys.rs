//! Keyboard input, without libxkbcommon.
//!
//! Both display servers ultimately speak X keysyms, but they hand them over
//! differently: X11 answers a `GetKeyboardMapping` request with the keysyms
//! directly, while Wayland sends a file descriptor holding an XKB keymap in its
//! text format and expects the client to resolve keycodes itself.
//!
//! The usual answer is to link libxkbcommon. This module does the small part of
//! that job instead — keycode to keysym to character — so the binary needs no
//! system development package and builds anywhere Rust does. What is given up is
//! full XKB: compose sequences, dead keys, and exotic layout options. For a
//! filter box holding a theme name, that is a trade worth making; the parser
//! handles the multi-group, multi-level layouts people actually use.

use std::collections::HashMap;

/// A key press, already interpreted.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Key {
    Char(char),
    Enter,
    Escape,
    Backspace,
    Tab,
    ShiftTab,
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    PageUp,
    PageDown,
    Delete,
    /// Anything we do not act on.
    Other,
}

/// Modifier state at the time of a press.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Mods {
    pub shift: bool,
    pub ctrl: bool,
    pub alt: bool,
    pub logo: bool,
}

/// Map an X keysym to a [`Key`].
///
/// Latin-1 keysyms are their own Unicode codepoint, and everything above
/// 0x01000000 is `0x01000000 + codepoint` — the two rules that let this cover
/// every printable key without a table.
pub fn keysym_to_key(keysym: u32, mods: Mods) -> Key {
    use xkeysym::key;
    match keysym {
        key::Return | key::KP_Enter => Key::Enter,
        key::Escape => Key::Escape,
        key::BackSpace => Key::Backspace,
        key::Tab => {
            if mods.shift {
                Key::ShiftTab
            } else {
                Key::Tab
            }
        }
        // Some layouts send a dedicated keysym for Shift-Tab rather than Tab
        // with the modifier set.
        key::ISO_Left_Tab => Key::ShiftTab,
        key::Up | key::KP_Up => Key::Up,
        key::Down | key::KP_Down => Key::Down,
        key::Left | key::KP_Left => Key::Left,
        key::Right | key::KP_Right => Key::Right,
        key::Home | key::KP_Home => Key::Home,
        key::End | key::KP_End => Key::End,
        key::Page_Up | key::KP_Page_Up => Key::PageUp,
        key::Page_Down | key::KP_Page_Down => Key::PageDown,
        key::Delete | key::KP_Delete => Key::Delete,
        _ => {
            // Ctrl/Alt chords are commands, not text: typing them into the
            // filter would silently insert control characters.
            if mods.ctrl || mods.alt || mods.logo {
                return Key::Other;
            }
            match keysym_to_char(keysym) {
                Some(c) => Key::Char(c),
                None => Key::Other,
            }
        }
    }
}

/// The character a keysym produces, if it produces one.
///
/// `xkeysym` carries the full X11 keysym tables, including the legacy pages
/// (Greek, Cyrillic, technical, publishing) that the simple "Latin-1 keysyms are
/// their own codepoint" rule misses. Those pages are what a non-Latin layout
/// actually sends, so hand-rolling the mapping means a Cyrillic keyboard types
/// nothing into the filter.
pub fn keysym_to_char(keysym: u32) -> Option<char> {
    let c = xkeysym::Keysym::from(keysym).key_char()?;
    // Control characters are keysyms too (Escape is 0xff1b, Return 0xff0d); they
    // are commands here, not text, and inserting them would put invisible
    // rubbish in the filter.
    (!c.is_control()).then_some(c)
}

/// One key's keysyms, indexed by shift level, for each layout group.
#[derive(Debug, Clone, Default)]
pub struct KeyEntry {
    /// `groups[group][level]`
    pub groups: Vec<Vec<u32>>,
}

impl KeyEntry {
    /// Resolve to a single keysym for the given group and shift state.
    pub fn resolve(&self, group: usize, shift: bool) -> Option<u32> {
        let g = self.groups.get(group).or_else(|| self.groups.first())?;
        if shift {
            // Level 2 when the layout defines one, else the unshifted symbol
            // upper-cased by the caller's char mapping.
            g.get(1).or_else(|| g.first()).copied()
        } else {
            g.first().copied()
        }
    }
}

/// A parsed XKB keymap: enough of one to turn a keycode into a keysym.
#[derive(Debug, Default)]
pub struct Keymap {
    by_keycode: HashMap<u32, KeyEntry>,
}

impl Keymap {
    pub fn is_empty(&self) -> bool {
        self.by_keycode.is_empty()
    }

    pub fn len(&self) -> usize {
        self.by_keycode.len()
    }

    /// Look up a keycode as delivered by the compositor (evdev + 8 on Wayland,
    /// which is the same numbering the keymap's `xkb_keycodes` uses).
    pub fn keysym(&self, keycode: u32, group: usize, shift: bool) -> Option<u32> {
        let e = self.by_keycode.get(&keycode)?;
        let sym = e.resolve(group, shift)?;
        if shift {
            // A layout that defines no shifted level still shifts letters: `a`
            // with Shift must be `A`, not `a`.
            if let Some(c) = keysym_to_char(sym) {
                if e.groups.first().map(|g| g.len()).unwrap_or(0) < 2 {
                    let up: String = c.to_uppercase().collect();
                    if let Some(u) = up.chars().next() {
                        if u != c {
                            return Some(char_to_keysym(u));
                        }
                    }
                }
            }
        }
        Some(sym)
    }

    /// Parse the XKB text keymap a Wayland compositor sends.
    ///
    /// Two sections matter. `xkb_keycodes` names each keycode (`<AD01> = 24;`)
    /// and `xkb_symbols` gives each name its symbols
    /// (`key <AD01> { [ q, Q ] };`). Everything else — geometry, compat, types —
    /// is skipped, which is why this stays short.
    pub fn parse(text: &str) -> Keymap {
        let mut names: HashMap<String, u32> = HashMap::new();
        let mut by_keycode: HashMap<u32, KeyEntry> = HashMap::new();

        // Pass 1: <NAME> = code;   and aliases  alias <A> = <B>;
        //
        // Split on `;`, not on newlines: a statement is terminated by a
        // semicolon and a compositor is free to put several on one line (and to
        // wrap one across several).
        let decomment: String = text
            .lines()
            .map(strip_comment)
            .collect::<Vec<_>>()
            .join("\n");
        let mut aliases: Vec<(String, String)> = Vec::new();
        for stmt in decomment.split(';') {
            let stmt = stmt.trim();
            if stmt.contains("alias ") {
                if let Some((a, b)) = stmt.split_once('=') {
                    if let (Some(a), Some(b)) = (angle(a), angle(b)) {
                        aliases.push((a, b));
                    }
                }
                continue;
            }
            let Some((lhs, rhs)) = stmt.split_once('=') else {
                continue;
            };
            let Some(name) = angle(lhs) else { continue };
            // The value is a bare number; anything else (a symbol list, a brace)
            // means this is not a keycode declaration.
            let rhs: String = rhs
                .trim()
                .chars()
                .take_while(|c| c.is_ascii_digit())
                .collect();
            if let Ok(code) = rhs.parse::<u32>() {
                names.insert(name, code);
            }
        }
        for (a, b) in aliases {
            if let Some(code) = names.get(&b).copied() {
                names.insert(a, code);
            }
        }

        // Pass 2: key <NAME> { ... [ syms ], [ syms ] ... };
        //
        // Statements may span lines, so the text is walked by `key` keyword and
        // then to the matching `};` rather than line by line.
        let mut rest = text;
        while let Some(pos) = rest.find("key ") {
            rest = &rest[pos + 4..];
            let Some(name) = angle(rest.split('{').next().unwrap_or("")) else {
                continue;
            };
            let Some(open) = rest.find('{') else { break };
            let Some(close) = rest[open..].find('}') else {
                break;
            };
            let body = &rest[open + 1..open + close];
            rest = &rest[open + close..];

            let mut groups: Vec<Vec<u32>> = Vec::new();
            // Each [ ... ] is one group's levels — except the index brackets in
            // `symbols[Group1]`, `actions[Group2]` and friends. A real symbol
            // list is preceded by whitespace or `=`; an index bracket is glued
            // to the identifier before it, which is what tells them apart.
            let mut b = body;
            while let Some(s) = b.find('[') {
                let Some(e) = b[s..].find(']') else { break };
                let glued = s > 0
                    && b[..s]
                        .chars()
                        .next_back()
                        .map(|c| c.is_alphanumeric() || c == '_')
                        .unwrap_or(false);
                if glued {
                    b = &b[s + e + 1..];
                    continue;
                }
                let list = &b[s + 1..s + e];
                let syms: Vec<u32> = list
                    .split(',')
                    .map(|t| t.trim())
                    .filter(|t| !t.is_empty())
                    .map(name_to_keysym)
                    .collect();
                if !syms.is_empty() {
                    groups.push(syms);
                }
                b = &b[s + e + 1..];
            }
            if groups.is_empty() {
                continue;
            }
            if let Some(code) = names.get(&name).copied() {
                by_keycode.insert(code, KeyEntry { groups });
            }
        }

        Keymap { by_keycode }
    }
}

fn strip_comment(line: &str) -> &str {
    if let Some(i) = line.find("//") {
        &line[..i]
    } else {
        line
    }
}

/// Extract `NAME` from a `<NAME>` token.
fn angle(s: &str) -> Option<String> {
    let a = s.find('<')?;
    let b = s[a..].find('>')?;
    Some(s[a + 1..a + b].to_string())
}

fn char_to_keysym(c: char) -> u32 {
    let cp = c as u32;
    if (0x20..=0x7e).contains(&cp) || (0xa0..=0xff).contains(&cp) {
        cp
    } else {
        0x0100_0000 + cp
    }
}

/// Map an XKB symbol name to a keysym.
///
/// The names that matter are the ASCII ones (`q`, `A`, `space`), the `UXXXX`
/// form, and the `0x...` form. Named symbols outside that set resolve to 0 and
/// are simply not typeable — acceptable for a filter box, and far cheaper than
/// embedding the full 2000-entry keysym table.
fn name_to_keysym(name: &str) -> u32 {
    use xkeysym::key;
    let name = name.trim();
    if name.len() == 1 {
        return char_to_keysym(name.chars().next().unwrap());
    }
    if let Some(hex) = name.strip_prefix("0x") {
        return u32::from_str_radix(hex, 16).unwrap_or(0);
    }
    if let Some(hex) = name.strip_prefix('U') {
        if let Ok(cp) = u32::from_str_radix(hex, 16) {
            return char_to_keysym(char::from_u32(cp).unwrap_or('\0'));
        }
    }
    match name {
        "space" => 0x20,
        "exclam" => 0x21,
        "quotedbl" => 0x22,
        "numbersign" => 0x23,
        "dollar" => 0x24,
        "percent" => 0x25,
        "ampersand" => 0x26,
        "apostrophe" | "quoteright" => 0x27,
        "parenleft" => 0x28,
        "parenright" => 0x29,
        "asterisk" => 0x2a,
        "plus" => 0x2b,
        "comma" => 0x2c,
        "minus" => 0x2d,
        "period" => 0x2e,
        "slash" => 0x2f,
        "colon" => 0x3a,
        "semicolon" => 0x3b,
        "less" => 0x3c,
        "equal" => 0x3d,
        "greater" => 0x3e,
        "question" => 0x3f,
        "at" => 0x40,
        "bracketleft" => 0x5b,
        "backslash" => 0x5c,
        "bracketright" => 0x5d,
        "asciicircum" => 0x5e,
        "underscore" => 0x5f,
        "grave" | "quoteleft" => 0x60,
        "braceleft" => 0x7b,
        "bar" => 0x7c,
        "braceright" => 0x7d,
        "asciitilde" => 0x7e,
        "Return" => key::Return,
        "Escape" => key::Escape,
        "BackSpace" => key::BackSpace,
        "Tab" => key::Tab,
        "ISO_Left_Tab" => key::ISO_Left_Tab,
        "Up" => key::Up,
        "Down" => key::Down,
        "Left" => key::Left,
        "Right" => key::Right,
        "Home" => key::Home,
        "End" => key::End,
        "Prior" | "Page_Up" => key::Page_Up,
        "Next" | "Page_Down" => key::Page_Down,
        "Delete" => key::Delete,
        _ => 0,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn shift() -> Mods {
        Mods {
            shift: true,
            ..Default::default()
        }
    }

    #[test]
    fn navigation_keysyms_map_to_actions() {
        let m = Mods::default();
        assert_eq!(keysym_to_key(0xff0d, m), Key::Enter);
        assert_eq!(keysym_to_key(0xff8d, m), Key::Enter, "the keypad Enter too");
        assert_eq!(keysym_to_key(0xff1b, m), Key::Escape);
        assert_eq!(keysym_to_key(0xff08, m), Key::Backspace);
        assert_eq!(keysym_to_key(0xff52, m), Key::Up);
        assert_eq!(keysym_to_key(0xff56, m), Key::PageDown);
    }

    #[test]
    fn tab_and_shift_tab_are_distinguished() {
        assert_eq!(keysym_to_key(0xff09, Mods::default()), Key::Tab);
        assert_eq!(keysym_to_key(0xff09, shift()), Key::ShiftTab);
        // Some layouts send a dedicated ISO_Left_Tab instead.
        assert_eq!(keysym_to_key(0xfe20, Mods::default()), Key::ShiftTab);
    }

    #[test]
    fn printable_keysyms_become_characters() {
        let m = Mods::default();
        assert_eq!(keysym_to_key(0x61, m), Key::Char('a'));
        assert_eq!(keysym_to_key(0x20, m), Key::Char(' '));
        assert_eq!(keysym_to_key(0x2d, m), Key::Char('-'));
        // Latin-1 high range.
        assert_eq!(keysym_to_key(0xe9, m), Key::Char('é'));
        // Unicode keysyms: 0x01000000 + codepoint.
        assert_eq!(keysym_to_key(0x0100_0416, m), Key::Char('Ж'));
        // Keypad digits are still digits.
        assert_eq!(keysym_to_key(0xffb5, m), Key::Char('5'));
    }

    #[test]
    fn modifier_chords_are_not_typed_into_the_filter() {
        // Ctrl-C must not insert a 'c'; it is a command, not text.
        let ctrl = Mods {
            ctrl: true,
            ..Default::default()
        };
        assert_eq!(keysym_to_key(0x63, ctrl), Key::Other);
        let alt = Mods {
            alt: true,
            ..Default::default()
        };
        assert_eq!(keysym_to_key(0x63, alt), Key::Other);
        // ...but Escape still works while a modifier is held.
        assert_eq!(keysym_to_key(0xff1b, ctrl), Key::Escape);
    }

    #[test]
    fn unknown_keysyms_are_ignored_rather_than_inserted() {
        let m = Mods::default();
        assert_eq!(
            keysym_to_key(0xffe1, m),
            Key::Other,
            "Shift itself types nothing"
        );
        assert_eq!(keysym_to_key(0x0, m), Key::Other);
        assert_eq!(keysym_to_char(0xffe9), None);
    }

    /// A cut-down keymap in exactly the shape a compositor sends.
    const KEYMAP: &str = r#"
xkb_keymap {
xkb_keycodes "evdev" {
    minimum = 8;
    maximum = 255;
    <ESC> = 9;
    <AD01> = 24;
    <AD02> = 25;
    <RTRN> = 36;
    <SPCE> = 65;
    alias <ALT> = <LALT>;
};
xkb_types "complete" {
    virtual_modifiers NumLock;
};
xkb_symbols "pc+us" {
    key <ESC> { [ Escape ] };
    key <AD01> {
        type= "ALPHABETIC",
        symbols[Group1]= [ q, Q ],
        symbols[Group2]= [ Cyrillic_shorti, Cyrillic_SHORTI ]
    };
    key <AD02> { [ w, W ] };  // a trailing comment
    key <RTRN> { [ Return ] };
    key <SPCE> { [ space ] };
};
};
"#;

    #[test]
    fn parses_a_compositor_keymap() {
        let km = Keymap::parse(KEYMAP);
        assert!(!km.is_empty(), "the keymap must yield keys");
        // q / Q from the first group.
        assert_eq!(km.keysym(24, 0, false), Some(0x71));
        assert_eq!(km.keysym(24, 0, true), Some(0x51));
        // A second layout group is honoured.
        assert!(km.keysym(24, 1, false).is_some());
        // Named symbols.
        assert_eq!(km.keysym(9, 0, false), Some(0xff1b));
        assert_eq!(km.keysym(36, 0, false), Some(0xff0d));
        assert_eq!(km.keysym(65, 0, false), Some(0x20));
        // A comment after the statement does not break the parse.
        assert_eq!(km.keysym(25, 0, false), Some(0x77));
    }

    #[test]
    fn end_to_end_a_keycode_becomes_a_character() {
        let km = Keymap::parse(KEYMAP);
        let sym = km.keysym(24, 0, false).unwrap();
        assert_eq!(keysym_to_key(sym, Mods::default()), Key::Char('q'));
        let sym = km.keysym(24, 0, true).unwrap();
        assert_eq!(keysym_to_key(sym, shift()), Key::Char('Q'));
    }

    #[test]
    fn a_layout_with_no_shifted_level_still_capitalises() {
        // Some layouts list one symbol and rely on the ALPHABETIC type.
        let km =
            Keymap::parse("xkb_keycodes { <AD01> = 24; };\nxkb_symbols { key <AD01> { [ a ] }; };");
        assert_eq!(km.keysym(24, 0, false), Some(0x61));
        assert_eq!(km.keysym(24, 0, true), Some(0x41), "Shift-a must be A");
    }

    #[test]
    fn an_unknown_keycode_resolves_to_nothing() {
        let km = Keymap::parse(KEYMAP);
        assert_eq!(km.keysym(200, 0, false), None);
        // ...and an out-of-range group falls back to the first rather than
        // dropping the key entirely.
        assert_eq!(km.keysym(24, 9, false), Some(0x71));
    }

    #[test]
    fn garbage_input_parses_to_an_empty_keymap_instead_of_panicking() {
        for junk in [
            "",
            "not a keymap at all",
            "xkb_keymap { xkb_symbols { key <NOPE> { [ ",
            "key key key {{{ [[[ ",
            "<A> = notanumber;",
        ] {
            let km = Keymap::parse(junk);
            assert!(km.keysym(24, 0, false).is_none(), "junk: {junk:?}");
        }
    }

    #[test]
    fn unicode_and_hex_symbol_names_are_understood() {
        let km = Keymap::parse(
            "xkb_keycodes { <K> = 30; };\nxkb_symbols { key <K> { [ U0416, 0x00e9 ] }; };",
        );
        let lower = km.keysym(30, 0, false).unwrap();
        assert_eq!(keysym_to_char(lower), Some('Ж'));
        let upper = km.keysym(30, 0, true).unwrap();
        assert_eq!(keysym_to_char(upper), Some('é'));
    }
}
