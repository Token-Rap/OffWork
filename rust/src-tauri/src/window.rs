//! Widget geometry (work-area placement, scale, expand/collapse, DPI moves)
//! and what happens to the web view when the widget is hidden.

use crate::model::Model;
use offwork_core::placement::{self, Rect};
use std::sync::atomic::Ordering;
use tauri::{AppHandle, Emitter, LogicalSize, Manager, PhysicalPosition, WebviewWindow};

pub fn show(app: &AppHandle) {
    if let Some(w) = app.get_webview_window("main") {
        set_webview_active(&w, true);
        let _ = w.unminimize();
        let _ = w.show();
        let _ = w.set_focus();
        if app.state::<Model>().pinned.load(Ordering::SeqCst) {
            let _ = w.set_always_on_top(true);
        }
        let _ = app.emit_to("main", "widget-visibility", true);
    }
}

/// Hides the widget and lets the web view sleep: the UI stops its clock,
/// and on Windows WebView2 is marked invisible, suspended and asked to use
/// little memory. Reminders keep running in Rust.
pub fn hide(app: &AppHandle) -> bool {
    let Some(w) = app.get_webview_window("main") else {
        return false;
    };
    let _ = app.emit_to("main", "widget-visibility", false);
    let hidden = w.hide().is_ok();
    if hidden {
        set_webview_active(&w, false);
    }
    hidden
}

#[cfg(windows)]
fn set_webview_active(w: &WebviewWindow, active: bool) {
    let _ = w.with_webview(move |webview| {
        use webview2_com::Microsoft::Web::WebView2::Win32::{
            ICoreWebView2_19, ICoreWebView2_3, COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_LOW,
            COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_NORMAL,
        };
        use windows_core::Interface;
        let controller = webview.controller();
        // SAFETY: COM calls on the UI thread that owns the controller, which
        // `with_webview` guarantees; every result is checked or ignored.
        unsafe {
            if !active {
                // TrySuspend requires an invisible controller.
                let _ = controller.SetIsVisible(false);
            }
            if let Ok(core) = controller.CoreWebView2() {
                if let Ok(core) = core.cast::<ICoreWebView2_19>() {
                    let _ = core.SetMemoryUsageTargetLevel(if active {
                        COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_NORMAL
                    } else {
                        COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_LOW
                    });
                }
                if let Ok(core) = core.cast::<ICoreWebView2_3>() {
                    if active {
                        let _ = core.Resume();
                    } else {
                        let handler =
                            webview2_com::TrySuspendCompletedHandler::create(Box::new(|_, _| {
                                Ok(())
                            }));
                        let _ = core.TrySuspend(&handler);
                    }
                }
            }
            if active {
                let _ = controller.SetIsVisible(true);
            }
        }
    });
}

/// WebKit (macOS, Linux) already marks a hidden window's page as hidden
/// and throttles it; the UI stops its own clock on the visibility event.
#[cfg(not(windows))]
fn set_webview_active(_w: &WebviewWindow, _active: bool) {}

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

/// The widget scales with the web view's own zoom, so the page lays out
/// against the real window (CSS `100vh`) at every scale. A CSS transform
/// left WebKit unpainted below the unscaled height.
pub fn zoom(w: &WebviewWindow, scale: i32) {
    let _ = w.set_zoom(f64::from(scale.clamp(75, 150)) / 100.0);
}

/// GTK keeps a non-resizable window at the size it was created with (360 px
/// wide), so 75 % stayed 360 px wide and spilled past the work area (found by
/// scripts/layout_smoke.py). On Linux the window is resizable and pinned to
/// the exact size with min = max constraints instead.
fn resize(w: &WebviewWindow, lw: f64, lh: f64) {
    let size = LogicalSize::new(lw, lh);
    #[cfg(target_os = "linux")]
    {
        let _ = w.set_resizable(true);
        let _ = w.set_min_size(None::<LogicalSize<f64>>);
        let _ = w.set_max_size(None::<LogicalSize<f64>>);
    }
    let _ = w.set_size(size);
    #[cfg(target_os = "linux")]
    {
        let _ = w.set_min_size(Some(size));
        let _ = w.set_max_size(Some(size));
    }
}

/// After any real resize (ours, a DPI move, the window manager), keep the
/// whole window inside the work area using its actual size.
pub fn keep_inside(w: &WebviewWindow) {
    let (Ok(pos), Ok(size), Some(work)) = (w.outer_position(), w.outer_size(), work_area(w)) else {
        return;
    };
    let actual = (size.width as i32, size.height as i32);
    let (x, y) = placement::clamp_into(work, pos.x, pos.y, actual);
    if (x, y) != (pos.x, pos.y) {
        let _ = w.set_position(PhysicalPosition::new(x, y));
    }
}

/// Logical size for `scale` that fits the current work area.
fn fitted(w: &WebviewWindow, scale: i32, expanded: bool) -> (f64, f64) {
    match (work_area(w), w.scale_factor()) {
        (Some(work), Ok(factor)) => placement::fitted_size(
            scale,
            expanded,
            (f64::from(work.w) / factor, f64::from(work.h) / factor),
        ),
        _ => placement::window_size(scale, expanded),
    }
}

/// Resizes to the logical size for `scale` (never taller than the work area,
/// the settings panel scrolls instead), then keeps the window inside the
/// work area: expanding near the bottom moves it up.
pub fn fit(w: &WebviewWindow, scale: i32, expanded: bool) {
    zoom(w, scale);
    let (lw, lh) = fitted(w, scale, expanded);
    resize(w, lw, lh);
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
    zoom(w, scale);
    let (lw, lh) = fitted(w, scale, false);
    resize(w, lw, lh);
    let (Ok(factor), Some(work)) = (w.scale_factor(), work_area(w)) else {
        return;
    };
    let size = ((lw * factor).round() as i32, (lh * factor).round() as i32);
    let (x, y) = placement::initial_position(work, size, (placement::MARGIN * factor) as i32);
    let _ = w.set_position(PhysicalPosition::new(x, y));
}
