//! Port of tests/reminder_tests.cpp, plus sleep/wake and night-shift cases.
use offwork_core::reminder::*;
use offwork_core::schedule::{end_from_duration, Phase, Shift};

const DAY: i32 = 86400;
fn at(h: i32, m: i32, s: i32) -> i32 {
    h * 3600 + m * 60 + s
}
fn phase(now: i32, start: i32, end: i32) -> Phase {
    Shift::simple(start, end, 500.).at(now).unwrap().phase
}
fn count(state: &mut ReminderState, from: i32, to: i32, start: i32, end: i32) -> i32 {
    let mut n = 0;
    for t in from..to {
        let now = t % DAY;
        let d = state.tick(
            phase(now, start, end),
            seconds_since_shift_end(now, end),
            None,
        );
        *state = d.next;
        n += i32::from(d.notify_end);
    }
    n
}
fn reset(p: Phase) -> ReminderState {
    ReminderState::reset(p, None)
}

#[test]
fn seconds_since_shift_end_wraps_around_midnight() {
    assert_eq!(seconds_since_shift_end(at(18, 0, 0), 1080), 0);
    assert_eq!(seconds_since_shift_end(at(18, 5, 0), 1080), 300);
    assert_eq!(seconds_since_shift_end(at(17, 59, 0), 1080), DAY - 60);
    assert_eq!(seconds_since_shift_end(at(6, 0, 30), 360), 30);
    assert_eq!(seconds_since_shift_end(at(0, 10, 0), 1435), 900);
}

#[test]
fn transitions() {
    let d = reset(Phase::Working).tick(Phase::Working, DAY - 1, None);
    assert!(!d.notify_end);
    let d = d.next.tick(Phase::After, 0, None);
    assert!(d.notify_end);
    let d = d.next.tick(Phase::After, 1, None);
    assert!(!d.notify_end);
    assert!(!d.next.tick(Phase::After, 2, None).notify_end);

    let mut d = reset(Phase::Working).tick(Phase::After, 0, None);
    assert!(d.notify_end);
    for s in 1..4 * 3600 {
        d = d.next.tick(Phase::After, s, None);
        assert!(!d.notify_end);
    }
    // Startup after work is silent, even right at the end moment.
    assert!(!reset(Phase::After).tick(Phase::After, 5, None).notify_end);
    assert!(!reset(Phase::After).tick(Phase::After, 0, None).notify_end);
    // An uninitialised state only establishes a baseline.
    let d = ReminderState::default().tick(Phase::After, 0, None);
    assert!(!d.notify_end && d.next.initialized && d.next.last_phase == Some(Phase::After));
    // Saving a schedule that moves the end before "now" is silent.
    assert!(!reset(Phase::After).tick(Phase::After, 0, None).notify_end);
    assert!(reset(Phase::Working).tick(Phase::After, 1, None).notify_end);
}

#[test]
fn rearming() {
    let d = reset(Phase::Working).tick(Phase::After, 0, None);
    assert!(d.notify_end);
    let d = d.next.tick(Phase::Before, 0, None);
    let d = d.next.tick(Phase::Working, 0, None);
    assert!(!d.notify_end);
    assert!(d.next.tick(Phase::After, 0, None).notify_end);
    // Overnight shifts never see Before.
    let d = reset(Phase::Working).tick(Phase::After, 0, None);
    let d = d.next.tick(Phase::Working, 0, None);
    assert!(d.next.tick(Phase::After, 0, None).notify_end);
}

#[test]
fn grace_window_and_sleep() {
    let s = reset(Phase::Working);
    assert!(s.tick(Phase::After, GRACE_SECONDS, None).notify_end);
    assert!(!s.tick(Phase::After, GRACE_SECONDS + 1, None).notify_end);
    // Asleep from 17:50 until 19:40.
    let d = s.tick(Phase::After, 100 * 60, None);
    assert!(!d.notify_end && !d.next.tick(Phase::After, 6001, None).notify_end);
    // Sleeping through the whole shift.
    assert!(!reset(Phase::Before).tick(Phase::After, 0, None).notify_end);
}

#[test]
fn simulated_days() {
    let mut s = reset(phase(at(8, 0, 0), 540, 1080));
    assert_eq!(count(&mut s, at(8, 0, 0), at(17, 59, 59), 540, 1080), 0);
    assert_eq!(count(&mut s, at(17, 59, 59), at(18, 0, 1), 540, 1080), 1);
    assert_eq!(count(&mut s, at(18, 0, 1), at(23, 59, 59), 540, 1080), 0);
    let mut s = reset(phase(at(7, 0, 0), 540, 1080));
    assert_eq!(
        count(&mut s, at(7, 0, 0), at(7, 0, 0) + 2 * DAY, 540, 1080),
        2
    );
    let mut s = reset(phase(at(21, 0, 0), 1320, 360));
    assert_eq!(
        count(&mut s, at(21, 0, 0), at(21, 0, 0) + DAY, 1320, 360),
        1
    );
}

#[test]
fn five_minute_transitions() {
    let s = ReminderState::reset(Phase::Working, Some(301));
    let d = s.tick(Phase::Working, DAY - 300, Some(300));
    assert!(d.notify_five_minutes);
    let d = d.next.tick(Phase::Working, DAY - 299, Some(299));
    assert!(!d.notify_five_minutes);
    // A clock rollback must not repeat within the same shift.
    let d = d.next.tick(Phase::Working, DAY - 400, Some(400));
    let d = d.next.tick(Phase::Working, DAY - 300, Some(300));
    assert!(!d.notify_five_minutes);
    let d = d.next.tick(Phase::After, 0, Some(0));
    assert!(d.notify_end && !d.notify_five_minutes);
    let d = d.next.tick(Phase::Working, DAY - 301, Some(301));
    assert!(
        d.next
            .tick(Phase::Working, DAY - 300, Some(300))
            .notify_five_minutes
    );
}

#[test]
fn five_minute_startup_reset_and_stale_are_silent() {
    for r in [300, 299, 1, 0] {
        let s = ReminderState::reset(Phase::Working, Some(r));
        assert!(!s.tick(Phase::Working, DAY - r, Some(r)).notify_five_minutes);
    }
    assert!(
        !ReminderState::default()
            .tick(Phase::Working, DAY - 299, Some(299))
            .notify_five_minutes
    );
    let d = ReminderState::reset(Phase::Working, Some(1200)).tick(
        Phase::Working,
        DAY - 1200,
        Some(1200),
    );
    let d = d.next.tick(Phase::Working, DAY - 60, Some(60));
    assert!(!d.notify_five_minutes, "slept for 19 minutes");
    assert!(
        !d.next
            .tick(Phase::Working, DAY - 59, Some(59))
            .notify_five_minutes
    );
}

#[test]
fn five_minute_reminder_uses_actual_end_during_lunch() {
    for start in [540, 1320] {
        let end = end_from_duration(start, 480).unwrap();
        for lunch_offset in [180, 470] {
            let ls = (start + lunch_offset) % 1440;
            let le = (start + if lunch_offset == 180 { 270 } else { 480 }) % 1440;
            let shift = Shift::simple(start, end, 800.).lunch(ls, le);
            let w = shift.at(start * 60).unwrap();
            let mut state = ReminderState::reset(w.phase, Some(w.remaining));
            let (mut five, mut ends) = (0, 0);
            for elapsed in 1..=480 * 60 + 1 {
                let now = (start * 60 + elapsed) % DAY;
                let w = shift.at(now).unwrap();
                let d = state.tick(
                    w.phase,
                    seconds_since_shift_end(now, end),
                    Some(w.remaining),
                );
                assert_eq!(d.notify_five_minutes, elapsed == 475 * 60);
                assert_eq!(d.notify_end, elapsed == 480 * 60);
                if elapsed == 475 * 60 && lunch_offset == 470 {
                    assert!(w.on_lunch);
                }
                five += i32::from(d.notify_five_minutes);
                ends += i32::from(d.notify_end);
                state = d.next;
            }
            assert_eq!((five, ends), (1, 1));
        }
    }
}

#[test]
fn wake_after_sleep_inside_grace_still_reminds_once() {
    // 17:52 sleep, 18:04 wake: Working → After observed 4 minutes late.
    let shift = Shift::simple(540, 1080, 500.);
    let w = shift.at(at(17, 52, 0)).unwrap();
    let s = ReminderState::reset(w.phase, Some(w.remaining));
    let wake = at(18, 4, 0);
    let w = shift.at(wake).unwrap();
    let d = s.tick(
        w.phase,
        seconds_since_shift_end(wake, 1080),
        Some(w.remaining),
    );
    assert!(d.notify_end && !d.notify_five_minutes);
}
