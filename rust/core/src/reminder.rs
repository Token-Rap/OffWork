use crate::{Phase, Snapshot};

#[derive(Default)]
pub struct Reminder {
    previous: Option<(Phase, i32, i64)>,
    five_handled: bool,
}

#[derive(Debug, PartialEq)]
pub enum Notice {
    FiveMinutes,
    Finished,
}

impl Reminder {
    pub fn observe(
        &mut self,
        snapshot: &Snapshot,
        timestamp: i64,
        seconds_since_end: i32,
    ) -> Option<Notice> {
        let current = (snapshot.phase, snapshot.remaining, timestamp);
        let previous = self.previous.replace(current);
        if !matches!(previous, Some((Phase::Working, _, _))) {
            self.five_handled = snapshot.remaining <= 300;
            return None;
        }
        if snapshot.phase == Phase::After && seconds_since_end <= 600 {
            return Some(Notice::Finished);
        }
        if let Some((_, remaining, time)) = previous {
            if snapshot.phase == Phase::Working && snapshot.remaining <= 300 {
                let notify = !self.five_handled
                    && remaining > 300
                    && (0..=600).contains(&(timestamp - time));
                self.five_handled = true;
                if notify {
                    return Some(Notice::FiveMinutes);
                }
            }
        }
        None
    }
}
