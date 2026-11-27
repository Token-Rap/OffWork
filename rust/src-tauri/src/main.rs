#![cfg_attr(all(windows, not(debug_assertions)), windows_subsystem = "windows")]
#[cfg(all(unix, any(test, not(target_os = "macos"))))]
mod appimage;
mod autostart;
mod calendar;
mod commands;
mod model;
mod notify;
mod ocr;
#[cfg(not(windows))]
mod tray;
#[cfg(windows)]
#[path = "windows_tray.rs"]
mod tray;
mod updater;
mod window;

use chrono::{Local, Timelike};
use model::Model;
use offwork_core::edit::{self, Commit};
use offwork_core::reminder::{seconds_since_shift_end, ReminderState};
use offwork_core::store::{self, Legacy, LegacyIni};
use std::path::PathBuf;
use std::sync::atomic::Ordering;
use std::time::Duration;
use tauri::{AppHandle, DragDropEvent, Emitter, Manager, WindowEvent};

/// Hidden windows keep counting: one observation per wall-clock second,
/// recomputed from the local clock after every sleep or wake.
fn watch(app: AppHandle) {
    std::thread::spawn(move || {
        let mut state = ReminderState::default();
        let mut generation = u64::MAX;
        loop {
            let model = app.state::<Model>();
            let s = model.current();
            let now = Local::now();
            let seconds = now.num_seconds_from_midnight() as i32;
            if let Ok(w) = s.workday(seconds) {
                let current = model.schedule_generation.load(Ordering::SeqCst);
                if current != generation {
                    // Startup and schedule edits never fire a catch-up reminder.
                    generation = current;
                    state = ReminderState::reset(w.phase, Some(w.remaining));
                }
                let d = state.tick(
                    w.phase,
                    seconds_since_shift_end(seconds, s.end_minutes),
                    Some(w.remaining),
                );
                state = d.next;
                if d.notify_five_minutes {
                    notify::notify(&app, "还有五分钟下班");
                }
                if d.notify_end {
                    notify::notify(&app, "下班时间到了");
                }
            }
            updater::poll(&app);
            let millis = 1000 - u64::from(now.timestamp_subsec_millis()).min(999);
            std::thread::sleep(Duration::from_millis(millis));
        }
    });
}

fn ocr_error(app: &AppHandle, message: String) {
    notify::notify(app, &message);
    let _ = app.emit_to("main", "ocr-error", message);
}

/// Validates synchronously, recognises on a worker, then applies the time
/// with the same rules as a manual edit. Errors keep the saved schedule.
fn drop_image(app: AppHandle, paths: &[PathBuf]) {
    let model = app.state::<Model>();
    let path = match ocr::check(paths) {
        Ok(path) => path.to_owned(),
        Err(e) => return ocr_error(&app, e),
    };
    if model.ocr_busy.swap(true, Ordering::SeqCst) {
        return ocr_error(&app, "正在识别，请稍后重试。".into());
    }
    std::thread::spawn(move || {
        let model = app.state::<Model>();
        // Recognition is slow; the apply step is one serialized edit on the
        // latest settings, so a manual change made meanwhile is kept.
        let result = ocr::recognize(&path).and_then(|minutes| {
            model.update(|s| match edit::apply_clock_in(s, minutes) {
                Ok(next) => Commit::Saved(next),
                Err(message) => Commit::Rejected(message),
            })
        });
        model.ocr_busy.store(false, Ordering::SeqCst);
        match result {
            Ok(_) => {
                let _ = app.emit_to("main", "settings", commands::view(&app, None));
            }
            Err(e) => ocr_error(&app, e),
        }
    });
}

#[cfg(windows)]
mod legacy {
    use std::sync::OnceLock;
    use tauri::AppHandle;

    extern "C" {
        fn offwork_legacy_acquire() -> i32;
        fn offwork_legacy_window(on_event: extern "C" fn(i32)) -> i32;
    }
    static APP: OnceLock<AppHandle> = OnceLock::new();

    extern "C" fn on_event(kind: i32) {
        let Some(app) = APP.get().cloned() else {
            return;
        };
        // Leave the window procedure before touching Tauri windows.
        std::thread::spawn(move || match kind {
            1 => crate::window::show(&app),
            2 => crate::tray::quit(&app),
            3 => {
                crate::window::hide(&app);
            }
            4 => crate::tray::toggle_autostart(&app),
            _ => {}
        });
    }

    /// `false` when another OffWork (either generation) is running; it has
    /// been asked to come to the front.
    pub fn acquire() -> bool {
        // SAFETY: plain Win32 calls with no pointers from Rust.
        unsafe { offwork_legacy_acquire() != 0 }
    }

    pub fn install(app: &AppHandle) {
        let _ = APP.set(app.clone());
        // SAFETY: runs on the main thread, which pumps this window's messages.
        if unsafe { offwork_legacy_window(on_event) } == 0 {
            eprintln!("Legacy compatibility window unavailable");
        }
    }
}

fn open_settings() -> (Option<PathBuf>, store::Loaded) {
    let path = store::paths::settings_json();
    let legacy = store::paths::legacy_ini().map(LegacyIni);
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0, |d| d.as_secs());
    let loaded = store::open_any(
        path.as_deref(),
        legacy.as_ref().map(|l| l as &dyn Legacy),
        now,
    );
    (path, loaded)
}

fn main() {
    #[cfg(windows)]
    if !legacy::acquire() {
        return;
    }
    let app = tauri::Builder::default()
        .plugin(tauri_plugin_single_instance::init(|app, _, _| {
            window::show(app)
        }))
        .plugin(tauri_plugin_notification::init())
        .setup(|app| {
            let handle = app.handle().clone();
            // Settings are opened only after the single-instance lock.
            let (path, loaded) = open_settings();
            let scale = loaded.settings.scale_percent;
            app.manage(Model::new(path, loaded));
            app.manage(updater::UpdateState(Default::default()));
            #[cfg(windows)]
            legacy::install(&handle);
            let has_tray = tray::build(app).is_ok() && tray::host_available();
            app.state::<Model>().tray.store(has_tray, Ordering::SeqCst);
            let w = app
                .get_webview_window("main")
                .ok_or("main window missing")?;
            if !commands::pin_supported() {
                let _ = w.set_always_on_top(false);
                let _ = w.set_decorations(true);
                app.state::<Model>().pinned.store(false, Ordering::SeqCst);
            }
            window::place_initially(&w, scale);
            notify::init(&handle);
            watch(handle);
            Ok(())
        })
        .on_window_event(|w, e| {
            let app = w.app_handle();
            if w.label() == calendar::LABEL {
                return;
            }
            match e {
                WindowEvent::CloseRequested { api, .. } => {
                    api.prevent_close();
                    calendar::close(app);
                    // Close hides to the tray; without a tray it quits.
                    if !app.state::<Model>().tray.load(Ordering::SeqCst) || !window::hide(app) {
                        app.exit(0);
                    }
                }
                WindowEvent::DragDrop(DragDropEvent::Drop { paths, .. }) => {
                    drop_image(app.clone(), paths)
                }
                WindowEvent::Resized(_) => {
                    if let Some(main) = app.get_webview_window("main") {
                        window::keep_inside(&main);
                    }
                }
                WindowEvent::ScaleFactorChanged { .. } => {
                    // A DPI change moves the popup's anchor: cancel it like the
                    // C++ calendar did, and keep the logical size exact.
                    calendar::close(app);
                    if let Some(main) = app.get_webview_window("main") {
                        let model = app.state::<Model>();
                        window::fit(
                            &main,
                            model.current().scale_percent,
                            model.expanded.load(Ordering::SeqCst),
                        );
                    }
                }
                _ => {}
            }
        })
        .invoke_handler(tauri::generate_handler![
            commands::state,
            commands::commit_field,
            commands::live_field,
            commands::set_end_mode,
            commands::toggle,
            commands::set_window,
            commands::set_expanded,
            commands::set_pinned,
            commands::set_autostart,
            commands::first_frame,
            commands::measure_layout,
            commands::ticks,
            calendar::open_calendar,
            calendar::calendar_init,
            calendar::calendar_finish,
            updater::start_update,
        ])
        .build(tauri::generate_context!())
        .expect("Unable to start OffWork");
    app.run(|app, event| {
        #[cfg(target_os = "macos")]
        if let tauri::RunEvent::Reopen { .. } = event {
            window::show(app);
        }
        #[cfg(not(target_os = "macos"))]
        let _ = (app, event);
    });
}
