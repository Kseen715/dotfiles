//! Text shaping and glyph rasterisation.
//!
//! Glyphs are rasterised on the CPU by cosmic-text and handed to the backends as
//! 8-bit coverage masks. That is the single decision that makes three renderers
//! affordable: none of them contains a line breaker, a shaper, or a hinting
//! path, and all three therefore draw type identically — which is also why a
//! golden-image test taken from the CPU backend is meaningful for the GPU ones.

use std::collections::HashMap;

use cosmic_text::{Attrs, Buffer, Family, FontSystem, Metrics, Shaping, SwashCache, Weight};

/// One positioned glyph, ready for a backend to blit.
pub struct Glyph {
    /// Top-left of the coverage bitmap, in pixels, relative to the text origin.
    pub x: i32,
    pub y: i32,
    pub width: usize,
    pub height: usize,
    /// `width * height` coverage values.
    pub coverage: Vec<u8>,
}

/// Shapes strings and rasterises glyphs, caching the font system across frames.
///
/// `FontSystem` scans the system's fonts on construction, which is slow enough
/// (tens of milliseconds) that doing it per frame would be visible when typing.
pub struct TextEngine {
    font_system: FontSystem,
    swash: SwashCache,
    family: Option<String>,
    /// Measured widths, keyed by (text, size in 1/16 px, bold). Typing re-measures
    /// the same labels every keystroke.
    width_cache: HashMap<(String, u32, bool), f32>,
}

impl TextEngine {
    pub fn new(family: Option<String>) -> Self {
        Self {
            font_system: FontSystem::new(),
            swash: SwashCache::new(),
            family,
            width_cache: HashMap::new(),
        }
    }

    fn attrs(&self, bold: bool) -> Attrs<'_> {
        let mut a = Attrs::new();
        if let Some(f) = &self.family {
            a = a.family(Family::Name(f));
        }
        if bold {
            a = a.weight(Weight::BOLD);
        }
        a
    }

    /// Advance width of `text` at `size`, in pixels.
    pub fn measure(&mut self, text: &str, size: f32, bold: bool) -> f32 {
        let key = (text.to_string(), (size * 16.0) as u32, bold);
        if let Some(w) = self.width_cache.get(&key) {
            return *w;
        }
        let mut buffer = Buffer::new(&mut self.font_system, Metrics::new(size, size * 1.3));
        // No wrapping: every string here is a single line that gets ellipsised
        // if it does not fit, so an unconstrained width is what we want to know.
        buffer.set_size(None, None);
        let attrs = self.attrs(bold);
        buffer.set_text(text, &attrs, Shaping::Advanced, None);
        buffer.shape_until_scroll(&mut self.font_system, false);
        let w = buffer
            .layout_runs()
            .map(|r| r.line_w)
            .fold(0.0f32, f32::max);
        self.width_cache.insert(key, w);
        w
    }

    /// Shorten `text` with a trailing ellipsis until it fits `max_width`.
    ///
    /// Truncation is by characters, not bytes: a path with a multi-byte
    /// character in it must not be cut mid-codepoint.
    pub fn ellipsize(&mut self, text: &str, size: f32, bold: bool, max_width: f32) -> String {
        if max_width <= 0.0 {
            return String::new();
        }
        if self.measure(text, size, bold) <= max_width {
            return text.to_string();
        }
        let chars: Vec<char> = text.chars().collect();
        // Binary search the longest prefix that fits with the ellipsis appended.
        let (mut lo, mut hi) = (0usize, chars.len());
        while lo < hi {
            let mid = (lo + hi).div_ceil(2);
            let mut candidate: String = chars[..mid].iter().collect();
            candidate.push('\u{2026}');
            if self.measure(&candidate, size, bold) <= max_width {
                lo = mid;
            } else {
                hi = mid - 1;
            }
        }
        if lo == 0 {
            // Not even one character plus the ellipsis fits.
            return String::new();
        }
        let mut out: String = chars[..lo].iter().collect();
        out.push('\u{2026}');
        out
    }

    /// Shape `text` and rasterise its glyphs. Positions are relative to the text
    /// origin: x at the left edge, y at the TOP of the line box.
    pub fn layout(&mut self, text: &str, size: f32) -> Vec<Glyph> {
        self.layout_weighted(text, size, false)
    }

    pub fn layout_weighted(&mut self, text: &str, size: f32, bold: bool) -> Vec<Glyph> {
        let mut buffer = Buffer::new(&mut self.font_system, Metrics::new(size, size * 1.3));
        buffer.set_size(None, None);
        let attrs = self.attrs(bold);
        buffer.set_text(text, &attrs, Shaping::Advanced, None);
        buffer.shape_until_scroll(&mut self.font_system, false);

        let mut out = Vec::new();
        for run in buffer.layout_runs() {
            for g in run.glyphs.iter() {
                let physical = g.physical((0.0, 0.0), 1.0);
                let Some(img) = self
                    .swash
                    .get_image(&mut self.font_system, physical.cache_key)
                    .as_ref()
                else {
                    continue;
                };
                let w = img.placement.width as usize;
                let h = img.placement.height as usize;
                if w == 0 || h == 0 {
                    continue; // a space: shaped, nothing to draw
                }
                // Only 8-bit masks are handled; a colour-emoji font would give
                // back RGBA here. Skipping keeps the picker from drawing noise,
                // and no label in this UI is emoji.
                let coverage = match img.content {
                    cosmic_text::SwashContent::Mask => img.data.clone(),
                    cosmic_text::SwashContent::SubpixelMask | cosmic_text::SwashContent::Color => {
                        continue
                    }
                };
                if coverage.len() < w * h {
                    continue;
                }
                out.push(Glyph {
                    x: physical.x + img.placement.left,
                    y: run.line_y as i32 + physical.y - img.placement.top,
                    width: w,
                    height: h,
                    coverage,
                });
            }
        }
        out
    }

    /// Height of one line at `size` — what the layout code reserves per row.
    pub fn line_height(&self, size: f32) -> f32 {
        size * 1.3
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// One engine for the whole module: constructing a FontSystem scans the
    /// system font directories, and doing that per test makes the suite crawl.
    fn engine() -> TextEngine {
        TextEngine::new(None)
    }

    #[test]
    fn measures_wider_text_as_wider() {
        let mut e = engine();
        let short = e.measure("nord", 14.0, false);
        let long = e.measure("nord and then some more", 14.0, false);
        assert!(short > 0.0, "text must have a width");
        assert!(long > short);
    }

    #[test]
    fn measurement_scales_with_size() {
        let mut e = engine();
        let small = e.measure("gruvbox", 10.0, false);
        let large = e.measure("gruvbox", 20.0, false);
        assert!(large > small * 1.5, "20px should be far wider than 10px");
    }

    #[test]
    fn empty_text_has_no_width_and_no_glyphs() {
        let mut e = engine();
        assert_eq!(e.measure("", 14.0, false), 0.0);
        assert!(e.layout("", 14.0).is_empty());
    }

    #[test]
    fn ellipsize_fits_the_budget() {
        let mut e = engine();
        let text = "a very long wallpaper file name that will not fit.png";
        let full = e.measure(text, 14.0, false);
        let budget = full / 3.0;
        let cut = e.ellipsize(text, 14.0, false, budget);
        assert!(
            cut.ends_with('\u{2026}'),
            "shortened text is marked with an ellipsis"
        );
        assert!(cut.chars().count() < text.chars().count());
        assert!(
            e.measure(&cut, 14.0, false) <= budget,
            "the result must actually fit the budget"
        );
    }

    #[test]
    fn ellipsize_leaves_fitting_text_alone() {
        let mut e = engine();
        let text = "nord";
        let w = e.measure(text, 14.0, false);
        assert_eq!(e.ellipsize(text, 14.0, false, w + 10.0), text);
        assert_eq!(
            e.ellipsize(text, 14.0, false, 0.0),
            "",
            "no room means no text"
        );
    }

    #[test]
    fn ellipsize_never_splits_a_codepoint() {
        let mut e = engine();
        // Multi-byte characters throughout: a byte-wise truncation would panic
        // or produce invalid UTF-8 here.
        let text = "Тема с длинным названием — обои.png";
        for budget in [5.0, 20.0, 50.0, 120.0] {
            let cut = e.ellipsize(text, 14.0, false, budget);
            assert!(text.starts_with(cut.trim_end_matches('\u{2026}')));
        }
    }

    #[test]
    fn lays_out_glyphs_with_coverage() {
        let mut e = engine();
        let glyphs = e.layout("Nord", 20.0);
        assert!(!glyphs.is_empty(), "visible text produces glyphs");
        for g in &glyphs {
            assert_eq!(
                g.coverage.len(),
                g.width * g.height,
                "coverage matches the bitmap size"
            );
            assert!(g.width > 0 && g.height > 0);
        }
        // At least one glyph must have ink, or we are drawing nothing.
        assert!(glyphs.iter().any(|g| g.coverage.iter().any(|&c| c > 0)));
    }

    #[test]
    fn a_space_only_string_draws_nothing() {
        let mut e = engine();
        assert!(
            e.layout("   ", 14.0).is_empty(),
            "spaces shape but have no ink"
        );
        assert!(
            e.measure("   ", 14.0, false) > 0.0,
            "...though they still advance"
        );
    }

    #[test]
    fn bold_is_at_least_as_wide_as_regular() {
        let mut e = engine();
        let regular = e.measure("Catppuccin Mocha", 14.0, false);
        let bold = e.measure("Catppuccin Mocha", 14.0, true);
        assert!(bold >= regular);
    }
}
