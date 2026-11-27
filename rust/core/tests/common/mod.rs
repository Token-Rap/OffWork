//! Helpers that stand in for `WritePrivateProfileStringW` and the C++
//! `SaveSettings`, so the old INI tests can run on every OS.
#![allow(dead_code)]
use offwork_core::legacy::{self, IniText};
use offwork_core::settings::{EndMode, Settings};

/// Ordered INI document edited like `WritePrivateProfileStringW`.
#[derive(Clone, Default)]
pub struct Ini {
    sections: Vec<(String, Vec<(String, String)>)>,
}

impl Ini {
    pub fn set(&mut self, section: &str, key: &str, value: Option<&str>) {
        let index = match self
            .sections
            .iter()
            .position(|(s, _)| s.eq_ignore_ascii_case(section))
        {
            Some(i) => i,
            None if value.is_none() => return,
            None => {
                self.sections.push((section.into(), Vec::new()));
                self.sections.len() - 1
            }
        };
        let entries = &mut self.sections[index].1;
        let found = entries
            .iter()
            .position(|(k, _)| k.eq_ignore_ascii_case(key));
        match (found, value) {
            (Some(i), Some(v)) => entries[i].1 = v.into(),
            (Some(i), None) => {
                entries.remove(i);
            }
            (None, Some(v)) => entries.push((key.into(), v.into())),
            (None, None) => {}
        }
    }
    pub fn text(&self) -> String {
        let mut out = String::new();
        for (name, entries) in &self.sections {
            out += &format!("[{name}]\r\n");
            for (k, v) in entries {
                out += &format!("{k}={v}\r\n");
            }
        }
        out
    }
    /// Exactly what the C++ `SaveSettings` wrote (keys in its order).
    pub fn save(&mut self, s: &Settings) {
        let n = s.normalized();
        let b = |v: bool| Some(if v { "1" } else { "0" });
        self.set("resignation", "date", Some(&n.resignation_date));
        self.set("resignation", "show", b(n.show_resignation));
        self.set("window", "hideEarned", b(n.hide_earned));
        self.set("work", "startMinutes", Some(&n.start_minutes.to_string()));
        self.set("work", "endMinutes", Some(&n.end_minutes.to_string()));
        let mode = if n.end_mode == EndMode::Duration {
            "duration"
        } else {
            "endTime"
        };
        self.set("work", "endInputMode", Some(mode));
        self.set(
            "work",
            "lunchStartMinutes",
            Some(&n.lunch_start_minutes.to_string()),
        );
        self.set(
            "work",
            "lunchEndMinutes",
            Some(&n.lunch_end_minutes.to_string()),
        );
        self.set("work", "lunchDurationMinutes", None);
        self.set("work", "deductLunch", b(n.deduct_lunch));
        self.set(
            "work",
            "dailySalary",
            Some(&format!("{:.2}", n.daily_salary)),
        );
        self.set("window", "scalePercent", Some(&n.scale_percent.to_string()));
        self.set(
            "window",
            "opacityPercent",
            Some(&n.opacity_percent.to_string()),
        );
    }
    /// `LoadSettings`: the portable reader, cross-checked against the real
    /// Win32 profile API on Windows.
    pub fn load(&self) -> Settings {
        load_bytes(self.text().as_bytes())
    }
}

pub fn load_bytes(bytes: &[u8]) -> Settings {
    let portable = legacy::load(&IniText::parse(bytes));
    #[cfg(windows)]
    {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("settings.ini");
        std::fs::write(&path, bytes).unwrap();
        let native = legacy::load(&legacy::win32::Win32Profile::new(&path));
        assert_eq!(
            portable,
            native,
            "IniText disagrees with GetPrivateProfile*W for {:?}",
            String::from_utf8_lossy(bytes)
        );
    }
    portable
}
