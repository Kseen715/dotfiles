//! Discovery: what themes exist, what they look like, what they can paint.
//!
//! Proteus reads the same `theme.list` os-rice writes, but nothing here knows
//! about os-rice beyond that file format. Point `themes_dir` at any directory of
//! `<name>/theme.list` and the picker works — which is what keeps this crate
//! standalone rather than a subdirectory of a dotfiles repo that happens to
//! compile.
//!
//! The format is deliberately not TOML (see os-rice DESIGN, "Decisions"): it is
//! `key: value` lines with `#` comments, so a POSIX shell can parse it with
//! `while read` and no dependency. The one subtlety it costs is the comment
//! rule, which this parser must match exactly — see [`strip_comment`].

use std::collections::BTreeMap;
use std::fs;
use std::path::{Path, PathBuf};

/// A colour from a theme's palette.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Rgb {
    pub r: u8,
    pub g: u8,
    pub b: u8,
}

impl Rgb {
    pub const fn new(r: u8, g: u8, b: u8) -> Self {
        Self { r, g, b }
    }

    /// Parse `#rrggbb` (or `#rgb`). Returns `None` for anything else, including
    /// the named colours a theme may use for terminal palettes (`cyan`), which
    /// are meaningful to a terminal and meaningless as a pixel value here.
    pub fn parse(s: &str) -> Option<Self> {
        let h = s.strip_prefix('#')?;
        let d = |i: usize| u8::from_str_radix(&h[i..i + 1], 16).ok();
        match h.len() {
            3 => Some(Self::new(d(0)? * 17, d(1)? * 17, d(2)? * 17)),
            6 => Some(Self::new(
                u8::from_str_radix(&h[0..2], 16).ok()?,
                u8::from_str_radix(&h[2..4], 16).ok()?,
                u8::from_str_radix(&h[4..6], 16).ok()?,
            )),
            _ => None,
        }
    }

    /// Perceived brightness (ITU-R BT.601), 0.0–1.0. Used to decide whether a
    /// generated preview needs light or dark text on it.
    pub fn luma(self) -> f32 {
        (0.299 * self.r as f32 + 0.587 * self.g as f32 + 0.114 * self.b as f32) / 255.0
    }

    /// Blend towards `other` by `t` in 0.0..=1.0.
    pub fn mix(self, other: Rgb, t: f32) -> Rgb {
        let t = t.clamp(0.0, 1.0);
        let f = |a: u8, b: u8| (a as f32 + (b as f32 - a as f32) * t).round() as u8;
        Rgb::new(f(self.r, other.r), f(self.g, other.g), f(self.b, other.b))
    }
}

/// Which display server a theme's layers target.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Session {
    Any,
    X11,
    Wayland,
}

impl Session {
    fn parse(s: &str) -> Self {
        match s.trim().to_ascii_lowercase().as_str() {
            "x11" => Session::X11,
            "wayland" => Session::Wayland,
            _ => Session::Any,
        }
    }

    /// Whether a theme declaring this session is fully at home in `current`.
    ///
    /// An off-session theme is shown and applicable, never hidden: its terminal,
    /// shell and GTK layers land and look right regardless, and only the
    /// compositor-specific ones go unread. Hiding it would be a lie about what
    /// applying it does.
    pub fn fits(self, current: Session) -> bool {
        matches!((self, current), (Session::Any, _) | (_, Session::Any)) || self == current
    }
}

/// The palette Proteus styles itself with when a theme is selected, and paints
/// as a preview when a theme ships no wallpaper.
#[derive(Debug, Clone)]
pub struct Palette {
    pub bg: Rgb,
    pub surface: Rgb,
    pub fg: Rgb,
    pub dim: Rgb,
    pub accent: Rgb,
    pub extra: BTreeMap<String, Rgb>,
}

impl Default for Palette {
    /// A neutral dark palette, used when a theme defines none. Chosen so the
    /// picker is legible rather than pretty: a theme with a broken palette must
    /// still be selectable, because selecting it is how you fix it.
    fn default() -> Self {
        Self {
            bg: Rgb::new(0x18, 0x18, 0x18),
            surface: Rgb::new(0x30, 0x30, 0x30),
            fg: Rgb::new(0xe0, 0xe0, 0xe0),
            dim: Rgb::new(0x80, 0x80, 0x80),
            accent: Rgb::new(0x7a, 0xa2, 0xf7),
            extra: BTreeMap::new(),
        }
    }
}

/// One theme on disk.
#[derive(Debug, Clone)]
pub struct Theme {
    /// Directory name — the identifier passed to the apply command.
    pub name: String,
    /// Human name for the list (`display:`), falling back to `name`.
    pub display: String,
    pub description: String,
    pub session: Session,
    pub polarity: String,
    pub palette: Palette,
    pub dir: PathBuf,
    /// Every image under `wallpapers/`, lexically ordered.
    pub wallpapers: Vec<PathBuf>,
}

impl Theme {
    /// The image to show as this theme's preview, if it has one.
    pub fn preview(&self) -> Option<&Path> {
        self.wallpapers.first().map(|p| p.as_path())
    }
}

/// Strip a comment, matching os-rice's `_osr_theme_lines` exactly.
///
/// A comment is `#` at the start of a line, or a hash with whitespace on BOTH
/// sides. It cannot be "everything after the first `#`", because a palette value
/// *is* a hash: `color: background #2e3440` would strip to nothing. `#rrggbb` never has
/// a space after the hash, so the two are unambiguous.
///
/// If this ever disagrees with the shell, the picker and the thing it drives
/// read different themes — so `tests::comment_rule_matches_shell` pins it.
fn strip_comment(line: &str) -> &str {
    let t = line.trim_start();
    if t.starts_with('#') {
        return "";
    }
    let b = line.as_bytes();
    for i in 1..b.len() {
        if b[i] == b'#' && b[i - 1].is_ascii_whitespace() {
            // A trailing hash at end-of-line, or one followed by whitespace.
            if i + 1 >= b.len() || b[i + 1].is_ascii_whitespace() {
                return &line[..i];
            }
        }
    }
    line
}

/// Parsed `key: value` directives, in file order. Multi-valued keys (`config:`,
/// `color:`) appear once per line.
fn directives(text: &str) -> Vec<(String, String)> {
    let mut out = Vec::new();
    for raw in text.lines() {
        let line = strip_comment(raw).trim();
        if line.is_empty() {
            continue;
        }
        let Some((k, v)) = line.split_once(':') else {
            continue;
        };
        out.push((k.trim().to_string(), v.trim().to_string()));
    }
    out
}

/// Image extensions the picker will decode. Matches os-rice's `osr_is_image`;
/// a theme's `wallpapers/*.txt` placeholder must never be treated as an image.
const IMAGE_EXTS: &[&str] = &["jpg", "jpeg", "png", "webp", "bmp", "gif"];

fn is_image(p: &Path) -> bool {
    p.is_file()
        && p.extension()
            .and_then(|e| e.to_str())
            .map(|e| IMAGE_EXTS.contains(&e.to_ascii_lowercase().as_str()))
            .unwrap_or(false)
}

/// List images in `dir`, lexically ordered so a theme's default is stable.
pub fn images_in(dir: &Path) -> Vec<PathBuf> {
    let Ok(rd) = fs::read_dir(dir) else {
        return Vec::new();
    };
    let mut v: Vec<PathBuf> = rd
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| is_image(p))
        .collect();
    v.sort();
    v
}

impl Theme {
    /// Load one theme from its directory. `None` when there is no `theme.list`
    /// (the directory is then not a theme, which is not an error — a themes dir
    /// may hold a README or a stray checkout).
    pub fn load(dir: &Path) -> Option<Theme> {
        let manifest = dir.join("theme.list");
        let text = fs::read_to_string(&manifest).ok()?;
        let name = dir.file_name()?.to_string_lossy().to_string();

        let mut display = None;
        let mut description = String::new();
        let mut session = Session::Any;
        let mut polarity = "dark".to_string();
        let mut palette = Palette::default();
        let mut named: BTreeMap<String, Rgb> = BTreeMap::new();

        for (k, v) in directives(&text) {
            match k.as_str() {
                "display" => display = Some(v),
                "description" => description = v,
                "session" => session = Session::parse(&v),
                "polarity" => polarity = v,
                "color" => {
                    let mut it = v.split_whitespace();
                    if let (Some(role), Some(hex)) = (it.next(), it.next()) {
                        if let Some(c) = Rgb::parse(hex) {
                            named.insert(role.to_string(), c);
                        }
                    }
                }
                _ => {}
            }
        }

        // Roles the UI needs; anything else stays available under `extra` so a
        // theme can carry more without this struct growing a field per colour -
        // and it carries a lot more now that a theme.list is the ONLY place a
        // theme's colours live (os-rice DESIGN section 6b): the 16 ANSI slots,
        // the TUI chrome roles, the per-app accents. Proteus wants five of them.
        if let Some(c) = named.remove("background") {
            palette.bg = c;
        }
        if let Some(c) = named.remove("surface") {
            palette.surface = c;
        }
        if let Some(c) = named.remove("foreground") {
            palette.fg = c;
        }
        if let Some(c) = named.remove("text_dim") {
            palette.dim = c;
        }
        if let Some(c) = named.remove("accent") {
            palette.accent = c;
        }
        palette.extra = named;

        Some(Theme {
            display: display.unwrap_or_else(|| name.clone()),
            name,
            description,
            session,
            polarity,
            palette,
            wallpapers: images_in(&dir.join("wallpapers")),
            dir: dir.to_path_buf(),
        })
    }
}

/// Every theme under a directory, ordered by name so the list never reshuffles
/// between runs (a picker whose rows move is a picker you cannot use by muscle
/// memory).
pub fn load_themes(themes_dir: &Path) -> Vec<Theme> {
    let Ok(rd) = fs::read_dir(themes_dir) else {
        return Vec::new();
    };
    let mut dirs: Vec<PathBuf> = rd
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| p.is_dir())
        .collect();
    dirs.sort();
    dirs.iter().filter_map(|d| Theme::load(d)).collect()
}

/// Read a `key=value` state file (os-rice's `~/.config/osr/state`). Missing or
/// unreadable is an empty map, never an error: the picker still works, it just
/// does not know what is currently applied.
pub fn read_state(path: &Path) -> BTreeMap<String, String> {
    let mut m = BTreeMap::new();
    let Ok(text) = fs::read_to_string(path) else {
        return m;
    };
    for line in text.lines() {
        if let Some((k, v)) = line.split_once('=') {
            // Last assignment wins, matching osr_state_get.
            m.insert(k.trim().to_string(), v.trim().to_string());
        }
    }
    m
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    fn tmpdir(tag: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!(
            "proteus-test-{tag}-{}-{:?}",
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
        if let Some(parent) = p.parent() {
            fs::create_dir_all(parent).unwrap();
        }
        let mut f = fs::File::create(p).unwrap();
        f.write_all(s.as_bytes()).unwrap();
    }

    #[test]
    fn parses_hex_colours() {
        assert_eq!(Rgb::parse("#2e3440"), Some(Rgb::new(0x2e, 0x34, 0x40)));
        assert_eq!(Rgb::parse("#fff"), Some(Rgb::new(255, 255, 255)));
        // A terminal palette may name colours; those are not pixel values.
        assert_eq!(Rgb::parse("cyan"), None);
        assert_eq!(Rgb::parse("#12345"), None);
        assert_eq!(Rgb::parse(""), None);
        assert_eq!(Rgb::parse("#zzzzzz"), None);
    }

    /// The comment rule must match os-rice's shell parser exactly. If it drifts,
    /// the picker and the applier disagree about what a theme is.
    #[test]
    fn comment_rule_matches_shell() {
        // A palette value is a hash and must survive.
        assert_eq!(strip_comment("color: background #2e3440"), "color: background #2e3440");
        // A hash with space on both sides is a comment.
        assert_eq!(
            strip_comment("color: background #2e3440 # muted").trim(),
            "color: background #2e3440"
        );
        // A full-line comment is nothing.
        assert_eq!(strip_comment("# just a note"), "");
        assert_eq!(strip_comment("   # indented note"), "");
        // A trailing bare hash.
        assert_eq!(strip_comment("display: Nord #").trim(), "display: Nord");
        // `#no-space` is NOT a comment - documented, so the ambiguity stays gone.
        assert_eq!(strip_comment("display: Nord #tag"), "display: Nord #tag");
    }

    #[test]
    fn loads_a_theme_with_palette_and_wallpapers() {
        let d = tmpdir("load");
        let t = d.join("nord");
        write(
            &t.join("theme.list"),
            "# a comment\n\
             display: Nord\n\
             description: Arctic, north-bluish palette\n\
             polarity: dark\n\
             session: any\n\
             \n\
             color: background #2e3440\n\
             color: surface #3b4252\n\
             color: foreground #d8dee9\n\
             color: dim     #4c566a\n\
             color: accent  #88c0d0\n\
             color: success #a3be8c\n",
        );
        // Two images and a placeholder that must be ignored.
        write(&t.join("wallpapers/b.png"), "x");
        write(&t.join("wallpapers/a.jpg"), "x");
        write(&t.join("wallpapers/nord.txt"), "drop a real image here");

        let theme = Theme::load(&t).expect("theme loads");
        assert_eq!(theme.name, "nord");
        assert_eq!(theme.display, "Nord");
        assert_eq!(theme.description, "Arctic, north-bluish palette");
        assert_eq!(theme.session, Session::Any);
        assert_eq!(theme.palette.bg, Rgb::new(0x2e, 0x34, 0x40));
        assert_eq!(theme.palette.accent, Rgb::new(0x88, 0xc0, 0xd0));
        // Roles beyond the five the UI needs stay reachable.
        assert_eq!(
            theme.palette.extra.get("success"),
            Some(&Rgb::new(0xa3, 0xbe, 0x8c))
        );

        // Lexical order, placeholder excluded.
        assert_eq!(
            theme.wallpapers.len(),
            2,
            "the .txt placeholder is not an image"
        );
        assert!(theme.wallpapers[0].ends_with("a.jpg"));
        assert_eq!(theme.preview().unwrap(), theme.wallpapers[0]);

        fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn a_theme_without_a_manifest_is_not_a_theme() {
        let d = tmpdir("nomanifest");
        fs::create_dir_all(d.join("notatheme")).unwrap();
        assert!(Theme::load(&d.join("notatheme")).is_none());
        assert!(load_themes(&d).is_empty());
        fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn a_theme_with_no_palette_still_loads_legibly() {
        let d = tmpdir("bare");
        write(&d.join("bare/theme.list"), "display: Bare\n");
        let t = Theme::load(&d.join("bare")).unwrap();
        // Defaults, not black-on-black: a theme with a broken palette must stay
        // selectable, because selecting it is how you get to fix it.
        let def = Palette::default();
        assert_eq!(t.palette.bg, def.bg);
        assert_eq!(t.palette.fg, def.fg);
        assert_eq!(t.palette.accent, def.accent);
        assert!(
            t.palette.fg.luma() > t.palette.bg.luma(),
            "text stays legible on the background"
        );
        assert!(t.preview().is_none());
        assert_eq!(t.session, Session::Any, "session defaults to any");
        fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn themes_are_listed_in_a_stable_order() {
        let d = tmpdir("order");
        for n in ["zeta", "alpha", "mid"] {
            write(&d.join(n).join("theme.list"), "display: x\n");
        }
        let names: Vec<String> = load_themes(&d).into_iter().map(|t| t.name).collect();
        assert_eq!(names, vec!["alpha", "mid", "zeta"]);
        fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn session_compatibility_never_hides_a_theme() {
        assert!(Session::Any.fits(Session::X11));
        assert!(Session::X11.fits(Session::X11));
        assert!(!Session::X11.fits(Session::Wayland));
        assert!(!Session::Wayland.fits(Session::X11));
        // With an unknown current session, everything fits: guessing wrong must
        // not remove a theme from the list.
        assert!(Session::Wayland.fits(Session::Any));
    }

    #[test]
    fn reads_the_state_file() {
        let d = tmpdir("state");
        let p = d.join("state");
        write(
            &p,
            "rice=xin\ntheme=nord\nwallpaper=/x/y.png\ntheme=gruvbox\n",
        );
        let s = read_state(&p);
        assert_eq!(s.get("rice").unwrap(), "xin");
        // Last assignment wins, matching osr_state_get's `tail -n 1`.
        assert_eq!(s.get("theme").unwrap(), "gruvbox");
        assert!(read_state(&d.join("nope")).is_empty());
        fs::remove_dir_all(&d).ok();
    }
}
