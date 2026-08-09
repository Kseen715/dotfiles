//! Picker state: what is listed, what is selected, what typing does.
//!
//! Kept free of any windowing or drawing type on purpose. Every interaction rule
//! that is easy to get subtly wrong — where the cursor lands after filtering,
//! what wraps, what Escape means when a filter is active — is decided here and
//! tested without a display server.

use std::path::PathBuf;

use crate::anim::{Eased, SCROLL_TAU};
use crate::catalog::{Session, Theme};
use crate::fuzzy;

/// What the picker is browsing.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Mode {
    Themes,
    Wallpapers,
}

impl Mode {
    pub fn other(self) -> Mode {
        match self {
            Mode::Themes => Mode::Wallpapers,
            Mode::Wallpapers => Mode::Themes,
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Mode::Themes => "themes",
            Mode::Wallpapers => "wallpapers",
        }
    }
}

/// What the user asked for by pressing a key. The event loop turns these into
/// side effects; the model itself never runs a command.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Action {
    None,
    Redraw,
    /// Apply the theme with this name.
    ApplyTheme(String),
    /// Set this wallpaper.
    ApplyWallpaper(PathBuf),
    Quit,
}

/// One row in the list.
#[derive(Debug, Clone)]
pub struct Row {
    pub title: String,
    pub subtitle: String,
    /// Image to show as this row's preview, if any.
    pub preview: Option<PathBuf>,
    /// Marks the entry currently applied on the system.
    pub current: bool,
    /// False when the entry targets a different display server.
    pub fits_session: bool,
}

pub struct Model {
    pub themes: Vec<Theme>,
    pub wallpapers: Vec<PathBuf>,
    pub mode: Mode,
    pub query: String,
    /// Index into `visible`, not into the underlying list.
    pub cursor: usize,
    /// Indices into the active list, after filtering and ranking.
    pub visible: Vec<usize>,
    pub session: Session,
    pub current_theme: Option<String>,
    pub current_wallpaper: Option<PathBuf>,
    /// How many items the window can show; the view sets this on resize.
    pub rows_visible: usize,

    /// The selection's position on an UNBOUNDED line, before wrapping.
    ///
    /// `cursor` is this modulo the list length. The two exist separately because
    /// the strip is a ring: stepping right from the last item must slide FORWARD
    /// into the first, and a wrapped position cannot express that - it would
    /// jump backwards past every card instead. Keeping an unbounded position and
    /// wrapping only when asking "which item is that" makes the motion
    /// continuous and the selection correct at the same time.
    virtual_cursor: i64,
    /// The position the VIEW draws at, easing toward `virtual_cursor`.
    cursor_anim: Eased,
    /// Distance in pixels from one item to the next along the scroll axis.
    ///
    /// The animation runs in pixels because that is where its settle threshold
    /// is meaningful (a third of a pixel is invisible; a third of an ITEM is
    /// not). The view reports it, so the model does not need to know that the
    /// axis is horizontal.
    item_advance: f32,
    /// When false every move lands instantly - the config's `animate = false`.
    animate: bool,
}

impl Model {
    pub fn new(themes: Vec<Theme>, wallpapers: Vec<PathBuf>, session: Session) -> Self {
        let mut m = Model {
            themes,
            wallpapers,
            mode: Mode::Themes,
            query: String::new(),
            cursor: 0,
            visible: Vec::new(),
            session,
            current_theme: None,
            current_wallpaper: None,
            rows_visible: 8,
            virtual_cursor: 0,
            cursor_anim: Eased::new(0.0, SCROLL_TAU),
            item_advance: 60.0,
            animate: true,
        };
        m.refilter();
        m
    }

    /// Advance the animation. Returns true while another frame is needed.
    pub fn tick(&mut self, dt: std::time::Duration) -> bool {
        self.cursor_anim.tick(dt)
    }

    /// True while anything is still moving.
    pub fn animating(&self) -> bool {
        self.cursor_anim.animating()
    }

    /// Where the strip should be drawn, as a fractional VIRTUAL index.
    ///
    /// Unbounded: it runs past the end of the list and keeps going, which is
    /// what makes the ring turn instead of snapping back. The view wraps it to
    /// pick which item each card shows.
    pub fn cursor_offset(&self) -> f32 {
        self.cursor_anim.value() / self.item_advance.max(1.0)
    }

    /// The selection's unbounded position.
    pub fn virtual_cursor(&self) -> i64 {
        self.virtual_cursor
    }

    /// Whether the strip wraps around.
    ///
    /// A single item must not: the ring would be that one card repeated across
    /// the whole strip, and moving would slide identical pictures past for ever.
    pub fn wraps(&self) -> bool {
        self.visible.len() > 1
    }

    /// Wrap a virtual index onto the visible list.
    ///
    /// `None` past the ends when the ring does not wrap. Hit-testing and drawing
    /// must agree about that: a single-item ring draws one card, so a click on
    /// the empty strip beside it has to find nothing rather than wrapping round
    /// to the one item and applying it.
    pub fn item_at_virtual(&self, v: i64) -> Option<usize> {
        let n = self.visible.len();
        if n == 0 {
            return None;
        }
        if !self.wraps() {
            return (v >= 0 && v < n as i64).then_some(v as usize);
        }
        Some(v.rem_euclid(n as i64) as usize)
    }

    /// Configure the motion. `tau` is the easing time constant in seconds.
    pub fn set_animation(&mut self, animate: bool, tau: f32) {
        self.animate = animate;
        self.cursor_anim = Eased::new(self.cursor_anim.value(), tau);
        if !animate {
            self.settle();
        }
    }

    /// Point the animation at the current virtual position.
    fn retarget(&mut self) {
        if !self.animate {
            self.settle();
            return;
        }
        self.cursor_anim
            .set_target(self.virtual_cursor as f32 * self.item_advance);
    }

    /// Jump both animations to the current state without animating - for the
    /// first frame, and for changes where sliding would be meaningless motion
    /// (a mode switch, a filter that reorders the list under the cursor).
    fn settle(&mut self) {
        self.cursor_anim
            .set_immediate(self.virtual_cursor as f32 * self.item_advance);
    }

    /// Number of entries in the active list, before filtering.
    pub fn len(&self) -> usize {
        match self.mode {
            Mode::Themes => self.themes.len(),
            Mode::Wallpapers => self.wallpapers.len(),
        }
    }

    pub fn is_empty(&self) -> bool {
        self.visible.is_empty()
    }

    /// Recompute `visible` from the query, then put the cursor somewhere sane.
    ///
    /// The cursor follows the selected ITEM where it can: typing a character
    /// that reorders the list should not silently move the selection onto a
    /// different theme, because the next key might be Enter.
    pub fn refilter(&mut self) {
        let previously_selected = self.selected_key();
        self.visible = match self.mode {
            Mode::Themes => fuzzy::rank(&self.themes, &self.query, |t| t.name.as_str()),
            Mode::Wallpapers => {
                // Match on the file name, not the whole path: every path shares
                // the directory prefix, so matching it ranks nothing.
                let names: Vec<String> = self
                    .wallpapers
                    .iter()
                    .map(|p| {
                        p.file_name()
                            .map(|s| s.to_string_lossy().to_string())
                            .unwrap_or_default()
                    })
                    .collect();
                fuzzy::rank(&names, &self.query, |s| s.as_str())
            }
        };
        self.cursor = previously_selected
            .and_then(|key| {
                self.visible
                    .iter()
                    .position(|&i| self.key_at(i).as_deref() == Some(key.as_str()))
            })
            .unwrap_or(0);
        // Filtering rebuilds the list, so the cards around the cursor are not
        // the ones that were there a frame ago: sliding between two unrelated
        // positions is noise, not motion. Re-anchor the ring on the selection.
        self.virtual_cursor = self.cursor as i64;
        self.settle();
    }

    /// A stable identifier for the selected entry, used to keep the selection
    /// across a refilter.
    fn selected_key(&self) -> Option<String> {
        self.visible.get(self.cursor).and_then(|&i| self.key_at(i))
    }

    /// `None` when the index does not address the ACTIVE list. That is not
    /// paranoia: `visible` holds indices into whichever list was active when it
    /// was built, so a mode switch leaves indices that are valid for the other
    /// list and may be out of range for this one.
    fn key_at(&self, index: usize) -> Option<String> {
        match self.mode {
            Mode::Themes => self.themes.get(index).map(|t| t.name.clone()),
            Mode::Wallpapers => self
                .wallpapers
                .get(index)
                .map(|p| p.to_string_lossy().to_string()),
        }
    }

    /// The rows to draw, in order.
    pub fn rows(&self) -> Vec<Row> {
        self.visible
            .iter()
            .map(|&i| match self.mode {
                Mode::Themes => {
                    let t = &self.themes[i];
                    Row {
                        title: t.display.clone(),
                        subtitle: if t.description.is_empty() {
                            t.name.clone()
                        } else {
                            t.description.clone()
                        },
                        preview: t.preview().map(|p| p.to_path_buf()),
                        current: self.current_theme.as_deref() == Some(t.name.as_str()),
                        fits_session: t.session.fits(self.session),
                    }
                }
                Mode::Wallpapers => {
                    let p = &self.wallpapers[i];
                    Row {
                        title: p
                            .file_name()
                            .map(|s| s.to_string_lossy().to_string())
                            .unwrap_or_default(),
                        subtitle: p
                            .parent()
                            .map(|s| s.to_string_lossy().to_string())
                            .unwrap_or_default(),
                        preview: Some(p.clone()),
                        current: self.current_wallpaper.as_deref() == Some(p.as_path()),
                        fits_session: true,
                    }
                }
            })
            .collect()
    }

    /// The image to show in the large preview pane.
    pub fn preview(&self) -> Option<PathBuf> {
        let &i = self.visible.get(self.cursor)?;
        match self.mode {
            Mode::Themes => self.themes[i].preview().map(|p| p.to_path_buf()),
            Mode::Wallpapers => Some(self.wallpapers[i].clone()),
        }
    }

    /// The theme whose palette the picker should style itself with: the one
    /// under the cursor, so moving the selection previews the theme live.
    pub fn styling_theme(&self) -> Option<&Theme> {
        match self.mode {
            Mode::Themes => self.visible.get(self.cursor).map(|&i| &self.themes[i]),
            // In wallpaper mode there is no theme under the cursor; keep the
            // applied one so the window does not flash to default colours.
            Mode::Wallpapers => self
                .current_theme
                .as_ref()
                .and_then(|n| self.themes.iter().find(|t| &t.name == n)),
        }
    }

    pub fn move_by(&mut self, delta: isize) {
        if self.visible.is_empty() {
            return;
        }
        let len = self.visible.len() as isize;
        // Wrap: a picker with a handful of rows is faster to reach by going up
        // from the top than by holding Down.
        if !self.wraps() {
            return; // a ring of one is just that one card
        }
        let _ = len;
        // Move along the UNBOUNDED line. Stepping right from the last item takes
        // the virtual position past the end, so the strip keeps sliding forward
        // and the first item comes round - rather than the selection snapping
        // back across the whole list.
        self.virtual_cursor += delta as i64;
        self.cursor = self
            .item_at_virtual(self.virtual_cursor)
            .unwrap_or(self.cursor);
        self.retarget();
    }

    /// Select `index`, taking the shorter way round the ring.
    ///
    /// Home and End are the reason this matters: on a ring, "the last item" is
    /// one step backwards, not `n - 1` steps forwards, and sliding the long way
    /// would be a needless tour of the whole list.
    pub fn move_to(&mut self, index: usize) {
        let n = self.visible.len();
        if index >= n {
            return;
        }
        let current = self.virtual_cursor.rem_euclid(n as i64);
        let mut delta = index as i64 - current;
        if self.wraps() {
            let half = n as i64 / 2;
            if delta > half {
                delta -= n as i64;
            } else if delta < -half {
                delta += n as i64;
            }
        }
        self.virtual_cursor += delta;
        self.cursor = index;
        self.retarget();
    }

    /// Select whichever item sits at an exact position on the ring.
    ///
    /// A click names a card on screen, not an item in a list: the same item may
    /// appear twice on a short ring, and the one the pointer is over is the one
    /// that should slide to the middle.
    pub fn move_to_virtual(&mut self, v: i64) {
        let Some(index) = self.item_at_virtual(v) else {
            return;
        };
        self.virtual_cursor = v;
        self.cursor = index;
        self.retarget();
    }

    /// Move by whole screenfuls.
    ///
    /// Wraps like the arrows do. It used to clamp, on the reasoning that
    /// "PageDown at the bottom should stop rather than teleport" - but on a ring
    /// there is no bottom to stop at, and a key that sometimes moves and
    /// sometimes does not is worse than one that always does the same thing.
    pub fn page_by(&mut self, pages: isize) {
        let step = self.rows_visible.max(1) as isize;
        self.move_by(pages * step);
    }

    /// The view reports its geometry; both are needed to animate in pixels.
    pub fn set_view(&mut self, rows_visible: usize, row_height: f32) {
        let changed = self.rows_visible != rows_visible.max(1)
            || (self.item_advance - row_height).abs() > 0.01;
        self.rows_visible = rows_visible.max(1);
        self.item_advance = row_height.max(1.0);
        if changed {
            // A resize is not motion the user asked for: land, do not slide.
            self.settle();
        }
    }

    pub fn set_rows_visible(&mut self, n: usize) {
        self.set_view(n, self.item_advance);
    }

    pub fn switch_mode(&mut self) {
        self.mode = self.mode.other();
        // The query belonged to the other list; carrying it over would show an
        // empty picker and look like a crash.
        self.query.clear();
        self.cursor = 0;
        self.virtual_cursor = 0;
        self.refilter();
        self.settle();
    }

    pub fn push_char(&mut self, c: char) {
        self.query.push(c);
        self.refilter();
    }

    pub fn pop_char(&mut self) {
        self.query.pop();
        self.refilter();
    }

    pub fn clear_query(&mut self) {
        self.query.clear();
        self.refilter();
    }

    /// What Enter should do right now.
    pub fn activate(&self) -> Action {
        let Some(&i) = self.visible.get(self.cursor) else {
            return Action::None;
        };
        match self.mode {
            Mode::Themes => Action::ApplyTheme(self.themes[i].name.clone()),
            Mode::Wallpapers => Action::ApplyWallpaper(self.wallpapers[i].clone()),
        }
    }

    /// A copy for rendering a single frame (the screenshot path), where the
    /// row count is set from the real layout rather than the default.
    pub fn clone_for_render(&self) -> Model {
        Model {
            themes: self.themes.clone(),
            wallpapers: self.wallpapers.clone(),
            mode: self.mode,
            query: self.query.clone(),
            cursor: self.cursor,
            visible: self.visible.clone(),
            session: self.session,
            current_theme: self.current_theme.clone(),
            current_wallpaper: self.current_wallpaper.clone(),
            rows_visible: self.rows_visible,
            virtual_cursor: self.virtual_cursor,
            cursor_anim: self.cursor_anim,
            item_advance: self.item_advance,
            animate: self.animate,
        }
    }

    /// What Escape should do: clear a filter first, quit only when there is
    /// none. Quitting on the first Escape loses the typing you just did.
    pub fn escape(&mut self) -> Action {
        if self.query.is_empty() {
            Action::Quit
        } else {
            self.clear_query();
            Action::Redraw
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::catalog::{Palette, Rgb};
    use std::path::Path;

    fn theme(name: &str, session: Session, wallpapers: &[&str]) -> Theme {
        Theme {
            name: name.to_string(),
            display: name.to_string(),
            description: format!("{name} description"),
            session,
            polarity: "dark".into(),
            palette: Palette {
                accent: Rgb::new(1, 2, 3),
                ..Palette::default()
            },
            dir: PathBuf::from("/themes").join(name),
            wallpapers: wallpapers.iter().map(PathBuf::from).collect(),
        }
    }

    fn model() -> Model {
        Model::new(
            vec![
                theme("catppuccin", Session::Any, &["/w/cat.png"]),
                theme("glass", Session::Wayland, &[]),
                theme("gruvbox", Session::Any, &["/w/gruv.png"]),
                theme("nord", Session::Any, &[]),
                theme("rosemary", Session::X11, &["/w/rose.png"]),
            ],
            vec![
                PathBuf::from("/w/forest.png"),
                PathBuf::from("/w/city.jpg"),
                PathBuf::from("/w/sea.png"),
            ],
            Session::X11,
        )
    }

    #[test]
    fn starts_on_themes_with_everything_visible() {
        let m = model();
        assert_eq!(m.mode, Mode::Themes);
        assert_eq!(m.visible.len(), 5);
        assert_eq!(m.cursor, 0);
        assert_eq!(m.rows()[0].title, "catppuccin");
    }

    #[test]
    fn navigation_wraps_forwards_on_the_ring() {
        let mut m = model();
        m.set_view(2, 60.0);

        // Stepping back from the first item lands on the last...
        m.move_by(-1);
        assert_eq!(m.cursor, 4);
        // ...and it animates from one step BACK, not four forward. That is the
        // whole point: the strip keeps going the way you pushed it instead of
        // snapping across the list.
        assert_eq!(m.virtual_cursor(), -1);

        m.move_by(1);
        assert_eq!(m.cursor, 0);
        assert_eq!(m.virtual_cursor(), 0);

        // Forwards off the end continues into the first item.
        for _ in 0..5 {
            m.move_by(1);
        }
        assert_eq!(
            m.cursor, 0,
            "five steps from 0 in a list of five is 0 again"
        );
        assert_eq!(
            m.virtual_cursor(),
            5,
            "having gone all the way round, not back"
        );
    }

    #[test]
    fn paging_wraps_too() {
        let mut m = model();
        m.set_view(2, 60.0);
        m.page_by(1);
        assert_eq!(m.cursor, 2, "a page is a screenful");
        // Paging used to clamp at the ends. On a ring there is no end to clamp
        // to, and a key that sometimes moves and sometimes does not is worse
        // than one that always behaves the same.
        m.page_by(10);
        assert_eq!(m.virtual_cursor(), 22);
        assert_eq!(m.cursor, 22 % 5);
    }

    #[test]
    fn a_single_item_does_not_spin() {
        // A ring of one would be that card repeated across the whole strip,
        // sliding identical pictures past for ever.
        let mut m = Model::new(
            vec![theme("only", Session::Any, &[])],
            Vec::new(),
            Session::Any,
        );
        m.set_view(3, 60.0);
        assert!(!m.wraps());
        m.move_by(1);
        assert_eq!(m.cursor, 0);
        assert_eq!(m.virtual_cursor(), 0, "nowhere to go");
        assert!(!m.animating());
    }

    #[test]
    fn selecting_takes_the_short_way_round() {
        let mut m = model(); // five themes
        m.set_view(3, 60.0);
        // From item 0 the last item is ONE step back on the ring, not four
        // forward - End should not tour the whole list to get there.
        m.move_to(4);
        assert_eq!(m.cursor, 4);
        assert_eq!(m.virtual_cursor(), -1, "went backwards by one");
        m.move_to(0);
        assert_eq!(m.virtual_cursor(), 0);
    }

    #[test]
    fn clicking_a_card_slides_to_that_card_not_to_the_nearest_copy() {
        let mut m = model();
        m.set_view(3, 60.0);
        // On a short ring the same item shows up more than once. Clicking the
        // copy three places right must move three right, even though the same
        // item is also two to the left.
        m.move_to_virtual(3);
        assert_eq!(m.virtual_cursor(), 3);
        assert_eq!(m.cursor, 3);
        m.move_to_virtual(7);
        assert_eq!(m.virtual_cursor(), 7);
        assert_eq!(m.cursor, 2, "7 wraps onto item 2 of five");
    }

    #[test]
    fn a_virtual_index_wraps_onto_the_list() {
        let m = model();
        assert_eq!(m.item_at_virtual(0), Some(0));
        assert_eq!(m.item_at_virtual(5), Some(0));
        assert_eq!(
            m.item_at_virtual(-1),
            Some(4),
            "negative wraps from the end"
        );
        assert_eq!(m.item_at_virtual(-6), Some(4));
        let empty = Model::new(Vec::new(), Vec::new(), Session::Any);
        assert_eq!(empty.item_at_virtual(0), None, "an empty list has no items");
    }

    #[test]
    fn filtering_keeps_the_selected_item_under_the_cursor() {
        let mut m = model();
        m.move_to(3); // nord
        assert_eq!(m.rows()[m.cursor].title, "nord");
        // Typing a query that still matches nord must not move the selection to
        // some other theme - the next key might be Enter.
        m.push_char('n');
        assert_eq!(
            m.rows()[m.cursor].title,
            "nord",
            "selection followed the item, not the index"
        );
    }

    #[test]
    fn filtering_to_nothing_leaves_an_empty_list_and_a_safe_enter() {
        let mut m = model();
        for c in "zzzz".chars() {
            m.push_char(c);
        }
        assert!(m.is_empty());
        assert_eq!(
            m.activate(),
            Action::None,
            "Enter on an empty list does nothing"
        );
        assert!(m.preview().is_none());
        // ...and backspacing recovers.
        m.pop_char();
        m.pop_char();
        m.pop_char();
        m.pop_char();
        assert_eq!(m.visible.len(), 5);
    }

    #[test]
    fn escape_clears_the_filter_before_quitting() {
        let mut m = model();
        m.push_char('n');
        assert_eq!(m.escape(), Action::Redraw, "first Escape clears the query");
        assert!(m.query.is_empty());
        assert_eq!(m.escape(), Action::Quit, "Escape with no query quits");
    }

    #[test]
    fn enter_applies_the_selected_entry() {
        let mut m = model();
        m.move_to(3);
        assert_eq!(m.activate(), Action::ApplyTheme("nord".into()));
        m.switch_mode();
        assert_eq!(m.mode, Mode::Wallpapers);
        assert_eq!(
            m.activate(),
            Action::ApplyWallpaper(PathBuf::from("/w/forest.png"))
        );
    }

    #[test]
    fn switching_mode_resets_the_query() {
        let mut m = model();
        m.push_char('n');
        m.switch_mode();
        assert!(
            m.query.is_empty(),
            "a query for themes must not filter wallpapers"
        );
        assert_eq!(m.visible.len(), 3);
        assert_eq!(m.cursor, 0);
    }

    #[test]
    fn wallpapers_are_matched_on_filename_not_full_path() {
        let mut m = model();
        m.switch_mode();
        for c in "city".chars() {
            m.push_char(c);
        }
        assert_eq!(m.visible.len(), 1);
        assert_eq!(m.rows()[0].title, "city.jpg");
        // The shared directory prefix must not be matchable, or every query
        // starting with "w" would match everything.
        m.clear_query();
        assert_eq!(
            m.rows()[0].subtitle,
            "/w",
            "the directory is shown as the subtitle"
        );
    }

    #[test]
    fn off_session_themes_are_marked_but_never_hidden() {
        let m = model(); // current session is X11
        let rows = m.rows();
        let glass = rows
            .iter()
            .find(|r| r.title == "glass")
            .expect("glass is listed");
        assert!(
            !glass.fits_session,
            "a Wayland theme is marked on an X11 session"
        );
        let rose = rows.iter().find(|r| r.title == "rosemary").unwrap();
        assert!(rose.fits_session);
        assert_eq!(rows.len(), 5, "no theme is filtered out by session");
    }

    #[test]
    fn the_applied_entry_is_marked() {
        let mut m = model();
        m.current_theme = Some("gruvbox".into());
        let rows = m.rows();
        assert!(rows.iter().filter(|r| r.current).count() == 1);
        assert!(rows.iter().find(|r| r.title == "gruvbox").unwrap().current);
    }

    #[test]
    fn styling_follows_the_cursor_in_theme_mode() {
        let mut m = model();
        m.move_to(2);
        assert_eq!(m.styling_theme().unwrap().name, "gruvbox");
        // In wallpaper mode there is no theme under the cursor, so the applied
        // one keeps the window from flashing to default colours.
        m.current_theme = Some("nord".into());
        m.switch_mode();
        assert_eq!(m.styling_theme().unwrap().name, "nord");
    }

    #[test]
    fn a_theme_without_a_wallpaper_has_no_preview() {
        let mut m = model();
        m.move_to(3); // nord ships none
        assert!(m.preview().is_none());
        m.move_to(0);
        assert_eq!(m.preview().unwrap(), Path::new("/w/cat.png"));
    }

    #[test]
    fn moving_slides_and_then_settles_on_the_row() {
        let mut m = model();
        m.set_view(3, 60.0);
        assert!(!m.animating(), "at rest to begin with");

        m.move_by(1);
        assert!(m.animating(), "a move starts the slide");
        assert!(
            m.cursor_offset() < 1.0,
            "and it starts from where the bar was, not at the destination"
        );

        while m.tick(crate::anim::FRAME) {}
        assert!((m.cursor_offset() - 1.0).abs() < 1e-3, "it lands on row 1");
        assert!(!m.animating());
    }

    #[test]
    fn the_ring_position_keeps_running_past_the_end() {
        let mut m = model();
        m.set_view(2, 60.0);
        // Six steps forward on a five-item ring: the drawn position is past the
        // end, not wrapped back to the start. That unbounded value is what the
        // view turns into a strip that keeps moving.
        for _ in 0..6 {
            m.move_by(1);
        }
        while m.tick(crate::anim::FRAME) {}
        assert_eq!(m.virtual_cursor(), 6);
        assert!(
            m.cursor_offset() > 5.0,
            "the drawn position ran past the list, got {}",
            m.cursor_offset()
        );
        assert_eq!(m.cursor, 1, "while the selection is the wrapped item");
    }

    #[test]
    fn animation_can_be_turned_off() {
        let mut m = model();
        m.set_view(3, 60.0);
        m.set_animation(false, 0.035);
        m.move_by(1);
        assert!(!m.animating(), "with animation off a move is instant");
        assert!((m.cursor_offset() - 1.0).abs() < 1e-3);
    }

    #[test]
    fn filtering_does_not_slide_between_unrelated_rows() {
        let mut m = model();
        m.set_view(3, 60.0);
        m.move_to(3);
        while m.tick(crate::anim::FRAME) {}
        // Typing rebuilds the list, so the row at a given index is a different
        // theme: sliding between the two positions would be noise.
        m.push_char('g');
        assert!(
            !m.animating(),
            "a refilter should land the selection, not animate it"
        );
    }

    #[test]
    fn an_empty_catalogue_does_not_panic() {
        let mut m = Model::new(Vec::new(), Vec::new(), Session::Any);
        assert!(m.is_empty());
        m.move_by(1);
        m.page_by(1);
        assert_eq!(m.activate(), Action::None);
        assert_eq!(m.escape(), Action::Quit);
        assert!(m.rows().is_empty());
    }
}
