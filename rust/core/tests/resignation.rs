use offwork_core::{parse_date, workdays};
#[test]
fn ctest_date_cases() {
    let count = |a, b| parse_date(b).and_then(|d| workdays(parse_date(a).unwrap(), d));
    assert_eq!(count("2026-11-26", "2026-11-30"), Some(2));
    assert_eq!(count("2026-11-27", "2026-11-30"), Some(1));
    assert_eq!(count("2026-11-28", "2026-11-30"), Some(1));
    assert_eq!(count("2026-11-29", "2026-11-30"), Some(1));
    assert_eq!(count("2026-11-30", "2026-11-30"), Some(0));
    assert_eq!(count("2026-12-01", "2026-11-30"), None);
    assert_eq!(count("2026-11-27", "2026-11-29"), Some(0));
    assert_eq!(count("2026-11-29", "2026-11-29"), Some(0));
    assert_eq!(count("2026-11-23", "2026-11-30"), Some(5));
    assert_eq!(count("2026-12-31", "2027-01-01"), Some(1));
    assert_eq!(count("2024-02-28", "2024-03-01"), Some(2));
    assert_eq!(count("2026-03-06", "2026-03-09"), Some(1));
    assert_eq!(count("2026-11-26", ""), None);
    assert_eq!(count("2026-11-26", "2026-02-29"), None);
    assert_eq!(count("9999-12-31", "9999-12-31"), Some(0));
}
