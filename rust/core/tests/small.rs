//! Ports of money_display, image_file and autostart tests.
use offwork_core::{autostart, image_file::is_supported, money};

#[test]
fn money_display_masks_every_amount() {
    for amount in [
        "¥0.00",
        "¥0.01",
        "¥500.00",
        "¥1000000.00",
        "¥12.34/h",
        "¥12345.67/month",
    ] {
        assert_eq!(money::display(amount, true), "****");
        assert_eq!(money::display(amount, false), amount);
    }
    assert_eq!(money::display("", true), "****");
    assert_eq!(money::display("", false), "");
}

#[test]
fn supported_image_files() {
    for path in [
        "clock.png",
        "clock.jpg",
        "clock.jpeg",
        "clock.bmp",
        "C:\\截图\\打卡.PnG",
        "C:/images/clock.JPEG",
        "/home/u/打卡.png",
    ] {
        assert!(is_supported(path), "{path}");
    }
    for path in [
        "",
        "clock",
        "clock.gif",
        "clock.webp",
        "clock.png.exe",
        "clock.png ",
        "C:\\images.png\\clock",
        "clock.png/",
        ".png",
        "clock.png:stream",
        "clock.png\0.exe",
    ] {
        assert!(!is_supported(path), "{path:?}");
    }
}

#[test]
fn autostart_quoting_and_entries() {
    assert_eq!(
        autostart::quote(r"C:\Program Files\OffWork\OffWork.exe"),
        r#""C:\Program Files\OffWork\OffWork.exe""#
    );
    assert_eq!(autostart::quote(r"D:\OffWork.exe"), r#""D:\OffWork.exe""#);
    assert_eq!(autostart::quote(""), "\"\"");
    for path in [
        r"C:\Program Files\OffWork\OffWork.exe",
        r"D:\OffWork.exe",
        r"C:\Users\Someone\AppData\Local\Programs\OffWork\OffWork.exe",
    ] {
        assert_eq!(autostart::unquote(&autostart::quote(path)), path);
    }
    assert_eq!(autostart::unquote(r"D:\OffWork.exe"), r"D:\OffWork.exe");
    assert_eq!(autostart::unquote(""), "");
    assert_eq!(autostart::unquote("\""), "\"");
    // Shared with installer/offwork.iss.
    assert_eq!(
        autostart::RUN_KEY,
        r"Software\Microsoft\Windows\CurrentVersion\Run"
    );
    assert_eq!(autostart::RUN_VALUE, "OffWork");
    let iss = std::fs::read_to_string(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../installer/offwork.iss"
    ))
    .unwrap();
    assert!(iss.contains(r#"#define RunKey "Software\Microsoft\Windows\CurrentVersion\Run""#));
    assert!(iss.contains(r#"#define RunValueName "OffWork""#));
    assert!(iss.contains("AppId={{8F3A1C2E-5B47-4D9A-9E61-7C0D2A6F4B83}"));

    let program = std::path::Path::new("/opt/Off Work/$HOME/off%work\"x");
    assert_eq!(
        autostart::desktop_exec(program),
        r#""/opt/Off Work/\$HOME/off%%work\"x""#
    );
    assert!(autostart::launch_agent(std::path::Path::new(
        "/Applications/A&B.app/Contents/MacOS/offwork"
    ))
    .contains("A&amp;B.app"));
}

#[test]
fn file_autostart_round_trip_in_a_temp_home() {
    let home = tempfile::tempdir().unwrap();
    let program = std::path::Path::new("/Applications/OffWork.app/Contents/MacOS/offwork");
    for entry in [
        autostart::FileEntry::launch_agent(home.path()),
        autostart::FileEntry::xdg(&home.path().join(".config")),
    ] {
        assert!(!entry.enabled());
        entry.set(false, program).unwrap(); // Disabling twice is fine.
        entry.set(true, program).unwrap();
        assert!(entry.enabled());
        assert_eq!(entry.target().as_deref(), Some(program.to_str().unwrap()));
        entry.set(true, program).unwrap(); // Idempotent.
        entry.set(false, program).unwrap();
        assert!(!entry.enabled() && !entry.path.exists());
    }
    // A desktop that disabled the entry is the truth, not the file's existence.
    let xdg = autostart::FileEntry::xdg(home.path());
    xdg.set(true, program).unwrap();
    std::fs::write(
        &xdg.path,
        autostart::desktop_entry(program).replace("enabled=true", "enabled=false"),
    )
    .unwrap();
    assert!(!xdg.enabled());
    // An unwritable location reports a readable error instead of pretending.
    let blocker = home.path().join("file");
    std::fs::write(&blocker, "").unwrap();
    let bad = autostart::FileEntry::xdg(&blocker);
    assert!(bad
        .set(true, program)
        .unwrap_err()
        .starts_with("无法开启开机自启"));
    assert!(!bad.enabled());
}

#[cfg(windows)]
#[test]
fn registry_round_trip_on_a_scratch_key() {
    use autostart::registry;
    let key = format!(r"Software\OffWorkTests\Run-{}", std::process::id());
    let exe = r"C:\Program Files\OffWork\OffWork.exe";
    assert_eq!(registry::read(&key, "OffWork"), None);
    registry::set(&key, "OffWork", true, exe).unwrap();
    assert_eq!(
        registry::read(&key, "OffWork").as_deref(),
        Some(r#""C:\Program Files\OffWork\OffWork.exe""#)
    );
    registry::set(&key, "OffWork", false, exe).unwrap();
    registry::set(&key, "OffWork", false, exe).unwrap();
    assert_eq!(registry::read(&key, "OffWork"), None);
    assert!(registry::set(&key, "OffWork", true, "").is_err());
    let _ = winreg::RegKey::predef(winreg::enums::HKEY_CURRENT_USER)
        .delete_subkey_all(r"Software\OffWorkTests");
}
