//! Port of `src/resignation.h`: strict local civil dates, Monday–Friday only.

use chrono::{Datelike, NaiveDate, Weekday};

/// Strict `YYYY-MM-DD`, years 0001–9999, no surrounding text.
pub fn parse_date(text: &str) -> Option<NaiveDate> {
    let bytes = text.as_bytes();
    if bytes.len() != 10 || bytes[4] != b'-' || bytes[7] != b'-' {
        return None;
    }
    let mut parts = [0u32; 3];
    let mut part = 0;
    for (i, &b) in bytes.iter().enumerate() {
        if i == 4 || i == 7 {
            part += 1;
            continue;
        }
        if !b.is_ascii_digit() {
            return None;
        }
        parts[part] = parts[part] * 10 + u32::from(b - b'0');
    }
    if parts[0] < 1 {
        return None;
    }
    NaiveDate::from_ymd_opt(parts[0] as i32, parts[1], parts[2])
}

/// Counts `(today, target]` Monday–Friday. No holiday or make-up-day dataset.
/// Returns `None` when hidden, invalid or already past.
pub fn remaining_workdays(today: NaiveDate, target: &str, shown: bool) -> Option<i64> {
    if !shown {
        return None;
    }
    let target = parse_date(target)?;
    if target < today {
        return None;
    }
    let distance = (target - today).num_days();
    let mut result = distance / 7 * 5;
    for offset in 1..=distance % 7 {
        let day = today + chrono::Days::new(offset as u64);
        if !matches!(day.weekday(), Weekday::Sat | Weekday::Sun) {
            result += 1;
        }
    }
    Some(result)
}
