//! Layout: the one place that decides where things go.
//!
//! Takes a [`Model`] and a [`Style`] and produces a [`Scene`]. It draws nothing
//! itself, which is what lets the whole appearance be tested by rasterising the
//! result — and lets the three backends stay ignorant of what a "row" is.

use crate::catalog::Rgb;
use crate::config::Style;
use crate::images::{palette_preview, ImageStore};
use crate::model::{Model, Row};
use crate::scene::{Align, Color, Fit, ImageId, Rect, Scene};
use crate::text::TextEngine;

/// The five colours the UI draws with, after the theme and the config overrides
/// have both had their say.
#[derive(Debug, Clone, Copy)]
pub struct Skin {
    pub bg: Rgb,
    pub surface: Rgb,
    pub fg: Rgb,
    pub dim: Rgb,
    pub accent: Rgb,
}

impl Skin {
    /// Resolve the skin for the current frame.
    ///
    /// The theme under the cursor supplies the palette (so moving the selection
    /// previews the theme live), and any colour named in the config wins over
    /// it — an explicit setting is a decision, not a suggestion.
    pub fn resolve(model: &Model, style: &Style) -> Skin {
        let p = style
            .follow_theme
            .then(|| model.styling_theme().map(|t| t.palette.clone()))
            .flatten()
            .unwrap_or_default();
        Skin {
            bg: style.bg_override().unwrap_or(p.bg),
            surface: style.surface_override().unwrap_or(p.surface),
            fg: style.fg_override().unwrap_or(p.fg),
            dim: style.dim_override().unwrap_or(p.dim),
            accent: style.accent_override().unwrap_or(p.accent),
        }
    }

    /// A colour that stays readable on `bg`, for text drawn over an image or an
    /// accent fill. Picking fg blindly gives white-on-white on a light theme.
    pub fn on(&self, bg: Rgb) -> Rgb {
        if bg.luma() > 0.55 {
            Rgb::new(0x10, 0x10, 0x10)
        } else {
            Rgb::new(0xf0, 0xf0, 0xf0)
        }
    }
}

/// Where each part of the window lives. Computed once per frame and reused by
/// the hit-testing code, so what you click is what you see.
///
/// The picker is a horizontal strip: the wallpapers are portrait cards, leaning,
/// laid out left to right, with the selected one in the middle and the text
/// about it above. A wallpaper is a portrait of a screen, and a row of them
/// reads as a shelf you move along — which is the shape of the decision being
/// made, "which of these", not "which line of a list".
#[derive(Debug, Clone, Copy)]
pub struct Layout {
    pub window: Rect,
    pub input: Rect,
    /// Title and description of whatever is selected.
    pub header: Rect,
    /// The band the cards live in.
    pub strip: Rect,
    pub footer: Rect,
    /// Width of one card.
    pub card_w: f32,
    /// Height of one card.
    pub card_h: f32,
    /// Distance from one card to the next.
    pub advance: f32,
    /// Horizontal lean, in pixels.
    pub skew: f32,
    /// Space kept under the cards for the selection bar.
    pub shelf: f32,
    /// How far the shadow and rim extend outside a card.
    ///
    /// Owned by the layout rather than recomputed while drawing, so the space
    /// reserved for it and the space it actually uses cannot disagree.
    pub depth: f32,
}

/// Portrait: a card is this many times as wide as it is tall. 0.62 is close to
/// a phone screen turned upright, which is what "portrait wallpaper" looks like.
const CARD_ASPECT: f32 = 0.62;

/// Distance from one card to the next, as a fraction of a card's width.
///
/// Below 1.0 the cards overlap, each overhanging its outer neighbour like a
/// fanned deck. The draw order below is what makes that an overhang rather than
/// a cover-up: everything tucks BEHIND the selection, which is never overlapped.
///
/// It has to be below the *shrunk* width, not the full one. Cards away from the
/// selection are drawn at [`MIN_SCALE`], so an advance of 0.86 looked like a row
/// of separate cards with gaps - the overlap only appeared next to the
/// selection, where the cards are full size.
const ADVANCE_FACTOR: f32 = 0.70;

/// How small a card gets once it is a full step away from the selection.
const MIN_SCALE: f32 = 0.78;

/// Largest shadow/rim the drawing puts AROUND a card, in pixels.
///
/// The layout has to reserve this above the cards. It did not, so a full-size
/// card sat flush against the top of the strip and its own shadow and rim were
/// clipped away - which reads as the card being cropped.
const CARD_DEPTH_MAX: f32 = 12.0;

impl Layout {
    pub fn compute(width: f32, height: f32, style: &Style, text: &TextEngine) -> Layout {
        let pad = style.padding;
        let window = Rect::new(0.0, 0.0, width, height);
        let line = text.line_height(style.font_size);

        let input = Rect::new(pad, pad, width - pad * 2.0, line + pad * 0.6);

        // The header holds a large title and a description under it.
        let header_h = line * 1.9 + line * 0.95;
        let header = Rect::new(pad, input.bottom() + pad * 0.7, width - pad * 2.0, header_h);

        let footer_h = text.line_height(style.font_size * 0.8) + pad * 0.5;
        let footer = Rect::new(
            pad,
            height - footer_h - pad * 0.25,
            width - pad * 2.0,
            footer_h,
        );

        let strip_y = header.bottom() + pad * 0.5;
        let strip_h = (footer.y - strip_y - pad * 0.5).max(1.0);
        let strip = Rect::new(0.0, strip_y, width, strip_h);

        // Cards fill the strip's height; the width follows from the aspect. The
        // count the user asked for decides how wide they can be, so `rows = 3`
        // gives three big cards and `rows = 9` gives nine small ones.
        // Room under the cards for the selection bar. Without it the bar is
        // drawn outside the strip's clip and simply never appears - which loses
        // the only thing tying the header to the card it describes.
        let shelf = 12.0;
        // ...and room above for the shadow and rim the cards are drawn with.
        let headroom = CARD_DEPTH_MAX + 2.0;
        let by_height = (strip_h - shelf - headroom).max(24.0);
        let by_count =
            (width - pad * 2.0) / style.rows.max(1) as f32 / (CARD_ASPECT * ADVANCE_FACTOR);
        let card_h = by_height.min(by_count).max(24.0);
        let card_w = (card_h * CARD_ASPECT).max(16.0);

        Layout {
            window,
            input,
            header,
            strip,
            footer,
            card_w,
            card_h,
            advance: card_w * ADVANCE_FACTOR,
            skew: card_h * 0.10,
            shelf,
            depth: (card_w * 0.06).clamp(3.0, CARD_DEPTH_MAX),
        }
    }

    /// How many cards fit across the strip.
    pub fn rows_visible(&self) -> usize {
        if self.advance <= 0.0 {
            return 1;
        }
        ((self.strip.w / self.advance).floor() as usize).max(1)
    }

    /// Where the middle of the strip is.
    pub fn center_x(&self) -> f32 {
        self.window.w / 2.0
    }

    /// Which card sits in the middle of the strip.
    ///
    /// This used to clamp at both ends, so the strip stopped and only the
    /// selection moved. On a ring there are no ends to stop at: the position
    /// runs on unbounded and the cards that come round are the ones from the
    /// other side. That is the whole difference between a scroller that jumps
    /// back at the end and one that keeps going.
    pub fn anchor(&self, cursor_offset: f32, _count: usize) -> f32 {
        cursor_offset
    }

    /// The rect of the card `offset` places from the selected one.
    ///
    /// Fractional because the strip slides: mid-animation the selection sits
    /// between two cards, and snapping to a whole one is the jerk that
    /// animating was meant to remove.
    pub fn card_rect(&self, offset: f32, scale: f32) -> Rect {
        let w = self.card_w * scale;
        let h = self.card_h * scale;
        // Cards share a baseline rather than a centre line, so a smaller
        // neighbour reads as standing further back on the same shelf.
        let bottom = self.strip.bottom() - self.shelf;
        Rect::new(
            self.center_x() + offset * self.advance - w / 2.0,
            bottom - h,
            w,
            h,
        )
    }

    /// Legacy vertical-list accessor, kept for the tests that predate the
    /// strip: the i-th card as if the selection were at index 0.
    pub fn row_rect(&self, i: usize) -> Rect {
        self.card_rect(i as f32, 1.0)
    }

    pub fn row_rect_at(&self, offset: f32) -> Rect {
        self.card_rect(offset, 1.0)
    }

    /// Which card a point is over, as a VIRTUAL index on the ring.
    ///
    /// Virtual rather than an item index because the same item can appear more
    /// than once on a short ring: the answer has to name the card under the
    /// pointer, not one of the places that item happens to be.
    ///
    /// `anchor` is where the strip currently sits - the same value the drawing
    /// uses, so what is clicked is what is on screen.
    pub fn card_at(&self, x: f32, y: f32, anchor: f32) -> Option<i64> {
        if !self.strip.contains(x, y) || self.advance <= 0.0 {
            return None;
        }
        let rel = (x - self.center_x()) / self.advance + anchor;
        // +0.5 so the halfway point between two cards is the boundary.
        Some((rel + 0.5).floor() as i64)
    }
}

/// A card's preview image.
///
/// Every preview fills its card - a wallpaper is cropped to the portrait shape
/// rather than letterboxed inside it, because a card with bars down its sides
/// reads as a broken image rather than as a wide picture.
struct Preview {
    id: ImageId,
}

/// Resolve the image to draw for a row, generating a palette swatch when the
/// theme ships no wallpaper so that every row has something to look at.
fn row_image(
    row: &Row,
    model: &Model,
    images: &mut ImageStore,
    size: (u32, u32),
) -> Option<Preview> {
    if let Some(p) = &row.preview {
        if let Some(id) = images.load(p) {
            return Some(Preview { id });
        }
    }
    // No wallpaper (or it failed to decode): fall back to the theme's colours.
    let theme = model.themes.iter().find(|t| t.display == row.title)?;
    let key = format!("swatch:{}:{}x{}", theme.name, size.0, size.1);
    if let Some(id) = images.cached(std::path::Path::new(&key)) {
        return Some(Preview { id });
    }
    let id = images.insert_generated(&key, palette_preview(size.0, size.1, &theme.palette));
    Some(Preview { id })
}

/// How far a card is shrunk and darkened by distance from the selection.
///
/// Continuous in the distance rather than a boolean "selected or not", because
/// the distance is fractional while the strip slides — so the card growing into
/// focus and the ones falling back animate for free, with no second animation
/// to keep in step with the first.
///
/// The second value is a BRIGHTNESS, not an opacity. Once the cards overlap,
/// fading them would let each one show through its neighbour and the shelf would
/// turn to mush; darkening keeps every card solid, so an overhang reads as one
/// card in front of another.
fn emphasis(distance: f32) -> (f32, f32) {
    let d = distance.abs();
    let near = d.min(1.0);
    let scale = 1.0 - (1.0 - MIN_SCALE) * near;
    // Two falloffs. The first is the step back from the selection to its
    // neighbours. The second keeps darkening slowly with distance, so the ring
    // fades toward the edges of the strip - which is what makes it read as
    // continuing rather than as the same few cards repeated.
    let far = ((d - 1.0).max(0.0) / 4.0).min(1.0);
    let shade = 1.0 - 0.34 * near - 0.28 * far;
    (scale, shade)
}

/// Build the frame.
pub fn build_scene(
    model: &Model,
    style: &Style,
    layout: &Layout,
    images: &mut ImageStore,
    text: &mut TextEngine,
) -> Scene {
    let skin = Skin::resolve(model, style);
    // Clear to nothing, not to the background colour: the plate below paints
    // the background WITH a corner radius, and a filled clear would square the
    // corners off again by painting behind them.
    let mut s = Scene::new(layout.window.w, layout.window.h, Color::TRANSPARENT);
    s.rect(
        layout.window,
        Color::opaque(skin.bg).with_alpha(style.opacity),
        style.radius,
    );

    let rows = model.rows();
    let cursor_offset = model.cursor_offset();
    // Position comes from the anchor (clamped at the ends), emphasis from the
    // cursor: the selected card is highlighted wherever along the strip it sits.
    let anchor = layout.anchor(cursor_offset, rows.len());

    // --- filter line ---------------------------------------------------------
    s.rect(
        layout.input,
        Color::opaque(skin.surface),
        style.radius * 0.6,
    );
    let inner = layout.input.inset(style.padding * 0.4);
    let prompt = format!("{} ", mode_glyph(model));
    let prompt_w = text.measure(&prompt, style.font_size, true);
    s.text(
        inner,
        &prompt,
        Color::opaque(skin.accent),
        style.font_size,
        Align::Left,
        true,
    );
    let query_rect = Rect::new(
        inner.x + prompt_w,
        inner.y,
        (inner.w - prompt_w).max(0.0),
        inner.h,
    );
    if model.query.is_empty() {
        s.text(
            query_rect,
            format!("filter {}...", model.mode.label()),
            Color::opaque(skin.dim),
            style.font_size,
            Align::Left,
            false,
        );
    } else {
        let qw = text.measure(&model.query, style.font_size, false);
        s.text(
            query_rect,
            &model.query,
            Color::opaque(skin.fg),
            style.font_size,
            Align::Left,
            false,
        );
        // A caret, so an empty-looking window is visibly accepting input.
        s.rect(
            Rect::new(
                query_rect.x + qw + 2.0,
                query_rect.y + query_rect.h * 0.15,
                2.0,
                query_rect.h * 0.7,
            ),
            Color::opaque(skin.accent),
            1.0,
        );
    }

    // --- header: what is selected -------------------------------------------
    let selected = rows.get(model.cursor);
    let title_size = style.font_size * 1.45;
    let title_h = text.line_height(title_size);
    if let Some(row) = selected {
        s.text(
            Rect::new(layout.header.x, layout.header.y, layout.header.w, title_h),
            &row.title,
            Color::opaque(skin.fg),
            title_size,
            Align::Left,
            true,
        );
        let mut sub = row.subtitle.clone();
        if !row.fits_session {
            sub = format!("{sub}   (other session)");
        }
        s.text(
            Rect::new(
                layout.header.x,
                layout.header.y + title_h,
                layout.header.w,
                text.line_height(style.font_size),
            ),
            sub,
            Color::opaque(skin.dim),
            style.font_size * 0.95,
            Align::Left,
            false,
        );
        // A dot on the right when this is what is applied right now.
        if row.current {
            let r = Rect::new(
                layout.header.right() - 10.0,
                layout.header.y + title_h * 0.45,
                8.0,
                8.0,
            );
            s.rect(r, Color::opaque(skin.accent), 4.0);
        }
    } else {
        s.text(
            Rect::new(layout.header.x, layout.header.y, layout.header.w, title_h),
            "no matches",
            Color::opaque(skin.dim),
            title_size,
            Align::Left,
            false,
        );
    }

    // The palette of the selected theme, as a thin rule under the header.
    if let Some(t) = model.styling_theme() {
        let p = &t.palette;
        let swatches = [p.bg, p.surface, p.dim, p.fg, p.accent];
        let sw = 26.0;
        let y = layout.header.bottom() - 4.0;
        for (i, c) in swatches.iter().enumerate() {
            s.rect(
                Rect::new(layout.header.x + i as f32 * (sw + 3.0), y, sw, 3.0),
                Color::opaque(*c),
                1.5,
            );
        }
    }

    // --- the card strip ------------------------------------------------------
    s.push_clip(layout.strip);

    // Only what can be seen, plus one either side so a card slides in already
    // drawn rather than appearing at the edge. These are VIRTUAL indices: they
    // run negative and past the end, and each is wrapped to an item below.
    let span = (layout.strip.w / layout.advance.max(1.0)).ceil() as i64 / 2 + 2;
    let centre = anchor.round() as i64;
    let (first, last) = if model.wraps() {
        (centre - span, centre + span)
    } else {
        // A ring of one card must not repeat it across the whole strip.
        (0, rows.len().saturating_sub(1) as i64)
    };

    // Back to front: the selected card is drawn last so its neighbours tuck
    // behind it rather than over it.
    let mut order: Vec<i64> = (first..=last).collect();
    order.sort_by(|a, b| {
        let da = (*a as f32 - cursor_offset).abs();
        let db = (*b as f32 - cursor_offset).abs();
        db.partial_cmp(&da).unwrap_or(std::cmp::Ordering::Equal)
    });

    for v in order {
        // Wrap the ring position onto the list. This is the one place the two
        // ideas meet: `v` says where on screen, the item says what to draw.
        let Some(i) = model.item_at_virtual(v) else {
            continue;
        };
        let row = &rows[i];
        let distance = v as f32 - cursor_offset;
        let (scale, shade) = emphasis(distance);
        let card = layout.card_rect(v as f32 - anchor, scale);
        let skew = layout.skew * scale;

        // Two rings behind each card, and between them they are what makes the
        // overlap read as depth rather than as a collage.
        //
        // Cards are drawn back to front, so each card's shadow falls on the one
        // it overhangs. A rim alone was not enough: at a hairline it is lost
        // against a dark wallpaper, and widening it just looks like a border.
        let depth = layout.depth * scale;
        s.parallelogram(
            Rect::new(
                card.x - depth,
                card.y - depth * 0.5,
                card.w + depth * 2.0,
                card.h + depth * 0.5,
            ),
            skew,
            Color::rgba(0, 0, 0, 110).with_alpha(0.43 * style.opacity),
        );
        // A light rim right at the edge, so the front card has a defined lip
        // where it crosses the one behind.
        s.parallelogram(
            Rect::new(card.x - 1.5, card.y - 1.5, card.w + 3.0, card.h + 3.0),
            skew,
            Color::opaque(skin.fg).with_alpha(0.30 * style.opacity),
        );

        // A plate behind the image, so a card whose preview fails to decode is
        // still a card rather than a hole. Opaque: see `emphasis`.
        s.parallelogram(
            card,
            skew,
            Color::opaque(skin.surface.mix(skin.bg, 1.0 - shade)).with_alpha(style.opacity),
        );

        let size = (
            (card.w + skew.abs()).max(1.0) as u32,
            card.h.max(1.0) as u32,
        );
        if let Some(pv) = row_image(row, model, images, size) {
            // Multiplied over the image: a grey below white darkens it while
            // leaving it fully opaque.
            let mut level = shade;
            if !row.fits_session {
                // Off-session entries are pushed back further, not hidden:
                // applying one is valid, it just will not paint the compositor
                // bits.
                level *= 0.65;
            }
            let v = (255.0 * level).clamp(0.0, 255.0) as u8;
            s.image_full(
                card,
                pv.id,
                0.0,
                Fit::Cover,
                Color::rgba(v, v, v, 255),
                skew,
            );
        }

        // The selection is marked by a bar on the shelf beneath the card, not
        // by an outline: an outline on a leaning card is a diagonal sliver that
        // reads as a stray line, and it competes with the wallpaper it frames.
        if distance.abs() < 1.0 {
            let t = 1.0 - distance.abs();
            // Spans the card's bottom edge exactly. The bar sits on the shelf
            // under that edge, so it is `card.w` wide and starts at `card.x` -
            // the sheared footprint is wider, and centring on that pushed the
            // bar to the right of the card it belongs to.
            let bar_w = card.w * t;
            s.rect(
                Rect::new(
                    card.x + (card.w - bar_w) / 2.0,
                    card.bottom() + layout.shelf * 0.35,
                    bar_w,
                    3.0,
                ),
                Color::opaque(skin.accent).with_alpha(t),
                1.5,
            );
        }
    }
    s.pop_clip();

    // --- footer --------------------------------------------------------------
    let hint = format!(
        "{}/{}   \u{2190} \u{2192} browse   \u{2022}   Tab: {}   \u{2022}   Enter: apply   \u{2022}   Esc: close",
        if rows.is_empty() { 0 } else { model.cursor + 1 },
        rows.len(),
        model.mode.other().label(),
    );
    s.text(
        layout.footer,
        hint,
        Color::opaque(skin.dim),
        style.font_size * 0.8,
        Align::Left,
        false,
    );

    s
}

fn mode_glyph(model: &Model) -> &'static str {
    match model.mode {
        crate::model::Mode::Themes => "theme",
        crate::model::Mode::Wallpapers => "wall",
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::catalog::{Palette, Session, Theme};
    use crate::model::Mode;
    use std::path::PathBuf;

    fn theme(name: &str, accent: Rgb) -> Theme {
        Theme {
            name: name.into(),
            display: name.into(),
            description: format!("{name} desc"),
            session: Session::Any,
            polarity: "dark".into(),
            palette: Palette {
                accent,
                ..Palette::default()
            },
            dir: PathBuf::from("/t").join(name),
            wallpapers: Vec::new(),
        }
    }

    fn model() -> Model {
        Model::new(
            vec![
                theme("alpha", Rgb::new(255, 0, 0)),
                theme("beta", Rgb::new(0, 255, 0)),
                theme("gamma", Rgb::new(0, 0, 255)),
            ],
            vec![PathBuf::from("/w/a.png")],
            Session::Any,
        )
    }

    fn layout(style: &Style) -> Layout {
        Layout::compute(style.width, style.height, style, &TextEngine::new(None))
    }

    #[test]
    fn regions_stay_inside_the_window_and_do_not_overlap() {
        let style = Style::default();
        let l = layout(&style);
        for (name, r) in [
            ("input", l.input),
            ("header", l.header),
            ("strip", l.strip),
            ("footer", l.footer),
        ] {
            assert!(r.x >= 0.0 && r.y >= 0.0, "{name} starts off-window");
            assert!(
                r.right() <= l.window.right() + 0.01 && r.bottom() <= l.window.bottom() + 0.01,
                "{name} overflows the window: {r:?} vs {:?}",
                l.window
            );
        }
        // Stacked top to bottom, none overlapping.
        assert!(
            l.input.bottom() <= l.header.y,
            "the filter line overlaps the header"
        );
        assert!(
            l.header.bottom() <= l.strip.y,
            "the header overlaps the strip"
        );
        assert!(
            l.strip.bottom() <= l.footer.y + 0.01,
            "the strip overlaps the footer"
        );
    }

    #[test]
    fn cards_are_portrait_and_fit_the_strip() {
        let style = Style::default();
        let l = layout(&style);
        assert!(l.card_h > l.card_w, "a wallpaper card is portrait");
        assert!(
            l.card_h + l.shelf <= l.strip.h + 0.01,
            "a card plus its shelf must fit the strip: {} + {} vs {}",
            l.card_h,
            l.shelf,
            l.strip.h
        );
        // Cards overlap on purpose - each overhangs its outer neighbour. The
        // advance has to be under the SHRUNK width, or the overlap only appears
        // beside the selection and the rest of the strip shows gaps.
        assert!(
            l.advance < l.card_w * MIN_SCALE,
            "cards should overhang: advance {} vs shrunk width {}",
            l.advance,
            l.card_w * MIN_SCALE
        );
        // ...but not so far that a card is swallowed whole.
        assert!(
            l.advance > l.card_w * 0.45,
            "an overhang must still leave most of each card visible"
        );
        assert!(l.skew > 0.0, "the cards lean");
        // The requested number of cards has to actually fit across the window.
        assert!(
            l.advance * style.rows as f32 <= l.window.w + l.advance,
            "{} cards of {} do not fit {}",
            style.rows,
            l.advance,
            l.window.w
        );
    }

    #[test]
    fn the_selection_bar_has_room_under_the_cards() {
        let style = Style::default();
        let l = layout(&style);
        let card = l.card_rect(0.0, 1.0);
        assert!(
            card.bottom() + l.shelf * 0.35 + 3.0 <= l.strip.bottom() + 0.01,
            "the selection bar would be clipped away by the strip"
        );
    }

    #[test]
    fn a_full_size_card_and_its_shadow_fit_inside_the_strip() {
        // The selected card is drawn at full scale WITH a shadow and rim around
        // it. Sizing cards to the bare strip height left zero headroom, so the
        // top of the focused card was clipped by the strip - it looked cropped.
        for (w, h) in [
            (1000.0, 460.0),
            (800.0, 400.0),
            (1400.0, 700.0),
            (600.0, 300.0),
        ] {
            let mut style = Style {
                width: w,
                height: h,
                ..Style::default()
            };
            style.sanitize();
            let l = layout(&style);
            let card = l.card_rect(0.0, 1.0);
            assert!(
                card.y - l.depth >= l.strip.y - 0.01,
                "{w}x{h}: the card's shadow starts at {} above the strip top {}",
                card.y - l.depth,
                l.strip.y
            );
            assert!(
                card.bottom() <= l.strip.bottom() + 0.01,
                "{w}x{h}: the card overruns the bottom of the strip"
            );
        }
    }

    #[test]
    fn the_selection_bar_spans_the_cards_bottom_edge() {
        let style = Style::default();
        let l = layout(&style);
        let card = l.card_rect(0.0, 1.0);
        // The bar sits on the shelf under the card's BOTTOM edge, which is
        // `card.w` wide starting at `card.x`. The sheared footprint is wider and
        // offset; centring the bar on that put it to the right of its own card
        // and left it short.
        let bar_w = card.w;
        let bar_x = card.x + (card.w - bar_w) / 2.0;
        assert!(
            (bar_x - card.x).abs() < 0.01,
            "the bar should start at the card's left edge"
        );
        assert!(
            (bar_x + bar_w - card.right()).abs() < 0.01,
            "and end at its right edge"
        );
    }

    #[test]
    fn the_strip_follows_the_ring_without_stopping_at_the_ends() {
        let style = Style::default();
        let l = layout(&style);
        // The anchor used to clamp, so at the ends the strip froze and only the
        // selection moved. A ring has no ends: the drawn position simply follows
        // the cursor, running negative and past the list, and the cards that
        // come round are the ones from the other side.
        for (offset, count) in [(0.0f32, 20), (7.5, 20), (19.0, 20), (-3.0, 20), (0.0, 3)] {
            assert!(
                (l.anchor(offset, count) - offset).abs() < 1e-6,
                "the strip should sit exactly where the cursor is ({offset} of {count})"
            );
        }
        // An empty list is still safe to ask about.
        assert!(l.anchor(0.0, 0).is_finite());
    }

    #[test]
    fn clicking_a_card_hits_the_card_that_is_drawn_there() {
        let style = Style::default();
        let l = layout(&style);
        let count = l.rows_visible() + 6;
        for cursor in [0.0f32, 3.0, 7.5] {
            let anchor = l.anchor(cursor, count);
            for i in 0..count {
                let card = l.card_rect(i as f32 - anchor, 1.0);
                let cx = card.x + card.w / 2.0;
                let cy = card.y + card.h / 2.0;
                if !l.strip.contains(cx, cy) {
                    continue; // off-screen at this anchor
                }
                assert_eq!(
                    l.card_at(cx, cy, anchor),
                    Some(i as i64),
                    "card {i} at cursor {cursor} is not clickable where it is drawn"
                );
            }
        }
        // Outside the strip is not a card.
        assert_eq!(l.card_at(l.window.w / 2.0, 1.0, 0.0), None);
    }

    #[test]
    fn a_tiny_window_still_produces_a_drawable_layout() {
        let mut style = Style {
            width: 200.0,
            height: 120.0,
            ..Style::default()
        };
        style.sanitize();
        let l = layout(&style);
        assert!(
            l.rows_visible() >= 1,
            "there must always be at least one card"
        );
        assert!(l.card_w > 0.0 && l.card_h > 0.0, "no zero-sized cards");
        assert!(l.strip.w >= 0.0 && l.strip.h >= 0.0, "no negative sizes");
    }

    #[test]
    fn emphasis_peaks_on_the_selection_and_falls_off() {
        let (s0, b0) = emphasis(0.0);
        let (s1, b1) = emphasis(1.0);
        let (s4, b4) = emphasis(4.0);
        let (s9, b9) = emphasis(9.0);

        assert!((s0 - 1.0).abs() < 1e-6, "the selected card is full size");
        assert!((b0 - 1.0).abs() < 1e-6, "and full brightness");
        assert!(s1 < s0 && b1 < b0, "a neighbour is smaller and darker");

        // Size stops shrinking after one step - otherwise cards far along the
        // ring would dwindle to nothing and the strip would look like it ends.
        assert!((s4 - s1).abs() < 1e-6 && (s9 - s1).abs() < 1e-6);

        // Brightness keeps falling, slowly, so the ring fades toward the edges
        // of the strip and reads as continuing rather than as the same few
        // cards repeated.
        assert!(b4 < b1, "distance keeps darkening: {b4} vs {b1}");
        assert!(b9 <= b4);
        assert!(
            b9 > 0.25,
            "but never to black - a card must stay a card: {b9}"
        );

        // Symmetric: a card to the left is treated like one to the right.
        assert_eq!(emphasis(-1.0), emphasis(1.0));
        assert_eq!(emphasis(-6.0), emphasis(6.0));
    }

    #[test]
    fn the_skin_follows_the_theme_under_the_cursor() {
        let mut m = model();
        let style = Style::default();
        assert_eq!(Skin::resolve(&m, &style).accent, Rgb::new(255, 0, 0));
        m.move_to(1);
        assert_eq!(Skin::resolve(&m, &style).accent, Rgb::new(0, 255, 0));
    }

    #[test]
    fn config_colours_win_over_the_theme() {
        let m = model();
        let style = Style {
            accent: Some("#123456".into()),
            ..Style::default()
        };
        let skin = Skin::resolve(&m, &style);
        assert_eq!(skin.accent, Rgb::new(0x12, 0x34, 0x56));
        // Unset keys still come from the theme.
        assert_eq!(skin.bg, m.styling_theme().unwrap().palette.bg);
    }

    #[test]
    fn follow_theme_off_pins_the_defaults() {
        let mut m = model();
        let style = Style {
            follow_theme: false,
            ..Style::default()
        };
        let a = Skin::resolve(&m, &style);
        m.move_to(2);
        let b = Skin::resolve(&m, &style);
        assert_eq!(
            a.accent, b.accent,
            "with following off, moving must not restyle"
        );
    }

    #[test]
    fn contrast_colour_flips_with_background_luma() {
        let skin = Skin::resolve(&model(), &Style::default());
        assert!(
            skin.on(Rgb::new(255, 255, 255)).luma() < 0.5,
            "dark ink on light"
        );
        assert!(skin.on(Rgb::new(0, 0, 0)).luma() > 0.5, "light ink on dark");
    }

    #[test]
    fn the_scene_is_well_formed() {
        let m = model();
        let style = Style::default();
        let l = layout(&style);
        let mut images = ImageStore::new(256);
        let mut text = TextEngine::new(None);
        let s = build_scene(&m, &style, &l, &mut images, &mut text);

        assert!(s.clips_balanced(), "every clip push must be popped");
        assert!(!s.cmds.is_empty());
        assert_eq!(s.width, style.width);
        // Everything drawn stays inside the window (the clip stack aside).
        for c in &s.cmds {
            if let crate::scene::Cmd::Rect { rect, .. } = c {
                assert!(
                    rect.x >= -1.0 && rect.y >= -1.0,
                    "a rect starts outside the window: {rect:?}"
                );
            }
        }
    }

    #[test]
    fn every_row_gets_a_preview_even_without_a_wallpaper() {
        let m = model(); // none of these themes ship an image
        let style = Style::default();
        let l = layout(&style);
        let mut images = ImageStore::new(256);
        let mut text = TextEngine::new(None);
        let s = build_scene(&m, &style, &l, &mut images, &mut text);
        assert!(
            !s.images().is_empty(),
            "a themeless-wallpaper list must still show generated swatches"
        );
    }

    #[test]
    fn an_empty_list_renders_a_message_instead_of_nothing() {
        let mut m = model();
        for c in "zzzz".chars() {
            m.push_char(c);
        }
        let style = Style::default();
        let l = layout(&style);
        let mut images = ImageStore::new(256);
        let mut text = TextEngine::new(None);
        let s = build_scene(&m, &style, &l, &mut images, &mut text);
        let has_message = s.cmds.iter().any(
            |c| matches!(c, crate::scene::Cmd::Text { text, .. } if text.contains("no matches")),
        );
        assert!(has_message, "an empty result must say so");
        assert!(s.clips_balanced());
    }

    #[test]
    fn a_long_list_draws_only_what_is_near_the_strip() {
        let style = Style::default();
        let l = layout(&style);
        let mut images = ImageStore::new(256);
        let mut text = TextEngine::new(None);

        // Five hundred themes must not become five hundred draw calls: only the
        // cards near the strip are worth emitting, or every frame would cost
        // proportional to the catalogue.
        let many: Vec<Theme> = (0..500)
            .map(|i| theme(&format!("t{i}"), Rgb::new(1, 2, 3)))
            .collect();
        let mut big = Model::new(many, Vec::new(), Session::Any);
        big.set_view(l.rows_visible(), l.advance);
        let s = build_scene(&big, &style, &l, &mut images, &mut text);

        let images_drawn = s.images().len();
        assert!(
            images_drawn <= l.rows_visible() + 6,
            "{images_drawn} cards drawn for a {}-wide strip",
            l.rows_visible()
        );
        assert!(images_drawn > 0, "but some are drawn");
        assert!(s.clips_balanced());
    }

    #[test]
    fn the_strip_is_the_same_size_wherever_the_cursor_is() {
        let style = Style::default();
        let l = layout(&style);
        let mut images = ImageStore::new(256);
        let mut text = TextEngine::new(None);
        let many: Vec<Theme> = (0..40)
            .map(|i| theme(&format!("t{i}"), Rgb::new(1, 2, 3)))
            .collect();
        let mut m = Model::new(many, Vec::new(), Session::Any);
        m.set_view(l.rows_visible(), l.advance);

        let at_start = build_scene(&m, &style, &l, &mut images, &mut text)
            .cmds
            .len();
        m.move_to(20);
        let middle = build_scene(&m, &style, &l, &mut images, &mut text)
            .cmds
            .len();
        m.move_to(39);
        let at_end = build_scene(&m, &style, &l, &mut images, &mut text)
            .cmds
            .len();

        // Within a card's worth of commands: the strip is full at every
        // position, which is what the end-clamping is for.
        let spread = [at_start, middle, at_end];
        let lo = *spread.iter().min().unwrap();
        let hi = *spread.iter().max().unwrap();
        assert!(
            hi - lo <= 8,
            "the strip should stay full: start={at_start} middle={middle} end={at_end}"
        );
    }

    #[test]
    fn wallpaper_mode_renders_without_a_theme_under_the_cursor() {
        let mut m = model();
        m.switch_mode();
        assert_eq!(m.mode, Mode::Wallpapers);
        let style = Style::default();
        let l = layout(&style);
        let mut images = ImageStore::new(256);
        let mut text = TextEngine::new(None);
        // The wallpaper does not exist on disk; this must still not panic.
        let s = build_scene(&m, &style, &l, &mut images, &mut text);
        assert!(s.clips_balanced());
    }
}
