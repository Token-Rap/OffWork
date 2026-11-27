//! System reminders. Clicking one restores the widget on every platform.

use tauri::AppHandle;

/// Measurement hook: with OFFWORK_MEASURE=<dir>, every reminder the app
/// decides to show is also appended to `<dir>/notifications.log`, so CI can
/// check hidden-window reminders without a notification server.
pub fn record(message: &str) {
    if let Some(dir) = std::env::var_os("OFFWORK_MEASURE") {
        use std::io::Write;
        let line = format!("{} {message}\n", chrono::Local::now().format("%H:%M:%S"));
        let _ = std::fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(std::path::Path::new(&dir).join("notifications.log"))
            .and_then(|mut f| f.write_all(line.as_bytes()));
    }
}

#[cfg(not(windows))]
pub fn notify(app: &AppHandle, message: &str) {
    use tauri_plugin_notification::NotificationExt;
    record(message);
    if let Err(e) = app
        .notification()
        .builder()
        .title("下班手账")
        .body(message)
        .show()
    {
        eprintln!("Notification failed: {e}");
    }
}

/// Shell balloons work for the portable EXE without an installed
/// AppUserModelID; a click arrives as NIN_BALLOONUSERCLICK in balloon.c.
#[cfg(windows)]
pub fn notify(app: &AppHandle, message: &str) {
    use tauri::Manager;
    record(message);
    extern "C" {
        fn offwork_notify(window: *mut std::ffi::c_void, message: *const u16) -> i32;
    }
    let wide: Vec<u16> = message.encode_utf16().chain(Some(0)).collect();
    if let Some(window) = app.get_webview_window("main") {
        let _ = app.run_on_main_thread(move || {
            if let Ok(hwnd) = window.hwnd() {
                if unsafe { offwork_notify(hwnd.0, wide.as_ptr()) } == 0 {
                    eprintln!("Shell notification failed");
                }
            }
        });
    }
}

/// Registers the click handler before anything is shown.
pub fn init(app: &AppHandle) {
    #[cfg(not(windows))]
    {
        use tauri_plugin_notification::NotificationExt;
        let handle = app.clone();
        let _ = app
            .notification()
            .on_action(move |_| crate::window::show(&handle));
    }
    #[cfg(windows)]
    let _ = app;
}
