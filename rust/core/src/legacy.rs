//! Reads the C++ app's `settings.ini` exactly as `src/settings.cpp` did.
//!
//! [`Profile`] abstracts `GetPrivateProfileStringW`/`GetPrivateProfileIntW`.
//! [`IniText`] reimplements their rules for every OS (and for tests);
//! on Windows [`win32::Win32Profile`] calls the real API, and CI checks that
//! both agree on the same fixtures.

use crate::settings::{EndMode, Settings};

pub trait Profile {
    /// `None` when the key is absent; the value is truncated to `capacity - 1`
    /// UTF-16 units like a fixed `wchar_t` buffer.
    fn string(&self, section: &str, key: &str, capacity: usize) -> Option<String>;
    /// `GetPrivateProfileIntW`, cast to `int` like the C++ callers did.
    fn int(&self, section: &str, key: &str, default: i32) -> i32;
}

/// In-memory INI with Win32 profile semantics: case-insensitive section and
/// key names, first match wins, whitespace trimmed, one pair of matching
/// quotes stripped, `;` comment lines, UTF-16 BOMs honoured (UTF-8 BOMs are
/// not, exactly like Windows).
#[derive(Clone, Debug, Default)]
pub struct IniText {
    sections: Vec<(String, Vec<(String, String)>)>,
}

fn decode(bytes: &[u8]) -> String {
    let utf16 = |be: bool| {
        let units: Vec<u16> = bytes[2..]
            .chunks_exact(2)
            .map(|c| {
                if be {
                    u16::from_be_bytes([c[0], c[1]])
                } else {
                    u16::from_le_bytes([c[0], c[1]])
                }
            })
            .collect();
        String::from_utf16_lossy(&units)
    };
    match bytes {
        [0xff, 0xfe, ..] => utf16(false),
        [0xfe, 0xff, ..] => utf16(true),
        // ANSI code page; every value OffWork wrote is ASCII. A UTF-8 BOM is
        // NOT honoured by the profile API (verified against Windows in CI):
        // it stays glued to the first line, so a leading section is unseen.
        _ => String::from_utf8_lossy(bytes).into_owned(),
    }
}

fn unquote(value: &str) -> &str {
    let b = value.as_bytes();
    if b.len() >= 2 && (b[0] == b'"' || b[0] == b'\'') && b[b.len() - 1] == b[0] {
        &value[1..value.len() - 1]
    } else {
        value
    }
}

fn truncate_utf16(text: &str, capacity: usize) -> String {
    let mut units = 0;
    text.chars()
        .take_while(|c| {
            units += c.len_utf16();
            units < capacity
        })
        .collect()
}

impl IniText {
    pub fn parse(bytes: &[u8]) -> IniText {
        let text = decode(bytes);
        let mut ini = IniText::default();
        for raw in text.split(['\n', '\r']) {
            let line = raw.trim_matches([' ', '\t']);
            if line.is_empty() || line.starts_with(';') {
                continue;
            }
            if let Some(rest) = line.strip_prefix('[') {
                let name = rest
                    .split(']')
                    .next()
                    .unwrap_or(rest)
                    .trim_matches([' ', '\t']);
                ini.sections.push((name.to_owned(), Vec::new()));
                continue;
            }
            let Some((_, entries)) = ini.sections.last_mut() else {
                continue;
            };
            let (key, value) = line.split_once('=').unwrap_or((line, ""));
            entries.push((
                key.trim_matches([' ', '\t']).to_owned(),
                unquote(value.trim_matches([' ', '\t'])).to_owned(),
            ));
        }
        ini
    }

    fn find(&self, section: &str, key: &str) -> Option<&str> {
        self.sections
            .iter()
            .filter(|(name, _)| name.eq_ignore_ascii_case(section))
            .flat_map(|(_, entries)| entries)
            .find(|(k, _)| k.eq_ignore_ascii_case(key))
            .map(|(_, v)| v.as_str())
    }
}

/// `RtlUnicodeStringToInteger(base 10)`: leading blanks, a sign, then digits
/// up to the first non-digit, wrapping at 32 bits.
fn profile_int(text: &str) -> i32 {
    let mut chars = text
        .trim_start_matches(|c: char| c <= ' ')
        .chars()
        .peekable();
    let negative = match chars.peek() {
        Some('-') => {
            chars.next();
            true
        }
        Some('+') => {
            chars.next();
            false
        }
        _ => false,
    };
    let mut value: u32 = 0;
    for c in chars {
        let Some(d) = c.to_digit(10) else { break };
        value = value.wrapping_mul(10).wrapping_add(d);
    }
    (if negative {
        value.wrapping_neg()
    } else {
        value
    }) as i32
}

impl Profile for IniText {
    fn string(&self, section: &str, key: &str, capacity: usize) -> Option<String> {
        self.find(section, key).map(|v| truncate_utf16(v, capacity))
    }
    fn int(&self, section: &str, key: &str, default: i32) -> i32 {
        match self.string(section, key, 31) {
            Some(v) if !v.is_empty() => profile_int(&v),
            _ => default,
        }
    }
}

/// Presence separately from validity: an empty or invalid interval key must
/// not resurrect a legacy duration. Strict digits, below 1440, else -1.
fn read_minutes<P: Profile + ?Sized>(p: &P, key: &str) -> Option<i32> {
    let text = p.string("work", key, 64)?;
    if text.is_empty() || text.encode_utf16().count() >= 63 {
        return Some(-1);
    }
    let mut value = 0;
    for b in text.bytes() {
        if !b.is_ascii_digit() {
            return Some(-1);
        }
        value = value * 10 + i32::from(b - b'0');
        if value >= 1440 {
            return Some(-1);
        }
    }
    Some(value)
}

/// `wcstod` prefix: blanks, sign, digits, fraction, exponent, `inf`/`nan`
/// (rejected later as non-finite) and hexadecimal integers.
fn wcstod_prefix(text: &str) -> Option<f64> {
    let s = text.trim_start_matches([' ', '\t', '\n', '\r', '\x0b', '\x0c']);
    let (sign, body) = match s.as_bytes().first() {
        Some(b'-') => (-1.0, &s[1..]),
        Some(b'+') => (1.0, &s[1..]),
        _ => (1.0, s),
    };
    let lower = body.to_ascii_lowercase();
    if lower.starts_with("inf") || lower.starts_with("nan") {
        return Some(f64::NAN);
    }
    if let Some(hex) = lower.strip_prefix("0x") {
        let digits: String = hex.chars().take_while(char::is_ascii_hexdigit).collect();
        if !digits.is_empty() {
            return Some(
                sign * u128::from_str_radix(&digits, 16).map_or(f64::INFINITY, |v| v as f64),
            );
        }
    }
    let b = body.as_bytes();
    let mut end = 0;
    let int = b.iter().take_while(|c| c.is_ascii_digit()).count();
    end += int;
    let mut frac = 0;
    if b.get(end) == Some(&b'.') {
        frac = b[end + 1..]
            .iter()
            .take_while(|c| c.is_ascii_digit())
            .count();
        if int + frac > 0 {
            end += 1 + frac;
        }
    }
    if int + frac == 0 {
        return None;
    }
    if matches!(b.get(end), Some(b'e' | b'E')) {
        let mut e = end + 1;
        if matches!(b.get(e), Some(b'+' | b'-')) {
            e += 1;
        }
        let exp = b[e.min(b.len())..]
            .iter()
            .take_while(|c| c.is_ascii_digit())
            .count();
        if exp > 0 {
            end = e + exp;
        }
    }
    body[..end].parse::<f64>().ok().map(|v| sign * v)
}

/// `LoadSettings` from `src/settings.cpp`, in its exact order, then normalized.
pub fn load<P: Profile + ?Sized>(p: &P) -> Settings {
    let mut s = Settings {
        resignation_date: p.string("resignation", "date", 32).unwrap_or_default(),
        show_resignation: p.int("resignation", "show", 0) == 1,
        deduct_lunch: p.int("work", "deductLunch", 0) == 1,
        hide_earned: p.int("window", "hideEarned", 0) == 1,
        ..Settings::default()
    };
    s.start_minutes = p.int("work", "startMinutes", s.start_minutes);
    s.end_minutes = p.int("work", "endMinutes", s.end_minutes);
    s.scale_percent = p.int("window", "scalePercent", s.scale_percent);
    s.opacity_percent = p.int("window", "opacityPercent", s.opacity_percent);

    let lunch_start = read_minutes(p, "lunchStartMinutes");
    let lunch_end = read_minutes(p, "lunchEndMinutes");
    if lunch_start.is_some() || lunch_end.is_some() {
        s.lunch_start_minutes = lunch_start.unwrap_or(-1);
        s.lunch_end_minutes = lunch_end.unwrap_or(-1);
    } else if let Some(duration) = read_minutes(p, "lunchDurationMinutes") {
        (s.lunch_start_minutes, s.lunch_end_minutes) = if duration > 0 {
            (12 * 60, (12 * 60 + duration) % 1440)
        } else {
            (-1, -1)
        };
    }

    let mode = p
        .string("work", "endInputMode", 32)
        .unwrap_or_else(|| "duration".into());
    s.end_mode = if mode.eq_ignore_ascii_case("endTime") {
        EndMode::EndTime
    } else {
        EndMode::Duration
    };

    let salary = p
        .string("work", "dailySalary", 64)
        .unwrap_or_else(|| "500".into());
    if let Some(v) = wcstod_prefix(&salary).filter(|v| v.is_finite() && *v >= 0.0) {
        s.daily_salary = v;
    }
    s.normalized()
}

#[cfg(windows)]
pub mod win32 {
    //! The real profile API, for migration on Windows and for cross-checking
    //! [`super::IniText`] in CI.
    use super::Profile;
    use std::os::windows::ffi::OsStrExt;
    use std::path::Path;
    use windows_sys::Win32::System::WindowsProgramming::{
        GetPrivateProfileIntW, GetPrivateProfileStringW,
    };

    pub struct Win32Profile {
        path: Vec<u16>,
    }

    fn wide(s: &str) -> Vec<u16> {
        s.encode_utf16().chain(Some(0)).collect()
    }

    impl Win32Profile {
        pub fn new(path: &Path) -> Self {
            Self {
                path: path.as_os_str().encode_wide().chain(Some(0)).collect(),
            }
        }
        fn read(&self, section: &str, key: &str, default: &str, capacity: usize) -> (u32, String) {
            let mut buf = vec![0u16; capacity];
            let n = unsafe {
                GetPrivateProfileStringW(
                    wide(section).as_ptr(),
                    wide(key).as_ptr(),
                    wide(default).as_ptr(),
                    buf.as_mut_ptr(),
                    capacity as u32,
                    self.path.as_ptr(),
                )
            };
            (n, String::from_utf16_lossy(&buf[..n as usize]))
        }
    }

    impl Profile for Win32Profile {
        fn string(&self, section: &str, key: &str, capacity: usize) -> Option<String> {
            // The C++ presence trick: a sentinel default, then an empty default.
            let (_, value) = self.read(section, key, "missing", capacity);
            if value == "missing" && self.read(section, key, "", capacity).0 == 0 {
                return None;
            }
            Some(value)
        }
        fn int(&self, section: &str, key: &str, default: i32) -> i32 {
            unsafe {
                GetPrivateProfileIntW(
                    wide(section).as_ptr(),
                    wide(key).as_ptr(),
                    default,
                    self.path.as_ptr(),
                ) as i32
            }
        }
    }
}
