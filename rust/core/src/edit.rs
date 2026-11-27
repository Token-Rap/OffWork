//! Port of the pure parts of `src/app_edit.cpp`: field text ⇄ settings.
//!
//! The UI owns the six field drafts; these functions decide what a commit
//! (blur/Enter) or a live keystroke does to the saved settings. Every function
//! returns a new value and never touches its inputs.

use crate::resignation::parse_date;
use crate::schedule::{duration_from_hours, end_from_net, NO_LUNCH};
use crate::settings::{EndMode, Settings, MAX_SALARY};
use serde::{Deserialize, Serialize};

pub const VALIDATION_MESSAGE: &str = "请检查时间、工作时长、午休起止时间和日薪";
pub const RESIGNATION_MESSAGE: &str = "请输入有效离职日期 YYYY-MM-DD，或清空日期";
pub const OCR_CONFLICT_MESSAGE: &str = "时间与排班冲突，请调整设置。";

#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum Field {
    Start,
    End,
    Salary,
    LunchStart,
    LunchEnd,
    Resignation,
}

#[derive(Clone, Debug, Default, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase", default)]
pub struct Fields {
    pub start: String,
    pub end: String,
    pub salary: String,
    pub lunch_start: String,
    pub lunch_end: String,
    pub resignation: String,
}

impl Fields {
    pub fn get(&self, field: Field) -> &str {
        match field {
            Field::Start => &self.start,
            Field::End => &self.end,
            Field::Salary => &self.salary,
            Field::LunchStart => &self.lunch_start,
            Field::LunchEnd => &self.lunch_end,
            Field::Resignation => &self.resignation,
        }
    }
    pub fn with(&self, field: Field, text: &str) -> Fields {
        let mut next = self.clone();
        let slot = match field {
            Field::Start => &mut next.start,
            Field::End => &mut next.end,
            Field::Salary => &mut next.salary,
            Field::LunchStart => &mut next.lunch_start,
            Field::LunchEnd => &mut next.lunch_end,
            Field::Resignation => &mut next.resignation,
        };
        *slot = text.to_owned();
        next
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum Commit {
    /// Text equals the saved value: nothing is written.
    Unchanged,
    Saved(Settings),
    /// Invalid edit: the UI reverts the field and shows the message.
    Rejected(&'static str),
}

fn trim_zeros(mut text: String) -> String {
    while text.len() > 1 && text.ends_with('0') {
        text.pop();
    }
    if text.ends_with('.') {
        text.pop();
    }
    text
}

pub fn format_time(minutes: i32) -> String {
    format!("{:02}:{:02}", minutes / 60, minutes % 60)
}
pub fn format_salary(salary: f64) -> String {
    trim_zeros(format!("{salary:.2}"))
}
pub fn format_duration_hours(minutes: i32) -> String {
    trim_zeros(format!("{:.2}", f64::from(minutes) / 60.0))
}

/// `H:MM`, `HH:M` or `HH:MM`; hours 0–23, minutes 0–59.
pub fn parse_time(text: &str) -> Option<i32> {
    if !text.is_ascii() {
        return None;
    }
    let colon = text.find(':')?;
    if !(1..=2).contains(&colon) || text.len() <= colon + 1 || text.len() > colon + 3 {
        return None;
    }
    let (hour, minute) = (&text[..colon], &text[colon + 1..]);
    if !hour
        .bytes()
        .chain(minute.bytes())
        .all(|b| b.is_ascii_digit())
    {
        return None;
    }
    let (hour, minute): (i32, i32) = (hour.parse().ok()?, minute.parse().ok()?);
    (hour <= 23 && minute <= 59).then_some(hour * 60 + minute)
}

/// Lunch endpoints must be complete `HH:MM`.
pub fn parse_lunch_time(text: &str) -> Option<i32> {
    (text.len() == 5 && text.as_bytes()[2] == b':')
        .then(|| parse_time(text))
        .flatten()
}

/// Digits with at most one decimal point, the characters the field accepts.
fn parse_decimal(text: &str) -> Option<f64> {
    let digits = text.bytes().filter(u8::is_ascii_digit).count();
    let dots = text.bytes().filter(|&b| b == b'.').count();
    (digits > 0 && dots <= 1 && digits + dots == text.len())
        .then(|| text.parse::<f64>().ok())
        .flatten()
        .filter(|v| v.is_finite())
}

pub fn parse_duration(text: &str) -> Option<i32> {
    let hours = parse_decimal(text)?;
    (hours > 0.0 && hours < 24.0)
        .then(|| duration_from_hours(hours).ok())
        .flatten()
}

/// Rounded to the cents shown in the editor and written to disk.
pub fn parse_salary(text: &str) -> Option<f64> {
    let value = parse_decimal(text)?;
    (0.0..=MAX_SALARY)
        .contains(&value)
        .then(|| (value * 100.0).round() / 100.0)
}

fn net(s: &Settings) -> i32 {
    s.net_minutes().unwrap_or(0)
}

fn with_net(s: &Settings, minutes: i32) -> Option<Settings> {
    let end = end_from_net(
        s.start_minutes,
        minutes,
        s.lunch_start_minutes,
        s.lunch_end_minutes,
    )
    .ok()?;
    Some(Settings {
        end_minutes: end,
        ..s.clone()
    })
}

fn valid(s: &Settings) -> bool {
    s.net_minutes().is_ok()
}

fn lunch_text(minutes: i32) -> String {
    if minutes < 0 {
        String::new()
    } else {
        format_time(minutes)
    }
}

/// What the editor shows for a saved value (`SettingsField`).
pub fn field_text(s: &Settings, field: Field) -> String {
    match field {
        Field::Start => format_time(s.start_minutes),
        Field::End => match s.end_mode {
            EndMode::Duration => format_duration_hours(net(s)),
            EndMode::EndTime => format_time(s.end_minutes),
        },
        Field::Salary => format_salary(s.daily_salary),
        Field::LunchStart => lunch_text(s.lunch_start_minutes),
        Field::LunchEnd => lunch_text(s.lunch_end_minutes),
        Field::Resignation => s.resignation_date.clone(),
    }
}

/// `PopulateFields`: loading the editor never migrates the end time.
pub fn fields(s: &Settings) -> Fields {
    Fields {
        start: field_text(s, Field::Start),
        end: field_text(s, Field::End),
        salary: field_text(s, Field::Salary),
        lunch_start: field_text(s, Field::LunchStart),
        lunch_end: field_text(s, Field::LunchEnd),
        resignation: field_text(s, Field::Resignation),
    }
}

fn read_lunch(s: &Settings, f: &Fields) -> Option<Settings> {
    let (start, end) = if f.lunch_start.is_empty() && f.lunch_end.is_empty() {
        NO_LUNCH
    } else {
        (
            parse_lunch_time(&f.lunch_start)?,
            parse_lunch_time(&f.lunch_end)?,
        )
    };
    Some(Settings {
        lunch_start_minutes: start,
        lunch_end_minutes: end,
        ..s.clone()
    })
}

fn keep_net(saved: &Settings, draft: Settings) -> Option<Settings> {
    match draft.end_mode {
        EndMode::Duration => with_net(&draft, net(saved)),
        EndMode::EndTime => Some(draft),
    }
}

/// `ApplyFieldChange`: commit one field on blur or Enter.
pub fn commit(s: &Settings, field: Field, f: &Fields) -> Commit {
    if field == Field::Resignation {
        let text = &f.resignation;
        return if *text == s.resignation_date {
            Commit::Unchanged
        } else if text.is_empty() || parse_date(text).is_some() {
            Commit::Saved(Settings {
                resignation_date: text.clone(),
                ..s.clone()
            })
        } else {
            Commit::Rejected(RESIGNATION_MESSAGE)
        };
    }
    let lunch = matches!(field, Field::LunchStart | Field::LunchEnd);
    let same = |field| f.get(field) == field_text(s, field);
    if same(field) && (!lunch || (same(Field::LunchStart) && same(Field::LunchEnd))) {
        return Commit::Unchanged;
    }
    let text = f.get(field);
    let draft = match field {
        Field::Start => parse_time(text).and_then(|start| {
            keep_net(
                s,
                Settings {
                    start_minutes: start,
                    ..s.clone()
                },
            )
        }),
        Field::End => match s.end_mode {
            EndMode::Duration => parse_duration(text).and_then(|d| with_net(s, d)),
            EndMode::EndTime => parse_time(text).map(|end| Settings {
                end_minutes: end,
                ..s.clone()
            }),
        },
        Field::Salary => parse_salary(text).map(|salary| Settings {
            daily_salary: salary,
            ..s.clone()
        }),
        _ => read_lunch(s, f).and_then(|d| keep_net(s, d)),
    };
    match draft.filter(valid) {
        Some(d) => Commit::Saved(d),
        None => Commit::Rejected(VALIDATION_MESSAGE),
    }
}

/// `ApplyLiveEndChange` / `ApplyLiveLunchChange`: a complete, valid edit of
/// the end or lunch fields saves while still focused; partial input never does.
pub fn live(s: &Settings, field: Field, f: &Fields) -> Option<Settings> {
    match field {
        Field::End => {
            let text = &f.end;
            let draft = match s.end_mode {
                EndMode::Duration => {
                    if text.ends_with('.') {
                        return None;
                    }
                    with_net(s, parse_duration(text)?)?
                }
                EndMode::EndTime => {
                    let end = parse_time(text)?;
                    if text.len() - text.find(':')? != 3 {
                        return None;
                    }
                    Settings {
                        end_minutes: end,
                        ..s.clone()
                    }
                }
            };
            (draft.end_minutes != s.end_minutes && valid(&draft)).then_some(draft)
        }
        Field::LunchStart | Field::LunchEnd => {
            let draft = keep_net(s, read_lunch(s, f)?)?;
            let changed = (draft.lunch_start_minutes, draft.lunch_end_minutes)
                != (s.lunch_start_minutes, s.lunch_end_minutes);
            (valid(&draft) && changed).then_some(draft)
        }
        _ => None,
    }
}

/// `ReadFieldSettings`: every field parsed with field 1 read in `mode`.
pub fn read_fields(s: &Settings, mode: EndMode, f: &Fields) -> Option<Settings> {
    let start = parse_time(&f.start)?;
    let mut draft = read_lunch(
        &Settings {
            start_minutes: start,
            end_mode: mode,
            ..s.clone()
        },
        f,
    )?;
    draft = match mode {
        EndMode::Duration => with_net(&draft, parse_duration(&f.end)?)?,
        EndMode::EndTime => {
            let end = parse_time(&f.end)?;
            if end == draft.start_minutes {
                return None;
            }
            Settings {
                end_minutes: end,
                ..draft
            }
        }
    };
    let salary = parse_salary(&f.salary)?;
    valid(&draft).then_some(Settings {
        daily_salary: salary,
        ..draft
    })
}

/// `SetEndInputMode`: reads all drafts in the current mode, then saves them in
/// the new one so the end field can be shown as a time or as net hours.
pub fn switch_end_mode(s: &Settings, mode: EndMode, f: &Fields) -> Commit {
    if s.end_mode == mode {
        return Commit::Unchanged;
    }
    match read_fields(s, s.end_mode, f) {
        Some(draft) => Commit::Saved(Settings {
            end_mode: mode,
            ..draft
        }),
        None => Commit::Rejected(VALIDATION_MESSAGE),
    }
}

/// OCR result: duration mode keeps net hours, fixed mode keeps the end time.
pub fn apply_clock_in(s: &Settings, minutes: i32) -> Result<Settings, &'static str> {
    let mut updated = s.clone();
    if updated.end_mode == EndMode::Duration {
        updated.end_minutes = end_from_net(
            minutes,
            net(s),
            updated.lunch_start_minutes,
            updated.lunch_end_minutes,
        )
        .map_err(|_| OCR_CONFLICT_MESSAGE)?;
    }
    updated.start_minutes = minutes;
    if updated.normalized().start_minutes != updated.start_minutes
        || updated.start_minutes == updated.end_minutes
        || !valid(&updated)
    {
        return Err(OCR_CONFLICT_MESSAGE);
    }
    Ok(updated)
}
