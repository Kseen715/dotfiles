//! Decoded image storage.
//!
//! Wallpapers are big — a 4K JPEG is 24 MB decoded — and the picker shows a row
//! of them. So nothing keeps a full-size image: everything is downscaled to a
//! bounding box on load and only the small result is retained. A theme list of
//! thirty 4K wallpapers then costs a few megabytes instead of ~700.
//!
//! Decoding is also where a picker meets hostile input: `~/Pictures/Wallpapers`
//! is a user directory that may hold a truncated download or a file that is not
//! an image at all. Every failure here is a `None`, never a panic — a broken
//! image must cost you its thumbnail, not the whole picker.

use std::collections::{HashMap, VecDeque};
use std::path::{Path, PathBuf};

use crate::scene::ImageId;

/// An RGBA8 image held in memory.
#[derive(Debug, Clone)]
pub struct Bitmap {
    pub width: u32,
    pub height: u32,
    /// `width * height * 4` bytes, straight (non-premultiplied) RGBA.
    pub pixels: Vec<u8>,
}

impl Bitmap {
    pub fn aspect(&self) -> f32 {
        if self.height == 0 {
            1.0
        } else {
            self.width as f32 / self.height as f32
        }
    }

    /// Sample with nearest-neighbour at normalised coordinates. Out-of-range
    /// coordinates clamp rather than wrap, so a rounding error at an edge shows
    /// the edge pixel instead of the opposite side of the image.
    pub fn sample(&self, u: f32, v: f32) -> [u8; 4] {
        if self.width == 0 || self.height == 0 {
            return [0, 0, 0, 0];
        }
        let x = ((u.clamp(0.0, 1.0) * self.width as f32) as u32).min(self.width - 1);
        let y = ((v.clamp(0.0, 1.0) * self.height as f32) as u32).min(self.height - 1);
        let i = ((y * self.width + x) * 4) as usize;
        [
            self.pixels[i],
            self.pixels[i + 1],
            self.pixels[i + 2],
            self.pixels[i + 3],
        ]
    }

    /// A flat colour image, used for generated previews.
    pub fn solid(width: u32, height: u32, rgba: [u8; 4]) -> Bitmap {
        Bitmap {
            width,
            height,
            pixels: rgba
                .iter()
                .copied()
                .cycle()
                .take((width * height * 4) as usize)
                .collect(),
        }
    }
}

/// Loads images on demand and hands out stable [`ImageId`]s.
///
/// Two things keep a big wallpaper library from turning into a memory leak:
///
/// - **A byte budget.** Decoded images are held in an LRU and evicted once the
///   total passes `budget`. Without it, browsing 200 wallpapers at ~2.5 MB each
///   would grow to half a gigabyte and never come back down — the cache would
///   be indistinguishable from a leak.
/// - **A disk cache.** The downscaled result is written to
///   `$XDG_CACHE_HOME/proteus/thumbs`, so the second run reads a ~40 KB PNG
///   instead of decoding a 24 MB JPEG. That is what makes a library of hundreds
///   openable at all.
///
/// Ids stay valid after eviction: the slot is emptied, not removed, so a
/// [`ImageId`] handed to a scene never dangles or silently points at a different
/// picture. A drawn-but-evicted image simply does not draw.
pub struct ImageStore {
    bitmaps: Vec<Option<Bitmap>>,
    by_key: HashMap<String, ImageId>,
    /// Longest edge a decoded image is downscaled to.
    max_edge: u32,
    /// Bytes of decoded pixels currently held.
    bytes: usize,
    /// Ceiling for `bytes`; 0 disables eviction.
    budget: usize,
    /// Ids in least-recently-used order, front = oldest.
    lru: VecDeque<ImageId>,
    /// Where downscaled copies are kept between runs; `None` disables it.
    disk_cache: Option<PathBuf>,
}

/// Default in-memory budget.
///
/// 25 MB holds a screenful of previews several times over while staying
/// unremarkable in `top`. Past it, images are evicted and re-read from the disk
/// cache on demand - which is why the two features belong together: eviction is
/// only cheap because coming back costs a small PNG read rather than decoding
/// the original again.
pub const DEFAULT_BUDGET: usize = 25 * 1024 * 1024;

/// Ceiling for the on-disk thumbnail cache. Past this the oldest thumbnails are
/// deleted: a cache that only ever grows is a disk leak with a friendly name.
pub const DEFAULT_DISK_BUDGET: u64 = 200 * 1024 * 1024;

impl Default for ImageStore {
    fn default() -> Self {
        Self::new(1024)
    }
}

impl ImageStore {
    pub fn new(max_edge: u32) -> Self {
        Self {
            bitmaps: Vec::new(),
            by_key: HashMap::new(),
            max_edge: max_edge.max(16),
            bytes: 0,
            budget: DEFAULT_BUDGET,
            lru: VecDeque::new(),
            disk_cache: None,
        }
    }

    /// Set the in-memory budget in bytes. 0 means unbounded.
    pub fn with_budget(mut self, bytes: usize) -> Self {
        self.budget = bytes;
        self
    }

    /// Keep downscaled copies under `dir` between runs.
    pub fn with_disk_cache(mut self, dir: Option<PathBuf>) -> Self {
        self.disk_cache = dir;
        self
    }

    /// The conventional location: `$XDG_CACHE_HOME/proteus/thumbs`.
    pub fn default_cache_dir() -> Option<PathBuf> {
        let base = std::env::var_os("XDG_CACHE_HOME")
            .map(PathBuf::from)
            .or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".cache")))?;
        Some(base.join("proteus").join("thumbs"))
    }

    pub fn get(&self, id: ImageId) -> Option<&Bitmap> {
        self.bitmaps.get(id as usize)?.as_ref()
    }

    /// Number of slots handed out (including evicted ones).
    pub fn len(&self) -> usize {
        self.bitmaps.len()
    }

    pub fn is_empty(&self) -> bool {
        self.bitmaps.is_empty()
    }

    /// Bytes of decoded pixels currently resident.
    pub fn bytes_used(&self) -> usize {
        self.bytes
    }

    /// How many images are actually resident right now.
    pub fn resident(&self) -> usize {
        self.bitmaps.iter().filter(|b| b.is_some()).count()
    }

    fn touch(&mut self, id: ImageId) {
        if let Some(pos) = self.lru.iter().position(|&i| i == id) {
            self.lru.remove(pos);
        }
        self.lru.push_back(id);
    }

    /// Drop least-recently-used images until the budget is met.
    ///
    /// The most recent entry is never evicted even if it alone exceeds the
    /// budget: it is the one being drawn this frame, and evicting it would mean
    /// decoding it again immediately, for ever.
    fn evict(&mut self) {
        if self.budget == 0 {
            return;
        }
        while self.bytes > self.budget && self.lru.len() > 1 {
            let Some(oldest) = self.lru.pop_front() else {
                break;
            };
            if let Some(slot) = self.bitmaps.get_mut(oldest as usize) {
                if let Some(bmp) = slot.take() {
                    self.bytes = self.bytes.saturating_sub(bmp.pixels.len());
                }
            }
        }
    }

    fn insert(&mut self, key: String, bmp: Bitmap) -> ImageId {
        let id = self.bitmaps.len() as ImageId;
        self.bytes += bmp.pixels.len();
        self.bitmaps.push(Some(bmp));
        self.by_key.insert(key, id);
        self.lru.push_back(id);
        self.evict();
        id
    }

    /// Load and downscale `path`, or return the id already held for it.
    ///
    /// `None` on any failure — unreadable, undecodable, or not an image. The
    /// caller draws a generated preview instead.
    pub fn load(&mut self, path: &Path) -> Option<ImageId> {
        let key = path.to_string_lossy().to_string();
        if let Some(&id) = self.by_key.get(&key) {
            if self
                .bitmaps
                .get(id as usize)
                .and_then(|s| s.as_ref())
                .is_some()
            {
                self.touch(id);
                return Some(id);
            }
            // Known but evicted: decode again into the same slot, so the id the
            // caller may still be holding stays correct.
            let bmp = self.decode(path)?;
            self.bytes += bmp.pixels.len();
            self.bitmaps[id as usize] = Some(bmp);
            self.touch(id);
            self.evict();
            return Some(id);
        }
        let bmp = self.decode(path)?;
        Some(self.insert(key, bmp))
    }

    /// Decode via the disk cache when possible.
    fn decode(&self, path: &Path) -> Option<Bitmap> {
        if let Some(dir) = &self.disk_cache {
            let cached = thumb_path(dir, path, self.max_edge);
            if let Some(bmp) = read_thumb(&cached) {
                return Some(bmp);
            }
            let bmp = decode_scaled(path, self.max_edge)?;
            // Best-effort: a cache that cannot be written is a slow start, not
            // a failure.
            write_thumb(&cached, &bmp);
            return Some(bmp);
        }
        decode_scaled(path, self.max_edge)
    }

    /// Register an image built in memory under a synthetic key.
    pub fn insert_generated(&mut self, key: &str, bmp: Bitmap) -> ImageId {
        if let Some(&id) = self.by_key.get(key) {
            if self
                .bitmaps
                .get(id as usize)
                .and_then(|s| s.as_ref())
                .is_some()
            {
                self.touch(id);
                return id;
            }
            self.bytes += bmp.pixels.len();
            self.bitmaps[id as usize] = Some(bmp);
            self.touch(id);
            self.evict();
            return id;
        }
        self.insert(key.to_string(), bmp)
    }

    /// Whether a path is resident right now — lets the UI decide between a real
    /// preview and a placeholder without triggering a decode mid-frame.
    pub fn cached(&self, path: &Path) -> Option<ImageId> {
        let id = *self.by_key.get(&path.to_string_lossy().to_string())?;
        self.bitmaps.get(id as usize)?.as_ref().map(|_| id)
    }
}

/// Where a given source image's thumbnail lives.
///
/// The name folds in the modification time and size, so editing or replacing a
/// wallpaper produces a different name and the stale thumbnail is simply never
/// read again. That is cheaper and more reliable than trying to invalidate.
fn thumb_path(dir: &Path, source: &Path, max_edge: u32) -> PathBuf {
    use std::hash::{Hash, Hasher};
    let mut h = std::collections::hash_map::DefaultHasher::new();
    source.hash(&mut h);
    max_edge.hash(&mut h);
    if let Ok(meta) = std::fs::metadata(source) {
        meta.len().hash(&mut h);
        if let Ok(t) = meta.modified() {
            if let Ok(d) = t.duration_since(std::time::UNIX_EPOCH) {
                d.as_secs().hash(&mut h);
            }
        }
    }
    dir.join(format!("{:016x}.png", h.finish()))
}

fn read_thumb(path: &Path) -> Option<Bitmap> {
    let img = image::ImageReader::open(path).ok()?.decode().ok()?;
    let (w, h) = (img.width(), img.height());
    (w > 0 && h > 0).then(|| Bitmap {
        width: w,
        height: h,
        pixels: img.to_rgba8().into_raw(),
    })
}

/// Delete the oldest thumbnails until the directory is under `budget` bytes.
///
/// Cheap and approximate on purpose: it runs once at startup, reads one
/// directory, and sorts by mtime. A thumbnail deleted while still wanted costs
/// one re-decode.
pub fn prune_cache_dir(dir: &Path, budget: u64) {
    let Ok(entries) = std::fs::read_dir(dir) else {
        return;
    };
    let mut files: Vec<(std::time::SystemTime, u64, PathBuf)> = entries
        .filter_map(|e| e.ok())
        .filter_map(|e| {
            let meta = e.metadata().ok()?;
            if !meta.is_file() {
                return None;
            }
            Some((
                meta.modified().unwrap_or(std::time::UNIX_EPOCH),
                meta.len(),
                e.path(),
            ))
        })
        .collect();
    let mut total: u64 = files.iter().map(|(_, n, _)| *n).sum();
    if total <= budget {
        return;
    }
    files.sort_by_key(|(t, _, _)| *t);
    for (_, size, path) in files {
        if total <= budget {
            break;
        }
        if std::fs::remove_file(&path).is_ok() {
            total = total.saturating_sub(size);
        }
    }
}

fn write_thumb(path: &Path, bmp: &Bitmap) {
    let Some(dir) = path.parent() else { return };
    if std::fs::create_dir_all(dir).is_err() {
        return;
    }
    let Some(buf) = image::RgbaImage::from_raw(bmp.width, bmp.height, bmp.pixels.clone()) else {
        return;
    };
    // Written to a temp name and renamed, so a killed process cannot leave a
    // half-written PNG that later reads as a corrupt thumbnail. The format is
    // named explicitly because the temp extension is not one `image` can infer
    // from - inferring it silently wrote nothing at all.
    let tmp = path.with_extension(format!("tmp{}", std::process::id()));
    if buf.save_with_format(&tmp, image::ImageFormat::Png).is_ok() {
        let _ = std::fs::rename(&tmp, path);
    } else {
        let _ = std::fs::remove_file(&tmp);
    }
}

/// Decode `path` and downscale so its longest edge is at most `max_edge`.
fn decode_scaled(path: &Path, max_edge: u32) -> Option<Bitmap> {
    let reader = image::ImageReader::open(path)
        .ok()?
        .with_guessed_format()
        .ok()?;
    let img = reader.decode().ok()?;
    let (w, h) = (img.width(), img.height());
    if w == 0 || h == 0 {
        return None;
    }
    let scale = (max_edge as f32 / w.max(h) as f32).min(1.0);
    let (tw, th) = (
        ((w as f32 * scale).round() as u32).max(1),
        ((h as f32 * scale).round() as u32).max(1),
    );
    // Triangle: good enough for a thumbnail and markedly cheaper than Lanczos
    // on a 4K source, which matters because this runs while the window is open.
    let scaled = if (tw, th) == (w, h) {
        img.to_rgba8()
    } else {
        image::imageops::resize(
            &img.to_rgba8(),
            tw,
            th,
            image::imageops::FilterType::Triangle,
        )
    };
    Some(Bitmap {
        width: tw,
        height: th,
        pixels: scaled.into_raw(),
    })
}

/// A preview for a theme that ships no wallpaper: a small mock of a themed
/// screen, built from the theme's own palette.
///
/// This exists so every row has something to look at. A blank rectangle where a
/// preview should be reads as "this theme is broken", and a raw colour-bar chart
/// reads as data rather than as an appearance. A tiny window mock — a tinted
/// field, a surface panel, an accent bar and a few text lines — is the smallest
/// thing that answers "what will my desktop look like".
pub fn palette_preview(width: u32, height: u32, p: &crate::catalog::Palette) -> Bitmap {
    let mut pixels = vec![0u8; (width as usize) * (height as usize) * 4];
    if width == 0 || height == 0 {
        return Bitmap {
            width,
            height,
            pixels,
        };
    }
    let put = |pixels: &mut Vec<u8>, x: u32, y: u32, c: crate::catalog::Rgb| {
        if x >= width || y >= height {
            return;
        }
        let i = ((y * width + x) * 4) as usize;
        pixels[i] = c.r;
        pixels[i + 1] = c.g;
        pixels[i + 2] = c.b;
        pixels[i + 3] = 255;
    };

    // A gentle vertical wash from bg to surface, so the two darkest roles are
    // both visible even when they are close together.
    for y in 0..height {
        let t = y as f32 / (height.max(2) - 1) as f32;
        let c = p.bg.mix(p.surface, t * 0.85);
        for x in 0..width {
            put(&mut pixels, x, y, c);
        }
    }

    let fx = |f: f32| (f * width as f32) as u32;
    let fy = |f: f32| (f * height as f32) as u32;

    // A surface "panel" occupying the lower right, as a window would.
    for y in fy(0.30)..fy(0.86) {
        for x in fx(0.28)..fx(0.92) {
            put(&mut pixels, x, y, p.surface);
        }
    }
    // An accent bar along the top of the panel - the selection colour.
    for y in fy(0.30)..fy(0.38) {
        for x in fx(0.28)..fx(0.92) {
            put(&mut pixels, x, y, p.accent);
        }
    }
    // Text lines: one in fg, two in dim, as a list would look.
    let lines = [(0.46, 0.74, p.fg), (0.58, 0.66, p.dim), (0.70, 0.58, p.dim)];
    for (top, right, color) in lines {
        for y in fy(top)..fy(top + 0.06) {
            for x in fx(0.34)..fx(right) {
                put(&mut pixels, x, y, color);
            }
        }
    }
    // A small accent swatch, so the accent reads even if the panel is clipped.
    for y in fy(0.08)..fy(0.20) {
        for x in fx(0.08)..fx(0.20) {
            put(&mut pixels, x, y, p.accent);
        }
    }

    Bitmap {
        width,
        height,
        pixels,
    }
}

/// Every image in a directory, lexically ordered.
pub fn images_in(dir: &Path) -> Vec<PathBuf> {
    crate::catalog::images_in(dir)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::catalog::Rgb;
    use std::fs;

    fn tmpdir(tag: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!(
            "proteus-img-{tag}-{}-{:?}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        fs::create_dir_all(&d).unwrap();
        d
    }

    /// Write a real PNG of a known size and colour.
    fn write_png(path: &Path, w: u32, h: u32, rgb: [u8; 3]) {
        let mut img = image::RgbaImage::new(w, h);
        for p in img.pixels_mut() {
            *p = image::Rgba([rgb[0], rgb[1], rgb[2], 255]);
        }
        img.save(path).unwrap();
    }

    #[test]
    fn decodes_and_downscales_to_the_bounding_box() {
        let d = tmpdir("scale");
        let p = d.join("big.png");
        write_png(&p, 800, 400, [10, 20, 30]);

        let mut store = ImageStore::new(100);
        let id = store.load(&p).expect("a valid PNG decodes");
        let bmp = store.get(id).unwrap();
        assert_eq!(
            (bmp.width, bmp.height),
            (100, 50),
            "longest edge is capped, aspect kept"
        );
        assert_eq!(bmp.pixels.len(), (100 * 50 * 4) as usize);
        assert!((bmp.aspect() - 2.0).abs() < 0.01);
        // The colour survived the resize.
        let px = bmp.sample(0.5, 0.5);
        assert_eq!(&px[..3], &[10, 20, 30]);
        fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn a_small_image_is_not_upscaled() {
        let d = tmpdir("small");
        let p = d.join("tiny.png");
        write_png(&p, 32, 16, [1, 2, 3]);
        let mut store = ImageStore::new(512);
        let id = store.load(&p).unwrap();
        let bmp = store.get(id).unwrap().clone();
        assert_eq!(
            (bmp.width, bmp.height),
            (32, 16),
            "upscaling would only blur it"
        );
        fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn the_same_path_is_decoded_once() {
        let d = tmpdir("cache");
        let p = d.join("x.png");
        write_png(&p, 20, 20, [9, 9, 9]);
        let mut store = ImageStore::new(64);
        let a = store.load(&p).unwrap();
        let b = store.load(&p).unwrap();
        assert_eq!(a, b);
        assert_eq!(store.len(), 1, "a repeated path must not re-decode");
        assert_eq!(store.cached(&p), Some(a));
        fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn broken_input_costs_a_thumbnail_not_the_picker() {
        let d = tmpdir("broken");
        let mut store = ImageStore::new(64);

        // Missing file.
        assert!(store.load(&d.join("nope.png")).is_none());

        // A text file wearing a .png extension - exactly what a stray README in
        // a wallpapers dir looks like.
        fs::write(d.join("fake.png"), b"this is not a png").unwrap();
        assert!(store.load(&d.join("fake.png")).is_none());

        // A truncated PNG: a real header, then nothing.
        let p = d.join("cut.png");
        write_png(&p, 40, 40, [5, 5, 5]);
        let bytes = fs::read(&p).unwrap();
        fs::write(&p, &bytes[..bytes.len() / 2]).unwrap();
        assert!(store.load(&p).is_none(), "a truncated file must not panic");

        // An empty file.
        fs::write(d.join("empty.png"), b"").unwrap();
        assert!(store.load(&d.join("empty.png")).is_none());

        assert!(store.is_empty(), "nothing broken was stored");
        fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn sampling_clamps_at_the_edges() {
        let bmp = Bitmap::solid(4, 4, [7, 8, 9, 255]);
        assert_eq!(bmp.sample(0.0, 0.0), [7, 8, 9, 255]);
        // Past the far edge must clamp, not wrap to the opposite side.
        assert_eq!(bmp.sample(1.5, 1.5), [7, 8, 9, 255]);
        assert_eq!(bmp.sample(-1.0, -1.0), [7, 8, 9, 255]);
        let empty = Bitmap {
            width: 0,
            height: 0,
            pixels: Vec::new(),
        };
        assert_eq!(
            empty.sample(0.5, 0.5),
            [0, 0, 0, 0],
            "an empty bitmap samples transparent"
        );
        assert_eq!(
            empty.aspect(),
            1.0,
            "and reports a sane aspect instead of NaN"
        );
    }

    #[test]
    fn palette_preview_shows_every_role() {
        let p = crate::catalog::Palette {
            bg: Rgb::new(10, 10, 10),
            surface: Rgb::new(40, 40, 40),
            fg: Rgb::new(240, 240, 240),
            dim: Rgb::new(120, 120, 120),
            accent: Rgb::new(255, 0, 128),
            extra: Default::default(),
        };
        let bmp = palette_preview(120, 80, &p);
        assert_eq!((bmp.width, bmp.height), (120, 80));
        // Each role must actually appear, or the preview misrepresents the theme.
        for (name, c) in [
            ("surface", p.surface),
            ("fg", p.fg),
            ("dim", p.dim),
            ("accent", p.accent),
        ] {
            let found = bmp
                .pixels
                .chunks(4)
                .any(|q| q[0] == c.r && q[1] == c.g && q[2] == c.b);
            assert!(found, "{name} missing from the generated preview");
        }
        assert!(
            bmp.pixels.chunks(4).all(|q| q[3] == 255),
            "the preview is opaque"
        );
        // Degenerate sizes are a blank, not a panic.
        assert!(palette_preview(0, 0, &p).pixels.is_empty());
        assert_eq!(palette_preview(1, 1, &p).pixels.len(), 4);
    }

    #[test]
    fn the_memory_budget_is_enforced_by_eviction() {
        let d = tmpdir("budget");
        // Six images of 200x200 RGBA = 160 KB each.
        let mut paths = Vec::new();
        for i in 0..6 {
            let p = d.join(format!("w{i}.png"));
            write_png(&p, 200, 200, [i as u8 * 10, 0, 0]);
            paths.push(p);
        }
        // Room for about three.
        let mut store = ImageStore::new(512).with_budget(500 * 1024);

        for p in &paths {
            store.load(p).expect("each image decodes");
        }
        assert!(
            store.bytes_used() <= 500 * 1024,
            "the budget must actually bound memory, used {}",
            store.bytes_used()
        );
        assert!(
            store.resident() < paths.len(),
            "something must have been evicted, {} still resident",
            store.resident()
        );
        // The most recent is always kept - it is the one being drawn.
        assert!(store.cached(paths.last().unwrap()).is_some());
        fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn an_evicted_image_reloads_into_the_same_id() {
        let d = tmpdir("reload");
        let a = d.join("a.png");
        let b = d.join("b.png");
        write_png(&a, 200, 200, [1, 2, 3]);
        write_png(&b, 200, 200, [4, 5, 6]);
        // Only one fits.
        let mut store = ImageStore::new(512).with_budget(200 * 1024);
        let id_a = store.load(&a).unwrap();
        let id_b = store.load(&b).unwrap();
        assert_ne!(id_a, id_b);
        assert!(
            store.get(id_a).is_none(),
            "a was evicted to make room for b"
        );

        // Coming back gives the SAME id: a scene may still be holding it, and a
        // reused id would silently draw a different picture.
        let again = store.load(&a).unwrap();
        assert_eq!(again, id_a, "an id must be stable across eviction");
        assert!(store.get(id_a).is_some());
        fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn an_id_from_an_evicted_image_draws_nothing_rather_than_the_wrong_thing() {
        let d = tmpdir("dangling");
        let a = d.join("a.png");
        let b = d.join("b.png");
        write_png(&a, 200, 200, [1, 2, 3]);
        write_png(&b, 200, 200, [4, 5, 6]);
        let mut store = ImageStore::new(512).with_budget(200 * 1024);
        let id_a = store.load(&a).unwrap();
        store.load(&b).unwrap();
        // The renderer asks for a gone image and must get nothing, not b.
        assert!(store.get(id_a).is_none());
        fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn the_disk_cache_survives_a_new_store() {
        let d = tmpdir("disk");
        let src = d.join("big.png");
        write_png(&src, 400, 300, [11, 22, 33]);
        let cache = d.join("thumbs");

        let mut first = ImageStore::new(64).with_disk_cache(Some(cache.clone()));
        let id = first.load(&src).unwrap();
        let (w, h) = {
            let b = first.get(id).unwrap();
            (b.width, b.height)
        };
        assert!(cache.is_dir(), "a thumbnail should have been written");
        let thumbs: Vec<_> = fs::read_dir(&cache)
            .unwrap()
            .filter_map(|e| e.ok())
            .collect();
        assert_eq!(thumbs.len(), 1, "one source, one thumbnail");

        // A fresh store - as a second run of the program would be - reads the
        // thumbnail instead of decoding the original.
        let mut second = ImageStore::new(64).with_disk_cache(Some(cache.clone()));
        let id2 = second.load(&src).unwrap();
        let b2 = second.get(id2).unwrap();
        assert_eq!((b2.width, b2.height), (w, h), "same size from the cache");
        assert_eq!(
            &b2.sample(0.5, 0.5)[..3],
            &[11, 22, 33],
            "and the same pixels"
        );

        // Editing the source changes the cache key, so the stale thumbnail is
        // simply never read again.
        std::thread::sleep(std::time::Duration::from_millis(1100));
        write_png(&src, 400, 300, [99, 88, 77]);
        let mut third = ImageStore::new(64).with_disk_cache(Some(cache.clone()));
        let id3 = third.load(&src).unwrap();
        assert_eq!(
            &third.get(id3).unwrap().sample(0.5, 0.5)[..3],
            &[99, 88, 77],
            "a changed source must not serve the old thumbnail"
        );
        fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn the_disk_cache_is_pruned_to_its_budget() {
        let d = tmpdir("prune");
        let cache = d.join("thumbs");
        fs::create_dir_all(&cache).unwrap();
        // Ten files of ~10 KB.
        for i in 0..10 {
            fs::write(cache.join(format!("{i}.png")), vec![0u8; 10 * 1024]).unwrap();
            std::thread::sleep(std::time::Duration::from_millis(5));
        }
        let before: u64 = fs::read_dir(&cache)
            .unwrap()
            .filter_map(|e| e.ok())
            .map(|e| e.metadata().unwrap().len())
            .sum();
        assert!(before >= 100 * 1024);

        prune_cache_dir(&cache, 50 * 1024);
        let after: u64 = fs::read_dir(&cache)
            .unwrap()
            .filter_map(|e| e.ok())
            .map(|e| e.metadata().unwrap().len())
            .sum();
        assert!(
            after <= 50 * 1024,
            "pruning must reach the budget, left {after}"
        );
        assert!(after > 0, "and must not empty the cache entirely");
        // Pruning a directory that does not exist is a no-op, not a panic.
        prune_cache_dir(&d.join("nope"), 1024);
        fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn an_unbounded_budget_keeps_everything() {
        let d = tmpdir("unbounded");
        let mut store = ImageStore::new(512).with_budget(0);
        for i in 0..5 {
            let p = d.join(format!("w{i}.png"));
            write_png(&p, 100, 100, [i as u8, 0, 0]);
            store.load(&p).unwrap();
        }
        assert_eq!(store.resident(), 5, "budget 0 means no eviction");
        fs::remove_dir_all(&d).ok();
    }

    #[test]
    fn generated_images_are_reused_by_key() {
        let mut store = ImageStore::new(64);
        let a = store.insert_generated("swatch:nord", Bitmap::solid(4, 4, [1, 2, 3, 255]));
        let b = store.insert_generated("swatch:nord", Bitmap::solid(4, 4, [9, 9, 9, 255]));
        assert_eq!(a, b);
        assert_eq!(store.len(), 1);
    }
}
