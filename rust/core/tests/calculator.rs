//! Port of tests/calculator_tests.cpp.
use offwork_core::schedule::*;

fn near(a: f64, b: f64) -> bool {
    (a - b).abs() < 0.0001
}
fn day(now: i32, start: i32, end: i32, salary: f64, ls: i32, le: i32, deduct: bool) -> Workday {
    let shift = Shift::simple(start, end, salary).lunch(ls, le);
    let shift = if deduct { shift.deduct() } else { shift };
    shift.at(now).unwrap()
}
fn plain(now: i32, start: i32, end: i32, salary: f64) -> Workday {
    Shift::simple(start, end, salary).at(now).unwrap()
}

#[test]
fn net_duration_end() {
    assert_eq!(end_from_net(540, 480, 720, 780), Ok(1080));
    assert_eq!(end_from_net(535, 480, 720, 780), Ok(1075));
    assert_eq!(end_from_net(540, 480, -1, -1), Ok(1020));
    assert_eq!(end_from_net(1320, 420, 1410, 30), Ok(360));
    assert_eq!(end_from_net(1320, 480, 1410, 30), Ok(420));
    for (net, ls, le) in [
        (0, 720, 780),
        (1380, 720, 780),
        (1439, 720, 780),
        (60, 720, 780),
        (480, -1, 780),
        (480, 720, 720),
    ] {
        assert!(end_from_net(540, net, ls, le).is_err(), "{net} {ls} {le}");
    }
}

#[test]
fn display_never_changes_deadline_earnings_or_progress() {
    for (now, wall, display) in [
        (8 * 3600, 3600, 3600),
        (9 * 3600, 32400, 28800),
        (11 * 3600, 25200, 21600),
        (12 * 3600, 21600, 18000),
        (12 * 3600 + 1800, 19800, 18000),
        (13 * 3600, 18000, 18000),
        (17 * 3600 + 55 * 60, 300, 300),
        (18 * 3600, 0, 0),
    ] {
        let off = day(now, 540, 1080, 800., 720, 780, false);
        let on = day(now, 540, 1080, 800., 720, 780, true);
        assert!(off.remaining == wall && on.remaining == wall);
        assert!(off.countdown == wall && on.countdown == display);
        assert!(near(off.earned, on.earned) && near(off.progress, on.progress));
        assert_eq!(off.on_lunch, on.on_lunch);
    }
    assert_eq!(
        day(10 * 3600, 540, 1080, 800., -1, -1, true).countdown,
        28800
    );
    assert_eq!(
        day(23 * 3600, 1320, 360, 700., 1410, 30, true).countdown,
        21600
    );
    assert_eq!(day(0, 1320, 360, 700., 1410, 30, true).countdown, 19800);
    assert_eq!(day(3600, 1320, 360, 700., 1410, 30, true).countdown, 18000);
}

#[test]
fn durations() {
    assert_eq!(end_from_duration(540, 480), Ok(1020));
    assert_eq!(end_from_duration(540, 540), Ok(1080));
    assert_eq!(end_from_duration(1320, 480), Ok(360));
    assert_eq!(work_duration(540, 1020), Ok(480));
    assert_eq!(work_duration(1320, 360), Ok(480));
    assert_eq!(duration_from_hours(8.0), Ok(480));
    assert_eq!(duration_from_hours(8.5), Ok(510));
    assert_eq!(duration_from_hours(9.0), Ok(540));
    assert!(end_from_duration(540, 1440).is_err());
    assert!(duration_from_hours(24.0).is_err());
}

#[test]
fn phases_and_earnings() {
    let working = plain(13 * 3600 + 1800, 540, 1080, 900.);
    assert_eq!(working.phase, Phase::Working);
    assert_eq!(working.remaining, 4 * 3600 + 1800);
    assert!(near(working.earned, 450.) && near(working.progress, 0.5));
    let before = plain(8 * 3600, 540, 1080, 600.);
    assert!(before.phase == Phase::Before && before.remaining == 3600 && near(before.earned, 0.));
    let after = plain(20 * 3600, 540, 1080, 800.);
    assert!(after.phase == Phase::After && after.remaining == 0 && near(after.earned, 800.));
    let overnight = plain(3600, 1320, 360, 800.);
    assert!(overnight.phase == Phase::Working && overnight.remaining == 5 * 3600);
    assert!(near(overnight.earned, 300.));
    assert_eq!(
        Shift::simple(540, 540, 500.).at(12 * 3600),
        Err(ScheduleError::WorkHours)
    );
    assert_eq!(
        Shift::simple(540, 1080, -1.).at(12 * 3600),
        Err(ScheduleError::Salary)
    );
    assert_eq!(
        Shift::simple(540, 1080, 1.).at(86400),
        Err(ScheduleError::Now)
    );
}

#[test]
fn lunch_pauses_progress_and_earnings() {
    let at = |now| day(now, 540, 1080, 750., 720, 810, false);
    assert!(at(12 * 3600).on_lunch && near(at(12 * 3600).earned, 300.));
    let inside = at(13 * 3600);
    assert!(
        near(inside.earned, 300.) && near(inside.progress, 0.4) && inside.remaining == 5 * 3600
    );
    assert!(!at(13 * 3600 + 1800).on_lunch && near(at(13 * 3600 + 1800).earned, 300.));
    assert!(near(at(14 * 3600).earned, 350.));
    assert!(near(at(18 * 3600).earned, 750.));
    for now in 12 * 3600..13 * 3600 + 1800 {
        let frozen = at(now);
        assert!(frozen.phase == Phase::Working && frozen.on_lunch);
        assert!(near(frozen.earned, 300.) && near(frozen.progress, 0.4));
        assert_eq!(frozen.remaining, 18 * 3600 - now);
    }
    assert!(near(at(12 * 3600 - 1).earned, 300. - 100. / 3600.));
    assert!(near(at(13 * 3600 + 1801).earned, 300. + 100. / 3600.));
    assert!(!at(12 * 3600 - 1).on_lunch);
    assert!(near(at(8 * 3600).earned, 0.) && at(8 * 3600).remaining == 3600);
    assert!(!at(18 * 3600).on_lunch);
    // A restart in the lunch window derives the same frozen value, even at midnight.
    let night = |now| day(now, 1320, 360, 700., 1410, 30, false);
    assert!(night(0).on_lunch && near(night(0).earned, 150.) && night(0).remaining == 6 * 3600);
    assert!(near(night(1800).earned, 150.) && !night(1800).on_lunch);
    assert!(near(night(3600).earned, 200.));
    assert!(near(night(6 * 3600).earned, 700.));
    // Lunch may touch either boundary but cannot consume the whole shift.
    assert!(day(9 * 3600, 540, 1080, 800., 540, 600, false).on_lunch);
    assert!(day(17 * 3600, 540, 1080, 800., 1020, 1080, false).on_lunch);
    assert!(near(
        day(17 * 3600, 540, 1080, 800., 1020, 1080, false).earned,
        800.
    ));
    for (ls, le) in [
        (720, 720),
        (780, 720),
        (500, 600),
        (1020, 1081),
        (540, 1080),
        (-1, 780),
        (720, 1440),
    ] {
        let shift = Shift::simple(540, 1080, 750.).lunch(ls, le);
        assert!(shift.at(12 * 3600).is_err(), "{ls}-{le}");
    }
}
