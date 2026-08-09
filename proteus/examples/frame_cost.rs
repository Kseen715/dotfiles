use proteus::{
    app::App,
    config::Config,
    images::ImageStore,
    render::cpu::CpuRenderer,
    text::TextEngine,
    ui::{build_scene, Layout},
};
use std::time::Instant;
fn main() {
    let (cfg, _) = Config::parse(&format!(
        "[sources]\nthemes_dir = \"{}\"\n",
        std::env::args().nth(1).unwrap()
    ));
    let app = App::load(cfg, vec![]);
    let style = app.config.style.clone();
    let mut text = TextEngine::new(None);
    let layout = Layout::compute(style.width, style.height, &style, &text);
    let mut model = app.model.clone_for_render();
    model.set_rows_visible(layout.rows_visible());
    let mut images = ImageStore::new(1024);
    let mut r = CpuRenderer::new(style.width as u32, style.height as u32).unwrap();
    for _ in 0..3 {
        let s = build_scene(&model, &style, &layout, &mut images, &mut text);
        r.draw(&s, &images, &mut text);
    }

    let n = 60;
    let t = Instant::now();
    for _ in 0..n {
        let _ = build_scene(&model, &style, &layout, &mut images, &mut text);
    }
    let build = t.elapsed().as_secs_f64() * 1000.0 / n as f64;

    let scene = build_scene(&model, &style, &layout, &mut images, &mut text);
    let t = Instant::now();
    for _ in 0..n {
        r.draw(&scene, &images, &mut text);
    }
    let draw = t.elapsed().as_secs_f64() * 1000.0 / n as f64;

    let t = Instant::now();
    for _ in 0..n {
        let _ = r.bgra_premultiplied();
    }
    let conv = t.elapsed().as_secs_f64() * 1000.0 / n as f64;

    println!("scene build : {build:6.2} ms");
    println!("rasterise   : {draw:6.2} ms");
    println!("BGRA convert: {conv:6.2} ms");
    println!("total       : {:6.2} ms", build + draw + conv);
}
