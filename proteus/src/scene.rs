//! The display list: what to draw, in terms no backend disagrees about.
//!
//! Every renderer — CPU, wgpu, OpenGL 2 — consumes this same `Scene`. That is
//! what makes three backends affordable: the layout code exists once, the
//! backends only know how to put a rounded rectangle, an image and a glyph run
//! on screen. It is also what makes the whole UI testable, because a `Scene` can
//! be rasterised to a PNG with no display server anywhere.
//!
//! Coordinates are logical pixels with the origin top-left. Scaling for HiDPI
//! happens once, when the scene is built.

use crate::catalog::Rgb;

/// Straight (non-premultiplied) RGBA.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Color {
    pub r: u8,
    pub g: u8,
    pub b: u8,
    pub a: u8,
}

impl Color {
    pub const TRANSPARENT: Color = Color {
        r: 0,
        g: 0,
        b: 0,
        a: 0,
    };

    pub const fn rgba(r: u8, g: u8, b: u8, a: u8) -> Self {
        Self { r, g, b, a }
    }

    pub const fn opaque(c: Rgb) -> Self {
        Self {
            r: c.r,
            g: c.g,
            b: c.b,
            a: 255,
        }
    }

    pub fn with_alpha(self, a: f32) -> Self {
        Self {
            a: (a.clamp(0.0, 1.0) * 255.0).round() as u8,
            ..self
        }
    }

    pub fn is_visible(self) -> bool {
        self.a > 0
    }
}

impl From<Rgb> for Color {
    fn from(c: Rgb) -> Self {
        Color::opaque(c)
    }
}

impl Default for Color {
    /// Transparent. An empty `Scene` must clear to nothing rather than to an
    /// arbitrary colour: on Wayland the window is composited over the desktop,
    /// so a default of opaque black would show as a black flash before the
    /// first real frame.
    fn default() -> Self {
        Color::TRANSPARENT
    }
}

/// An axis-aligned rectangle in logical pixels.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Rect {
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
}

impl Rect {
    pub const fn new(x: f32, y: f32, w: f32, h: f32) -> Self {
        Self { x, y, w, h }
    }

    pub fn right(&self) -> f32 {
        self.x + self.w
    }

    pub fn bottom(&self) -> f32 {
        self.y + self.h
    }

    pub fn contains(&self, px: f32, py: f32) -> bool {
        px >= self.x && px < self.right() && py >= self.y && py < self.bottom()
    }

    /// Shrink on every side by `d` (negative grows). Never returns a negative
    /// size — a rect inset past nothing is empty, not inside-out.
    pub fn inset(&self, d: f32) -> Rect {
        Rect {
            x: self.x + d,
            y: self.y + d,
            w: (self.w - 2.0 * d).max(0.0),
            h: (self.h - 2.0 * d).max(0.0),
        }
    }

    pub fn is_empty(&self) -> bool {
        self.w <= 0.0 || self.h <= 0.0
    }

    /// The largest rect with `aspect` (w/h) that fits inside this one, centred.
    /// Used to letterbox a wallpaper preview without distorting it.
    pub fn fit_aspect(&self, aspect: f32) -> Rect {
        if aspect <= 0.0 || self.is_empty() {
            return *self;
        }
        let own = self.w / self.h;
        let (w, h) = if own > aspect {
            (self.h * aspect, self.h)
        } else {
            (self.w, self.w / aspect)
        };
        Rect::new(
            self.x + (self.w - w) / 2.0,
            self.y + (self.h - h) / 2.0,
            w,
            h,
        )
    }

    /// The overlapping part of two rects, empty when they do not touch.
    pub fn intersect(&self, o: &Rect) -> Rect {
        let x = self.x.max(o.x);
        let y = self.y.max(o.y);
        let r = self.right().min(o.right());
        let b = self.bottom().min(o.bottom());
        Rect::new(x, y, (r - x).max(0.0), (b - y).max(0.0))
    }
}

/// How an image fills its rect.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Fit {
    /// Cover the rect, cropping the overflow (thumbnails, backgrounds).
    Cover,
    /// Fit inside the rect, letterboxed (the large preview).
    Contain,
}

/// Horizontal alignment of a text run inside its rect.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Align {
    Left,
    Center,
    Right,
}

/// An image already decoded to RGBA8, referenced by handle so the same bytes are
/// uploaded once per frame no matter how many times they are drawn.
pub type ImageId = u32;

/// Horizontal shear, in pixels, of a shape's top edge relative to its bottom.
///
/// A positive value leans the shape to the right. The rect stays the shape's
/// bounding box in the sense that its height and its bottom edge are unchanged,
/// so a row of sheared cards at the same `y` still lines up.
pub type Skew = f32;

/// One thing to draw. Kept small and copyable; the text run's string is the only
/// allocation, and it is short by construction (a theme name, a file name).
#[derive(Debug, Clone, PartialEq)]
pub enum Cmd {
    /// A filled, optionally rounded rectangle.
    Rect {
        rect: Rect,
        color: Color,
        radius: f32,
    },
    /// A filled parallelogram: `rect` sheared horizontally by `skew`.
    Parallelogram {
        rect: Rect,
        skew: Skew,
        color: Color,
    },
    /// A rounded rectangle outline drawn inside the rect's bounds.
    Border {
        rect: Rect,
        color: Color,
        radius: f32,
        width: f32,
    },
    /// An image, clipped to `rect` and rounded by `radius`.
    ///
    /// With a non-zero `skew` the clip is a parallelogram instead: the image
    /// itself is NOT distorted, it is cropped to the leaning shape. Skewing the
    /// picture too would make every wallpaper look wrong; cropping reads as a
    /// card standing at an angle.
    Image {
        rect: Rect,
        image: ImageId,
        radius: f32,
        fit: Fit,
        /// Multiplied over the image; used to dim an off-session preview.
        tint: Color,
        skew: Skew,
    },
    /// A single line of text, vertically centred in `rect`.
    Text {
        rect: Rect,
        text: String,
        color: Color,
        size: f32,
        align: Align,
        /// Draw with the bold face.
        bold: bool,
    },
    /// Restrict following commands to this rect until `PopClip`.
    PushClip(Rect),
    PopClip,
}

/// A frame's worth of drawing commands, in back-to-front order.
#[derive(Debug, Clone, Default)]
pub struct Scene {
    pub width: f32,
    pub height: f32,
    /// Painted before anything else; the window's clear colour.
    pub background: Color,
    pub cmds: Vec<Cmd>,
}

impl Scene {
    pub fn new(width: f32, height: f32, background: Color) -> Self {
        Self {
            width,
            height,
            background,
            cmds: Vec::new(),
        }
    }

    pub fn rect(&mut self, rect: Rect, color: Color, radius: f32) -> &mut Self {
        // Dropping invisible commands here keeps every backend from having to
        // remember to check, and keeps the golden-image diffs readable.
        if !rect.is_empty() && color.is_visible() {
            self.cmds.push(Cmd::Rect {
                rect,
                color,
                radius,
            });
        }
        self
    }

    pub fn border(&mut self, rect: Rect, color: Color, radius: f32, width: f32) -> &mut Self {
        if !rect.is_empty() && color.is_visible() && width > 0.0 {
            self.cmds.push(Cmd::Border {
                rect,
                color,
                radius,
                width,
            });
        }
        self
    }

    pub fn image(&mut self, rect: Rect, image: ImageId, radius: f32, fit: Fit) -> &mut Self {
        self.image_full(
            rect,
            image,
            radius,
            fit,
            Color::rgba(255, 255, 255, 255),
            0.0,
        )
    }

    /// A filled parallelogram - `rect` sheared by `skew`.
    pub fn parallelogram(&mut self, rect: Rect, skew: Skew, color: Color) -> &mut Self {
        if !rect.is_empty() && color.is_visible() {
            self.cmds.push(Cmd::Parallelogram { rect, skew, color });
        }
        self
    }

    /// An image with every knob: tint and shear as well as fit and radius.
    pub fn image_full(
        &mut self,
        rect: Rect,
        image: ImageId,
        radius: f32,
        fit: Fit,
        tint: Color,
        skew: Skew,
    ) -> &mut Self {
        if !rect.is_empty() {
            self.cmds.push(Cmd::Image {
                rect,
                image,
                radius,
                fit,
                tint,
                skew,
            });
        }
        self
    }

    pub fn image_tinted(
        &mut self,
        rect: Rect,
        image: ImageId,
        radius: f32,
        fit: Fit,
        tint: Color,
    ) -> &mut Self {
        self.image_full(rect, image, radius, fit, tint, 0.0)
    }

    pub fn text(
        &mut self,
        rect: Rect,
        text: impl Into<String>,
        color: Color,
        size: f32,
        align: Align,
        bold: bool,
    ) -> &mut Self {
        let text = text.into();
        if !text.is_empty() && color.is_visible() && !rect.is_empty() {
            self.cmds.push(Cmd::Text {
                rect,
                text,
                color,
                size,
                align,
                bold,
            });
        }
        self
    }

    pub fn push_clip(&mut self, rect: Rect) -> &mut Self {
        self.cmds.push(Cmd::PushClip(rect));
        self
    }

    pub fn pop_clip(&mut self) -> &mut Self {
        self.cmds.push(Cmd::PopClip);
        self
    }

    /// Every image referenced this frame, deduplicated — what a GPU backend
    /// needs to have resident before it starts.
    pub fn images(&self) -> Vec<ImageId> {
        let mut v: Vec<ImageId> = self
            .cmds
            .iter()
            .filter_map(|c| match c {
                Cmd::Image { image, .. } => Some(*image),
                _ => None,
            })
            .collect();
        v.sort_unstable();
        v.dedup();
        v
    }

    /// True when clip pushes and pops are balanced. A backend that keeps a clip
    /// stack will misdraw every later frame if they are not, and the symptom
    /// (content vanishing) is far from the cause.
    pub fn clips_balanced(&self) -> bool {
        let mut depth = 0i32;
        for c in &self.cmds {
            match c {
                Cmd::PushClip(_) => depth += 1,
                Cmd::PopClip => {
                    depth -= 1;
                    if depth < 0 {
                        return false;
                    }
                }
                _ => {}
            }
        }
        depth == 0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rect_geometry() {
        let r = Rect::new(10.0, 20.0, 100.0, 50.0);
        assert_eq!(r.right(), 110.0);
        assert_eq!(r.bottom(), 70.0);
        assert!(r.contains(10.0, 20.0));
        assert!(!r.contains(110.0, 70.0), "the far edge is exclusive");
        assert!(!r.contains(9.0, 20.0));
    }

    #[test]
    fn inset_never_goes_inside_out() {
        let r = Rect::new(0.0, 0.0, 10.0, 10.0);
        assert_eq!(r.inset(2.0), Rect::new(2.0, 2.0, 6.0, 6.0));
        let over = r.inset(50.0);
        assert!(over.is_empty());
        assert!(
            over.w >= 0.0 && over.h >= 0.0,
            "a negative size would flip every backend"
        );
    }

    #[test]
    fn fit_aspect_letterboxes_without_distorting() {
        let box_ = Rect::new(0.0, 0.0, 200.0, 100.0);
        // A 2:1 image exactly fills a 2:1 box.
        assert_eq!(box_.fit_aspect(2.0), box_);
        // A square image is centred horizontally and fills the height.
        let sq = box_.fit_aspect(1.0);
        assert_eq!((sq.w, sq.h), (100.0, 100.0));
        assert_eq!(sq.x, 50.0);
        assert_eq!(sq.y, 0.0);
        // A very wide image is centred vertically and fills the width.
        let wide = box_.fit_aspect(4.0);
        assert_eq!((wide.w, wide.h), (200.0, 50.0));
        assert_eq!(wide.y, 25.0);
        // Degenerate input is passed through rather than producing NaN.
        assert_eq!(box_.fit_aspect(0.0), box_);
        assert_eq!(box_.fit_aspect(-1.0), box_);
    }

    #[test]
    fn intersect_of_disjoint_rects_is_empty() {
        let a = Rect::new(0.0, 0.0, 10.0, 10.0);
        let b = Rect::new(20.0, 20.0, 10.0, 10.0);
        assert!(a.intersect(&b).is_empty());
        let c = Rect::new(5.0, 5.0, 10.0, 10.0);
        assert_eq!(a.intersect(&c), Rect::new(5.0, 5.0, 5.0, 5.0));
    }

    #[test]
    fn invisible_commands_are_dropped() {
        let mut s = Scene::new(100.0, 100.0, Color::TRANSPARENT);
        s.rect(
            Rect::new(0.0, 0.0, 0.0, 10.0),
            Color::rgba(255, 0, 0, 255),
            0.0,
        );
        s.rect(Rect::new(0.0, 0.0, 10.0, 10.0), Color::TRANSPARENT, 0.0);
        s.text(
            Rect::new(0.0, 0.0, 10.0, 10.0),
            "",
            Color::rgba(255, 255, 255, 255),
            12.0,
            Align::Left,
            false,
        );
        s.border(
            Rect::new(0.0, 0.0, 10.0, 10.0),
            Color::rgba(255, 0, 0, 255),
            0.0,
            0.0,
        );
        assert!(s.cmds.is_empty(), "nothing invisible reaches a backend");
    }

    #[test]
    fn images_are_deduplicated() {
        let mut s = Scene::new(100.0, 100.0, Color::TRANSPARENT);
        let r = Rect::new(0.0, 0.0, 10.0, 10.0);
        s.image(r, 7, 0.0, Fit::Cover);
        s.image(r, 3, 0.0, Fit::Cover);
        s.image(r, 7, 0.0, Fit::Contain);
        assert_eq!(s.images(), vec![3, 7]);
    }

    #[test]
    fn clip_balance_is_checkable() {
        let mut s = Scene::new(10.0, 10.0, Color::TRANSPARENT);
        assert!(s.clips_balanced());
        s.push_clip(Rect::new(0.0, 0.0, 5.0, 5.0));
        assert!(!s.clips_balanced(), "an unclosed clip is caught");
        s.pop_clip();
        assert!(s.clips_balanced());
        s.pop_clip();
        assert!(!s.clips_balanced(), "an extra pop is caught too");
    }

    #[test]
    fn colors_convert_and_fade() {
        let c = Color::opaque(Rgb::new(0x88, 0xc0, 0xd0));
        assert_eq!((c.r, c.g, c.b, c.a), (0x88, 0xc0, 0xd0, 255));
        assert_eq!(c.with_alpha(0.5).a, 128);
        assert_eq!(c.with_alpha(0.0).a, 0);
        assert!(!c.with_alpha(0.0).is_visible());
        assert_eq!(c.with_alpha(2.0).a, 255, "alpha is clamped, not wrapped");
    }
}
