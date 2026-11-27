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

#[cfg(windows)]
mod desktop_toast;
#[cfg(windows)]
pub use desktop_toast::notify;

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
    desktop_toast::set_identity(app);
}
