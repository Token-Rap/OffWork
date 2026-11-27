//! Typed commands the UI calls. The UI never writes settings directly: it
//! sends field drafts and the core edit rules decide what is saved.

use crate::model::{Autostart, Model, VERSION};
use crate::updater::{self, UpdateView};
use crate::{autostart, calendar, tray, window};
use chrono::{Datelike, Local, Timelike};
use offwork_core::edit::{self, Commit, Field, Fields};
use offwork_core::resignation::remaining_workdays;
use offwork_core::{EndMode, Phase, Settings};
use serde::{Deserialize, Serialize};
use std::sync::atomic::Ordering;
use tauri::{AppHandle, Manager, State, WebviewWindow};

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

#[tauri::command]
pub fn tick(model: State<Model>) -> Result<Tick, String> {
    let s = model.current();
    let now = Local::now();
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

fn apply(app: &AppHandle, outcome: Commit) -> View {
    let error = match outcome {
        Commit::Saved(next) => app.state::<Model>().persist(next).err(),
        Commit::Rejected(message) => Some(message.to_owned()),
        Commit::Unchanged => None,
    };
    view(app, error)
}

/// Blur / Enter on one field.
#[tauri::command]
pub fn commit_field(app: AppHandle, field: Field, fields: Fields) -> View {
    let current = app.state::<Model>().current();
    apply(&app, edit::commit(&current, field, &fields))
}

/// A keystroke in the end or lunch fields: saves only complete, valid input.
#[tauri::command]
pub fn live_field(app: AppHandle, field: Field, fields: Fields) -> Option<View> {
    let current = app.state::<Model>().current();
    edit::live(&current, field, &fields).map(|next| apply(&app, Commit::Saved(next)))
}

#[tauri::command]
pub fn set_end_mode(app: AppHandle, mode: EndMode, fields: Fields) -> View {
    let current = app.state::<Model>().current();
    apply(&app, edit::switch_end_mode(&current, mode, &fields))
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
    let mut s = app.state::<Model>().current();
    match flag {
        Toggle::HideEarned => s.hide_earned = !s.hide_earned,
        Toggle::DeductLunch => s.deduct_lunch = !s.deduct_lunch,
        Toggle::ShowResignation => s.show_resignation = !s.show_resignation,
    }
    apply(&app, Commit::Saved(s))
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
    let next = Settings {
        scale_percent: scale,
        opacity_percent: opacity,
        ..model.current()
    }
    .normalized();
    window::fit(
        &window,
        next.scale_percent,
        model.expanded.load(Ordering::SeqCst),
    );
    if persist {
        apply(&app, Commit::Saved(next))
    } else {
        *model.settings.lock().unwrap() = next;
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

/// Measurement hook for scripts/measure.py (OFFWORK_MEASURE=<dir>).
#[tauri::command]
pub fn first_frame(app: AppHandle) {
    let Some(dir) = std::env::var_os("OFFWORK_MEASURE").map(std::path::PathBuf::from) else {
        return;
    };
    if std::fs::write(dir.join("frame"), []).is_err() {
        return;
    }
    std::thread::spawn(move || loop {
        if dir.join("hide").exists() {
            if let Some(w) = app.get_webview_window("main") {
                if w.hide().is_ok() && matches!(w.is_visible(), Ok(false)) {
                    let _ = std::fs::write(dir.join("hidden"), []);
                }
            }
            break;
        }
        std::thread::sleep(std::time::Duration::from_millis(100));
    });
}
