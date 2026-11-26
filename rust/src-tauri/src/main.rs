#![cfg_attr(all(windows, not(debug_assertions)), windows_subsystem = "windows")]
mod ocr;
use chrono::{Local, Timelike};
use offwork_core::{Phase, Settings, Snapshot};
use std::{
    path::PathBuf,
    sync::{
        atomic::{AtomicBool, Ordering},
        Mutex,
    },
    time::Duration,
};
use tauri::{
    menu::MenuBuilder,
    tray::{MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent},
    AppHandle, DragDropEvent, Emitter, Manager, State, WindowEvent,
};
use tauri_plugin_notification::NotificationExt;

struct Model {
    settings: Mutex<Settings>,
    path: PathBuf,
    busy: AtomicBool,
    tray: bool,
}
fn snapshot(s: &Settings) -> Result<Snapshot, String> {
    let now = Local::now();
    s.snapshot(now.num_seconds_from_midnight() as i32, now.date_naive())
}
#[tauri::command]
fn initial(model: State<Model>) -> Settings {
    model.settings.lock().unwrap().clone()
}
#[tauri::command]
fn save(model: State<Model>, settings: Settings) -> Result<(), String> {
    snapshot(&settings)?;
    let mut current = model.settings.lock().unwrap();
    let write = || -> Result<(), Box<dyn std::error::Error>> {
        let dir = model.path.parent().unwrap();
        std::fs::create_dir_all(dir)?;
        let mut file = tempfile::NamedTempFile::new_in(dir)?;
        serde_json::to_writer(&mut file, &settings)?;
        file.as_file().sync_all()?;
        file.persist(&model.path)?;
        Ok(())
    };
    write().map_err(|e| format!("保存失败：{e}"))?;
    *current = settings;
    Ok(())
}
#[tauri::command]
fn tick(model: State<Model>) -> Result<Snapshot, String> {
    snapshot(&model.settings.lock().unwrap())
}
fn show(app: &AppHandle) {
    if let Some(w) = app.get_webview_window("main") {
        let _ = w.unminimize();
        let _ = w.show();
        let _ = w.set_focus();
    }
}
fn watch(app: AppHandle) {
    std::thread::spawn(move || {
        let mut last: Option<(Settings, Phase, i32, i64)> = None;
        loop {
            let s = app.state::<Model>().settings.lock().unwrap().clone();
            let now = Local::now();
            if let Ok(v) = s.snapshot(now.num_seconds_from_midnight() as i32, now.date_naive()) {
                if let Some((old, Phase::Working, remaining, previous)) = &last {
                    if old == &s && (0..=600).contains(&(now.timestamp() - previous)) {
                        let message = match v.phase {
                            Phase::Working if *remaining > 300 && v.remaining <= 300 => {
                                Some("还有五分钟下班")
                            }
                            Phase::After => Some("下班时间到了"),
                            _ => None,
                        };
                        if let Some(msg) = message {
                            if let Err(e) = app
                                .notification()
                                .builder()
                                .title("下班手账")
                                .body(msg)
                                .show()
                            {
                                eprintln!("Notification failed: {e}")
                            }
                        }
                    }
                }
                last = Some((s, v.phase, v.remaining, now.timestamp()));
            }
            std::thread::sleep(Duration::from_secs(1));
        }
    });
}
fn drop_image(app: AppHandle, paths: &[PathBuf]) {
    let model = app.state::<Model>();
    if paths.len() != 1 || model.busy.swap(true, Ordering::SeqCst) {
        let _ = app.emit("ocr-error", "请一次拖入一张图片，并等待识别完成");
        return;
    }
    let path = paths[0].clone();
    std::thread::spawn(move || {
        let result = ocr::recognize(&path);
        let model = app.state::<Model>();
        let result = result.and_then(|start| {
            let mut s = model.settings.lock().unwrap().clone();
            s.start = start;
            save(app.state(), s.clone())?;
            Ok(s)
        });
        match result {
            Ok(s) => {
                let _ = app.emit("ocr-applied", s);
            }
            Err(e) => {
                let _ = app.emit("ocr-error", e);
            }
        }
        model.busy.store(false, Ordering::SeqCst);
    });
}
fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_single_instance::init(|app, _, _| show(app)))
        .plugin(tauri_plugin_notification::init())
        .setup(|app| {
            let handle = app.handle().clone();
            let path = app.path().app_config_dir()?.join("settings.json");
            let settings: Settings = match std::fs::read(&path) {
                Ok(bytes) => serde_json::from_slice(&bytes)?,
                Err(e) if e.kind() == std::io::ErrorKind::NotFound => Settings::default(),
                Err(e) => return Err(e.into()),
            };
            let menu = MenuBuilder::new(app)
                .text("show", "显示手账")
                .text("hide", "隐藏手账")
                .separator()
                .text("quit", "退出")
                .build()?;
            let tray = TrayIconBuilder::new()
                .icon(app.default_window_icon().unwrap().clone())
                .menu(&menu)
                .on_menu_event(|app, event| match event.id.as_ref() {
                    "show" => show(app),
                    "hide" => {
                        let _ = app.get_webview_window("main").unwrap().hide();
                    }
                    "quit" => app.exit(0),
                    _ => {}
                })
                .on_tray_icon_event(|tray, event| {
                    if matches!(
                        event,
                        TrayIconEvent::Click {
                            button: MouseButton::Left,
                            button_state: MouseButtonState::Up,
                            ..
                        }
                    ) {
                        show(tray.app_handle())
                    }
                })
                .build(app);
            let has_tray = tray.is_ok()
                && !(cfg!(target_os = "linux")
                    && std::env::var("XDG_CURRENT_DESKTOP")
                        .unwrap_or_default()
                        .to_uppercase()
                        .contains("GNOME"));
            app.manage(Model {
                settings: Mutex::new(settings),
                path,
                busy: AtomicBool::new(false),
                tray: has_tray,
            });
            let w = app.get_webview_window("main").unwrap();
            if let Ok(Some(m)) = w.current_monitor() {
                let area = m.work_area();
                let scale = m.scale_factor();
                let _ = w.set_position(tauri::PhysicalPosition::new(
                    area.position.x + area.size.width as i32 - (376. * scale) as i32,
                    area.position.y + (16. * scale) as i32,
                ));
            }
            let h = handle.clone();
            app.notification().on_action(move |_| show(&h))?;
            watch(handle);
            Ok(())
        })
        .on_window_event(|w, e| match e {
            WindowEvent::CloseRequested { api, .. } if w.state::<Model>().tray => {
                if w.hide().is_ok() {
                    api.prevent_close()
                }
            }
            WindowEvent::DragDrop(DragDropEvent::Drop { paths, .. }) => {
                drop_image(w.app_handle().clone(), paths)
            }
            _ => {}
        })
        .invoke_handler(tauri::generate_handler![initial, tick, save, first_frame])
        .run(tauri::generate_context!())
        .expect("Unable to run OffWork")
}
#[tauri::command]
fn first_frame(app: AppHandle) {
    if let Some(dir) = std::env::var_os("OFFWORK_MEASURE") {
        let dir = PathBuf::from(dir);
        if std::fs::write(dir.join("frame"), []).is_ok() {
            std::thread::spawn(move || loop {
                if dir.join("hide").exists() {
                    if let Some(w) = app.get_webview_window("main") {
                        if w.hide().is_ok() && matches!(w.is_visible(), Ok(false)) {
                            let _ = std::fs::write(dir.join("hidden"), []);
                        }
                    }
                    break;
                }
                std::thread::sleep(Duration::from_millis(100));
            });
        }
    }
}
