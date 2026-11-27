//! The resignation calendar as an owned popup window, placed with the same
//! rules as the C++ fix: below the date box, flipped above when there is no
//! room, clamped to the monitor work area, scrolling when it cannot fit.

use crate::commands::{view, View};
use crate::model::Model;
use offwork_core::edit::{self, Commit, Field, Fields};
use offwork_core::placement::{self, Rect};
use serde::{Deserialize, Serialize};
use tauri::{
    AppHandle, Emitter, Manager, PhysicalPosition, PhysicalSize, State, WebviewUrl, WebviewWindow,
    WebviewWindowBuilder,
};

pub const LABEL: &str = "calendar";
/// Natural size of calendar.html at 100 %, in CSS pixels.
const CONTENT: (f64, f64) = (288.0, 336.0);
const SCROLL_BAR: f64 = 17.0;

#[derive(Deserialize)]
pub struct Anchor {
    x: f64,
    y: f64,
    w: f64,
    h: f64,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Init {
    date: String,
    today: String,
    scale: i32,
}

#[derive(Deserialize)]
#[serde(tag = "action", rename_all = "camelCase")]
pub enum Finish {
    Pick { date: String },
    Clear,
    Cancel,
}

fn today() -> String {
    chrono::Local::now().format("%Y-%m-%d").to_string()
}

/// Opens (or re-focuses) the popup. A typed date is committed first, so the
/// popup starts from the saved date, otherwise from today.
#[tauri::command]
pub fn open_calendar(
    app: AppHandle,
    window: WebviewWindow,
    model: State<Model>,
    anchor: Anchor,
    fields: Fields,
) -> Result<View, String> {
    let current = model.current();
    let mut error = None;
    match edit::commit(&current, Field::Resignation, &fields) {
        Commit::Saved(next) => error = model.persist(next).err(),
        Commit::Rejected(message) => error = Some(message.to_owned()),
        Commit::Unchanged => {}
    }
    if let Some(existing) = app.get_webview_window(LABEL) {
        let _ = existing.set_focus();
        return Ok(view(&app, error));
    }
    let factor = window.scale_factor().map_err(|e| e.to_string())?;
    let inner = window.inner_position().map_err(|e| e.to_string())?;
    let work = crate::window::work_area(&window).ok_or("无法读取显示器工作区")?;
    let scale = f64::from(model.current().scale_percent) / 100.0;
    let px = |v: f64| (v * factor).round() as i32;
    let anchor = Rect {
        x: inner.x + px(anchor.x),
        y: inner.y + px(anchor.y),
        w: px(anchor.w),
        h: px(anchor.h),
    };
    let content = (px(CONTENT.0 * scale), px(CONTENT.1 * scale));
    let placed = placement::popup(anchor, work, content, (px(SCROLL_BAR), px(SCROLL_BAR)))
        .ok_or("显示器工作区无效")?;
    let popup = WebviewWindowBuilder::new(&app, LABEL, WebviewUrl::App("calendar.html".into()))
        .title("选择离职日期")
        .decorations(false)
        .resizable(false)
        .skip_taskbar(true)
        .always_on_top(true)
        .visible(false)
        .parent(&window)
        .map_err(|e| e.to_string())?
        .build()
        .map_err(|e| e.to_string())?;
    let r = placed.rect;
    let _ = popup.set_size(PhysicalSize::new(r.w as u32, r.h as u32));
    let _ = popup.set_position(PhysicalPosition::new(r.x, r.y));
    Ok(view(&app, error))
}

#[tauri::command]
pub fn calendar_init(model: State<Model>) -> Init {
    let s = model.current();
    Init {
        date: s.resignation_date,
        today: today(),
        scale: s.scale_percent,
    }
}

/// Enter/click saves, 清空日期 clears, Esc / click outside cancels.
#[tauri::command]
pub fn calendar_finish(app: AppHandle, model: State<Model>, finish: Finish) {
    let current = model.current();
    let date = match finish {
        Finish::Pick { date } => Some(date),
        Finish::Clear => Some(String::new()),
        Finish::Cancel => None,
    };
    let mut error = None;
    if let Some(date) = date {
        let fields = edit::fields(&current).with(Field::Resignation, &date);
        match edit::commit(&current, Field::Resignation, &fields) {
            Commit::Saved(next) => error = model.persist(next).err(),
            Commit::Rejected(m) => error = Some(m.to_owned()),
            Commit::Unchanged => {}
        }
    }
    close(&app);
    if let Some(main) = app.get_webview_window("main") {
        let _ = main.set_focus();
    }
    let _ = app.emit_to("main", "calendar-closed", view(&app, error));
}

pub fn close(app: &AppHandle) {
    if let Some(popup) = app.get_webview_window(LABEL) {
        let _ = popup.destroy();
    }
}
