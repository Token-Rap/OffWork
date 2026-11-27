//! Tray / menu bar: show, hide, start at login (shared with the settings
//! switch, always read back from the system) and quit.

use crate::{autostart, commands, window};
use tauri::menu::{CheckMenuItem, MenuBuilder, MenuItem};
use tauri::tray::{MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent};
use tauri::{App, AppHandle, Emitter, Manager, Wry};

pub struct TrayMenu(pub CheckMenuItem<Wry>);

pub fn build(app: &App) -> tauri::Result<()> {
    let autostart_item = CheckMenuItem::with_id(
        app,
        "autostart",
        "开机自启",
        true,
        autostart::state(None).enabled,
        None::<&str>,
    )?;
    let menu = MenuBuilder::new(app)
        .item(&MenuItem::with_id(
            app,
            "show",
            "显示手账",
            true,
            None::<&str>,
        )?)
        .item(&MenuItem::with_id(
            app,
            "hide",
            "隐藏手账",
            true,
            None::<&str>,
        )?)
        .separator()
        .item(&autostart_item)
        .separator()
        .item(&MenuItem::with_id(
            app,
            "quit",
            "退出 OffWork",
            true,
            None::<&str>,
        )?)
        .build()?;
    app.manage(TrayMenu(autostart_item));
    TrayIconBuilder::with_id("offwork")
        .icon(
            app.default_window_icon()
                .cloned()
                .ok_or(tauri::Error::InvalidIcon(std::io::Error::other(
                    "missing icon",
                )))?,
        )
        .tooltip("OffWork 下班手账")
        .menu(&menu)
        .show_menu_on_left_click(false)
        .on_menu_event(|app, event| match event.id.as_ref() {
            "show" => window::show(app),
            "hide" => window::hide(app),
            "autostart" => {
                let current = autostart::state(None).enabled;
                let state = autostart::set(!current);
                sync(app, state.enabled);
                let _ = app.emit_to(
                    "main",
                    "settings",
                    commands::view_with(app, state.error.clone(), Some(state)),
                );
            }
            "quit" => app.exit(0),
            _ => {}
        })
        .on_tray_icon_event(|tray, event| match event {
            TrayIconEvent::Click {
                button: MouseButton::Left,
                button_state: MouseButtonState::Up,
                ..
            }
            | TrayIconEvent::DoubleClick {
                button: MouseButton::Left,
                ..
            } => window::show(tray.app_handle()),
            _ => {}
        })
        .build(app)?;
    Ok(())
}

/// Keeps the menu check mark on the real login-item state.
pub fn sync(app: &AppHandle, enabled: bool) {
    if let Some(menu) = app.try_state::<TrayMenu>() {
        let _ = menu.0.set_checked(enabled);
    }
}

#[cfg(target_os = "linux")]
pub fn host_available() -> bool {
    std::process::Command::new("gdbus")
        .args([
            "call",
            "--session",
            "--dest",
            "org.kde.StatusNotifierWatcher",
            "--object-path",
            "/StatusNotifierWatcher",
            "--method",
            "org.freedesktop.DBus.Properties.Get",
            "org.kde.StatusNotifierWatcher",
            "IsStatusNotifierHostRegistered",
        ])
        .output()
        .is_ok_and(|r| r.status.success() && String::from_utf8_lossy(&r.stdout).contains("true"))
}
#[cfg(not(target_os = "linux"))]
pub fn host_available() -> bool {
    true
}
