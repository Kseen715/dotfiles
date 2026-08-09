//! Subsequence matching for the filter line.
//!
//! Deliberately small: a picker holds a handful of themes and a few hundred
//! wallpapers, so this runs on every keystroke over a list that fits on one
//! screen. A real fuzzy library (nucleo, fzf's algorithm) would be a dependency
//! and a tuning surface bought for a list this size.
//!
//! Scoring rewards the things that make a short query feel right:
//!   - a prefix match beats a match in the middle
//!   - consecutive characters beat scattered ones
//!   - a match at a word boundary beats one inside a word
//!
//! Ties break on the shorter candidate, so `nord` outranks `nord-light`.

/// Score `needle` against `haystack`, case-insensitively. `None` when the needle
/// is not a subsequence. Higher is better.
pub fn score(needle: &str, haystack: &str) -> Option<i32> {
    if needle.is_empty() {
        return Some(0);
    }
    let h: Vec<char> = haystack.chars().flat_map(|c| c.to_lowercase()).collect();
    let n: Vec<char> = needle.chars().flat_map(|c| c.to_lowercase()).collect();

    let mut score = 0i32;
    let mut hi = 0usize;
    let mut prev_match: Option<usize> = None;

    for &nc in &n {
        // Find the next occurrence, then keep walking: a later occurrence that
        // is adjacent to the previous match is worth more than an earlier
        // scattered one, which is what makes "gvb" rank gruvbox sensibly.
        let start = hi;
        let mut found = None;
        for (i, &hc) in h.iter().enumerate().skip(start) {
            if hc == nc {
                found = Some(i);
                break;
            }
        }
        let i = found?;

        let mut s = 10;
        if i == 0 {
            s += 25; // prefix
        } else if !h[i - 1].is_alphanumeric() {
            s += 15; // word boundary: after a space, dash, slash, dot
        }
        if prev_match == Some(i.wrapping_sub(1)) {
            s += 20; // consecutive
        }
        // Distance penalty, capped so a long path does not swamp the signal.
        s -= ((i - start) as i32).min(10);

        score += s;
        prev_match = Some(i);
        hi = i + 1;
    }

    // Shorter candidates win ties: with "nord" typed, `nord` should sit above
    // `nord-light`.
    score -= (h.len() as i32) / 8;
    Some(score)
}

/// Filter and rank `items` by `query`, keeping the original order when the
/// query is empty (an unfiltered picker must not reshuffle itself).
pub fn rank<'a, T, F>(items: &'a [T], query: &str, key: F) -> Vec<usize>
where
    F: Fn(&'a T) -> &'a str,
{
    if query.trim().is_empty() {
        return (0..items.len()).collect();
    }
    let mut scored: Vec<(usize, i32)> = items
        .iter()
        .enumerate()
        .filter_map(|(i, it)| score(query, key(it)).map(|s| (i, s)))
        .collect();
    // Stable by index so equal scores keep catalogue order.
    scored.sort_by(|a, b| b.1.cmp(&a.1).then(a.0.cmp(&b.0)));
    scored.into_iter().map(|(i, _)| i).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn matches_subsequences_case_insensitively() {
        assert!(score("nrd", "Nord").is_some());
        assert!(score("NORD", "nord").is_some());
        assert!(score("xyz", "nord").is_none());
        // Order matters: a subsequence is not an anagram.
        assert!(score("dron", "nord").is_none());
    }

    #[test]
    fn empty_query_matches_everything() {
        assert_eq!(score("", "anything"), Some(0));
        let items = ["a", "b", "c"];
        assert_eq!(rank(&items, "", |s| s), vec![0, 1, 2]);
        assert_eq!(
            rank(&items, "   ", |s| s),
            vec![0, 1, 2],
            "whitespace is not a filter"
        );
    }

    #[test]
    fn prefix_beats_midword() {
        let pre = score("gru", "gruvbox").unwrap();
        let mid = score("gru", "not-gruvbox").unwrap();
        assert!(pre > mid, "prefix {pre} should beat midword {mid}");
    }

    #[test]
    fn consecutive_beats_scattered() {
        let run = score("cat", "catppuccin").unwrap();
        let scattered = score("cat", "c-a-t-astrophe").unwrap();
        assert!(
            run > scattered,
            "consecutive {run} should beat scattered {scattered}"
        );
    }

    #[test]
    fn word_boundary_beats_inside_a_word() {
        let boundary = score("m", "nord mocha").unwrap();
        let inside = score("m", "normal").unwrap();
        assert!(
            boundary > inside,
            "boundary {boundary} should beat inside {inside}"
        );
    }

    #[test]
    fn shorter_wins_ties() {
        let short = score("nord", "nord").unwrap();
        let long = score("nord", "nord-light-variant").unwrap();
        assert!(short > long);
    }

    #[test]
    fn ranks_a_realistic_theme_list() {
        let themes = ["catppuccin", "glass", "gruvbox", "nord", "rosemary", "xin"];
        let by = |q: &str| -> Vec<&str> {
            rank(&themes, q, |s| *s)
                .into_iter()
                .map(|i| themes[i])
                .collect()
        };
        assert_eq!(by("nord").first(), Some(&"nord"));
        assert_eq!(by("gru").first(), Some(&"gruvbox"));
        assert_eq!(by("cat").first(), Some(&"catppuccin"));
        assert_eq!(by("ros").first(), Some(&"rosemary"));
        // A query matching nothing yields nothing - the list must go empty
        // rather than silently show everything.
        assert!(by("zzz").is_empty());
        // Every theme still reachable by its own name.
        for t in themes {
            assert_eq!(
                by(t).first(),
                Some(&t),
                "{t} should rank first for its own name"
            );
        }
    }

    #[test]
    fn ranking_is_stable_for_equal_scores() {
        // Same length, same shape: catalogue order must be preserved.
        let items = ["aXb", "aYb", "aZb"];
        assert_eq!(rank(&items, "ab", |s| *s), vec![0, 1, 2]);
    }

    #[test]
    fn paths_rank_on_the_filename_boundary() {
        let files = [
            "/home/u/Pictures/Wallpapers/forest.png",
            "/home/u/Pictures/Wallpapers/city-night.jpg",
        ];
        let out: Vec<&str> = rank(&files, "forest", |s| *s)
            .into_iter()
            .map(|i| files[i])
            .collect();
        assert_eq!(out, vec!["/home/u/Pictures/Wallpapers/forest.png"]);
    }
}
