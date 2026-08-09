//! Animation: values that move toward a target over time.
//!
//! Scrolling a list by whole rows is what the model does; what the eye wants is
//! the list *sliding* there. This is the piece in between — a value the UI reads
//! every frame that lags behind the model's discrete state.
//!
//! Exponential smoothing rather than a duration-based tween, for one reason
//! that matters in a picker: it is **frame-rate independent and interruptible**.
//! Holding the Down key retargets the animation dozens of times a second, and a
//! tween would restart on every press (visibly stuttering) or queue up. Easing
//! toward a moving target just tracks it.

use std::time::Duration;

/// A value that eases toward a target.
#[derive(Debug, Clone, Copy)]
pub struct Eased {
    current: f32,
    target: f32,
    /// Time constant in seconds: the value covers ~63% of the remaining
    /// distance every `tau`. Smaller is snappier.
    tau: f32,
    /// Distance below which the value simply snaps, so animation actually ends
    /// instead of approaching asymptotically and repainting for ever.
    ///
    /// These values are in PIXELS, so the default is a third of one: below that
    /// nothing on screen changes, and continuing to animate would burn frames
    /// on motion nobody can see. Getting this wrong is why the first version
    /// took half a second to "settle" a scroll the eye saw finish in 150ms.
    epsilon: f32,
}

impl Eased {
    /// A value in pixels, easing with time constant `tau` seconds.
    pub fn new(value: f32, tau: f32) -> Self {
        Self {
            current: value,
            target: value,
            tau: tau.max(0.001),
            epsilon: 0.3,
        }
    }

    /// Override the settle threshold, for a value that is not in pixels.
    pub fn with_epsilon(mut self, epsilon: f32) -> Self {
        self.epsilon = epsilon.max(1e-6);
        self
    }

    /// Instant, no animation — for the first frame and for a mode switch, where
    /// sliding from the old list's position would be meaningless motion.
    pub fn set_immediate(&mut self, value: f32) {
        self.current = value;
        self.target = value;
    }

    pub fn set_target(&mut self, value: f32) {
        self.target = value;
    }

    pub fn target(&self) -> f32 {
        self.target
    }

    pub fn value(&self) -> f32 {
        self.current
    }

    /// True while the value is still moving.
    pub fn animating(&self) -> bool {
        (self.target - self.current).abs() > self.epsilon
    }

    /// Advance by `dt`. Returns true while still animating.
    ///
    /// `1 - exp(-dt/tau)` is the frame-rate independent form: two 8ms steps land
    /// in the same place as one 16ms step, so a dropped frame changes the timing
    /// and not the path.
    pub fn tick(&mut self, dt: Duration) -> bool {
        if !self.animating() {
            self.current = self.target;
            return false;
        }
        let dt = dt.as_secs_f32().clamp(0.0, 0.1); // a long stall must not teleport
        let alpha = 1.0 - (-dt / self.tau).exp();
        self.current += (self.target - self.current) * alpha;
        if !self.animating() {
            self.current = self.target;
            return false;
        }
        true
    }
}

/// How long a frame should wait when something is animating: ~60fps.
pub const FRAME: Duration = Duration::from_millis(16);

/// Time constant for list scrolling and the selection bar.
///
/// 35ms puts a one-row move at about 190ms end to end. Slower reads as lag when
/// a key is held; much faster and the motion stops being legible, which defeats
/// the point of animating at all.
pub const SCROLL_TAU: f32 = 0.035;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_value_starts_settled() {
        let e = Eased::new(5.0, 0.06);
        assert_eq!(e.value(), 5.0);
        assert!(!e.animating(), "nothing to animate toward yet");
    }

    #[test]
    fn it_moves_toward_the_target_and_stops() {
        // One row's worth of scroll, in pixels, at the default time constant.
        let mut e = Eased::new(0.0, SCROLL_TAU);
        e.set_target(60.0);
        assert!(e.animating());

        let mut ticks = 0;
        while e.tick(FRAME) {
            ticks += 1;
            assert!(ticks < 1000, "animation never settled");
        }
        assert_eq!(e.value(), 60.0, "it lands exactly on the target");
        assert!(!e.animating());
        // Under a fifth of a second: long enough to read as motion, short
        // enough that holding an arrow key never feels like it lags.
        // ln(60px / 0.3px) * tau ~= 0.19s ~= 12 frames at 60Hz: long enough to
        // read as motion, short enough that holding an arrow key never lags.
        assert!(
            (8..=16).contains(&ticks),
            "a one-row scroll should settle in ~12 frames, took {ticks}"
        );
    }

    #[test]
    fn it_is_monotonic_and_never_overshoots() {
        let mut e = Eased::new(0.0, 0.06).with_epsilon(0.001);
        e.set_target(1.0);
        let mut last = e.value();
        while e.tick(FRAME) {
            let v = e.value();
            assert!(v >= last, "value went backwards: {v} < {last}");
            assert!(v <= 1.0 + 1e-6, "overshot the target: {v}");
            last = v;
        }
    }

    #[test]
    fn the_path_does_not_depend_on_the_frame_rate() {
        // The same elapsed time in different step sizes must reach the same
        // place, or a dropped frame would change where the list is.
        let mut coarse = Eased::new(0.0, 0.06);
        let mut fine = Eased::new(0.0, 0.06);
        coarse.set_target(100.0);
        fine.set_target(100.0);
        for _ in 0..10 {
            coarse.tick(Duration::from_millis(16));
        }
        for _ in 0..20 {
            fine.tick(Duration::from_millis(8));
        }
        assert!(
            (coarse.value() - fine.value()).abs() < 0.5,
            "16ms steps gave {} but 8ms steps gave {}",
            coarse.value(),
            fine.value()
        );
    }

    #[test]
    fn retargeting_mid_flight_tracks_the_new_target() {
        // This is what holding an arrow key does: the target moves every few
        // frames and the value must follow rather than restart.
        let mut e = Eased::new(0.0, 0.06).with_epsilon(0.001);
        e.set_target(5.0);
        for _ in 0..3 {
            e.tick(FRAME);
        }
        let mid = e.value();
        assert!(mid > 0.0 && mid < 5.0, "should be in flight, at {mid}");

        e.set_target(10.0);
        assert!(e.animating());
        while e.tick(FRAME) {}
        assert_eq!(e.value(), 10.0);
    }

    #[test]
    fn a_long_stall_does_not_teleport() {
        // If the process is suspended for a second, the next frame must not
        // jump the whole distance - that reads as a glitch, not as motion.
        let mut e = Eased::new(0.0, 0.06);
        e.set_target(100.0);
        e.tick(Duration::from_secs(5));
        assert!(
            e.value() < 100.0,
            "a huge dt should still be a step, not a jump"
        );
        assert!(e.animating());
    }

    #[test]
    fn set_immediate_skips_the_animation() {
        let mut e = Eased::new(0.0, 0.06);
        e.set_target(10.0);
        e.tick(FRAME);
        e.set_immediate(3.0);
        assert_eq!(e.value(), 3.0);
        assert!(!e.animating(), "an immediate set leaves nothing to animate");
    }

    #[test]
    fn ticking_a_settled_value_reports_no_work() {
        let mut e = Eased::new(2.0, 0.06);
        assert!(
            !e.tick(FRAME),
            "a settled value must not ask for more frames"
        );
    }
}
