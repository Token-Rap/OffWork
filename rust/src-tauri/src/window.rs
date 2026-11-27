//! Widget geometry: work-area placement, scale, expand/collapse, DPI moves.

use crate::model::Model;
use offwork_core::placement::{self, Rect};
use tauri::{AppHandle, LogicalSize, Manager, PhysicalPosition, WebviewWindow};

pub fn show(app: &AppHandle) {
    if let Some(w) = app.get_webview_window("main") {
        let _ = w.unminimize();
        let _ = w.show();
        let _ = w.set_focus();
        if app
            .state::<Model>()
            .pinned
            .load(std::sync::atomic::Ordering::SeqCst)
        {
            let _ = w.set_always_on_top(true);
        }
    }
}

pub fn hide(app: &AppHandle) {
    if let Some(w) = app.get_webview_window("main") {
        let _ = w.hide();
    }
}

pub fn work_area(w: &WebviewWindow) -> Option<Rect> {
    let monitor = w
        .current_monitor()
        .ok()
        .flatten()
        .or_else(|| w.primary_monitor().ok().flatten())?;
    let area = monitor.work_area();
    Some(Rect {
        x: area.position.x,
        y: area.position.y,
        w: area.size.width as i32,
        h: area.size.height as i32,
    })
}

/// Resizes to the logical size for `scale`, then keeps the window inside
/// the work area (expanding near the bottom moves it up).
pub fn fit(w: &WebviewWindow, scale: i32, expanded: bool) {
    let (lw, lh) = placement::window_size(scale, expanded);
    let _ = w.set_size(LogicalSize::new(lw, lh));
    let (Ok(pos), Ok(factor), Some(work)) = (w.outer_position(), w.scale_factor(), work_area(w))
    else {
        return;
    };
    let size = ((lw * factor).round() as i32, (lh * factor).round() as i32);
    let (x, y) = placement::clamp_into(work, pos.x, pos.y, size);
    if (x, y) != (pos.x, pos.y) {
        let _ = w.set_position(PhysicalPosition::new(x, y));
    }
}

/// First launch position: the top right of the current monitor's work area.
pub fn place_initially(w: &WebviewWindow, scale: i32) {
    let (lw, lh) = placement::window_size(scale, false);
    let _ = w.set_size(LogicalSize::new(lw, lh));
    let (Ok(factor), Some(work)) = (w.scale_factor(), work_area(w)) else {
        return;
    };
    let size = ((lw * factor).round() as i32, (lh * factor).round() as i32);
    let (x, y) = placement::initial_position(work, size, (placement::MARGIN * factor) as i32);
    let _ = w.set_position(PhysicalPosition::new(x, y));
}
