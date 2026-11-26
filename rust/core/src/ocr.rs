use regex::Regex;
use std::sync::LazyLock;

static TOKEN: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"[0-9]+(:[0-9]+)+").unwrap());
static SPACE: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"([0-9])\s+([0-9])").unwrap());
pub fn clock_in(text: &str) -> Option<i32> {
    let mut times = Vec::new();
    let mut labels = Vec::new();
    let mut schedule_next = false;
    for (row, raw) in text.lines().enumerate() {
        let normalized: String = raw
            .chars()
            .map(|c| match c {
                '０'..='９' => char::from_u32(c as u32 - '０' as u32 + 48).unwrap(),
                '：' => ':',
                _ => c,
            })
            .collect();
        // Preserve gaps between complete times while joining split clock digits.
        let spaced = SPACE.replace_all(&normalized, "$1|$2");
        let compact: String = spaced.chars().filter(|c| !c.is_whitespace()).collect();
        let mut line = String::new();
        let chars: Vec<_> = compact.chars().collect();
        for (i, c) in chars.iter().enumerate() {
            if *c == '|' {
                let split_hour =
                    chars.get(i + 2) == Some(&':') && (i < 2 || !chars[i - 2].is_ascii_digit());
                let split_minute = i >= 2
                    && chars[i - 2] == ':'
                    && chars.get(i + 2).is_none_or(|c| !c.is_ascii_digit());
                if !split_hour && !split_minute {
                    line.push(' ')
                }
            } else {
                line.push(*c)
            }
        }
        let schedule = line.contains("班次") || line.contains("应上班");
        let excluded = schedule
            || (schedule_next
                && !["上班打卡", "打卡成功", "打卡•成功"]
                    .iter()
                    .any(|s| line.contains(s)));
        schedule_next = schedule && !TOKEN.is_match(&line);
        if excluded {
            continue;
        }
        for token in TOKEN.find_iter(&line) {
            let fields: Vec<_> = token.as_str().split(':').collect();
            if fields.len() > 3 || fields.iter().any(|s| s.len() != 2) {
                continue;
            }
            let values: Vec<i32> = fields.iter().map(|s| s.parse().unwrap()).collect();
            if values[0] < 24 && values[1] < 60 && values.get(2).copied().unwrap_or(0) < 60 {
                times.push((
                    row,
                    line[..token.start()].chars().count(),
                    line[..token.end()].chars().count(),
                    values[0] * 60 + values[1],
                ));
            }
        }
        for (label, rank) in [
            ("上班打卡", 0),
            ("打卡成功", 1),
            ("打卡•成功", 1),
            ("打卡·成功", 1),
            ("上班", 2),
            ("打卡", 3),
        ] {
            for (pos, _) in line.match_indices(label) {
                if !line[..pos].ends_with("下班") {
                    let start = line[..pos].chars().count();
                    labels.push((row, start, start + label.chars().count(), rank));
                }
            }
        }
    }
    let mut best = None;
    for (row, start, end, rank) in labels {
        let same = times.iter().any(|t| t.0 == row);
        for &(r, a, b, time) in &times {
            let distance = if r == row {
                start.saturating_sub(b).max(a.saturating_sub(end))
            } else if !same && r.abs_diff(row) == 1 {
                40
            } else {
                continue;
            };
            let score = rank * 1000 + distance;
            if distance <= 40 && best.is_none_or(|(old, _)| score < old) {
                best = Some((score, time))
            }
        }
    }
    best.map(|(_, t)| t)
        .or_else(|| times.iter().map(|t| t.3).min())
}
