//! Settings migration from the C++ INI, in temporary user directories.
//! Covers: idempotent relaunch, failed write then retry, crash leftovers,
//! corrupt and newer JSON, unknown legacy keys, legacy lunch format, and
//! rollback (the INI is never touched, so the old app reads what it wrote).
mod common;
use common::Ini;
use offwork_core::settings::{EndMode, Settings};
use offwork_core::store::{self, Legacy, LegacyIni, Origin};
use std::path::{Path, PathBuf};

struct Home {
    _dir: tempfile::TempDir,
    json: PathBuf,
    ini: PathBuf,
}

/// `%LOCALAPPDATA%\OffWork` with an INI written by the C++ `SaveSettings`.
fn home(ini_text: Option<&str>) -> Home {
    let dir = tempfile::tempdir().unwrap();
    let app = dir.path().join("OffWork");
    std::fs::create_dir_all(&app).unwrap();
    let ini = app.join("settings.ini");
    if let Some(text) = ini_text {
        std::fs::write(&ini, text).unwrap();
    }
    Home {
        json: app.join("settings.json"),
        ini,
        _dir: dir,
    }
}

fn user_settings() -> Settings {
    Settings {
        start_minutes: 1320,
        end_minutes: 360,
        lunch_start_minutes: 1410,
        lunch_end_minutes: 30,
        daily_salary: 1234.57,
        scale_percent: 125,
        opacity_percent: 60,
        hide_earned: true,
        deduct_lunch: true,
        resignation_date: "2027-03-15".into(),
        show_resignation: true,
        end_mode: EndMode::EndTime,
    }
}

fn legacy_text(s: &Settings) -> String {
    let mut ini = Ini::default();
    ini.save(s);
    ini.text()
}

fn open(h: &Home) -> store::Loaded {
    store::open(
        &h.json,
        Some(&LegacyIni(h.ini.clone()) as &dyn Legacy),
        1_700_000_000,
    )
}

#[test]
fn first_launch_migrates_every_key_and_leaves_the_ini_alone() {
    let text = legacy_text(&user_settings());
    let h = home(Some(&text));
    let loaded = open(&h);
    assert_eq!(loaded.origin, Origin::Migrated(h.ini.clone()));
    assert_eq!(loaded.settings, user_settings());
    assert!(loaded.notice().contains("旧版仍读取原设置"));
    let json = std::fs::read_to_string(&h.json).unwrap();
    assert!(json.contains("\"schemaVersion\": 1"));
    assert_eq!(store::from_json(json.as_bytes()).unwrap(), user_settings());
    // Rollback: the old app still reads exactly what it wrote.
    assert_eq!(std::fs::read_to_string(&h.ini).unwrap(), text);
    assert_eq!(common::load_bytes(text.as_bytes()), user_settings());
    // No temp files remain.
    let names: Vec<_> = std::fs::read_dir(h.json.parent().unwrap())
        .unwrap()
        .map(|e| e.unwrap().file_name())
        .collect();
    assert_eq!(names.len(), 2, "{names:?}");
}

#[test]
fn relaunch_is_idempotent_and_ignores_later_ini_changes() {
    let h = home(Some(&legacy_text(&user_settings())));
    open(&h);
    let first = std::fs::read(&h.json).unwrap();
    // The user rolls back, changes something in 0.4.5, then upgrades again.
    std::fs::write(&h.ini, legacy_text(&Settings::default())).unwrap();
    for _ in 0..3 {
        let again = open(&h);
        assert_eq!(again.origin, Origin::Existing);
        assert_eq!(again.settings, user_settings());
        assert_eq!(std::fs::read(&h.json).unwrap(), first);
    }
}

#[test]
fn failed_write_is_retried_and_never_leaves_a_partial_file() {
    let h = home(Some(&legacy_text(&user_settings())));
    // Make the settings directory unusable: a file where the folder must be.
    let blocked = h.json.parent().unwrap().join("blocked");
    std::fs::write(&blocked, "").unwrap();
    let json = blocked.join("settings.json");
    let loaded = store::open(&json, Some(&LegacyIni(h.ini.clone()) as &dyn Legacy), 1);
    assert!(
        matches!(loaded.origin, Origin::MigrationPending { .. }),
        "{:?}",
        loaded.origin
    );
    assert!(loaded.writable());
    assert_eq!(
        loaded.settings,
        user_settings(),
        "the app still runs with the old settings"
    );
    assert!(loaded.notice().contains("重试"));
    assert!(!json.exists());
    assert_eq!(
        std::fs::read_to_string(&h.ini).unwrap(),
        legacy_text(&user_settings())
    );
    // Next launch, the problem is gone: the same import completes.
    std::fs::remove_file(&blocked).unwrap();
    let retried = store::open(&json, Some(&LegacyIni(h.ini.clone()) as &dyn Legacy), 2);
    assert_eq!(retried.origin, Origin::Migrated(h.ini.clone()));
    assert_eq!(
        store::from_json(&std::fs::read(&json).unwrap()).unwrap(),
        user_settings()
    );
}

#[test]
fn crash_leftovers_do_not_confuse_the_next_launch() {
    let h = home(Some(&legacy_text(&user_settings())));
    // A temp file from a run killed before the rename.
    std::fs::write(
        h.json.with_file_name(".settings-crash.tmp"),
        "{\"schemaVersion\":1,\"work\":",
    )
    .unwrap();
    let loaded = open(&h);
    assert_eq!(loaded.origin, Origin::Migrated(h.ini.clone()));
    assert_eq!(loaded.settings, user_settings());
}

#[test]
fn corrupt_json_is_backed_up_and_newer_json_is_never_overwritten() {
    let h = home(Some(&legacy_text(&user_settings())));
    std::fs::write(&h.json, "{ not json").unwrap();
    let loaded = open(&h);
    let backup = h.json.with_file_name("settings.json.bad-1700000000");
    assert_eq!(
        loaded.origin,
        Origin::Recovered {
            backup: backup.clone()
        }
    );
    assert_eq!(std::fs::read_to_string(&backup).unwrap(), "{ not json");
    assert_eq!(loaded.settings, user_settings(), "re-imported from the INI");
    assert!(loaded.writable());

    let newer = "{\"schemaVersion\": 2, \"work\": {\"startMinutes\": 1}}";
    std::fs::write(&h.json, newer).unwrap();
    let loaded = open(&h);
    assert!(matches!(loaded.origin, Origin::ReadOnly(_)));
    assert!(!loaded.writable());
    assert!(loaded.notice().contains("schema 2"));
    assert_eq!(std::fs::read_to_string(&h.json).unwrap(), newer);
    // Wrong types are corruption, not a newer version.
    std::fs::write(
        &h.json,
        "{\"schemaVersion\": 1, \"work\": {\"startMinutes\": \"9\"}}",
    )
    .unwrap();
    assert!(matches!(open(&h).origin, Origin::Recovered { .. }));
}

#[test]
fn unreadable_settings_are_kept_and_retried() {
    let h = home(Some(&legacy_text(&user_settings())));
    std::fs::create_dir(&h.json).unwrap(); // Reading a directory fails.
    let loaded = open(&h);
    assert!(
        matches!(loaded.origin, Origin::ReadOnly(_)),
        "{:?}",
        loaded.origin
    );
    assert!(h.json.is_dir());
    std::fs::remove_dir(&h.json).unwrap();
    assert!(matches!(open(&h).origin, Origin::Migrated(_)));
}

#[test]
fn no_ini_means_product_defaults() {
    let h = home(None);
    let loaded = open(&h);
    assert_eq!(loaded.origin, Origin::Fresh);
    assert_eq!(loaded.settings, Settings::default());
    assert!(
        !h.json.exists(),
        "defaults are written on the first real save"
    );
    let loaded = store::open(&h.json, None, 0);
    assert_eq!(loaded.origin, Origin::Fresh);
}

#[test]
fn unknown_keys_legacy_lunch_and_odd_values() {
    let text = "[work]\r\nstartMinutes=500\r\nendMinutes=1000\r\nlunchDurationMinutes=45\r\nfutureKey=1\r\n[telemetry]\r\nid=abc\r\n[window]\r\nscalePercent=999\r\nopacityPercent=5\r\n[resignation]\r\ndate=2026-02-30\r\nshow=1\r\n";
    let h = home(Some(text));
    let s = open(&h).settings;
    assert_eq!((s.start_minutes, s.end_minutes), (500, 1000));
    assert_eq!((s.lunch_start_minutes, s.lunch_end_minutes), (720, 765));
    assert_eq!((s.scale_percent, s.opacity_percent), (150, 25));
    assert!(s.resignation_date.is_empty() && s.show_resignation);
    assert_eq!(s.end_mode, EndMode::Duration);
}

#[test]
fn json_round_trip_and_unknown_fields() {
    let s = user_settings();
    let json = store::to_json(&s);
    assert_eq!(store::from_json(json.as_bytes()).unwrap(), s);
    // Missing sections take defaults; unknown fields are ignored; values are normalized.
    let partial = r#"{"schemaVersion":1,"window":{"scalePercent":10,"extra":true},"later":{}}"#;
    let loaded = store::from_json(partial.as_bytes()).unwrap();
    assert_eq!(
        loaded,
        Settings {
            scale_percent: 75,
            ..Settings::default()
        }
    );
    let mode = r#"{"schemaVersion":1,"work":{"endInputMode":"sideways"}}"#;
    assert!(store::from_json(mode.as_bytes()).is_err());
}

#[test]
fn save_is_atomic_and_verified() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("deep/offwork/settings.json");
    store::save(&path, &user_settings()).unwrap();
    store::save(&path, &Settings::default()).unwrap();
    assert_eq!(
        store::from_json(&std::fs::read(&path).unwrap()).unwrap(),
        Settings::default()
    );
    assert_eq!(
        std::fs::read_dir(path.parent().unwrap()).unwrap().count(),
        1
    );
    assert!(store::save(Path::new("/"), &Settings::default()).is_err());
}

#[test]
fn default_paths_follow_the_plan() {
    let path = store::paths::settings_json().unwrap();
    let text = path.to_string_lossy().replace('\\', "/");
    #[cfg(windows)]
    {
        assert!(text.ends_with("/OffWork/settings.json"), "{text}");
        assert!(store::paths::legacy_ini()
            .unwrap()
            .ends_with(r"OffWork\settings.ini"));
    }
    #[cfg(target_os = "macos")]
    assert!(
        text.ends_with("Library/Application Support/OffWork/settings.json"),
        "{text}"
    );
    #[cfg(all(unix, not(target_os = "macos")))]
    assert!(text.ends_with("offwork/settings.json"), "{text}");
}

#[cfg(unix)]
#[test]
fn read_only_folder_fails_the_write_then_retries() {
    use std::os::unix::fs::PermissionsExt;
    let h = home(Some(&legacy_text(&user_settings())));
    let dir = h.json.parent().unwrap();
    std::fs::set_permissions(dir, std::fs::Permissions::from_mode(0o555)).unwrap();
    let loaded = open(&h);
    std::fs::set_permissions(dir, std::fs::Permissions::from_mode(0o755)).unwrap();
    if nix_root() {
        return; // root ignores the mode bits
    }
    assert!(
        matches!(loaded.origin, Origin::MigrationPending { .. }),
        "{:?}",
        loaded.origin
    );
    assert!(!h.json.exists());
    assert_eq!(open(&h).origin, Origin::Migrated(h.ini.clone()));
}

#[cfg(unix)]
fn nix_root() -> bool {
    std::process::Command::new("id")
        .arg("-u")
        .output()
        .is_ok_and(|o| o.stdout.starts_with(b"0\n"))
}

#[test]
fn unknown_settings_folder_still_shows_the_legacy_settings_read_only() {
    // Review finding 6: with no JSON location the WINDIR fallback INI used
    // to be computed but never read.
    let h = home(Some(&legacy_text(&user_settings())));
    let loaded = store::open_any(None, Some(&LegacyIni(h.ini.clone()) as &dyn Legacy), 1);
    assert_eq!(loaded.settings, user_settings());
    assert!(!loaded.writable());
    assert_eq!(
        std::fs::read_to_string(&h.ini).unwrap(),
        legacy_text(&user_settings())
    );
    let none = store::open_any(None, None, 1);
    assert_eq!(none.settings, Settings::default());
    // With a location it is the normal import.
    assert_eq!(
        store::open_any(
            Some(&h.json),
            Some(&LegacyIni(h.ini.clone()) as &dyn Legacy),
            1
        )
        .origin,
        Origin::Migrated(h.ini.clone())
    );
}

#[cfg(windows)]
#[test]
fn legacy_path_follows_the_known_folder_not_the_environment() {
    // Review finding 6: the C++ app used SHGetKnownFolderPath, so a launcher
    // that overrides LOCALAPPDATA must not change where 0.4.5's INI is read.
    let real = store::paths::local_app_data().unwrap();
    let bogus = tempfile::tempdir().unwrap();
    let saved = std::env::var_os("LOCALAPPDATA");
    std::env::set_var("LOCALAPPDATA", bogus.path());
    let ini = store::paths::legacy_ini().unwrap();
    let json = store::paths::settings_json().unwrap();
    match saved {
        Some(v) => std::env::set_var("LOCALAPPDATA", v),
        None => std::env::remove_var("LOCALAPPDATA"),
    }
    assert_eq!(ini, real.join("OffWork").join("settings.ini"));
    assert_eq!(json, real.join("OffWork").join("settings.json"));
    assert!(!ini.starts_with(bogus.path()));
}
