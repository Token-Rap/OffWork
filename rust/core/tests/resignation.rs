//! Port of tests/resignation_tests.cpp.
mod common;
use common::Ini;
use offwork_core::resignation::{parse_date, remaining_workdays};

#[test]
fn strict_dates() {
    for text in [
        "",
        "2026-2-01",
        "2026-02-29",
        "1900-02-29",
        "0000-01-01",
        "10000-01-01",
        "2026-13-01",
        "2026-04-31",
        "2026-01-00",
        "2026-11-30junk",
        " 2026-11-30",
        "2026/11/30",
        "２０２６-11-30",
    ] {
        assert!(parse_date(text).is_none(), "{text}");
    }
    for text in ["2000-02-29", "0001-01-01", "9999-12-31"] {
        assert!(parse_date(text).is_some(), "{text}");
    }
}

#[test]
fn workdays() {
    let count =
        |today: &str, target: &str| remaining_workdays(parse_date(today).unwrap(), target, true);
    assert_eq!(count("2026-11-26", "2026-11-30"), Some(2)); // Thu -> Fri, Mon.
    assert_eq!(count("2026-11-27", "2026-11-30"), Some(1));
    assert_eq!(count("2026-11-28", "2026-11-30"), Some(1));
    assert_eq!(count("2026-11-29", "2026-11-30"), Some(1));
    assert_eq!(count("2026-11-30", "2026-11-30"), Some(0));
    assert_eq!(count("2026-12-01", "2026-11-30"), None);
    assert_eq!(count("2026-11-27", "2026-11-29"), Some(0)); // Weekend target.
    assert_eq!(count("2026-11-29", "2026-11-29"), Some(0));
    assert_eq!(count("2026-11-23", "2026-11-30"), Some(5));
    assert_eq!(count("2026-12-31", "2027-01-01"), Some(1)); // No holiday dataset.
    assert_eq!(count("2024-02-28", "2024-03-01"), Some(2));
    assert_eq!(count("2026-03-06", "2026-03-09"), Some(1)); // Civil dates, no DST hours.
    assert_eq!(
        remaining_workdays(parse_date("2026-11-26").unwrap(), "2026-11-30", false),
        None
    );
    assert_eq!(count("2026-11-26", ""), None);
    assert_eq!(count("2026-11-26", "2026-02-29"), None);
    assert_eq!(count("9999-12-31", "9999-12-31"), Some(0));
    assert!(count("0001-01-01", "9999-12-31").unwrap() > 2_000_000);
}

#[test]
fn persistence_from_the_ini() {
    let mut ini = Ini::default();
    let s = ini.load();
    assert!(!s.show_resignation && s.resignation_date.is_empty());
    // An old file without the section keeps every other value.
    let mut s = offwork_core::Settings {
        start_minutes: 480,
        end_minutes: 1020,
        hide_earned: true,
        ..Default::default()
    };
    ini.save(&s);
    ini.set("resignation", "date", None);
    ini.set("resignation", "show", None);
    let loaded = ini.load();
    assert!(!loaded.show_resignation && loaded.resignation_date.is_empty());
    assert!(loaded.start_minutes == 480 && loaded.end_minutes == 1020 && loaded.hide_earned);
    s.resignation_date = "2026-11-30".into();
    s.show_resignation = true;
    ini.save(&s);
    let loaded = ini.load();
    assert!(loaded.show_resignation && loaded.resignation_date == "2026-11-30");
    s.show_resignation = false;
    ini.save(&s);
    let loaded = ini.load();
    assert!(!loaded.show_resignation && loaded.resignation_date == "2026-11-30");
    for text in [
        "2026-02-29",
        "2026-11-30junk",
        "2026-11-30000000000000000000000000000000",
    ] {
        ini.set("resignation", "date", Some(text));
        assert!(ini.load().resignation_date.is_empty(), "{text}");
    }
}
