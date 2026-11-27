//! Port of the settings-editor rules in tests/app_edit_tests.cpp. The C++
//! test drives globals (`g.fields`, `g.settings`); here the same sequences
//! run through the pure edit functions, with `Editor` playing the window.
use offwork_core::edit::*;
use offwork_core::schedule::Shift;
use offwork_core::settings::{EndMode, Settings};

/// The widget: saved settings plus the six drafts, with C++ focus rules.
struct Editor {
    s: Settings,
    f: Fields,
    error: Option<&'static str>,
    focused: Option<Field>,
    saves: usize,
}

fn lunch(field: Field) -> bool {
    matches!(field, Field::LunchStart | Field::LunchEnd)
}

impl Editor {
    fn new(s: Settings) -> Self {
        Self {
            f: fields(&s),
            s,
            error: None,
            focused: None,
            saves: 0,
        }
    }
    fn type_(&mut self, field: Field, text: &str) {
        self.f = self.f.with(field, text);
    }
    fn save(&mut self, s: Settings) {
        self.s = s;
        self.saves += 1;
    }
    fn commit(&mut self, field: Field) {
        match commit(&self.s, field, &self.f) {
            Commit::Unchanged => return,
            Commit::Saved(s) => {
                self.save(s);
                self.error = None;
            }
            Commit::Rejected(m) => self.error = Some(m),
        }
        // Repopulate the committed field (and its lunch partner, and the
        // duration when the start moved).
        self.f = self.f.with(field, &field_text(&self.s, field));
        if lunch(field) {
            for other in [Field::LunchStart, Field::LunchEnd] {
                self.f = self.f.with(other, &field_text(&self.s, other));
            }
        }
        if field == Field::Start && self.s.end_mode == EndMode::Duration {
            self.f = self.f.with(Field::End, &field_text(&self.s, Field::End));
        }
    }
    fn live(&mut self, field: Field) {
        if let Some(s) = live(&self.s, field, &self.f) {
            self.save(s);
        }
    }
    /// `SetFocusedField`: lunch is one interval, so moving between its two
    /// endpoints keeps the draft and only applies a complete pair.
    fn focus(&mut self, next: Option<Field>) {
        if self.focused == next {
            return;
        }
        match (self.focused, next) {
            (Some(a), Some(b)) if lunch(a) && lunch(b) => self.live(a),
            (Some(a), _) => self.commit(a),
            _ => {}
        }
        self.focused = next;
    }
    fn edit(&mut self, field: Field, text: &str) {
        self.focus(Some(field));
        self.type_(field, text);
        self.focus(None);
    }
    fn mode(&mut self, mode: EndMode) {
        match switch_end_mode(&self.s, mode, &self.f) {
            Commit::Saved(s) => {
                self.save(s);
                self.f = fields(&self.s);
            }
            Commit::Rejected(m) => self.error = Some(m),
            Commit::Unchanged => {}
        }
    }
    fn ocr(&mut self, minutes: i32) {
        match apply_clock_in(&self.s, minutes) {
            Ok(s) => {
                self.save(s);
                self.f = fields(&self.s);
                self.error = None;
            }
            Err(m) => self.error = Some(m),
        }
    }
}

fn settings<F: FnOnce(&mut Settings)>(f: F) -> Settings {
    let mut s = Settings::default();
    f(&mut s);
    s
}

#[test]
fn net_duration_recalculates_end() {
    let mut e = Editor::new(settings(|s| s.lunch_end_minutes = 780));
    assert_eq!(e.s.end_minutes, 1080); // Loading the editor never migrates end.
    assert_eq!(e.f.end, "8");
    e.type_(Field::End, "8.5");
    e.live(Field::End);
    assert_eq!(e.s.end_minutes, 1110);
    e.type_(Field::Start, "08:55");
    e.commit(Field::Start);
    assert_eq!(e.s.end_minutes, 1105);
    e.type_(Field::LunchEnd, "13:30");
    e.live(Field::LunchEnd);
    assert_eq!(e.s.end_minutes, 1135);
    assert_eq!(e.f.end, "8.5");
    // Blur-only lunch edits also keep net hours, including removing lunch.
    e.type_(Field::LunchEnd, "13:00");
    e.commit(Field::LunchEnd);
    assert!(e.s.end_minutes == 1105 && e.f.end == "8.5");
    e.type_(Field::LunchStart, "");
    e.type_(Field::LunchEnd, "");
    e.commit(Field::LunchStart);
    assert_eq!(e.s.end_minutes, 1045);
    e.type_(Field::LunchStart, "12:00");
    e.type_(Field::LunchEnd, "13:00");
    e.commit(Field::LunchEnd);
    assert_eq!(e.s.end_minutes, 1105);
    // A >= 24-hour wall span is rejected without overwriting saved data.
    e.type_(Field::End, "23");
    e.live(Field::End);
    assert_eq!(e.s.end_minutes, 1105);
    e.commit(Field::End);
    assert!(e.f.end == "8.5" && e.s.end_minutes == 1105);
    e.type_(Field::Start, "13:00"); // Lunch is now outside the shift.
    e.commit(Field::Start);
    assert!(e.s.start_minutes == 535 && e.s.end_minutes == 1105);
    e.mode(EndMode::EndTime);
    assert_eq!(e.f.end, "18:25");
    e.mode(EndMode::Duration);
    assert!(e.f.end == "8.5" && e.s.end_minutes == 1105);
    // Two-decimal hours still round-trip a single net minute.
    let one = settings(|s| {
        (
            s.start_minutes,
            s.lunch_start_minutes,
            s.lunch_end_minutes,
            s.end_minutes,
        ) = (535, -1, -1, 536)
    });
    let f = fields(&one);
    assert_eq!(f.end, "0.02");
    assert_eq!(
        read_fields(&one, EndMode::Duration, &f)
            .unwrap()
            .end_minutes,
        536
    );
}

#[test]
fn settings_apply_on_focus_loss() {
    let mut e = Editor::new(settings(|s| {
        s.end_mode = EndMode::EndTime;
        s.hide_earned = true;
    }));
    e.edit(Field::Start, "08:30");
    assert_eq!(e.s.start_minutes, 510);
    e.edit(Field::End, "17:30");
    assert_eq!(e.s.end_minutes, 1050);
    e.edit(Field::Salary, "1234.56");
    assert_eq!(e.s.daily_salary, 1234.56);
    e.edit(Field::Salary, "1234.567");
    assert_eq!(e.s.daily_salary, 1234.57);
    e.edit(Field::LunchStart, "11:30");
    e.edit(Field::LunchEnd, "13:00");
    assert_eq!((e.s.lunch_start_minutes, e.s.lunch_end_minutes), (690, 780));
    e.mode(EndMode::Duration);
    e.edit(Field::End, "8.5");
    assert_eq!(e.s.end_minutes, 1110);
    assert!(e.s.hide_earned);
    // Closing the panel applies the final edit.
    e.focus(Some(Field::LunchEnd));
    e.type_(Field::LunchEnd, "13:30");
    e.focus(None);
    assert_eq!(e.s.lunch_end_minutes, 810);
}

#[test]
fn invalid_edits_revert_without_changing_saved_settings() {
    let base = settings(|s| s.end_mode = EndMode::EndTime);
    let valid = ["09:00", "18:00", "500", "12:00", "13:30"];
    let order = [
        Field::Start,
        Field::End,
        Field::Salary,
        Field::LunchStart,
        Field::LunchEnd,
    ];
    let cases: [&[&str]; 5] = [
        &[
            "",
            "25:00",
            "18:00",
            "09:00x",
            "999999999999999:00",
            "０９:00",
        ],
        &["", "09:00", "09:30", "23:60", "10:00x"],
        &[
            "", "abc", "nan", "inf", "1e9999", "-1", "1000001", "500x", "1.2.3", " 500",
        ],
        &[
            "-1", "60.5", "24:00", "08:59", "13:30", "14:00", "1:00", "12:0", "12:00x",
        ],
        &["12:00", "11:00", "18:01", "24:00", "13:60", "13:3"],
    ];
    for (i, texts) in cases.iter().enumerate() {
        for text in *texts {
            let mut e = Editor::new(base.clone());
            e.edit(order[i], text);
            assert_eq!(e.f.get(order[i]), valid[i], "{:?} {text}", order[i]);
            assert_eq!(e.s, base, "{:?} {text}", order[i]);
            assert_eq!(e.saves, 0);
        }
    }
    let mut e = Editor::new(base.clone());
    e.mode(EndMode::Duration);
    let saved = e.s.clone();
    for text in ["", "0", "24", "0.001", "nan", "1e9999", "1", "8x"] {
        e.edit(Field::End, text);
        assert_eq!(e.f.end, "7.5", "{text}");
        assert_eq!(e.s, saved);
    }
}

#[test]
fn overnight_lunch_can_be_entered_as_a_pair() {
    for first in [Field::LunchStart, Field::LunchEnd] {
        let second = if first == Field::LunchStart {
            Field::LunchEnd
        } else {
            Field::LunchStart
        };
        let text = |f| {
            if f == Field::LunchStart {
                "23:30"
            } else {
                "00:30"
            }
        };
        let mut e = Editor::new(settings(|s| {
            (s.start_minutes, s.end_minutes, s.end_mode) = (1320, 360, EndMode::EndTime);
            (s.lunch_start_minutes, s.lunch_end_minutes) = (-1, -1);
        }));
        e.focus(Some(first));
        e.type_(first, text(first));
        e.live(first);
        assert_eq!(e.s.lunch_start_minutes, -1);
        e.focus(Some(second));
        assert_eq!(e.f.get(first), text(first));
        e.type_(second, text(second));
        e.live(second);
        assert_eq!((e.s.lunch_start_minutes, e.s.lunch_end_minutes), (1410, 30));
        e.focus(None);
        assert_eq!((e.s.start_minutes, e.s.end_minutes), (1320, 360));
        // Leaving an incomplete pair restores both saved endpoints.
        e.focus(Some(Field::LunchStart));
        e.type_(Field::LunchStart, "23:");
        e.focus(Some(Field::LunchEnd));
        e.focus(Some(Field::Salary));
        assert!(e.f.lunch_start == "23:30" && e.f.lunch_end == "00:30");
        e.focus(Some(Field::LunchStart));
        e.type_(Field::LunchStart, "23:");
        e.focus(Some(Field::LunchEnd));
        e.type_(Field::LunchEnd, "08:00");
        e.focus(Some(Field::Salary));
        assert!(e.f.lunch_start == "23:30" && e.f.lunch_end == "00:30");
        assert_eq!(e.s.lunch_start_minutes, 1410);
        e.focus(None);
    }
}

#[test]
fn duration_ocr_preserves_net_hours() {
    let mut e = Editor::new(settings(|s| s.lunch_end_minutes = 780));
    e.ocr(535);
    assert!(e.s.end_minutes == 1075 && e.f.end == "8");
    let mut e = Editor::new(settings(|s| {
        (
            s.start_minutes,
            s.end_minutes,
            s.lunch_start_minutes,
            s.lunch_end_minutes,
        ) = (1320, 360, 1410, 30)
    }));
    assert_eq!(e.f.end, "7");
    e.ocr(1315);
    assert!(e.s.end_minutes == 355 && e.f.end == "7");
    e.type_(Field::LunchEnd, "01:00");
    e.live(Field::LunchEnd);
    assert!(e.s.end_minutes == 385 && e.f.end == "7");
}

#[test]
fn ocr_silently_updates_and_conflicts_keep_the_schedule() {
    let mut e = Editor::new(settings(|s| s.end_mode = EndMode::EndTime));
    e.ocr(535);
    assert!(e.s.start_minutes == 535 && e.s.end_minutes == 1080 && e.f.start == "08:55");
    assert!(e.error.is_none());
    let mut e = Editor::new(Settings::default());
    assert_eq!(e.f.end, "7.5");
    e.ocr(535);
    assert!(e.s.start_minutes == 535 && e.s.end_minutes == 1075 && e.f.end == "7.5");
    let mut fixed = e.s.clone();
    fixed.end_mode = EndMode::EndTime;
    let mut e = Editor::new(fixed);
    let end = e.s.end_minutes;
    e.ocr(end); // Equal start and end.
    assert_eq!(e.s.start_minutes, 535);
    assert_eq!(e.error, Some(OCR_CONFLICT_MESSAGE));
    e.ocr(1440);
    assert_eq!(e.s.start_minutes, 535);
    e.ocr(13 * 60); // Lunch no longer fits.
    assert_eq!(e.s.start_minutes, 535);
}

#[test]
fn end_time_edits_apply_before_ocr_repopulates_fields() {
    let mut e = Editor::new(settings(|s| {
        s.end_mode = EndMode::EndTime;
        s.end_minutes = 1130;
    }));
    e.focus(Some(Field::End));
    e.type_(Field::End, "19:10");
    e.live(Field::End); // Moving the caret must not require blur to save.
    assert_eq!(e.s.end_minutes, 1150);
    e.ocr(532);
    assert!(
        e.s.start_minutes == 532
            && e.s.end_minutes == 1150
            && e.f.start == "08:52"
            && e.f.end == "19:10"
    );
    let w = Shift {
        salary: 0.,
        ..e.s.shift()
    }
    .at(13 * 3600 + 27 * 60 + 53)
    .unwrap();
    assert_eq!(w.remaining, 20527);
    e.focus(Some(Field::End));
    e.type_(Field::End, "18:5");
    e.live(Field::End);
    assert_eq!(e.s.end_minutes, 1150);
    e.type_(Field::End, "18:50");
    e.live(Field::End);
    assert_eq!(e.s.end_minutes, 1130);
    e.type_(Field::End, "18:5"); // Backspace: partial input never saves.
    e.live(Field::End);
    assert_eq!(e.s.end_minutes, 1130);
    for text in ["08:52", "10:00", "25:00"] {
        e.type_(Field::End, text);
        e.live(Field::End);
        assert_eq!(e.s.end_minutes, 1130, "{text}");
    }
    e.focus(None);
    assert_eq!(e.f.end, "18:50");

    e.mode(EndMode::Duration);
    e.focus(Some(Field::End));
    e.type_(Field::End, "8.5");
    e.live(Field::End);
    assert_eq!(e.s.end_minutes, 1132);
    e.ocr(535);
    assert!(e.s.end_minutes == 1135 && e.f.end == "8.5");
    e.type_(Field::End, "8.");
    e.live(Field::End);
    assert_eq!(e.s.end_minutes, 1135); // "8." is incomplete.
    e.type_(Field::End, "8");
    e.live(Field::End);
    assert_eq!(e.s.end_minutes, 1105);
    e.focus(None);

    let mut e = Editor::new(settings(|s| {
        (
            s.start_minutes,
            s.end_minutes,
            s.lunch_start_minutes,
            s.lunch_end_minutes,
        ) = (1320, 360, -1, -1)
    }));
    e.type_(Field::End, "9");
    e.live(Field::End);
    assert_eq!(e.s.end_minutes, 420);
    e.ocr(1380);
    assert!(e.s.start_minutes == 1380 && e.s.end_minutes == 480 && e.f.end == "9");
    let mut fixed = e.s.clone();
    (fixed.start_minutes, fixed.end_minutes, fixed.end_mode) = (1320, 360, EndMode::EndTime);
    let mut e = Editor::new(fixed);
    e.type_(Field::End, "7:30"); // The time parser accepts a single hour digit.
    e.live(Field::End);
    assert_eq!(e.s.end_minutes, 450);
    e.ocr(1380);
    assert!(e.s.end_minutes == 450 && e.f.end == "07:30");
}

#[test]
fn resignation_editor_persists_independently() {
    let mut e = Editor::new(Settings::default());
    e.focus(Some(Field::Resignation));
    e.type_(Field::Resignation, "2026-11-30");
    e.focus(Some(Field::LunchStart)); // Must not be mistaken for a lunch endpoint.
    assert_eq!(e.s.resignation_date, "2026-11-30");
    assert!(!e.s.show_resignation);
    assert_eq!((e.s.lunch_start_minutes, e.s.lunch_end_minutes), (720, 810));
    e.focus(None);
    e.edit(Field::Resignation, "2026-02-29");
    assert_eq!(e.error, Some(RESIGNATION_MESSAGE));
    assert!(e.f.resignation == "2026-11-30" && e.s.resignation_date == "2026-11-30");
    e.edit(Field::Resignation, "");
    assert!(e.error.is_none() && e.s.resignation_date.is_empty());
    assert!(e.s.end_minutes == 1080 && e.s.lunch_end_minutes == 810);
}

#[test]
fn main_block_live_lunch_and_mode_switch() {
    let mut e = Editor::new(Settings::default());
    assert!(e.f.lunch_start == "12:00" && e.f.lunch_end == "13:30");
    assert!(read_fields(&e.s, e.s.end_mode, &e.f).is_some());
    assert_eq!(e.f.end, "7.5");
    e.type_(Field::End, "8.5");
    assert_eq!(
        read_fields(&e.s, EndMode::Duration, &e.f)
            .unwrap()
            .end_minutes,
        1140
    );
    e.mode(EndMode::EndTime);
    assert_eq!(e.f.end, "19:00");
    e.focus(Some(Field::LunchStart));
    e.type_(Field::LunchStart, "11:45");
    e.live(Field::LunchStart);
    assert_eq!(e.s.lunch_start_minutes, 705);
    e.type_(Field::LunchEnd, "13:0");
    e.live(Field::LunchEnd);
    assert_eq!(e.s.lunch_end_minutes, 810); // Partial edit never persists.
    e.type_(Field::LunchEnd, "13:00");
    e.live(Field::LunchEnd);
    assert!(e.s.lunch_end_minutes == 780 && e.s.end_minutes == 1140);
    e.type_(Field::LunchEnd, "11:30");
    e.live(Field::LunchEnd);
    assert_eq!(e.s.lunch_end_minutes, 780);
    e.focus(None);
    // Legacy zero durations remain editable: clear and then restore lunch.
    e.type_(Field::LunchStart, "");
    e.type_(Field::LunchEnd, "");
    e.commit(Field::LunchStart);
    assert_eq!((e.s.lunch_start_minutes, e.s.lunch_end_minutes), (-1, -1));
    assert!(e.f.lunch_start.is_empty() && e.f.lunch_end.is_empty());
    e.type_(Field::LunchStart, "12:00");
    e.type_(Field::LunchEnd, "13:00");
    e.commit(Field::LunchEnd);
    assert_eq!((e.s.lunch_start_minutes, e.s.lunch_end_minutes), (720, 780));
    // Unchanged text is not a save.
    let saves = e.saves;
    e.edit(Field::Salary, "500");
    assert_eq!(e.saves, saves);
}

#[test]
fn formatting_matches_the_cpp_editor() {
    assert_eq!(format_time(535), "08:55");
    assert_eq!(format_salary(500.0), "500");
    assert_eq!(format_salary(1234.5), "1234.5");
    assert_eq!(format_salary(0.0), "0");
    assert_eq!(format_salary(1234.567), "1234.57");
    assert_eq!(format_duration_hours(450), "7.5");
    assert_eq!(format_duration_hours(1), "0.02");
    assert_eq!(parse_time("7:30"), Some(450));
    assert_eq!(parse_time("07:3"), Some(423));
    assert_eq!(parse_time("123:00"), None);
    assert_eq!(parse_lunch_time("7:30"), None);
}
