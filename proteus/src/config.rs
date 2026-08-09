//! Configuration: where themes come from, what applying one runs, how it looks.
//!
//! Two principles shape this file.
//!
//! **Nothing about os-rice is hardcoded.** The defaults happen to point at an
//! os-rice checkout because that is what is usually there, but every path and
//! every command is configurable, so Proteus works against any directory of
//! themes and any script that applies them.
//!
//! **A broken config must not stop the picker from opening.** A theme switcher
//! that refuses to start because its own config has a typo is a theme switcher
//! you cannot use to fix the typo. Parse errors are collected and reported, and
//! the defaults are used for whatever did not parse.

use std::path::{Path, PathBuf};

use serde::Deserialize;

use crate::catalog::Rgb;

/// Which renderer to use.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize, Default)]
#[serde(rename_all = "lowercase")]
pub enum RendererChoice {
    /// Probe wgpu, then GL, then CPU, and use the first that initialises.
    #[default]
    Auto,
    /// Vulkan / Metal / DX12 / GL3.3+ via wgpu.
    Wgpu,
    /// OpenGL 2.1 / GLES 2.0 — the path for genuinely old hardware.
    Gl,
    /// Software rasteriser.
    Cpu,
}

impl RendererChoice {
    pub fn parse(s: &str) -> Option<Self> {
        match s.trim().to_ascii_lowercase().as_str() {
            "auto" => Some(Self::Auto),
            "wgpu" | "vulkan" => Some(Self::Wgpu),
            "gl" | "opengl" | "gles" => Some(Self::Gl),
            "cpu" | "software" => Some(Self::Cpu),
            _ => None,
        }
    }
}

/// How the window is placed on screen.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize, Default)]
#[serde(rename_all = "kebab-case")]
pub enum WindowMode {
    /// A true overlay: layer-shell on Wayland, override-redirect on X11.
    #[default]
    Overlay,
    /// An ordinary window the WM manages. The escape hatch for compositors
    /// where the overlay path is unavailable or unwanted.
    Toplevel,
}

fn default_width() -> f32 {
    1000.0
}
fn default_height() -> f32 {
    460.0
}
fn default_rows() -> usize {
    5
}
fn default_radius() -> f32 {
    12.0
}
fn default_padding() -> f32 {
    16.0
}
fn default_font_size() -> f32 {
    15.0
}
fn default_opacity() -> f32 {
    1.0
}
fn default_preview_ratio() -> f32 {
    0.42
}
fn default_true() -> bool {
    true
}

/// Layout and styling. Every field has a default, so a `[style]` table naming
/// one key is a valid config.
#[derive(Debug, Clone, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct Style {
    pub width: f32,
    pub height: f32,
    /// Rows visible at once; the window height follows from this and the row
    /// height unless `height` is set explicitly.
    pub rows: usize,
    pub radius: f32,
    pub padding: f32,
    pub font: Option<String>,
    pub font_size: f32,
    /// Window opacity, 0.0–1.0. Only meaningful on a compositor that blends.
    pub opacity: f32,
    /// Fraction of the width given to the large preview pane, 0.0 disables it.
    pub preview_ratio: f32,
    /// Follow the palette of the theme under the cursor, so moving the
    /// selection previews the theme live.
    pub follow_theme: bool,
    /// Slide the list and the selection bar between rows.
    ///
    /// Off makes every move instant. Worth having as a switch rather than a
    /// hard-coded taste: some people find motion distracting, and on hardware
    /// slow enough to miss the frame budget a jump looks better than a stutter.
    pub animate: bool,
    /// How quickly the slide settles, in seconds (the exponential time
    /// constant). Smaller is snappier; 0 is the same as `animate = false`.
    pub animation_speed: f32,
    /// Explicit colour overrides; unset keys come from the followed theme.
    pub bg: Option<String>,
    pub fg: Option<String>,
    pub accent: Option<String>,
    pub surface: Option<String>,
    pub dim: Option<String>,
}

impl Default for Style {
    fn default() -> Self {
        Self {
            width: default_width(),
            height: default_height(),
            rows: default_rows(),
            radius: default_radius(),
            padding: default_padding(),
            font: None,
            font_size: default_font_size(),
            opacity: default_opacity(),
            preview_ratio: default_preview_ratio(),
            follow_theme: default_true(),
            animate: default_true(),
            animation_speed: crate::anim::SCROLL_TAU,
            bg: None,
            fg: None,
            accent: None,
            surface: None,
            dim: None,
        }
    }
}

impl Style {
    /// Clamp every value into a range that can actually be drawn.
    ///
    /// Not defensive programming for its own sake: `rows = 0` divides by zero in
    /// the layout, a negative radius makes an inside-out path, and `opacity = 5`
    /// is a protocol error on Wayland. A config with a typo should look wrong,
    /// not crash.
    pub fn sanitize(&mut self) {
        self.width = self.width.clamp(200.0, 10_000.0);
        self.height = self.height.clamp(120.0, 10_000.0);
        self.rows = self.rows.clamp(1, 64);
        self.radius = self.radius.clamp(0.0, 200.0);
        self.padding = self.padding.clamp(0.0, 200.0);
        self.font_size = self.font_size.clamp(6.0, 96.0);
        self.opacity = self.opacity.clamp(0.05, 1.0);
        self.preview_ratio = self.preview_ratio.clamp(0.0, 0.8);
        // A negative or absurd time constant would either never settle or
        // divide the easing by zero.
        self.animation_speed = self.animation_speed.clamp(0.0, 2.0);
        if self.animation_speed <= 0.001 {
            self.animate = false;
        }
    }

    fn color(&self, which: &Option<String>) -> Option<Rgb> {
        which.as_deref().and_then(Rgb::parse)
    }

    pub fn bg_override(&self) -> Option<Rgb> {
        self.color(&self.bg)
    }
    pub fn fg_override(&self) -> Option<Rgb> {
        self.color(&self.fg)
    }
    pub fn accent_override(&self) -> Option<Rgb> {
        self.color(&self.accent)
    }
    pub fn surface_override(&self) -> Option<Rgb> {
        self.color(&self.surface)
    }
    pub fn dim_override(&self) -> Option<Rgb> {
        self.color(&self.dim)
    }
}

/// Where themes, wallpapers and state live.
#[derive(Debug, Clone, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct Sources {
    pub themes_dir: Option<String>,
    /// Extra directories of wallpapers, beyond each theme's own.
    pub wallpaper_dirs: Vec<String>,
    /// `key=value` file naming what is currently applied.
    pub state_file: Option<String>,
}

impl Default for Sources {
    fn default() -> Self {
        Self {
            themes_dir: None,
            wallpaper_dirs: vec!["~/Pictures/Wallpapers".into()],
            state_file: None,
        }
    }
}

/// The commands run when something is selected. `{}` is replaced with the theme
/// name or the wallpaper path.
#[derive(Debug, Clone, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct Actions {
    pub apply_theme: Vec<String>,
    pub apply_wallpaper: Vec<String>,
}

impl Default for Actions {
    fn default() -> Self {
        Self {
            apply_theme: vec!["osr".into(), "theme".into(), "{}".into()],
            apply_wallpaper: vec!["osr".into(), "wallpaper".into(), "{}".into()],
        }
    }
}

#[derive(Debug, Clone, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct Behavior {
    pub window: WindowMode,
    pub renderer: RendererChoice,
    /// Close the picker after applying. Off keeps it open to try several.
    pub close_on_apply: bool,
    /// Apply as the cursor moves, without waiting for Enter.
    pub apply_on_move: bool,
}

impl Default for Behavior {
    fn default() -> Self {
        Self {
            window: WindowMode::default(),
            renderer: RendererChoice::default(),
            close_on_apply: true,
            // Off by default: live-applying on every arrow key would run a
            // theme switch per keystroke, and holding Down would queue dozens.
            apply_on_move: false,
        }
    }
}

/// Preview caching. Decoded wallpapers are big; these two numbers decide how
/// much stays in RAM and how much is kept on disk so it need not be decoded
/// again next time.
#[derive(Debug, Clone, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct Cache {
    /// Megabytes of decoded previews held in memory. 0 means unbounded.
    pub memory_mb: u64,
    /// Megabytes of downscaled thumbnails kept on disk. 0 disables the disk
    /// cache entirely (every run then re-decodes).
    pub disk_mb: u64,
    /// Where the thumbnails live; unset means $XDG_CACHE_HOME/proteus/thumbs.
    pub dir: Option<String>,
}

impl Default for Cache {
    fn default() -> Self {
        Self {
            memory_mb: (crate::images::DEFAULT_BUDGET / (1024 * 1024)) as u64,
            disk_mb: crate::images::DEFAULT_DISK_BUDGET / (1024 * 1024),
            dir: None,
        }
    }
}

impl Cache {
    pub fn memory_bytes(&self) -> usize {
        (self.memory_mb as usize).saturating_mul(1024 * 1024)
    }

    pub fn disk_bytes(&self) -> u64 {
        self.disk_mb.saturating_mul(1024 * 1024)
    }

    /// The thumbnail directory, or `None` when the disk cache is off.
    pub fn dir(&self) -> Option<std::path::PathBuf> {
        if self.disk_mb == 0 {
            return None;
        }
        match &self.dir {
            Some(d) => Some(expand_path(d)),
            None => crate::images::ImageStore::default_cache_dir(),
        }
    }
}

#[derive(Debug, Clone, Default, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct Config {
    pub sources: Sources,
    pub actions: Actions,
    pub style: Style,
    pub behavior: Behavior,
    pub cache: Cache,
}

/// Expand a leading `~` and `$HOME`. Deliberately not a full shell expansion:
/// a config value is a path, not a command line.
pub fn expand_path(s: &str) -> PathBuf {
    let home = std::env::var("HOME").unwrap_or_default();
    if home.is_empty() {
        return PathBuf::from(s);
    }
    if let Some(rest) = s.strip_prefix("~/") {
        return PathBuf::from(&home).join(rest);
    }
    if s == "~" {
        return PathBuf::from(&home);
    }
    if let Some(rest) = s.strip_prefix("$HOME/") {
        return PathBuf::from(&home).join(rest);
    }
    PathBuf::from(s)
}

impl Config {
    /// Parse TOML. On error, returns the defaults plus the message — never
    /// nothing, because the picker has to open for the user to fix the config.
    pub fn parse(text: &str) -> (Config, Option<String>) {
        match toml::from_str::<Config>(text) {
            Ok(mut c) => {
                c.style.sanitize();
                (c, None)
            }
            Err(e) => {
                let mut c = Config::default();
                c.style.sanitize();
                (c, Some(e.to_string()))
            }
        }
    }

    /// Load from `path`, or return defaults when it does not exist (running
    /// with no config at all is the common case, not an error).
    pub fn load(path: &Path) -> (Config, Option<String>) {
        match std::fs::read_to_string(path) {
            Ok(text) => Config::parse(&text),
            Err(_) => {
                let mut c = Config::default();
                c.style.sanitize();
                (c, None)
            }
        }
    }

    /// The config file Proteus reads when none is given: `$XDG_CONFIG_HOME` or
    /// `~/.config`, then `proteus/proteus.toml`.
    pub fn default_path() -> PathBuf {
        let base = std::env::var("XDG_CONFIG_HOME")
            .ok()
            .filter(|s| !s.is_empty())
            .map(PathBuf::from)
            .unwrap_or_else(|| expand_path("~/.config"));
        base.join("proteus").join("proteus.toml")
    }

    /// Resolve the themes directory, searching the usual places when the config
    /// does not name one.
    pub fn resolve_themes_dir(&self) -> Option<PathBuf> {
        if let Some(d) = &self.sources.themes_dir {
            let p = expand_path(d);
            return p.is_dir().then_some(p);
        }
        for candidate in [
            "~/.config/osr/themes",
            "~/projects/dotfiles/os-rice/themes",
            "~/dotfiles/os-rice/themes",
            "/usr/share/os-rice/themes",
        ] {
            let p = expand_path(candidate);
            if p.is_dir() {
                return Some(p);
            }
        }
        None
    }

    pub fn resolve_state_file(&self) -> PathBuf {
        self.sources
            .state_file
            .as_deref()
            .map(expand_path)
            .unwrap_or_else(|| expand_path("~/.config/osr/state"))
    }

    pub fn resolve_wallpaper_dirs(&self) -> Vec<PathBuf> {
        self.sources
            .wallpaper_dirs
            .iter()
            .map(|s| expand_path(s))
            .filter(|p| p.is_dir())
            .collect()
    }
}

/// Substitute `{}` in a command template.
///
/// The argument vector is passed to exec directly — there is no shell, so a
/// wallpaper called `; rm -rf ~` is an ordinary file name and not a command.
/// That is the reason the config takes a list rather than a string.
pub fn build_command(template: &[String], arg: &str) -> Vec<String> {
    if template.is_empty() {
        return Vec::new();
    }
    template
        .iter()
        .map(|part| part.replace("{}", arg))
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn an_empty_config_is_valid_and_gives_defaults() {
        let (c, err) = Config::parse("");
        assert!(err.is_none());
        assert_eq!(c.style.rows, default_rows());
        assert_eq!(c.behavior.window, WindowMode::Overlay);
        assert_eq!(c.behavior.renderer, RendererChoice::Auto);
        assert_eq!(c.actions.apply_theme, vec!["osr", "theme", "{}"]);
    }

    #[test]
    fn partial_tables_only_override_what_they_name() {
        let (c, err) = Config::parse(
            r##"
            [style]
            rows = 10
            accent = "#ff00ff"
            "##,
        );
        assert!(err.is_none(), "{err:?}");
        assert_eq!(c.style.rows, 10);
        assert_eq!(c.style.accent_override(), Some(Rgb::new(255, 0, 255)));
        // Untouched keys keep their defaults.
        assert_eq!(c.style.font_size, default_font_size());
        assert_eq!(c.style.width, default_width());
    }

    #[test]
    fn a_broken_config_still_yields_a_usable_picker() {
        let (c, err) = Config::parse("this is not toml [[[");
        assert!(err.is_some(), "the error is reported");
        // ...and the picker can still open, which is how you get to fix it.
        assert_eq!(c.style.rows, default_rows());
        assert!(c.style.width > 0.0);

        // A wrongly-typed value is the same story.
        let (c2, err2) = Config::parse("[style]\nrows = \"lots\"\n");
        assert!(err2.is_some());
        assert_eq!(c2.style.rows, default_rows());

        // An unknown key is reported rather than silently ignored: a typo'd
        // setting that does nothing is the hardest kind of config bug.
        let (_, err3) = Config::parse("[style]\nraduis = 4\n");
        assert!(err3.is_some(), "an unknown key must be reported");
    }

    #[test]
    fn nonsense_values_are_clamped_not_obeyed() {
        let (c, _) = Config::parse(
            r#"
            [style]
            rows = 0
            radius = -50.0
            opacity = 9.0
            font_size = 0.0
            preview_ratio = 5.0
            width = 1.0
            "#,
        );
        assert_eq!(c.style.rows, 1, "zero rows would divide by zero in layout");
        assert!(
            c.style.radius >= 0.0,
            "a negative radius makes an inside-out path"
        );
        assert!(
            c.style.opacity <= 1.0,
            "opacity > 1 is a protocol error on Wayland"
        );
        assert!(c.style.font_size >= 6.0);
        assert!(
            c.style.preview_ratio <= 0.8,
            "the preview may not eat the whole list"
        );
        assert!(c.style.width >= 200.0);
    }

    #[test]
    fn colours_parse_and_bad_ones_fall_back() {
        let (c, _) = Config::parse(
            r##"
            [style]
            bg = "#101020"
            fg = "not a colour"
            "##,
        );
        assert_eq!(c.style.bg_override(), Some(Rgb::new(0x10, 0x10, 0x20)));
        assert_eq!(
            c.style.fg_override(),
            None,
            "an unparseable colour is simply unset"
        );
    }

    #[test]
    fn renderer_and_window_mode_parse_from_names() {
        let (c, err) = Config::parse(
            r#"
            [behavior]
            renderer = "gl"
            window = "toplevel"
            close_on_apply = false
            "#,
        );
        assert!(err.is_none(), "{err:?}");
        assert_eq!(c.behavior.renderer, RendererChoice::Gl);
        assert_eq!(c.behavior.window, WindowMode::Toplevel);
        assert!(!c.behavior.close_on_apply);

        assert_eq!(RendererChoice::parse("WGPU"), Some(RendererChoice::Wgpu));
        assert_eq!(RendererChoice::parse("software"), Some(RendererChoice::Cpu));
        assert_eq!(RendererChoice::parse("nonsense"), None);
    }

    #[test]
    fn paths_expand_home() {
        std::env::set_var("HOME", "/home/tester");
        assert_eq!(
            expand_path("~/Pictures"),
            PathBuf::from("/home/tester/Pictures")
        );
        assert_eq!(expand_path("~"), PathBuf::from("/home/tester"));
        assert_eq!(expand_path("$HOME/x"), PathBuf::from("/home/tester/x"));
        assert_eq!(expand_path("/abs/path"), PathBuf::from("/abs/path"));
        // A tilde that is not a home reference is left alone.
        assert_eq!(expand_path("~weird/x"), PathBuf::from("~weird/x"));
    }

    #[test]
    fn commands_substitute_the_argument() {
        let t = vec!["osr".to_string(), "theme".to_string(), "{}".to_string()];
        assert_eq!(build_command(&t, "nord"), vec!["osr", "theme", "nord"]);
        // Substitution happens inside an argument too.
        let t2 = vec!["sh".to_string(), "-c".to_string(), "set-bg {}".to_string()];
        assert_eq!(build_command(&t2, "/a/b.png")[2], "set-bg /a/b.png");
        assert!(build_command(&[], "x").is_empty());
    }

    /// The argv is handed to exec, never to a shell. A file name that looks like
    /// shell syntax must stay one argument.
    #[test]
    fn a_hostile_filename_stays_a_single_argument() {
        let t = vec!["osr".to_string(), "wallpaper".to_string(), "{}".to_string()];
        let evil = "/tmp/; rm -rf ~/.config; echo .png";
        let cmd = build_command(&t, evil);
        assert_eq!(cmd.len(), 3, "no splitting on ; or spaces");
        assert_eq!(cmd[2], evil);
    }

    #[test]
    fn custom_sources_and_actions_are_honoured() {
        let (c, err) = Config::parse(
            r#"
            [sources]
            themes_dir = "/opt/themes"
            wallpaper_dirs = ["/srv/walls"]
            state_file = "/var/lib/state"

            [actions]
            apply_theme = ["my-script", "--theme", "{}"]
            "#,
        );
        assert!(err.is_none(), "{err:?}");
        assert_eq!(c.sources.themes_dir.as_deref(), Some("/opt/themes"));
        assert_eq!(c.resolve_state_file(), PathBuf::from("/var/lib/state"));
        assert_eq!(
            build_command(&c.actions.apply_theme, "x"),
            vec!["my-script", "--theme", "x"]
        );
        // The wallpaper action was not named, so it keeps its default.
        assert_eq!(c.actions.apply_wallpaper[0], "osr");
    }

    #[test]
    fn a_configured_themes_dir_that_does_not_exist_resolves_to_none() {
        let (c, _) = Config::parse("[sources]\nthemes_dir = \"/definitely/not/here\"\n");
        assert_eq!(
            c.resolve_themes_dir(),
            None,
            "so the caller can say so, not crash"
        );
    }
}
