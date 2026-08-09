//! Renders the strip at several selections and mid-slide, for eyeballing.
use proteus::{
    anim::FRAME,
    app::App,
    config::Config,
    images::ImageStore,
    render::cpu::CpuRenderer,
    text::TextEngine,
    ui::{build_scene, Layout},
};
fn main() {
    let dir = std::env::args().nth(1).unwrap();
    let out = std::env::args().nth(2).unwrap();
    let (cfg, _) = Config::parse(&format!("[sources]\nthemes_dir = \"{dir}\"\n"));
    let app = App::load(cfg, vec![]);
    let style = app.config.style.clone();
    let mut text = TextEngine::new(style.font.clone());
    let layout = Layout::compute(style.width, style.height, &style, &text);
    let mut images = ImageStore::new(1024);
    let mut r = CpuRenderer::new(style.width as u32, style.height as u32).unwrap();
    let mut shot =
        |m: &proteus::model::Model, name: &str, images: &mut ImageStore, text: &mut TextEngine| {
            let s = build_scene(m, &style, &layout, images, text);
            r.draw(&s, images, text);
            r.save_png(std::path::Path::new(&format!("{out}/{name}.png")))
                .unwrap();
        };
    // Selection in the middle of the catalogue.
    let mut m = app.model.clone_for_render();
    m.set_view(layout.rows_visible(), layout.advance);
    m.move_to(1);
    while m.tick(FRAME) {}
    shot(&m, "strip-glass", &mut images, &mut text);
    // Mid-slide toward the last item.
    m.move_to(5);
    for _ in 0..3 {
        m.tick(FRAME);
    }
    shot(&m, "strip-sliding", &mut images, &mut text);
    // Settled at the end.
    while m.tick(FRAME) {}
    shot(&m, "strip-end", &mut images, &mut text);
    println!("wrote strip-mid, strip-sliding, strip-end");
}
