//! CPU rasteriser — the reference renderer.
//!
//! Two jobs, and the second is the reason it exists at all:
//!
//! 1. It is the fallback for hardware with no usable GL/Vulkan at all (very old
//!    iGPUs, a VM with no 3D, a broken driver). A picker that refuses to open
//!    because of a driver is worse than a picker that opens at 60fps on a CPU —
//!    and for a static list with a few images, the CPU is plenty.
//! 2. It is the definition of correct. The GPU backends are checked against
//!    images produced here, so "the wgpu path draws the selection bar 1px off"
//!    is a test failure rather than something you notice a month later.

use std::collections::HashMap;

use tiny_skia::{
    FillRule, Paint, PathBuilder, Pixmap, PixmapPaint, Rect as SkRect, Stroke, Transform,
};

use crate::images::ImageStore;
use crate::scene::{Align, Cmd, Color, Fit, Rect, Scene};
use crate::text::TextEngine;

/// Rasterises a [`Scene`] into an RGBA8 pixmap.
pub struct CpuRenderer {
    pixmap: Pixmap,
    /// Images composed at their final on-screen size: premultiplied, scaled and
    /// corner-rounded, ready to blit.
    ///
    /// Without this, every frame re-premultiplied AND re-scaled every visible
    /// image - hundreds of thousands of pixels of arithmetic and resampling per
    /// frame, which was ~90% of the frame cost. It only became visible once
    /// scrolling animated: before that a frame was drawn per keystroke and
    /// nobody could see 10ms.
    tiles: HashMap<TileKey, Pixmap>,
    /// One reusable clip mask. Allocating a full-window mask per clipped command
    /// is a 0.5 MB allocation several times a frame.
    scratch_mask: Option<tiny_skia::Mask>,
    /// Reused output buffer for `rgba`/`bgra_premultiplied`.
    ///
    /// A 900x520 frame is 1.9 MB; handing back a fresh Vec every frame means
    /// allocating and freeing that at up to 60Hz while scrolling, which costs
    /// allocator work and doubles peak footprint for no reason.
    out: Vec<u8>,
}

fn sk_color(c: Color) -> tiny_skia::Color {
    tiny_skia::Color::from_rgba8(c.r, c.g, c.b, c.a)
}

fn sk_rect(r: Rect) -> Option<SkRect> {
    SkRect::from_xywh(r.x, r.y, r.w, r.h)
}

/// A rounded-rectangle path. Falls back to a plain rect when the radius is zero
/// or too large for the box, which keeps a squashed row from turning into a
/// lens shape.
fn rounded_path(r: Rect, radius: f32) -> Option<tiny_skia::Path> {
    let rad = radius.min(r.w / 2.0).min(r.h / 2.0).max(0.0);
    let mut pb = PathBuilder::new();
    if rad <= 0.05 {
        pb.push_rect(sk_rect(r)?);
        return pb.finish();
    }
    let (x, y, w, h) = (r.x, r.y, r.w, r.h);
    // kappa: the control-point distance that approximates a quarter circle.
    let k = rad * 0.5522847;
    pb.move_to(x + rad, y);
    pb.line_to(x + w - rad, y);
    pb.cubic_to(x + w - rad + k, y, x + w, y + rad - k, x + w, y + rad);
    pb.line_to(x + w, y + h - rad);
    pb.cubic_to(
        x + w,
        y + h - rad + k,
        x + w - rad + k,
        y + h,
        x + w - rad,
        y + h,
    );
    pb.line_to(x + rad, y + h);
    pb.cubic_to(x + rad - k, y + h, x, y + h - rad + k, x, y + h - rad);
    pb.line_to(x, y + rad);
    pb.cubic_to(x, y + rad - k, x + rad - k, y, x + rad, y);
    pb.close();
    pb.finish()
}

/// Identifies one composed image tile. Everything that changes the pixels is in
/// here, so a hit is always safe to blit as-is.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
struct TileKey {
    image: u32,
    tint: u32,
    w: u32,
    h: u32,
    /// Quarter-pixel resolution, so a radius that differs invisibly does not
    /// build a second tile.
    radius: u32,
    cover: bool,
    /// Horizontal shear in quarter-pixels, offset so it can be negative.
    skew: i32,
}

/// Compose an image into a tile of exactly the size it will be drawn at.
fn build_tile(bmp: &crate::images::Bitmap, key: &TileKey) -> Option<Pixmap> {
    // Premultiply and tint the source once.
    let mut src = Pixmap::new(bmp.width, bmp.height)?;
    let tint = key.tint.to_be_bytes();
    for (i, px) in bmp.pixels.chunks_exact(4).enumerate() {
        let a = px[3] as f32 / 255.0 * (tint[3] as f32 / 255.0);
        let m = |v: u8, t: u8| ((v as f32) * (t as f32 / 255.0) * a).round() as u8;
        src.pixels_mut()[i] = tiny_skia::PremultipliedColorU8::from_rgba(
            m(px[0], tint[0]),
            m(px[1], tint[1]),
            m(px[2], tint[2]),
            (a * 255.0).round() as u8,
        )
        .unwrap_or_else(|| tiny_skia::PremultipliedColorU8::from_rgba(0, 0, 0, 0).unwrap());
    }

    let mut tile = Pixmap::new(key.w, key.h)?;
    // Scale into the tile. `Cover` scales so the short axis fills and centres
    // the overflow; `Contain` was already fitted by the caller, so the tile IS
    // the image's shape and a straight scale is right.
    let (sx, sy, ox, oy) = if key.cover {
        let s = (key.w as f32 / bmp.width as f32).max(key.h as f32 / bmp.height as f32);
        (
            s,
            s,
            (key.w as f32 - bmp.width as f32 * s) / 2.0,
            (key.h as f32 - bmp.height as f32 * s) / 2.0,
        )
    } else {
        (
            key.w as f32 / bmp.width as f32,
            key.h as f32 / bmp.height as f32,
            0.0,
            0.0,
        )
    };
    tile.draw_pixmap(
        0,
        0,
        src.as_ref(),
        &PixmapPaint {
            quality: tiny_skia::FilterQuality::Bilinear,
            ..Default::default()
        },
        Transform::from_row(sx, 0.0, 0.0, sy, ox, oy),
        None,
    );

    // Bake the shape into the tile's alpha - rounded corners, or the leaning
    // parallelogram - so the frame path needs no mask for it.
    let radius = key.radius as f32 / 4.0;
    let skew = key.skew as f32 / 4.0;
    if radius > 0.05 || skew.abs() > 0.01 {
        let mut mask = tiny_skia::Mask::new(key.w, key.h)?;
        let full = Rect::new(0.0, 0.0, key.w as f32, key.h as f32);
        let path = if skew.abs() > 0.01 {
            // The tile is drawn at the shape's bounding box, so the shear is
            // expressed inside it: the top edge starts `skew` in from the left
            // and the bottom edge ends `skew` in from the right.
            let mut pb = PathBuilder::new();
            let s = skew.abs();
            if skew > 0.0 {
                pb.move_to(s, 0.0);
                pb.line_to(full.w, 0.0);
                pb.line_to(full.w - s, full.h);
                pb.line_to(0.0, full.h);
            } else {
                pb.move_to(0.0, 0.0);
                pb.line_to(full.w - s, 0.0);
                pb.line_to(full.w, full.h);
                pb.line_to(s, full.h);
            }
            pb.close();
            pb.finish()?
        } else {
            rounded_path(full, radius)?
        };
        mask.fill_path(&path, FillRule::Winding, true, Transform::identity());
        tile.apply_mask(&mask);
    }
    Some(tile)
}

/// The path of `rect` sheared horizontally by `skew`.
///
/// The bottom edge stays put and the top edge slides, so a row of sheared cards
/// sharing a baseline still lines up. Corners are cut rather than rounded: a
/// rounded parallelogram needs elliptical arcs to look right, and at card size
/// the difference is invisible.
fn skewed_path(r: Rect, skew: f32) -> Option<tiny_skia::Path> {
    if skew.abs() <= 0.01 {
        return rounded_path(r, 0.0);
    }
    let mut pb = PathBuilder::new();
    pb.move_to(r.x + skew, r.y);
    pb.line_to(r.right() + skew, r.y);
    pb.line_to(r.right(), r.bottom());
    pb.line_to(r.x, r.bottom());
    pb.close();
    pb.finish()
}

impl CpuRenderer {
    pub fn new(width: u32, height: u32) -> Option<Self> {
        Some(Self {
            pixmap: Pixmap::new(width.max(1), height.max(1))?,
            tiles: HashMap::new(),
            scratch_mask: None,
            out: Vec::new(),
        })
    }

    pub fn resize(&mut self, width: u32, height: u32) -> bool {
        if self.pixmap.width() == width && self.pixmap.height() == height {
            return true;
        }
        match Pixmap::new(width.max(1), height.max(1)) {
            Some(p) => {
                self.pixmap = p;
                // The mask is window-sized; the image cache is not, so it stays.
                self.scratch_mask = None;
                true
            }
            None => false,
        }
    }

    pub fn width(&self) -> u32 {
        self.pixmap.width()
    }

    pub fn height(&self) -> u32 {
        self.pixmap.height()
    }

    /// Straight RGBA8 pixels — what a GPU texture upload wants.
    ///
    /// Borrowed from a buffer this renderer owns and reuses, so a 60Hz scroll
    /// does not allocate and free two megabytes per frame.
    pub fn rgba(&mut self) -> &[u8] {
        // tiny-skia stores premultiplied; the demultiplied form is what every
        // consumer here expects, and getting this backwards shows up as haloed
        // text rather than as an obvious failure.
        let n = (self.pixmap.width() as usize) * (self.pixmap.height() as usize) * 4;
        self.out.clear();
        self.out.reserve(n);
        for p in self.pixmap.pixels() {
            let d = p.demultiply();
            self.out
                .extend_from_slice(&[d.red(), d.green(), d.blue(), d.alpha()]);
        }
        &self.out
    }

    /// Premultiplied BGRA — the layout X11 and wl_shm actually want.
    pub fn bgra_premultiplied(&mut self) -> &[u8] {
        let n = (self.pixmap.width() as usize) * (self.pixmap.height() as usize) * 4;
        self.out.clear();
        self.out.reserve(n);
        for p in self.pixmap.pixels() {
            self.out
                .extend_from_slice(&[p.blue(), p.green(), p.red(), p.alpha()]);
        }
        &self.out
    }

    pub fn draw(&mut self, scene: &Scene, images: &ImageStore, text: &mut TextEngine) {
        debug_assert!(
            scene.clips_balanced(),
            "unbalanced clip stack would corrupt every later frame"
        );
        self.pixmap.fill(sk_color(scene.background));

        // Clip stack, intersected as it is pushed so a nested clip can only ever
        // shrink the drawable area.
        let full = Rect::new(
            0.0,
            0.0,
            self.pixmap.width() as f32,
            self.pixmap.height() as f32,
        );
        let mut clips: Vec<Rect> = vec![full];

        for cmd in &scene.cmds {
            let clip = *clips.last().unwrap_or(&full);
            match cmd {
                Cmd::PushClip(r) => {
                    clips.push(clip.intersect(r));
                    continue;
                }
                Cmd::PopClip => {
                    if clips.len() > 1 {
                        clips.pop();
                    }
                    continue;
                }
                _ => {}
            }
            if clip.is_empty() {
                continue;
            }
            match cmd {
                Cmd::Rect {
                    rect,
                    color,
                    radius,
                } => {
                    self.fill_rounded(*rect, *color, *radius, clip);
                }
                Cmd::Parallelogram { rect, skew, color } => {
                    self.fill_skewed(*rect, *skew, *color, clip);
                }
                Cmd::Border {
                    rect,
                    color,
                    radius,
                    width,
                } => {
                    self.stroke_rounded(*rect, *color, *radius, *width, clip);
                }
                Cmd::Image {
                    rect,
                    image,
                    radius,
                    fit,
                    tint,
                    skew,
                } => {
                    self.draw_image(*rect, *image, *radius, *fit, *tint, *skew, images, clip);
                }
                Cmd::Text {
                    rect,
                    text: s,
                    color,
                    size,
                    align,
                    bold,
                } => {
                    self.draw_text(*rect, s, *color, *size, *align, *bold, text, clip);
                }
                Cmd::PushClip(_) | Cmd::PopClip => unreachable!("handled above"),
            }
        }
    }

    /// Whether `inner` is entirely inside `outer` - then clipping is a no-op and
    /// the mask can be skipped, which is the common case for every row that is
    /// not half-scrolled off the edge.
    fn fully_inside(inner: Rect, outer: Rect) -> bool {
        inner.x >= outer.x
            && inner.y >= outer.y
            && inner.right() <= outer.right()
            && inner.bottom() <= outer.bottom()
    }

    /// Take the shared clip mask, prepared for `clip`, or `None` when the shape
    /// being drawn is already inside it.
    ///
    /// The mask is moved OUT of `self` so the caller can hold it while also
    /// borrowing `self.pixmap` mutably, then handed back with `put_mask`.
    /// Cloning instead would copy half a megabyte per draw - the exact cost this
    /// is here to avoid.
    fn take_mask(&mut self, shape: Rect, clip: Rect) -> Option<tiny_skia::Mask> {
        if Self::fully_inside(shape, clip) {
            return None;
        }
        let (w, h) = (self.pixmap.width(), self.pixmap.height());
        let mut mask = self
            .scratch_mask
            .take()
            .filter(|m| m.width() == w && m.height() == h)
            .or_else(|| tiny_skia::Mask::new(w, h))?;
        mask.clear();
        let path = rounded_path(clip, 0.0)?;
        mask.fill_path(&path, FillRule::Winding, true, Transform::identity());
        Some(mask)
    }

    /// Return a mask taken by `take_mask` so the next draw can reuse it.
    fn put_mask(&mut self, mask: Option<tiny_skia::Mask>) {
        if let Some(m) = mask {
            self.scratch_mask = Some(m);
        }
    }

    fn fill_rounded(&mut self, r: Rect, color: Color, radius: f32, clip: Rect) {
        let Some(path) = rounded_path(r, radius) else {
            return;
        };
        let mut paint = Paint::default();
        paint.set_color(sk_color(color));
        paint.anti_alias = true;
        let mask = self.take_mask(r, clip);
        self.pixmap.fill_path(
            &path,
            &paint,
            FillRule::Winding,
            Transform::identity(),
            mask.as_ref(),
        );
        self.put_mask(mask);
    }

    fn fill_skewed(&mut self, r: Rect, skew: f32, color: Color, clip: Rect) {
        let Some(path) = skewed_path(r, skew) else {
            return;
        };
        let mut paint = Paint::default();
        paint.set_color(sk_color(color));
        paint.anti_alias = true;
        // The shear moves pixels outside `r`, so the clip test has to use the
        // shape's real bounds or a leaning card would be cut off at its own edge.
        let bounds = Rect::new(r.x + skew.min(0.0), r.y, r.w + skew.abs(), r.h);
        let mask = self.take_mask(bounds, clip);
        self.pixmap.fill_path(
            &path,
            &paint,
            FillRule::Winding,
            Transform::identity(),
            mask.as_ref(),
        );
        self.put_mask(mask);
    }

    fn stroke_rounded(&mut self, r: Rect, color: Color, radius: f32, width: f32, clip: Rect) {
        // Inset by half the stroke width so the border lands INSIDE the rect;
        // a centred stroke would bleed a half-pixel outside every selection bar.
        let Some(path) = rounded_path(r.inset(width / 2.0), (radius - width / 2.0).max(0.0)) else {
            return;
        };
        let mut paint = Paint::default();
        paint.set_color(sk_color(color));
        paint.anti_alias = true;
        let stroke = Stroke {
            width,
            ..Default::default()
        };
        let mask = self.take_mask(r, clip);
        self.pixmap
            .stroke_path(&path, &paint, &stroke, Transform::identity(), mask.as_ref());
        self.put_mask(mask);
    }

    /// Draw a cached, already-composed image tile.
    ///
    /// Everything that does not change between frames — premultiplying, scaling
    /// to the destination size, and the rounded-corner alpha — is done once and
    /// kept in `tiles`. A frame then blits the finished tile at an integer
    /// offset with no transform and no resampling, which is the difference
    /// between a picker that scrolls smoothly and one that does not.
    ///
    /// The cache key includes the destination size, so a window resize builds
    /// new tiles rather than rescaling the old ones every frame.
    #[allow(clippy::too_many_arguments)]
    fn draw_image(
        &mut self,
        rect: Rect,
        id: u32,
        radius: f32,
        fit: Fit,
        tint: Color,
        skew: f32,
        images: &ImageStore,
        clip: Rect,
    ) {
        let Some(bmp) = images.get(id) else {
            return; // an image that failed to decode simply does not draw
        };
        if bmp.width == 0 || bmp.height == 0 {
            return;
        }

        // Where the image lands. `Contain` letterboxes inside the rect, `Cover`
        // fills it and crops.
        let dst = match fit {
            Fit::Contain => rect.fit_aspect(bmp.aspect()),
            Fit::Cover => rect,
        };
        // A sheared card occupies a wider box than its rect: the tile is built
        // at that bounding box and the lean lives inside it.
        let dst = Rect::new(dst.x + skew.min(0.0), dst.y, dst.w + skew.abs(), dst.h);
        let (tw, th) = (dst.w.round().max(1.0) as u32, dst.h.round().max(1.0) as u32);

        let key = TileKey {
            image: id,
            tint: u32::from_be_bytes([tint.r, tint.g, tint.b, tint.a]),
            w: tw,
            h: th,
            radius: (radius * 4.0).round() as u32,
            cover: matches!(fit, Fit::Cover),
            skew: (skew * 4.0).round() as i32,
        };

        if let std::collections::hash_map::Entry::Vacant(slot) = self.tiles.entry(key) {
            let Some(tile) = build_tile(bmp, &key) else {
                return;
            };
            slot.insert(tile);
        }
        // Moved out rather than cloned: `self.pixmap` is borrowed mutably below,
        // and cloning would copy the tile every frame - exactly what the cache
        // exists to stop. It goes back before returning.
        let Some(tile) = self.tiles.remove(&key) else {
            return;
        };

        let visible = clip.intersect(&dst);
        if !visible.is_empty() {
            // A mask is only needed while a row is partly scrolled past the edge
            // of the list; the rounded corners are already in the tile's alpha.
            let mask = self.take_mask(dst, clip);
            self.pixmap.draw_pixmap(
                dst.x.round() as i32,
                dst.y.round() as i32,
                tile.as_ref(),
                &PixmapPaint::default(),
                Transform::identity(),
                mask.as_ref(),
            );
            self.put_mask(mask);
        }
        self.tiles.insert(key, tile);
    }

    #[allow(clippy::too_many_arguments)]
    fn draw_text(
        &mut self,
        rect: Rect,
        s: &str,
        color: Color,
        size: f32,
        align: Align,
        bold: bool,
        text: &mut TextEngine,
        clip: Rect,
    ) {
        // Never overflow the box: a long theme description must be cut, not
        // painted over the preview next to it.
        let s = text.ellipsize(s, size, bold, rect.w);
        if s.is_empty() {
            return;
        }
        let width = text.measure(&s, size, bold);
        let x = match align {
            Align::Left => rect.x,
            Align::Center => rect.x + (rect.w - width) / 2.0,
            Align::Right => rect.right() - width,
        };
        let line_h = text.line_height(size);
        let y = rect.y + (rect.h - line_h) / 2.0;

        let visible = clip.intersect(&rect);
        if visible.is_empty() {
            return;
        }

        for g in text.layout_weighted(&s, size, bold) {
            let gx = x + g.x as f32;
            let gy = y + g.y as f32;
            for row in 0..g.height {
                let py = gy + row as f32;
                if py < visible.y || py >= visible.bottom() {
                    continue;
                }
                for col in 0..g.width {
                    let px = gx + col as f32;
                    if px < visible.x || px >= visible.right() {
                        continue;
                    }
                    let cov = g.coverage[row * g.width + col];
                    if cov == 0 {
                        continue;
                    }
                    self.blend_pixel(px as i32, py as i32, color, cov);
                }
            }
        }
    }

    /// Source-over blend of one coverage-weighted pixel.
    fn blend_pixel(&mut self, x: i32, y: i32, color: Color, coverage: u8) {
        if x < 0 || y < 0 || x >= self.pixmap.width() as i32 || y >= self.pixmap.height() as i32 {
            return;
        }
        let a = (color.a as f32 / 255.0) * (coverage as f32 / 255.0);
        if a <= 0.0 {
            return;
        }
        let idx = (y as u32 * self.pixmap.width() + x as u32) as usize;
        let dst = self.pixmap.pixels()[idx].demultiply();
        let blend = |s: u8, d: u8| ((s as f32) * a + (d as f32) * (1.0 - a)).round() as u8;
        let out_a = (a * 255.0 + dst.alpha() as f32 * (1.0 - a)).round() as u8;
        let px = tiny_skia::ColorU8::from_rgba(
            blend(color.r, dst.red()),
            blend(color.g, dst.green()),
            blend(color.b, dst.blue()),
            out_a,
        );
        self.pixmap.pixels_mut()[idx] = px.premultiply();
    }

    /// Save to a PNG — used by `--screenshot` and by the golden tests.
    pub fn save_png(&self, path: &std::path::Path) -> Result<(), String> {
        self.pixmap.save_png(path).map_err(|e| e.to_string())
    }

    /// The colour at a pixel, demultiplied. For tests and for picking.
    pub fn pixel(&self, x: u32, y: u32) -> Color {
        if x >= self.pixmap.width() || y >= self.pixmap.height() {
            return Color::TRANSPARENT;
        }
        let p = self.pixmap.pixels()[(y * self.pixmap.width() + x) as usize].demultiply();
        Color::rgba(p.red(), p.green(), p.blue(), p.alpha())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::images::Bitmap;

    fn renderer() -> CpuRenderer {
        CpuRenderer::new(200, 100).unwrap()
    }

    fn empty_store() -> ImageStore {
        ImageStore::new(256)
    }

    #[test]
    fn fills_the_background() {
        let mut r = renderer();
        let scene = Scene::new(200.0, 100.0, Color::rgba(10, 20, 30, 255));
        r.draw(&scene, &empty_store(), &mut TextEngine::new(None));
        assert_eq!(r.pixel(0, 0), Color::rgba(10, 20, 30, 255));
        assert_eq!(r.pixel(199, 99), Color::rgba(10, 20, 30, 255));
    }

    #[test]
    fn draws_a_rect_where_asked_and_nowhere_else() {
        let mut r = renderer();
        let mut s = Scene::new(200.0, 100.0, Color::rgba(0, 0, 0, 255));
        s.rect(
            Rect::new(50.0, 20.0, 40.0, 30.0),
            Color::rgba(255, 0, 0, 255),
            0.0,
        );
        r.draw(&s, &empty_store(), &mut TextEngine::new(None));

        assert_eq!(
            r.pixel(60, 30),
            Color::rgba(255, 0, 0, 255),
            "inside is filled"
        );
        assert_eq!(r.pixel(49, 30).r, 0, "just left of the rect is untouched");
        assert_eq!(r.pixel(91, 30).r, 0, "just right of the rect is untouched");
        assert_eq!(r.pixel(60, 19).r, 0, "just above is untouched");
        assert_eq!(r.pixel(60, 51).r, 0, "just below is untouched");
    }

    #[test]
    fn rounded_corners_are_actually_rounded() {
        let mut r = renderer();
        let mut s = Scene::new(200.0, 100.0, Color::rgba(0, 0, 0, 255));
        s.rect(
            Rect::new(0.0, 0.0, 60.0, 60.0),
            Color::rgba(255, 255, 255, 255),
            20.0,
        );
        r.draw(&s, &empty_store(), &mut TextEngine::new(None));
        // The very corner is outside a 20px radius...
        assert!(r.pixel(1, 1).r < 128, "the corner pixel is cut away");
        // ...while the middle of the edge is solid.
        assert_eq!(r.pixel(30, 1).r, 255);
        assert_eq!(r.pixel(1, 30).r, 255);
    }

    #[test]
    fn a_radius_larger_than_the_box_does_not_explode() {
        let mut r = renderer();
        let mut s = Scene::new(200.0, 100.0, Color::rgba(0, 0, 0, 255));
        // Radius far larger than the rect: must clamp to a capsule, not vanish
        // or wrap into a degenerate path.
        s.rect(
            Rect::new(10.0, 10.0, 20.0, 10.0),
            Color::rgba(0, 255, 0, 255),
            999.0,
        );
        r.draw(&s, &empty_store(), &mut TextEngine::new(None));
        assert_eq!(r.pixel(20, 15).g, 255, "the centre is still painted");
    }

    #[test]
    fn clipping_confines_drawing() {
        let mut r = renderer();
        let mut s = Scene::new(200.0, 100.0, Color::rgba(0, 0, 0, 255));
        s.push_clip(Rect::new(0.0, 0.0, 100.0, 100.0));
        s.rect(
            Rect::new(0.0, 0.0, 200.0, 100.0),
            Color::rgba(255, 0, 0, 255),
            0.0,
        );
        s.pop_clip();
        r.draw(&s, &empty_store(), &mut TextEngine::new(None));
        assert_eq!(r.pixel(50, 50).r, 255, "inside the clip is drawn");
        assert_eq!(r.pixel(150, 50).r, 0, "outside the clip is not");
    }

    #[test]
    fn popping_a_clip_restores_the_previous_one() {
        let mut r = renderer();
        let mut s = Scene::new(200.0, 100.0, Color::rgba(0, 0, 0, 255));
        s.push_clip(Rect::new(0.0, 0.0, 50.0, 100.0));
        s.pop_clip();
        s.rect(
            Rect::new(0.0, 0.0, 200.0, 100.0),
            Color::rgba(0, 0, 255, 255),
            0.0,
        );
        r.draw(&s, &empty_store(), &mut TextEngine::new(None));
        assert_eq!(
            r.pixel(150, 50).b,
            255,
            "after the pop, the full window draws again"
        );
    }

    #[test]
    fn alpha_blends_rather_than_replaces() {
        let mut r = renderer();
        let mut s = Scene::new(200.0, 100.0, Color::rgba(0, 0, 0, 255));
        s.rect(
            Rect::new(0.0, 0.0, 200.0, 100.0),
            Color::rgba(255, 255, 255, 128),
            0.0,
        );
        r.draw(&s, &empty_store(), &mut TextEngine::new(None));
        let p = r.pixel(100, 50);
        assert!(
            p.r > 100 && p.r < 160,
            "50% white over black is mid grey, got {}",
            p.r
        );
    }

    #[test]
    fn draws_an_image_scaled_into_its_rect() {
        let mut store = empty_store();
        let id = store.insert_generated("red", Bitmap::solid(2, 2, [255, 0, 0, 255]));
        let mut r = renderer();
        let mut s = Scene::new(200.0, 100.0, Color::rgba(0, 0, 0, 255));
        s.image(Rect::new(10.0, 10.0, 80.0, 80.0), id, 0.0, Fit::Cover);
        r.draw(&s, &store, &mut TextEngine::new(None));
        assert_eq!(r.pixel(50, 50).r, 255, "the image covers its rect");
        assert_eq!(r.pixel(5, 50).r, 0, "and nothing outside it");
    }

    #[test]
    fn a_missing_image_id_draws_nothing_instead_of_panicking() {
        let mut r = renderer();
        let mut s = Scene::new(200.0, 100.0, Color::rgba(0, 0, 0, 255));
        s.image(Rect::new(0.0, 0.0, 50.0, 50.0), 999, 0.0, Fit::Cover);
        r.draw(&s, &empty_store(), &mut TextEngine::new(None));
        assert_eq!(r.pixel(25, 25).r, 0);
    }

    #[test]
    fn contain_letterboxes_a_wide_image() {
        let mut store = empty_store();
        // 4:1 image into a square box: bands top and bottom must stay background.
        let id = store.insert_generated("wide", Bitmap::solid(40, 10, [0, 255, 0, 255]));
        let mut r = CpuRenderer::new(100, 100).unwrap();
        let mut s = Scene::new(100.0, 100.0, Color::rgba(0, 0, 0, 255));
        s.image(Rect::new(0.0, 0.0, 100.0, 100.0), id, 0.0, Fit::Contain);
        r.draw(&s, &store, &mut TextEngine::new(None));
        assert_eq!(r.pixel(50, 50).g, 255, "the image is centred");
        assert_eq!(r.pixel(50, 5).g, 0, "the top is letterboxed");
        assert_eq!(r.pixel(50, 95).g, 0, "the bottom is letterboxed");
    }

    #[test]
    fn text_puts_ink_inside_its_rect_only() {
        let mut r = renderer();
        let mut s = Scene::new(200.0, 100.0, Color::rgba(0, 0, 0, 255));
        s.text(
            Rect::new(10.0, 40.0, 180.0, 20.0),
            "Nord",
            Color::rgba(255, 255, 255, 255),
            16.0,
            Align::Left,
            false,
        );
        let mut te = TextEngine::new(None);
        r.draw(&s, &empty_store(), &mut te);

        let mut ink = 0;
        for y in 0..100u32 {
            for x in 0..200u32 {
                if r.pixel(x, y).r > 40 {
                    ink += 1;
                    assert!(
                        (40..60).contains(&y),
                        "ink at ({x},{y}) escaped the text rect"
                    );
                }
            }
        }
        assert!(
            ink > 20,
            "text should put real ink on the pixmap, got {ink}"
        );
    }

    #[test]
    fn text_is_clipped_by_an_enclosing_clip() {
        let mut r = renderer();
        let mut s = Scene::new(200.0, 100.0, Color::rgba(0, 0, 0, 255));
        s.push_clip(Rect::new(0.0, 0.0, 20.0, 100.0));
        s.text(
            Rect::new(10.0, 40.0, 180.0, 20.0),
            "Nord and more text",
            Color::rgba(255, 255, 255, 255),
            16.0,
            Align::Left,
            false,
        );
        s.pop_clip();
        r.draw(&s, &empty_store(), &mut TextEngine::new(None));
        for y in 0..100u32 {
            for x in 20..200u32 {
                assert!(r.pixel(x, y).r < 40, "ink at ({x},{y}) escaped the clip");
            }
        }
    }

    #[test]
    fn resize_keeps_the_renderer_usable() {
        let mut r = renderer();
        assert!(r.resize(320, 240));
        assert_eq!((r.width(), r.height()), (320, 240));
        let s = Scene::new(320.0, 240.0, Color::rgba(1, 2, 3, 255));
        r.draw(&s, &empty_store(), &mut TextEngine::new(None));
        assert_eq!(r.pixel(319, 239), Color::rgba(1, 2, 3, 255));
        // A zero size is coerced to 1x1 rather than failing.
        assert!(r.resize(0, 0));
        assert_eq!((r.width(), r.height()), (1, 1));
    }

    #[test]
    fn pixel_readback_outside_the_pixmap_is_transparent() {
        let r = renderer();
        assert_eq!(r.pixel(1000, 1000), Color::TRANSPARENT);
    }

    #[test]
    fn exports_both_pixel_layouts() {
        let mut r = CpuRenderer::new(2, 1).unwrap();
        let mut s = Scene::new(2.0, 1.0, Color::rgba(255, 0, 0, 255));
        s.rect(
            Rect::new(0.0, 0.0, 2.0, 1.0),
            Color::rgba(0, 0, 255, 255),
            0.0,
        );
        r.draw(&s, &empty_store(), &mut TextEngine::new(None));

        let rgba = r.rgba();
        assert_eq!(rgba.len(), 8);
        assert_eq!(&rgba[0..4], &[0, 0, 255, 255], "RGBA order");

        let bgra = r.bgra_premultiplied();
        assert_eq!(
            &bgra[0..4],
            &[255, 0, 0, 255],
            "BGRA order: blue lands first"
        );
    }
}
