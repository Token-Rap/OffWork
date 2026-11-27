//! Port of `src/settings.h` (the persisted model) and `NormalizeSettings`.

use crate::resignation::parse_date;
use crate::schedule::{self, Shift, Workday};
use serde::{Deserialize, Serialize};

pub const MIN_SCALE: i32 = 75;
pub const MAX_SCALE: i32 = 150;
pub const MIN_OPACITY: i32 = 25;
pub const MAX_SALARY: f64 = 1_000_000.0;

#[derive(Clone, Copy, Debug, Default, Deserialize, Eq, PartialEq, Serialize)]
pub enum EndMode {
    #[serde(rename = "endTime")]
    EndTime,
    #[default]
    #[serde(rename = "duration")]
    Duration,
}

#[derive(Clone, Debug, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Settings {
    pub start_minutes: i32,
    pub end_minutes: i32,
    /// Both -1 means no lunch.
    pub lunch_start_minutes: i32,
    pub lunch_end_minutes: i32,
    pub daily_salary: f64,
    pub scale_percent: i32,
    pub opacity_percent: i32,
    pub hide_earned: bool,
    /// Display only; earnings always exclude lunch.
    pub deduct_lunch: bool,
    /// `YYYY-MM-DD`, empty means unset.
    pub resignation_date: String,
    /// Hiding preserves the date.
    pub show_resignation: bool,
    pub end_mode: EndMode,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            start_minutes: 9 * 60,
            end_minutes: 18 * 60,
            lunch_start_minutes: 12 * 60,
            lunch_end_minutes: 13 * 60 + 30,
            daily_salary: 500.0,
            scale_percent: 100,
            opacity_percent: 100,
            hide_earned: false,
            deduct_lunch: false,
            resignation_date: String::new(),
            show_resignation: false,
            end_mode: EndMode::Duration,
        }
    }
}

impl Settings {
    /// Pure: clamps every field and falls back to defaults for a work day that
    /// cannot exist. Returns a new value; `self` is untouched.
    pub fn normalized(&self) -> Settings {
        let mut n = self.clone();
        if parse_date(&n.resignation_date).is_none() {
            n.resignation_date.clear();
        }
        n.scale_percent = self.scale_percent.clamp(MIN_SCALE, MAX_SCALE);
        n.opacity_percent = self.opacity_percent.clamp(MIN_OPACITY, 100);
        n.daily_salary = if !self.daily_salary.is_finite() || self.daily_salary < 0.0 {
            0.0
        } else {
            self.daily_salary.min(MAX_SALARY)
        };
        let day = 0..schedule::MINUTES_PER_DAY;
        if !day.contains(&self.start_minutes)
            || !day.contains(&self.end_minutes)
            || self.start_minutes == self.end_minutes
        {
            n.start_minutes = 9 * 60;
            n.end_minutes = 18 * 60;
        }
        if schedule::lunch_duration(
            n.start_minutes,
            n.end_minutes,
            n.lunch_start_minutes,
            n.lunch_end_minutes,
        )
        .is_err()
        {
            n.lunch_start_minutes = -1;
            n.lunch_end_minutes = -1;
        }
        n
    }

    pub fn shift(&self) -> Shift {
        Shift {
            start: self.start_minutes,
            end: self.end_minutes,
            salary: self.daily_salary,
            lunch_start: self.lunch_start_minutes,
            lunch_end: self.lunch_end_minutes,
            deduct_lunch: self.deduct_lunch,
        }
    }

    pub fn net_minutes(&self) -> schedule::Result<i32> {
        schedule::effective_work(
            self.start_minutes,
            self.end_minutes,
            self.lunch_start_minutes,
            self.lunch_end_minutes,
        )
    }

    pub fn workday(&self, now_seconds: i32) -> schedule::Result<Workday> {
        self.shift().at(now_seconds)
    }
}
