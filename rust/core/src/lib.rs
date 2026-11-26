use chrono::{Datelike, NaiveDate};
use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq)]
#[serde(default)]
pub struct Settings {
    pub start: i32,
    pub end: i32,
    pub duration_mode: bool,
    pub net: i32,
    pub lunch_start: i32,
    pub lunch_end: i32,
    pub salary: f64,
    pub deduct: bool,
    pub resignation: String,
    pub stamp: bool,
    pub hide_money: bool,
}
impl Default for Settings {
    fn default() -> Self {
        Self {
            start: 540,
            end: 1080,
            duration_mode: true,
            net: 450,
            lunch_start: 720,
            lunch_end: 810,
            salary: 500.,
            deduct: false,
            resignation: String::new(),
            stamp: false,
            hide_money: false,
        }
    }
}
fn span(start: i32, end: i32) -> Result<i32, String> {
    if !(0..1440).contains(&start) || !(0..1440).contains(&end) || start == end {
        return Err("请填写有效的起止时间".into());
    }
    Ok((end - start).rem_euclid(1440))
}
#[derive(Serialize, Clone, Debug)]
pub struct Snapshot {
    pub phase: Phase,
    pub remaining: i32,
    pub countdown: i32,
    pub on_lunch: bool,
    pub progress: f64,
    pub earned: f64,
    pub end: i32,
    pub days: Option<i64>,
}
#[derive(Serialize, Clone, Copy, Debug, Eq, PartialEq)]
pub enum Phase {
    Before,
    Working,
    After,
}
pub fn parse_date(s: &str) -> Option<NaiveDate> {
    let date = NaiveDate::parse_from_str(s, "%Y-%m-%d").ok()?;
    (date.year() > 0 && date.year() <= 9999 && date.format("%Y-%m-%d").to_string() == s)
        .then_some(date)
}
pub fn workdays(today: NaiveDate, target: NaiveDate) -> Option<i64> {
    let days = (target - today).num_days();
    (days >= 0).then(|| {
        days / 7 * 5
            + (1..=days % 7)
                .filter(|d| (today.weekday().num_days_from_monday() as i64 + d) % 7 < 5)
                .count() as i64
    })
}
impl Settings {
    pub fn snapshot(&self, now: i32, today: NaiveDate) -> Result<Snapshot, String> {
        if !(0..86400).contains(&now) || !(0.0..=1_000_000.).contains(&self.salary) {
            return Err("日薪须在 0 至 1,000,000 元之间".into());
        }
        if !self.resignation.is_empty() && parse_date(&self.resignation).is_none() {
            return Err("请输入有效的离职日期".into());
        }
        let lunch = if (self.lunch_start, self.lunch_end) == (-1, -1) {
            0
        } else {
            span(self.lunch_start, self.lunch_end)?
        };
        let end = if self.duration_mode {
            if !(1..1440 - lunch).contains(&self.net) || !(0..1440).contains(&self.start) {
                return Err("净工时与午休总长须少于一天".into());
            }
            (self.start + self.net + lunch) % 1440
        } else {
            self.end
        };
        let length = span(self.start, end)? * 60;
        let offset = (self.lunch_start - self.start).rem_euclid(1440) * 60;
        let lunch = lunch * 60;
        if lunch > 0 && (offset + lunch > length || lunch >= length) {
            return Err("午休须在班次内，并留有工作时间".into());
        }
        let relative = (now - self.start * 60).rem_euclid(86400);
        let (phase, elapsed, remaining) = if relative < length {
            (Phase::Working, relative, length - relative)
        } else if self.start < end && now < self.start * 60 {
            (Phase::Before, 0, self.start * 60 - now)
        } else {
            (Phase::After, length, 0)
        };
        let eaten = (elapsed - offset).clamp(0, lunch);
        let progress = (elapsed - eaten) as f64 / (length - lunch) as f64;
        Ok(Snapshot {
            end,
            phase,
            on_lunch: phase == Phase::Working && (offset..offset + lunch).contains(&elapsed),
            remaining,
            countdown: if self.deduct && phase == Phase::Working {
                remaining - lunch + eaten
            } else {
                remaining
            },
            earned: progress * self.salary,
            progress,
            days: parse_date(&self.resignation)
                .filter(|_| self.stamp)
                .and_then(|d| workdays(today, d)),
        })
    }
}
pub mod ocr;

pub mod reminder;
