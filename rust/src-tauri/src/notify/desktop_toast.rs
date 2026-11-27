//! Installed builds use the Start menu identity and a fixed protocol activation.
//! A portable copy may coexist with the install and uses its own tray instead.
use tauri::AppHandle;
use windows::core::{Result, HSTRING};
use windows::Data::Xml::Dom::XmlDocument;
use windows::Win32::System::WinRT::{RoInitialize, RoUninitialize, RO_INIT_MULTITHREADED};
use windows::UI::Notifications::{
    NotificationSetting, ToastNotification, ToastNotificationManager,
};

extern "system" {
    fn SetCurrentProcessExplicitAppUserModelID(id: *const u16) -> i32;
}
extern "C" {
    fn offwork_notify(message: *const u16) -> i32;
}

fn installed() -> bool {
    let command = offwork_core::autostart::registry::read(
        r"Software\Classes\offwork-reminder\shell\open\command",
        "",
    );
    let Some(target) = command.and_then(|s| dunce::canonicalize(s.trim_matches('"')).ok()) else {
        return false;
    };
    std::env::current_exe()
        .and_then(dunce::canonicalize)
        .is_ok_and(|exe| exe == target)
}

pub fn set_identity(app: &AppHandle) {
    if installed() {
        let id: Vec<u16> = app
            .config()
            .identifier
            .encode_utf16()
            .chain(Some(0))
            .collect();
        let hr = unsafe { SetCurrentProcessExplicitAppUserModelID(id.as_ptr()) };
        if hr < 0 {
            eprintln!("Could not set toast process identity: {hr:#x}");
        }
    }
}

fn toast(id: &str, message: &str) -> Result<()> {
    let xml = XmlDocument::new()?;
    xml.LoadXml(&HSTRING::from(r#"<toast activationType="protocol" launch="offwork-reminder:show"><visual><binding template="ToastGeneric"><text>下班手账</text><text/></binding></visual></toast>"#))?;
    xml.SelectSingleNode(&HSTRING::from("//text[2]"))?
        .SetInnerText(&HSTRING::from(message))?;
    let notification = ToastNotification::CreateToastNotification(&xml)?;
    let notifier = ToastNotificationManager::CreateToastNotifierWithId(&HSTRING::from(id))?;
    let setting = notifier.Setting()?;
    if setting == NotificationSetting::Enabled {
        // Windows itself applies Focus Assist / Do Not Disturb at delivery.
        notifier.Show(&notification)?;
    } else {
        eprintln!("Windows notifications disabled for {id}: {setting:?}");
    }
    if let Some(dir) = std::env::var_os("OFFWORK_MEASURE") {
        let report = serde_json::json!({"aumid": id, "xml": notification.Content()?.GetXml()?.to_string(),
            "notification_setting": setting.0});
        let _ = std::fs::write(
            std::path::Path::new(&dir).join("toast.json"),
            report.to_string(),
        );
    }
    Ok(())
}

pub fn notify(app: &AppHandle, message: &str) {
    super::record(message);
    if installed() {
        // Scheduled reminders and OCR errors can originate on different threads.
        let id = app.config().identifier.clone();
        let message = message.to_owned();
        std::thread::spawn(move || {
            let result = unsafe { RoInitialize(RO_INIT_MULTITHREADED) }.and_then(|()| {
                let result = toast(&id, &message);
                unsafe { RoUninitialize() };
                result
            });
            if let Err(error) = result {
                eprintln!("Windows toast failed: {error}");
            }
        });
    } else {
        let wide: Vec<u16> = message.encode_utf16().chain(Some(0)).collect();
        let _ = app.run_on_main_thread(move || {
            if unsafe { offwork_notify(wide.as_ptr()) } == 0 {
                eprintln!("Portable tray notification failed");
            }
        });
    }
}
