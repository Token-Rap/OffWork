use offwork_core::{
    parse_date,
    reminder::{Notice, Reminder},
    Settings,
};
#[test]
fn crosses_real_deadline_once_and_skips_stale_wakeups() {
    let s = Settings {
        deduct: true,
        ..Settings::default()
    };
    let day = parse_date("2026-11-26").unwrap();
    let mut reminders = Reminder::default();
    let mut tick = |t| {
        reminders.observe(
            &s.snapshot(t, day).unwrap(),
            t.into(),
            (t - 64800).rem_euclid(86400),
        )
    };
    assert_eq!(tick(64499), None);
    assert_eq!(tick(64500), Some(Notice::FiveMinutes));
    assert_eq!(tick(64501), None);
    assert_eq!(tick(64800), Some(Notice::Finished));
    assert_eq!(tick(64801), None);
    assert_eq!(tick(63000), None);
    assert_eq!(tick(64600), None);
    assert_eq!(tick(65500), None);
}
