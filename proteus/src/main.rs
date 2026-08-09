//! Proteus — a theme and wallpaper picker for X11 and Wayland.

use std::path::PathBuf;
use std::process::ExitCode;

use proteus::app::App;
use proteus::config::{Config, RendererChoice, WindowMode};
use proteus::render::cpu::CpuRenderer;
use proteus::run::new_image_store;
use proteus::text::TextEngine;
use proteus::ui::{build_scene, Layout};

const USAGE: &str = "\
proteus - a theme and wallpaper picker for X11 and Wayland

USAGE:
    proteus [OPTIONS]

OPTIONS:
    -c, --config <PATH>     config file (default: ~/.config/proteus/proteus.toml)
        --themes-dir <DIR>  override the themes directory
    -r, --renderer <NAME>   auto | wgpu | gl | cpu   (default: auto)
    -w, --window <MODE>     overlay | toplevel       (default: overlay)
        --size <WxH>        window size, e.g. 900x520
        --screenshot <PNG>  render one frame to a PNG and exit (no display needed)
        --list              print the themes found and exit
        --print-only        print the command an Enter would run, without running it
    -h, --help              this text
    -V, --version           version
";

struct Args {
    config: Option<PathBuf>,
    themes_dir: Option<String>,
    renderer: Option<RendererChoice>,
    window: Option<WindowMode>,
    size: Option<(f32, f32)>,
    screenshot: Option<PathBuf>,
    list: bool,
    print_only: bool,
}

fn parse_args() -> Result<Option<Args>, String> {
    let mut a = Args {
        config: None,
        themes_dir: None,
        renderer: None,
        window: None,
        size: None,
        screenshot: None,
        list: false,
        print_only: false,
    };
    let mut it = std::env::args().skip(1);
    while let Some(arg) = it.next() {
        let mut next = |what: &str| it.next().ok_or_else(|| format!("{what} needs a value"));
        match arg.as_str() {
            "-h" | "--help" => {
                print!("{USAGE}");
                return Ok(None);
            }
            "-V" | "--version" => {
                println!("proteus {}", env!("CARGO_PKG_VERSION"));
                return Ok(None);
            }
            "-c" | "--config" => a.config = Some(PathBuf::from(next("--config")?)),
            "--themes-dir" => a.themes_dir = Some(next("--themes-dir")?),
            "-r" | "--renderer" => {
                let v = next("--renderer")?;
                a.renderer = Some(
                    RendererChoice::parse(&v)
                        .ok_or_else(|| format!("unknown renderer '{v}' (auto|wgpu|gl|cpu)"))?,
                );
            }
            "-w" | "--window" => {
                let v = next("--window")?;
                a.window = Some(match v.as_str() {
                    "overlay" => WindowMode::Overlay,
                    "toplevel" => WindowMode::Toplevel,
                    _ => return Err(format!("unknown window mode '{v}' (overlay|toplevel)")),
                });
            }
            "--size" => {
                let v = next("--size")?;
                let (w, h) = v
                    .split_once(['x', 'X'])
                    .ok_or_else(|| format!("--size wants WxH, got '{v}'"))?;
                a.size = Some((
                    w.trim()
                        .parse()
                        .map_err(|_| format!("bad width in '{v}'"))?,
                    h.trim()
                        .parse()
                        .map_err(|_| format!("bad height in '{v}'"))?,
                ));
            }
            "--screenshot" => a.screenshot = Some(PathBuf::from(next("--screenshot")?)),
            "--list" => a.list = true,
            "--print-only" => a.print_only = true,
            other => return Err(format!("unknown option '{other}' (try --help)")),
        }
    }
    Ok(Some(a))
}

fn main() -> ExitCode {
    let args = match parse_args() {
        Ok(Some(a)) => a,
        Ok(None) => return ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("proteus: {e}");
            return ExitCode::from(2);
        }
    };

    let config_path = args.config.clone().unwrap_or_else(Config::default_path);
    let (mut config, config_err) = Config::load(&config_path);
    let mut warnings = Vec::new();
    if let Some(e) = config_err {
        // Not fatal: a picker that will not open because of its own config is a
        // picker you cannot use to fix the config.
        warnings.push(format!("{}: {e}", config_path.display()));
    }

    // Command line beats config file.
    if let Some(d) = args.themes_dir {
        config.sources.themes_dir = Some(d);
    }
    if let Some(r) = args.renderer {
        config.behavior.renderer = r;
    }
    if let Some(w) = args.window {
        config.behavior.window = w;
    }
    if let Some((w, h)) = args.size {
        config.style.width = w;
        config.style.height = h;
    }
    config.style.sanitize();

    let app = App::load(config, warnings);
    for w in &app.warnings {
        eprintln!("proteus: {w}");
    }

    if args.list {
        for t in &app.model.themes {
            println!(
                "{:<12} {:<10} {}",
                t.name,
                format!("{:?}", t.session).to_lowercase(),
                t.description
            );
        }
        return ExitCode::SUCCESS;
    }

    if args.print_only {
        match app.command_for(&app.model.activate()) {
            Some(cmd) => println!("{}", cmd.join(" ")),
            None => println!("(nothing to apply)"),
        }
        return ExitCode::SUCCESS;
    }

    if let Some(path) = args.screenshot {
        return match screenshot(&app, &path) {
            Ok(()) => {
                println!("wrote {}", path.display());
                ExitCode::SUCCESS
            }
            Err(e) => {
                eprintln!("proteus: {e}");
                ExitCode::FAILURE
            }
        };
    }

    match proteus::run::run(app) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("proteus: {e}");
            ExitCode::FAILURE
        }
    }
}

/// Render one frame headlessly. This is the same scene the window shows, which
/// is what makes it useful for both `--screenshot` and the test suite.
fn screenshot(app: &App, path: &std::path::Path) -> Result<(), String> {
    let style = &app.config.style;
    let mut text = TextEngine::new(style.font.clone());
    let layout = Layout::compute(style.width, style.height, style, &text);
    let mut model = app.model.clone_for_render();
    model.set_rows_visible(layout.rows_visible());

    let mut images = new_image_store(&app.config);
    let scene = build_scene(&model, style, &layout, &mut images, &mut text);
    let mut r = CpuRenderer::new(style.width as u32, style.height as u32)
        .ok_or_else(|| "could not allocate the frame buffer".to_string())?;
    r.draw(&scene, &images, &mut text);
    r.save_png(path)
}
