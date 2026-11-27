//! Port of `src/reminder.cpp`. Pure: no clock and no I/O. The shell feeds one
//! observation per tick (also after sleep/wake) and notifies when told to.
//! Every operation returns a new state.

use crate::schedule::{Phase, SECONDS_PER_DAY};

/// A transition first observed later than this after the end time is stale
/// (asleep, locked, clock jump) and stays silent. Inclusive.
pub const GRACE_SECONDS: i32 = 10 * 60;
pub const FIVE_MINUTES: i32 = 5 * 60;

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct ReminderState {
    pub initialized: bool,
    pub last_phase: Option<Phase>,
    pub last_remaining: Option<i32>,
    pub last_since_end: Option<i32>,
    pub five_minutes_handled: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Decision {
    pub next: ReminderState,
    pub notify_end: bool,
    pub notify_five_minutes: bool,
}

/// Seconds since the most recent end-of-shift moment, in `[0, 86400)`.
pub fn seconds_since_shift_end(now_seconds: i32, end_minutes: i32) -> i32 {
    (now_seconds - end_minutes * 60).rem_euclid(SECONDS_PER_DAY)
}

impl ReminderState {
    /// Baseline without notifying: at startup and whenever the schedule changes.
    pub fn reset(phase: Phase, remaining: Option<i32>) -> Self {
        Self {
            initialized: true,
            last_phase: Some(phase),
            last_remaining: remaining,
            last_since_end: None,
            five_minutes_handled: remaining.is_some_and(|r| (0..=FIVE_MINUTES).contains(&r)),
        }
    }

    /// One tick. The end reminder needs a fresh Working → After transition;
    /// the five-minute reminder needs the REAL remaining time to cross 300 s
    /// while working (lunch included). Startup, reset and stale ticks stay silent.
    pub fn tick(&self, phase: Phase, since_end: i32, remaining: Option<i32>) -> Decision {
        let mut next = Self::reset(phase, remaining);
        next.last_since_end = Some(since_end);
        let mut decision = Decision {
            next,
            notify_end: false,
            notify_five_minutes: false,
        };
        if !self.initialized {
            return decision;
        }
        let was_working = self.last_phase == Some(Phase::Working);
        if was_working && phase == Phase::Working {
            decision.next.five_minutes_handled = self.five_minutes_handled;
            if remaining.is_some_and(|r| (0..=FIVE_MINUTES).contains(&r)) {
                let gap = self
                    .last_since_end
                    .map_or(0, |last| (since_end - last).rem_euclid(SECONDS_PER_DAY));
                decision.notify_five_minutes = !self.five_minutes_handled
                    && self.last_remaining.is_some_and(|r| r > FIVE_MINUTES)
                    && gap <= GRACE_SECONDS;
                // Consume even a stale crossing so later ticks cannot catch up.
                decision.next.five_minutes_handled = true;
            }
        }
        // Rearming is implicit: any Working tick makes the next After a
        // transition again. Overnight shifts never report Before.
        decision.notify_end = was_working && phase == Phase::After && since_end <= GRACE_SECONDS;
        decision
    }
}
