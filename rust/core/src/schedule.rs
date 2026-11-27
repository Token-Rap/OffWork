//! Port of `src/calculator.h`. Minutes are local wall-clock minutes in
//! `[0, 1440)`; lunch `(-1, -1)` means "no lunch" (legacy duration 0).

use serde::Serialize;

pub const MINUTES_PER_DAY: i32 = 24 * 60;
pub const SECONDS_PER_DAY: i32 = 24 * 60 * 60;
pub const NO_LUNCH: (i32, i32) = (-1, -1);

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
pub enum Phase {
    Before,
    Working,
    After,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ScheduleError {
    WorkHours,
    StartTime,
    Duration,
    LunchOutsideShift,
    Now,
    Salary,
}

pub type Result<T> = std::result::Result<T, ScheduleError>;

fn valid_minute(m: i32) -> bool {
    (0..MINUTES_PER_DAY).contains(&m)
}

pub fn work_duration(start: i32, end: i32) -> Result<i32> {
    if !valid_minute(start) || !valid_minute(end) || start == end {
        return Err(ScheduleError::WorkHours);
    }
    Ok((end - start).rem_euclid(MINUTES_PER_DAY))
}

pub fn end_from_duration(start: i32, duration: i32) -> Result<i32> {
    if !valid_minute(start) {
        return Err(ScheduleError::StartTime);
    }
    if duration <= 0 || duration >= MINUTES_PER_DAY {
        return Err(ScheduleError::Duration);
    }
    Ok((start + duration) % MINUTES_PER_DAY)
}

pub fn duration_from_hours(hours: f64) -> Result<i32> {
    if !hours.is_finite() || hours <= 0.0 || hours >= 24.0 {
        return Err(ScheduleError::Duration);
    }
    let minutes = (hours * 60.0).round() as i32;
    if minutes <= 0 || minutes >= MINUTES_PER_DAY {
        return Err(ScheduleError::Duration);
    }
    Ok(minutes)
}

/// Offsets are relative to clock-in so overnight shifts and lunches work too.
pub fn lunch_duration(start: i32, end: i32, lunch_start: i32, lunch_end: i32) -> Result<i32> {
    let duration = work_duration(start, end)?;
    if (lunch_start, lunch_end) == NO_LUNCH {
        return Ok(0);
    }
    let lunch = work_duration(lunch_start, lunch_end)?;
    let offset = (lunch_start - start).rem_euclid(MINUTES_PER_DAY);
    if offset + lunch > duration || lunch >= duration {
        return Err(ScheduleError::LunchOutsideShift);
    }
    Ok(lunch)
}

pub fn effective_work(start: i32, end: i32, lunch_start: i32, lunch_end: i32) -> Result<i32> {
    Ok(work_duration(start, end)? - lunch_duration(start, end, lunch_start, lunch_end)?)
}

pub fn end_from_net(start: i32, net: i32, lunch_start: i32, lunch_end: i32) -> Result<i32> {
    if net <= 0 || net >= MINUTES_PER_DAY {
        return Err(ScheduleError::Duration);
    }
    let lunch = if (lunch_start, lunch_end) == NO_LUNCH {
        0
    } else {
        work_duration(lunch_start, lunch_end)?
    };
    let end = end_from_duration(start, net + lunch)?;
    // The chosen lunch interval must still fit inside the resulting shift.
    effective_work(start, end, lunch_start, lunch_end)?;
    Ok(end)
}

#[derive(Clone, Copy, Debug, PartialEq, Serialize)]
pub struct Workday {
    pub phase: Phase,
    /// Real time to the phase boundary; reminders must use this value.
    pub remaining: i32,
    pub earned: f64,
    pub progress: f64,
    pub on_lunch: bool,
    /// Display only; reminders always use `remaining`.
    pub countdown: i32,
}

pub struct Shift {
    pub start: i32,
    pub end: i32,
    pub salary: f64,
    pub lunch_start: i32,
    pub lunch_end: i32,
    pub deduct_lunch: bool,
}

impl Shift {
    pub fn simple(start: i32, end: i32, salary: f64) -> Self {
        Self {
            start,
            end,
            salary,
            lunch_start: -1,
            lunch_end: -1,
            deduct_lunch: false,
        }
    }
    pub fn lunch(mut self, lunch_start: i32, lunch_end: i32) -> Self {
        self.lunch_start = lunch_start;
        self.lunch_end = lunch_end;
        self
    }
    pub fn deduct(mut self) -> Self {
        self.deduct_lunch = true;
        self
    }

    pub fn at(&self, now: i32) -> Result<Workday> {
        if !(0..SECONDS_PER_DAY).contains(&now) {
            return Err(ScheduleError::Now);
        }
        if !valid_minute(self.start) || !valid_minute(self.end) || self.start == self.end {
            return Err(ScheduleError::WorkHours);
        }
        if self.salary < 0.0 || self.salary.is_nan() {
            return Err(ScheduleError::Salary);
        }
        let (start, end) = (self.start * 60, self.end * 60);
        let duration = work_duration(self.start, self.end)? * 60;
        let (mut phase, mut elapsed, mut remaining) = (Phase::After, duration, 0);
        if end > start {
            if now < start {
                (phase, elapsed, remaining) = (Phase::Before, 0, start - now);
            } else if now < end {
                (phase, elapsed, remaining) = (Phase::Working, now - start, end - now);
            }
        } else if now >= start || now < end {
            elapsed = (now - start).rem_euclid(SECONDS_PER_DAY);
            (phase, remaining) = (Phase::Working, duration - elapsed);
        }
        let lunch = lunch_duration(self.start, self.end, self.lunch_start, self.lunch_end)? * 60;
        let offset = (self.lunch_start - self.start).rem_euclid(MINUTES_PER_DAY) * 60;
        let on_lunch =
            phase == Phase::Working && lunch > 0 && elapsed >= offset && elapsed < offset + lunch;
        let eaten = if lunch == 0 {
            0
        } else {
            (elapsed - offset).clamp(0, lunch)
        };
        let progress = ((elapsed - eaten) as f64 / (duration - lunch) as f64).clamp(0.0, 1.0);
        let countdown = if self.deduct_lunch && phase == Phase::Working {
            (remaining - (lunch - eaten)).max(0)
        } else {
            remaining
        };
        Ok(Workday {
            phase,
            remaining,
            earned: self.salary * progress,
            progress,
            on_lunch,
            countdown,
        })
    }
}
