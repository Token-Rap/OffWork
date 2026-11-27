//! Port of tests/settings_tests.cpp: normalization and every INI load rule.
mod common;
use common::Ini;
use offwork_core::settings::*;

fn with<F: FnOnce(&mut Settings)>(f: F) -> Settings {
    let mut s = Settings::default();
    f(&mut s);
    s
}

#[test]
fn scale_and_opacity_are_clamped() {
    for (input, out) in [
        (10, MIN_SCALE),
        (500, MAX_SCALE),
        (100, 100),
        (75, 75),
        (150, 150),
    ] {
        assert_eq!(
            with(|s| s.scale_percent = input).normalized().scale_percent,
            out
        );
    }
    for (input, out) in [(0, MIN_OPACITY), (1000, 100), (50, 50)] {
        assert_eq!(
            with(|s| s.opacity_percent = input)
                .normalized()
                .opacity_percent,
            out
        );
    }
}

#[test]
fn impossible_days_fall_back_and_overnight_is_kept() {
    let fixed = with(|s| s.end_minutes = 540).normalized();
    assert_eq!((fixed.start_minutes, fixed.end_minutes), (540, 1080));
    let fixed = with(|s| (s.start_minutes, s.end_minutes) = (-5, 99999)).normalized();
    assert_eq!((fixed.start_minutes, fixed.end_minutes), (540, 1080));
    let kept = with(|s| (s.start_minutes, s.end_minutes) = (1320, 360)).normalized();
    assert_eq!((kept.start_minutes, kept.end_minutes), (1320, 360));
}

#[test]
fn salary_is_clamped_and_non_finite_rejected() {
    for (input, out) in [
        (-1.0, 0.0),
        (9e9, 1_000_000.0),
        (f64::NAN, 0.0),
        (500.0, 500.0),
        (f64::INFINITY, 0.0),
    ] {
        assert_eq!(
            with(|s| s.daily_salary = input).normalized().daily_salary,
            out
        );
    }
}

#[test]
fn normalize_does_not_touch_its_input() {
    let original = with(|s| s.scale_percent = 500);
    let copy = original.normalized();
    assert_eq!(
        (original.scale_percent, copy.scale_percent),
        (500, MAX_SCALE)
    );
}

#[test]
fn end_mode_round_trips_and_defaults_to_duration() {
    assert_eq!(Settings::default().end_mode, EndMode::Duration);
    let mut ini = Ini::default();
    assert_eq!(ini.load().end_mode, EndMode::Duration);
    let mut s = with(|s| s.end_minutes = 1050);
    ini.save(&s);
    let loaded = ini.load();
    assert_eq!(
        (loaded.start_minutes, loaded.end_minutes, loaded.end_mode),
        (540, 1050, EndMode::Duration)
    );
    s.end_mode = EndMode::EndTime;
    ini.save(&s);
    assert_eq!(ini.load().end_mode, EndMode::EndTime);

    let mut ini = Ini::default();
    ini.set("work", "startMinutes", Some("532"));
    ini.set("work", "endMinutes", Some("1130"));
    for (value, mode) in [
        ("", EndMode::Duration),
        ("unknown", EndMode::Duration),
        ("duration", EndMode::Duration),
        ("DURATION", EndMode::Duration),
        ("endTime", EndMode::EndTime),
        ("ENDTIME", EndMode::EndTime),
    ] {
        ini.set("work", "endInputMode", Some(value));
        let loaded = ini.load();
        assert_eq!(loaded.end_mode, mode, "{value}");
        assert_eq!((loaded.start_minutes, loaded.end_minutes), (532, 1130));
    }
    ini.set("work", "endInputMode", None);
    assert_eq!(ini.load().end_mode, EndMode::Duration);
}

#[test]
fn money_mask_persists() {
    let mut ini = Ini::default();
    assert!(!ini.load().hide_earned);
    ini.save(&with(|s| s.hide_earned = true));
    assert!(ini.load().hide_earned);
    ini.save(&with(|s| s.hide_earned = false));
    assert!(!ini.load().hide_earned);
    ini.set("window", "hideEarned", None);
    assert!(!ini.load().hide_earned);
}

#[test]
fn lunch_keys_migrate_like_the_cpp_loader() {
    let mut ini = Ini::default();
    let loaded = ini.load();
    assert_eq!(
        (loaded.lunch_start_minutes, loaded.lunch_end_minutes),
        (720, 810)
    );
    assert!(!loaded.deduct_lunch);
    // Duration-only settings migrate from noon, independently of the checkbox.
    for deduct in ["0", "1"] {
        ini.set("work", "deductLunch", Some(deduct));
        for duration in [30, 60, 90, 180] {
            ini.set("work", "lunchDurationMinutes", Some(&duration.to_string()));
            let l = ini.load();
            assert_eq!(
                (l.lunch_start_minutes, l.lunch_end_minutes),
                (720, 720 + duration)
            );
            assert_eq!((l.start_minutes, l.end_minutes), (540, 1080));
            assert_eq!(l.deduct_lunch, deduct == "1");
        }
    }
    for value in [
        "0",
        "",
        "-1",
        "1440",
        "60oops",
        "60.5",
        "99999999999999999999",
        "540",
    ] {
        ini.set("work", "lunchDurationMinutes", Some(value));
        let l = ini.load();
        assert_eq!(
            (l.lunch_start_minutes, l.lunch_end_minutes),
            (-1, -1),
            "{value}"
        );
    }
    // Explicit interval keys win, even when invalid or incomplete.
    ini.set("work", "lunchDurationMinutes", Some("60"));
    ini.set("work", "lunchStartMinutes", Some("690"));
    ini.set("work", "lunchEndMinutes", Some("780"));
    let loaded = ini.load();
    assert_eq!(
        (loaded.lunch_start_minutes, loaded.lunch_end_minutes),
        (690, 780)
    );
    ini.save(&loaded);
    assert!(!ini.text().contains("lunchDurationMinutes"));
    assert!(ini.load().deduct_lunch);
    for value in [
        "690",
        "1440",
        "-1",
        "oops",
        "780oops",
        "99999999999999999999",
        "",
        "540",
        "1081",
    ] {
        ini.set("work", "lunchEndMinutes", Some(value));
        let l = ini.load();
        assert_eq!(
            (l.lunch_start_minutes, l.lunch_end_minutes),
            (-1, -1),
            "{value}"
        );
    }
    ini.set("work", "lunchEndMinutes", None);
    let l = ini.load();
    assert_eq!(l.lunch_start_minutes, -1);
    // Disabled legacy lunches survive a save/reload without restoring defaults.
    ini.save(&l);
    assert_eq!(ini.load().lunch_end_minutes, -1);
    let overnight = with(|s| {
        (
            s.start_minutes,
            s.end_minutes,
            s.lunch_start_minutes,
            s.lunch_end_minutes,
        ) = (1320, 360, 1410, 30)
    });
    ini.save(&overnight);
    let l = ini.load();
    assert_eq!(
        (
            l.start_minutes,
            l.end_minutes,
            l.lunch_start_minutes,
            l.lunch_end_minutes
        ),
        (1320, 360, 1410, 30)
    );
}

#[test]
fn profile_api_edge_cases() {
    // Whitespace, case-insensitive names, quotes, comments, duplicate keys,
    // integer prefixes, negative numbers and wcstod prefixes.
    let text = "; written by hand\r\n[Work]\r\n  StartMinutes = 480xyz \r\nstartMinutes=1\r\nendMinutes=\"1020\"\r\ndailySalary=  650.5abc\r\n[WINDOW]\r\nscalePercent=-5\r\nopacityPercent=+60\r\n";
    let s = common::load_bytes(text.as_bytes());
    assert_eq!((s.start_minutes, s.end_minutes), (480, 1020));
    assert_eq!(s.daily_salary, 650.5);
    assert_eq!((s.scale_percent, s.opacity_percent), (MIN_SCALE, 60));
    for (salary, out) in [
        ("abc", 500.0),
        ("-3", 500.0),
        ("inf", 500.0),
        ("nan", 500.0),
        ("2e3", 2000.0),
        (".5", 0.5),
        ("9e99", 1_000_000.0),
    ] {
        let s = common::load_bytes(format!("[work]\r\ndailySalary={salary}\r\n").as_bytes());
        assert_eq!(s.daily_salary, out, "{salary}");
    }
    // UTF-16LE with BOM, as the profile API writes when the file has one.
    let utf16: Vec<u8> = [0xff, 0xfe]
        .into_iter()
        .chain(
            "[work]\r\nstartMinutes=600\r\n[resignation]\r\ndate=2027-01-04\r\nshow=1\r\n"
                .encode_utf16()
                .flat_map(u16::to_le_bytes),
        )
        .collect();
    let s = common::load_bytes(&utf16);
    assert_eq!(
        (
            s.start_minutes,
            s.resignation_date.as_str(),
            s.show_resignation
        ),
        (600, "2027-01-04", true)
    );
    // The profile API does not honour a UTF-8 BOM: the first section is lost,
    // so 0.4.5 itself saw defaults here and the import must agree.
    let s =
        common::load_bytes(b"\xef\xbb\xbf[work]\nendMinutes=1110\n[window]\nscalePercent=120\n");
    assert_eq!((s.end_minutes, s.scale_percent), (1080, 120));
}
