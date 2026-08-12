//! Wiring: turn a config into a loaded catalogue, a model, and applied actions.
//!
//! This is the part that knows Proteus is driving something. Everything it does
//! is expressed as "run this argv" — there is no shell, no string splitting, and
//! no assumption that the thing being run is os-rice.

use std::path::PathBuf;
use std::process::Command;

use crate::catalog::{self, Session, Theme};
use crate::config::{build_command, Config};
use crate::model::{Action, Model};

/// Everything loaded from disk for one run.
pub struct App {
    pub config: Config,
    pub model: Model,
    /// Non-fatal problems worth telling the user about (bad config, no themes).
    pub warnings: Vec<String>,
}

/// Detect the display server from the environment.
///
/// `WAYLAND_DISPLAY` is checked first because a Wayland session usually also has
/// `DISPLAY` set for XWayland; treating that as X11 would mark every Wayland
/// theme as off-session on the machine it was built for.
pub fn detect_session() -> Session {
    let wayland = std::env::var("WAYLAND_DISPLAY")
        .map(|v| !v.is_empty())
        .unwrap_or(false);
    let x11 = std::env::var("DISPLAY")
        .map(|v| !v.is_empty())
        .unwrap_or(false);
    match (wayland, x11) {
        (true, _) => Session::Wayland,
        (false, true) => Session::X11,
        // No display server: everything "fits", because nothing is excluded by
        // a session we cannot see.
        (false, false) => Session::Any,
    }
}

/// Collect wallpapers: every theme's own, then the configured directories.
/// Deduplicated by file name so the library the user browses matches what
/// os-rice would offer.
fn collect_wallpapers(themes: &[Theme], dirs: &[PathBuf]) -> Vec<PathBuf> {
    let mut seen = std::collections::HashSet::new();
    let mut out = Vec::new();
    for t in themes {
        for w in &t.wallpapers {
            if let Some(name) = w.file_name() {
                if seen.insert(name.to_os_string()) {
                    out.push(w.clone());
                }
            }
        }
    }
    for d in dirs {
        for w in catalog::images_in(d) {
            if let Some(name) = w.file_name() {
                if seen.insert(name.to_os_string()) {
                    out.push(w.clone());
                }
            }
        }
    }
    out
}

impl App {
    pub fn load(config: Config, mut warnings: Vec<String>) -> App {
        let themes = match config.resolve_themes_dir() {
            Some(dir) => {
                let t = catalog::load_themes(&dir);
                if t.is_empty() {
                    warnings.push(format!("no themes found in {}", dir.display()));
                }
                t
            }
            None => {
                warnings.push(
                    "no themes directory found - set [sources] themes_dir in the config".into(),
                );
                Vec::new()
            }
        };

        let wallpapers = collect_wallpapers(&themes, &config.resolve_wallpaper_dirs());
        let mut model = Model::new(themes, wallpapers, detect_session());

        // What is applied right now, so the list can mark it.
        let state = catalog::read_state(&config.resolve_state_file());
        model.current_theme = state.get("theme").cloned();
        model.current_wallpaper = state.get("wallpaper").map(PathBuf::from);
        model.refilter();

        App {
            config,
            model,
            warnings,
        }
    }

    /// The argv an action would run, without running it. Used by `--print-only`
    /// and by the tests, so what is asserted is exactly what would execute.
    pub fn command_for(&self, action: &Action) -> Option<Vec<String>> {
        let cmd = match action {
            Action::ApplyTheme(name) => build_command(&self.config.actions.apply_theme, name),
            Action::ApplyWallpaper(path) => build_command(
                &self.config.actions.apply_wallpaper,
                &path.to_string_lossy(),
            ),
            _ => return None,
        };
        (!cmd.is_empty()).then_some(cmd)
    }

    /// Run an action. Returns the error text on failure — a failed apply must
    /// be reported, not swallowed, or the picker silently does nothing.
    pub fn run(&self, action: &Action) -> Result<(), String> {
        let Some(argv) = self.command_for(action) else {
            return Ok(());
        };
        // No shell: argv[0] is the program and the rest are literal arguments,
        // so a wallpaper named `; rm -rf ~` is a file name and nothing else.
        let status = Command::new(&argv[0])
            .args(&argv[1..])
            .status()
            .map_err(|e| format!("{}: {e}", argv[0]))?;
        if status.success() {
            Ok(())
        } else {
            Err(format!("{} exited with {}", argv[0], status))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    fn tmpdir(tag: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!(
            "proteus-app-{tag}-{}-{:?}",
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
    use std::path::Path;

    fn png(path: &Path) {
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        image::RgbaImage::new(4, 4).save(path).unwrap();
    }

    /// A themes dir with two themes, one carrying two wallpapers.
    fn fixture() -> PathBuf {
        let d = tmpdir("fix");
        write(
            &d.join("themes/nord/theme.list"),
            "display: Nord\ndescription: cold\ncolor: background #2e3440\ncolor: accent #88c0d0\n",
        );
        png(&d.join("themes/nord/wallpapers/ice.png"));
        png(&d.join("themes/nord/wallpapers/snow.png"));
        write(
            &d.join("themes/xin/theme.list"),
            "display: Xin\nsession: x11\n",
        );
        // A library dir with one more image and one non-image.
        png(&d.join("walls/extra.png"));
        write(&d.join("walls/notes.txt"), "not an image");
        d
    }

    fn app_for(d: &Path, extra: &str) -> App {
        let toml = format!(
            "[sources]\nthemes_dir = \"{}\"\nwallpaper_dirs = [\"{}\"]\nstate_file = \"{}\"\n{}",
            d.join("themes").display(),
            d.join("walls").display(),
            d.join("state").display(),
            extra
        );
        let (cfg, err) = Config::parse(&toml);
        assert!(err.is_none(), "{err:?}");
        App::load(cfg, Vec::new())
    }

    #[test]
    fn loads_themes_and_wallpapers_from_the_configured_places() {
        let d = fixture();
        let app = app_for(&d, "");
        assert_eq!(app.model.themes.len(), 2);
        assert_eq!(app.model.themes[0].name, "nord");
        // Two theme wallpapers plus the library image; the .txt is not an image.
        assert_eq!(app.model.wallpapers.len(), 3);
        assert!(app.warnings.is_empty(), "{:?}", app.warnings);
        fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn marks_what_the_state_file_says_is_applied() {
        let d = fixture();
        fs::write(d.join("state"), "rice=xin\ntheme=nord\n").unwrap();
        let app = app_for(&d, "");
        assert_eq!(app.model.current_theme.as_deref(), Some("nord"));
        let marked: Vec<String> = app
            .model
            .rows()
            .into_iter()
            .filter(|r| r.current)
            .map(|r| r.title)
            .collect();
        assert_eq!(marked, vec!["Nord"]);
        fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn a_missing_themes_dir_warns_instead_of_failing() {
        let (cfg, _) = Config::parse("[sources]\nthemes_dir = \"/nope/nothing\"\n");
        let app = App::load(cfg, Vec::new());
        assert!(app.model.themes.is_empty());
        assert!(
            !app.warnings.is_empty(),
            "the user must be told why the list is empty"
        );
        // ...and the picker is still operable.
        assert_eq!(app.model.activate(), Action::None);
    }

    #[test]
    fn builds_the_argv_that_would_run() {
        let d = fixture();
        let app = app_for(&d, "");
        let cmd = app.command_for(&Action::ApplyTheme("nord".into())).unwrap();
        assert_eq!(cmd, vec!["osr", "theme", "nord"]);
        let cmd = app
            .command_for(&Action::ApplyWallpaper(PathBuf::from("/w/a b.png")))
            .unwrap();
        assert_eq!(
            cmd,
            vec!["osr", "wallpaper", "/w/a b.png"],
            "spaces stay one argument"
        );
        assert_eq!(app.command_for(&Action::Quit), None);
        fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn a_custom_action_replaces_the_default() {
        let d = fixture();
        let app = app_for(
            &d,
            "[actions]\napply_theme = [\"echo\", \"picked\", \"{}\"]\n",
        );
        assert_eq!(
            app.command_for(&Action::ApplyTheme("xin".into())).unwrap(),
            vec!["echo", "picked", "xin"]
        );
        fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn running_an_action_reports_failure_rather_than_swallowing_it() {
        let d = fixture();
        // A program that does not exist.
        let app = app_for(
            &d,
            "[actions]\napply_theme = [\"proteus-no-such-program\", \"{}\"]\n",
        );
        let err = app.run(&Action::ApplyTheme("nord".into())).unwrap_err();
        assert!(err.contains("proteus-no-such-program"), "got: {err}");

        // A program that runs and fails.
        let app2 = app_for(&d, "[actions]\napply_theme = [\"false\"]\n");
        assert!(app2.run(&Action::ApplyTheme("nord".into())).is_err());

        // A program that succeeds.
        let app3 = app_for(&d, "[actions]\napply_theme = [\"true\"]\n");
        assert!(app3.run(&Action::ApplyTheme("nord".into())).is_ok());
        fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn an_empty_action_template_runs_nothing_and_succeeds() {
        let d = fixture();
        let app = app_for(&d, "[actions]\napply_theme = []\n");
        assert_eq!(app.command_for(&Action::ApplyTheme("nord".into())), None);
        assert!(app.run(&Action::ApplyTheme("nord".into())).is_ok());
        fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn session_detection_prefers_wayland() {
        // A Wayland session normally also exports DISPLAY for XWayland; calling
        // that X11 would mark every Wayland theme as off-session.
        std::env::set_var("WAYLAND_DISPLAY", "wayland-0");
        std::env::set_var("DISPLAY", ":0");
        assert_eq!(detect_session(), Session::Wayland);

        std::env::remove_var("WAYLAND_DISPLAY");
        assert_eq!(detect_session(), Session::X11);

        std::env::remove_var("DISPLAY");
        assert_eq!(detect_session(), Session::Any);
    }
}
