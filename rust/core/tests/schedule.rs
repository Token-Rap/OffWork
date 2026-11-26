use offwork_core::{parse_date, Phase, Settings};
fn snap(s: &Settings, now: i32) -> offwork_core::Snapshot {
    s.snapshot(now, parse_date("2026-11-26").unwrap()).unwrap()
}
#[test]
fn wall_and_paid_countdowns() {
    let mut s = Settings {
        net: 480,
        lunch_end: 780,
        salary: 800.,
        ..Settings::default()
    };
    for (now, wall, display) in [
        (28800, 3600, 3600),
        (32400, 32400, 28800),
        (39600, 25200, 21600),
        (43200, 21600, 18000),
        (45000, 19800, 18000),
        (46800, 18000, 18000),
        (64500, 300, 300),
        (64800, 0, 0),
    ] {
        s.deduct = false;
        let ordinary = snap(&s, now);
        s.deduct = true;
        let paid = snap(&s, now);
        assert_eq!(
            (ordinary.countdown, paid.remaining, paid.countdown),
            (wall, wall, display)
        );
        assert_eq!(ordinary.earned, paid.earned);
        assert_eq!(ordinary.progress, paid.progress);
    }
}
#[test]
fn lunch_freezes_earnings_every_second() {
    let s = Settings {
        salary: 750.,
        ..Settings::default()
    };
    for now in 43200..48600 {
        let v = snap(&s, now);
        assert_eq!(v.phase, Phase::Working);
        assert!(v.on_lunch);
        assert_eq!(v.earned, 300.);
        assert_eq!(v.progress, 0.4);
        assert_eq!(v.remaining, 64800 - now);
    }
    assert!(!snap(&s, 48600).on_lunch);
    assert_eq!(snap(&s, 50400).earned, 350.);
    assert_eq!(snap(&s, 64800).earned, 750.);
}
#[test]
fn overnight_and_invalid_lunch() {
    let s = Settings {
        start: 1320,
        net: 420,
        salary: 700.,
        lunch_start: 1410,
        lunch_end: 30,
        deduct: true,
        ..Settings::default()
    };
    let v = snap(&s, 0);
    assert_eq!(
        (v.end, v.remaining, v.countdown, v.earned),
        (360, 21600, 19800, 150.)
    );
    assert!(v.on_lunch);
    assert_eq!(snap(&s, 3600).earned, 200.);
    assert!(!snap(&s, 1800).on_lunch);
    for (a, b) in [
        (720, 720),
        (780, 720),
        (500, 600),
        (1020, 1081),
        (540, 1080),
        (-1, 780),
        (720, 1440),
    ] {
        let bad = Settings {
            duration_mode: false,
            lunch_start: a,
            lunch_end: b,
            ..Settings::default()
        };
        assert!(bad
            .snapshot(43200, parse_date("2026-11-26").unwrap())
            .is_err())
    }
}
