//! Typed commands the UI calls. The UI never writes settings directly: it
//! sends field drafts and the core edit rules decide what is saved.

use crate::model::{Autostart, Model, VERSION};
use crate::updater::{self, UpdateView};
use crate::{autostart, calendar, tray, window};
use chrono::{DateTime, Datelike, Local, TimeZone, Timelike};
use offwork_core::edit::{self, Commit, Field, Fields};
use offwork_core::resignation::remaining_workdays;
use offwork_core::{EndMode, Phase, Settings};
use serde::{Deserialize, Serialize};
use std::sync::atomic::Ordering;
use tauri::{AppHandle, Emitter, Manager, State, WebviewWindow};

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct View {
    settings: Settings,
    fields: Fields,
    notice: String,
    error: Option<String>,
    pin_supported: bool,
    pinned: bool,
    tray: bool,
    autostart: Autostart,
    version: &'static str,
    update: UpdateView,
}

pub fn view_with(app: &AppHandle, error: Option<String>, autostart: Option<Autostart>) -> View {
    let model = app.state::<Model>();
    let settings = model.current();
    let notice = model.notice.lock().unwrap().clone();
    View {
        fields: edit::fields(&settings),
        settings,
        notice,
        error,
        pin_supported: pin_supported(),
        pinned: model.pinned.load(Ordering::SeqCst),
        tray: model.tray.load(Ordering::SeqCst),
        autostart: autostart.unwrap_or_else(|| autostart::state(None)),
        version: VERSION,
        update: updater::view(app),
    }
}

pub fn view(app: &AppHandle, error: Option<String>) -> View {
    view_with(app, error, None)
}

/// Wayland compositors refuse always-on-top and absolute placement.
pub fn pin_supported() -> bool {
    !(cfg!(target_os = "linux")
        && std::env::var("XDG_SESSION_TYPE").is_ok_and(|s| s.eq_ignore_ascii_case("wayland")))
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Tick {
    phase: Phase,
    remaining: i32,
    countdown: i32,
    on_lunch: bool,
    progress: f64,
    earned: f64,
    end_minutes: i32,
    days: Option<i64>,
    date: String,
    time: String,
}

#[tauri::command]
pub fn state(app: AppHandle) -> View {
    view(&app, None)
}

fn tick_at(s: &Settings, now: DateTime<Local>) -> Result<Tick, String> {
    let w = s
        .workday(now.num_seconds_from_midnight() as i32)
        .map_err(|_| edit::VALIDATION_MESSAGE.to_owned())?;
    let weekday = "一二三四五六日"
        .chars()
        .nth(now.weekday().num_days_from_monday() as usize)
        .unwrap_or('一');
    Ok(Tick {
        phase: w.phase,
        remaining: w.remaining,
        countdown: w.countdown,
        on_lunch: w.on_lunch,
        progress: w.progress,
        earned: w.earned,
        end_minutes: s.end_minutes,
        days: remaining_workdays(now.date_naive(), &s.resignation_date, s.show_resignation),
        date: format!("{}月{}日 周{weekday}", now.month(), now.day()),
        time: now.format("%H:%M:%S").to_string(),
    })
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Ticks {
    /// Unix milliseconds of `ticks[0]` (a whole second).
    start_ms: i64,
    ticks: Vec<Tick>,
}

/// The next `count` seconds of the clock, computed by the core rules for
/// each local second (DST and midnight included). The widget plays them
/// back and asks again about once a minute, instead of one IPC round trip
/// per second, which kept WebView2 allocating while it ran for hours.
fn ticks_from(s: &Settings, now: DateTime<Local>, count: u32) -> Result<Ticks, String> {
    let start = now.timestamp();
    let ticks = (0..i64::from(count.clamp(1, 120)))
        .filter_map(|i| Local.timestamp_opt(start + i, 0).single())
        .map(|t| tick_at(s, t))
        .collect::<Result<Vec<_>, _>>()?;
    Ok(Ticks {
        start_ms: start * 1000,
        ticks,
    })
}

#[tauri::command]
pub fn ticks(model: State<Model>, count: u32) -> Result<Ticks, String> {
    ticks_from(&model.current(), Local::now(), count)
}

/// Runs one serialized edit and returns the new view (with any error).
fn apply<F: FnOnce(&Settings) -> Commit>(app: &AppHandle, edit: F) -> View {
    let error = app.state::<Model>().update(edit).err();
    view(app, error)
}

/// Blur / Enter on one field.
#[tauri::command]
pub fn commit_field(app: AppHandle, field: Field, fields: Fields) -> View {
    apply(&app, |s| edit::commit(s, field, &fields))
}

/// A keystroke in the end or lunch fields: saves only complete, valid input.
#[tauri::command]
pub fn live_field(app: AppHandle, field: Field, fields: Fields) -> Option<View> {
    let result = app
        .state::<Model>()
        .update(|s| match edit::live(s, field, &fields) {
            Some(next) => Commit::Saved(next),
            None => Commit::Unchanged,
        });
    match result {
        Ok(None) => None,
        Ok(Some(_)) => Some(view(&app, None)),
        Err(e) => Some(view(&app, Some(e))),
    }
}

#[tauri::command]
pub fn set_end_mode(app: AppHandle, mode: EndMode, fields: Fields) -> View {
    apply(&app, |s| edit::switch_end_mode(s, mode, &fields))
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum Toggle {
    HideEarned,
    DeductLunch,
    ShowResignation,
}

/// Display toggles save immediately and never reset the reminder.
#[tauri::command]
pub fn toggle(app: AppHandle, flag: Toggle) -> View {
    apply(&app, |current| {
        let mut s = current.clone();
        match flag {
            Toggle::HideEarned => s.hide_earned = !s.hide_earned,
            Toggle::DeductLunch => s.deduct_lunch = !s.deduct_lunch,
            Toggle::ShowResignation => s.show_resignation = !s.show_resignation,
        }
        Commit::Saved(s)
    })
}

/// Scale 75–150 % and opacity 25–100 %. `persist` is false while dragging.
#[tauri::command]
pub fn set_window(
    app: AppHandle,
    window: WebviewWindow,
    scale: i32,
    opacity: i32,
    persist: bool,
) -> View {
    let model = app.state::<Model>();
    let with_prefs = |s: &Settings| {
        Settings {
            scale_percent: scale,
            opacity_percent: opacity,
            ..s.clone()
        }
        .normalized()
    };
    let next = model.preview(with_prefs);
    window::fit(
        &window,
        next.scale_percent,
        model.expanded.load(Ordering::SeqCst),
    );
    if persist {
        apply(&app, |s| Commit::Saved(with_prefs(s)))
    } else {
        view(&app, None)
    }
}

#[tauri::command]
pub fn set_expanded(app: AppHandle, window: WebviewWindow, expanded: bool) {
    let model = app.state::<Model>();
    model.expanded.store(expanded, Ordering::SeqCst);
    if !expanded {
        calendar::close(&app);
    }
    window::fit(&window, model.current().scale_percent, expanded);
}

#[tauri::command]
pub fn set_pinned(app: AppHandle, window: WebviewWindow, pinned: bool) -> View {
    let ok = pin_supported() && window.set_always_on_top(pinned).is_ok();
    app.state::<Model>()
        .pinned
        .store(pinned && ok, Ordering::SeqCst);
    view(
        &app,
        (!ok && pinned).then(|| "当前桌面不支持置顶，已使用普通窗口".to_owned()),
    )
}

#[tauri::command]
pub fn set_autostart(app: AppHandle, enabled: bool) -> View {
    let state = autostart::set(enabled);
    tray::sync(&app, state.enabled);
    view_with(&app, state.error.clone(), Some(state))
}

fn measure_dir() -> Option<std::path::PathBuf> {
    std::env::var_os("OFFWORK_MEASURE").map(std::path::PathBuf::from)
}

/// Measurement hook: records that a step happened (`<dir>/<name>`).
pub fn measure_mark(name: &str) {
    if let Some(dir) = measure_dir() {
        let _ = std::fs::write(dir.join(name), []);
    }
}

/// Measurement hooks for scripts/measure.py and the smoke tests
/// (OFFWORK_MEASURE=<dir>): `frame` after the first paint; touching
/// `open-calendar` opens the calendar through the real UI path and
/// `cancel-calendar` cancels it from inside the popup; `layout-check` makes
/// the widget expand and report its layout (see `measure_layout`); touching `hide`
/// hides the widget and writes `hidden` once the main loop has done it.
#[tauri::command]
pub fn first_frame(app: AppHandle) {
    let Some(dir) = measure_dir() else {
        return;
    };
    if std::fs::write(dir.join("frame"), []).is_err() {
        return;
    }
    std::thread::spawn(move || {
        let (mut opened, mut cancelled, mut checked) = (false, false, false);
        loop {
            if !opened && dir.join("open-calendar").exists() {
                opened = true;
                let _ = app.emit_to("main", "measure-open-calendar", ());
            }
            if !cancelled && dir.join("cancel-calendar").exists() {
                cancelled = true;
                let _ = app.emit_to(calendar::LABEL, "measure-cancel-calendar", ());
            }
            if !checked && dir.join("layout-check").exists() {
                checked = true;
                let _ = app.emit_to("main", "measure-layout-check", ());
            }
            if dir.join("hide").exists() {
                // The same path as × and the tray, including web view suspend.
                let hidden = window::hide(&app);
                let invisible = app
                    .get_webview_window("main")
                    .is_some_and(|w| matches!(w.is_visible(), Ok(false)));
                if hidden && invisible {
                    let _ = std::fs::write(dir.join("hidden"), []);
                }
                break;
            }
            std::thread::sleep(std::time::Duration::from_millis(100));
        }
    });
}

/// Layout report from the widget (OFFWORK_MEASURE only): what the page saw,
/// plus the real window and work area in physical pixels, written to
/// `<dir>/layout.json` for scripts/layout_smoke.py.
#[tauri::command]
pub fn measure_layout(window: WebviewWindow, report: serde_json::Value) {
    let Some(dir) = measure_dir() else {
        return;
    };
    let position = window.outer_position().ok();
    let size = window.outer_size().ok();
    let work = window::work_area(&window);
    let full = serde_json::json!({
        "page": report,
        "scale_factor": window.scale_factor().ok(),
        "window": position.zip(size).map(|(p, s)| serde_json::json!({"x": p.x, "y": p.y, "w": s.width, "h": s.height})),
        "work_area": work.map(|r| serde_json::json!({"x": r.x, "y": r.y, "w": r.w, "h": r.h})),
    });
    let _ = std::fs::write(dir.join("layout.json"), full.to_string());
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_batch_is_consecutive_seconds_from_the_core_rules() {
        let s = Settings {
            resignation_date: "2026-11-30".into(),
            show_resignation: true,
            ..Settings::default()
        };
        // Thursday 2026-11-26 17:59:30 local → crosses the 18:00 end.
        let now = Local.with_ymd_and_hms(2026, 11, 26, 17, 59, 30).unwrap();
        let batch = ticks_from(&s, now, 60).unwrap();
        assert_eq!(batch.ticks.len(), 60);
        assert_eq!(batch.start_ms, now.timestamp() * 1000);
        assert_eq!(batch.ticks[0].time, "17:59:30");
        assert_eq!(batch.ticks[0].remaining, 30);
        assert_eq!(batch.ticks[29].remaining, 1);
        assert_eq!(batch.ticks[30].phase, offwork_core::Phase::After);
        assert_eq!(batch.ticks[30].time, "18:00:00");
        assert_eq!(batch.ticks[0].days, Some(2));
        // Across midnight the date and the workday count change in the batch.
        let late = Local.with_ymd_and_hms(2026, 11, 26, 23, 59, 50).unwrap();
        let batch = ticks_from(&s, late, 20).unwrap();
        assert_eq!(batch.ticks[0].date, "11月26日 周四");
        assert_eq!(batch.ticks[10].date, "11月27日 周五");
        assert_eq!(batch.ticks[10].days, Some(1));
        assert_eq!(ticks_from(&s, late, 0).unwrap().ticks.len(), 1);
        assert_eq!(ticks_from(&s, late, 9999).unwrap().ticks.len(), 120);
    }
}
