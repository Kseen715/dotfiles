// firefox/user.js — dotfiles-owned Firefox prefs (os-rice §5), installed into
// every profile by modules/firefox.sh. Overwritten on update; the rice's colors
// are a separate file (chrome/userChrome.css).
//
// user.js, not prefs.js: Firefox rewrites prefs.js on exit, so edits there are
// lost. user.js is applied on top at every start.
//
// ─── Low-RAM tuning ──────────────────────────────────────────────────────────
// Two knobs dominate Firefox's memory on a small machine: how many content
// processes it forks, and how many fully-live back/forward page states it keeps.
// Everything else here is small change by comparison.

// Content processes. Default is 8; each one is a fixed ~80-150 MB floor even
// when idle. 2 keeps tab isolation while cutting the floor hard. Raise to 4 if
// a single heavy tab starts stalling the others.
user_pref("dom.ipc.processCount", 2);
user_pref("dom.ipc.processCount.webIsolated", 1);

// Fully-alive back/forward cache entries. Default scales with RAM (up to 8);
// each is a complete live DOM + JS heap.
user_pref("browser.sessionhistory.max_total_viewers", 2);

// Unload background tabs when the OS reports memory pressure instead of letting
// the OOM killer pick a victim. This is the single best "many tabs, little RAM"
// pref Firefox has.
user_pref("browser.tabs.unloadOnLowMemory", true);
user_pref("browser.low_commit_space_threshold_mb", 512);

// Memory cache: let Firefox size it, but cap it (-1 = automatic, which on a
// 4 GB box still reaches ~128 MB).
user_pref("browser.cache.memory.enable", true);
user_pref("browser.cache.memory.capacity", 65536);          // 64 MB, in KiB
user_pref("browser.cache.memory.max_entry_size", 5120);

// Disk cache instead of RAM for images/media.
user_pref("browser.cache.disk.enable", true);

// Session store writes the whole session to disk on a timer; 60s instead of 15s
// is fewer wakeups and less garbage, at the cost of losing up to a minute of
// tab state after a crash.
user_pref("browser.sessionstore.interval", 60000);

// Accessibility service allocates a parallel tree of every DOM node. Disable it
// unless you actually use a screen reader.
user_pref("accessibility.force_disabled", 1);

// Prefetching/speculative connections trade RAM and bandwidth for latency.
user_pref("network.prefetch-next", false);
user_pref("network.dns.disablePrefetch", true);
user_pref("network.predictor.enabled", false);
user_pref("browser.urlbar.speculativeConnect.enabled", false);

// Garbage collection: run the incremental GC a bit more eagerly so the heap
// gives memory back instead of growing to the high-water mark and staying there.
user_pref("javascript.options.mem.gc_incremental_slice_ms", 20);
user_pref("javascript.options.mem.high_water_mark", 32);

// ─── X11/i3 comfort ──────────────────────────────────────────────────────────

// Use the XDG portal file picker, so the dialog matches the desktop and can see
// gvfs mounts (modules/xdg.sh pins the gtk backend).
user_pref("widget.use-xdg-desktop-portal.file-picker", 1);
user_pref("widget.use-xdg-desktop-portal.mime-handler", 1);

// Smooth/pixel scrolling under X11 (pairs with MOZ_USE_XINPUT2 in the xprofile).
user_pref("mousewheel.default.delta_multiplier_y", 100);

// Follow the desktop's dark preference and drop the title bar — i3 draws the
// border, a second one is wasted rows.
user_pref("browser.theme.content-theme", 0);
user_pref("browser.theme.toolbar-theme", 0);
user_pref("browser.tabs.inTitlebar", 0);

// Required for chrome/userChrome.css (the rice's colors) to be read at all.
user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);
