//! The Windows shell menu returns its selected command after the popup closes.
use crate::{autostart, commands};
use tauri::{App, AppHandle, Emitter, Manager};

extern "C" {
    fn offwork_tray_init(widget: *mut std::ffi::c_void, enabled: i32) -> i32;
    fn offwork_tray_checked(enabled: i32);
}

pub fn build(app: &App) -> tauri::Result<()> {
    let window = app.get_webview_window("main").unwrap();
    let ok = unsafe { offwork_tray_init(window.hwnd()?.0, autostart::state(None).enabled.into()) };
    if ok == 0 {
        return Err(std::io::Error::other("Windows notification area unavailable").into());
    }
    Ok(())
}

pub fn host_available() -> bool {
    true
}

pub fn sync(_: &AppHandle, enabled: bool) {
    unsafe { offwork_tray_checked(enabled.into()) };
}

pub fn toggle_autostart(app: &AppHandle) {
    let state = autostart::set(!autostart::state(None).enabled);
    sync(app, state.enabled);
    let _ = app.emit_to(
        "main",
        "settings",
        commands::view_with(app, state.error.clone(), Some(state)),
    );
}

pub fn quit(app: &AppHandle) {
    // Destroy bypasses the close-to-tray handler and releases each WebView2
    // controller before requesting exit. Other apps' browser processes are untouched.
    for window in app.webview_windows().into_values() {
        let _ = window.destroy();
    }
    app.exit(0);
}
