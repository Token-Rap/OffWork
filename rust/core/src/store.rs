//! `settings.json` (schema 1) and the one-time import from the C++ INI.
//!
//! Rules (docs/rewrite/PLAN.md, 设置迁移与回退):
//! - A supported `settings.json` wins; the INI is never read again.
//! - Only when it is missing is the INI read, normalized, written to a temp
//!   file in the same directory, flushed, atomically renamed, re-read and
//!   compared. Only then is the migration complete.
//! - The INI is never modified, so the old app keeps working after a rollback.
//! - A failed write leaves no `settings.json`, so the next launch (or the
//!   next save) retries. A newer schema is never overwritten; a corrupt file
//!   is kept as a backup before starting over.

use crate::settings::{EndMode, Settings};
use serde::{Deserialize, Serialize};
use std::io::Write;
use std::path::{Path, PathBuf};

pub const SCHEMA_VERSION: u32 = 1;

#[derive(Deserialize, Serialize)]
#[serde(rename_all = "camelCase", default)]
struct Work {
    start_minutes: i32,
    end_minutes: i32,
    end_input_mode: EndMode,
    lunch_start_minutes: i32,
    lunch_end_minutes: i32,
    daily_salary: f64,
    deduct_lunch: bool,
}

#[derive(Deserialize, Serialize)]
#[serde(rename_all = "camelCase", default)]
struct Window {
    scale_percent: i32,
    opacity_percent: i32,
    hide_earned: bool,
}

#[derive(Default, Deserialize, Serialize)]
#[serde(default)]
struct Resignation {
    date: String,
    show: bool,
}

#[derive(Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
struct File {
    schema_version: u32,
    #[serde(default)]
    work: Work,
    #[serde(default)]
    window: Window,
    #[serde(default)]
    resignation: Resignation,
}

impl Default for Work {
    fn default() -> Self {
        File::from(&Settings::default()).work
    }
}
impl Default for Window {
    fn default() -> Self {
        File::from(&Settings::default()).window
    }
}

impl From<&Settings> for File {
    fn from(s: &Settings) -> Self {
        File {
            schema_version: SCHEMA_VERSION,
            work: Work {
                start_minutes: s.start_minutes,
                end_minutes: s.end_minutes,
                end_input_mode: s.end_mode,
                lunch_start_minutes: s.lunch_start_minutes,
                lunch_end_minutes: s.lunch_end_minutes,
                daily_salary: s.daily_salary,
                deduct_lunch: s.deduct_lunch,
            },
            window: Window {
                scale_percent: s.scale_percent,
                opacity_percent: s.opacity_percent,
                hide_earned: s.hide_earned,
            },
            resignation: Resignation {
                date: s.resignation_date.clone(),
                show: s.show_resignation,
            },
        }
    }
}

impl From<File> for Settings {
    fn from(f: File) -> Self {
        Settings {
            start_minutes: f.work.start_minutes,
            end_minutes: f.work.end_minutes,
            lunch_start_minutes: f.work.lunch_start_minutes,
            lunch_end_minutes: f.work.lunch_end_minutes,
            daily_salary: f.work.daily_salary,
            scale_percent: f.window.scale_percent,
            opacity_percent: f.window.opacity_percent,
            hide_earned: f.window.hide_earned,
            deduct_lunch: f.work.deduct_lunch,
            resignation_date: f.resignation.date,
            show_resignation: f.resignation.show,
            end_mode: f.work.end_input_mode,
        }
        .normalized()
    }
}

pub fn to_json(s: &Settings) -> String {
    serde_json::to_string_pretty(&File::from(&s.normalized())).expect("settings serialize") + "\n"
}

#[derive(Debug)]
pub enum ParseError {
    /// Written by a newer OffWork; keep it untouched.
    Newer(u32),
    Corrupt(String),
}

pub fn from_json(bytes: &[u8]) -> Result<Settings, ParseError> {
    #[derive(Deserialize)]
    #[serde(rename_all = "camelCase")]
    struct Header {
        schema_version: u32,
    }
    let header: Header =
        serde_json::from_slice(bytes).map_err(|e| ParseError::Corrupt(e.to_string()))?;
    match header.schema_version {
        SCHEMA_VERSION => serde_json::from_slice::<File>(bytes)
            .map(Settings::from)
            .map_err(|e| ParseError::Corrupt(e.to_string())),
        0 => Err(ParseError::Corrupt("schemaVersion 0".into())),
        newer => Err(ParseError::Newer(newer)),
    }
}

/// Atomic write: temp file in the same directory, flush, rename, re-read and
/// compare. On any failure the target is not left half-written.
pub fn save(path: &Path, s: &Settings) -> std::io::Result<()> {
    let dir = path
        .parent()
        .ok_or_else(|| std::io::Error::other("no parent directory"))?;
    std::fs::create_dir_all(dir)?;
    let json = to_json(s);
    let mut file = tempfile::Builder::new()
        .prefix(".settings-")
        .suffix(".tmp")
        .tempfile_in(dir)?;
    file.write_all(json.as_bytes())?;
    file.as_file().sync_all()?;
    file.persist(path).map_err(|e| e.error)?;
    let back = std::fs::read(path)?;
    if back != json.as_bytes() {
        return Err(std::io::Error::other("settings.json read-back mismatch"));
    }
    Ok(())
}

/// Where the settings came from on this launch.
#[derive(Clone, Debug, PartialEq)]
pub enum Origin {
    /// A supported `settings.json` was read.
    Existing,
    /// No JSON and no INI: product defaults (macOS/Linux first run).
    Fresh,
    /// Imported from the INI and written as JSON this launch.
    Migrated(PathBuf),
    /// Imported from the INI but the JSON write failed; retried on next save
    /// and next launch. The INI is untouched.
    MigrationPending { ini: PathBuf, error: String },
    /// The JSON is from a newer version or unreadable; saving is disabled so
    /// it is never overwritten.
    ReadOnly(String),
    /// A corrupt JSON was moved aside and the settings started over.
    Recovered { backup: PathBuf },
}

#[derive(Clone, Debug)]
pub struct Loaded {
    pub settings: Settings,
    pub origin: Origin,
}

impl Loaded {
    pub fn writable(&self) -> bool {
        !matches!(self.origin, Origin::ReadOnly(_))
    }
    /// User-facing status line, empty when there is nothing to say.
    pub fn notice(&self) -> String {
        match &self.origin {
            Origin::Existing | Origin::Fresh => String::new(),
            Origin::Migrated(_) => {
                "已导入旧版设置；之后的修改只保存在新版，旧版仍读取原设置".into()
            }
            Origin::MigrationPending { error, .. } => {
                format!("旧版设置已读取，但保存失败（{error}），下次保存或启动时重试")
            }
            Origin::ReadOnly(reason) => format!("{reason}，本次修改不会保存"),
            Origin::Recovered { backup } => {
                format!("设置文件已损坏，已备份为 {}", backup.display())
            }
        }
    }
}

/// A source of legacy settings, so tests can inject one without Windows.
pub trait Legacy {
    fn path(&self) -> &Path;
    fn read(&self) -> std::io::Result<Settings>;
}

/// Reads the INI with the platform's profile rules.
pub struct LegacyIni(pub PathBuf);

impl Legacy for LegacyIni {
    fn path(&self) -> &Path {
        &self.0
    }
    fn read(&self) -> std::io::Result<Settings> {
        // Read the bytes first: a missing or locked file is an error here,
        // not "defaults", so it can be retried instead of silently lost.
        let bytes = std::fs::read(&self.0)?;
        #[cfg(windows)]
        {
            let _ = bytes;
            Ok(crate::legacy::load(
                &crate::legacy::win32::Win32Profile::new(&self.0),
            ))
        }
        #[cfg(not(windows))]
        Ok(crate::legacy::load(&crate::legacy::IniText::parse(&bytes)))
    }
}

fn backup_path(path: &Path, stamp: u64) -> PathBuf {
    let mut name = path.file_name().unwrap_or_default().to_os_string();
    name.push(format!(".bad-{stamp}"));
    path.with_file_name(name)
}

fn import(path: &Path, legacy: Option<&dyn Legacy>) -> Loaded {
    let Some(ini) = legacy.filter(|l| l.path().is_file()) else {
        return Loaded {
            settings: Settings::default(),
            origin: Origin::Fresh,
        };
    };
    let settings = match ini.read() {
        Ok(s) => s,
        Err(e) => {
            return Loaded {
                settings: Settings::default(),
                origin: Origin::ReadOnly(format!("无法读取旧版设置（{e}），请重启重试")),
            }
        }
    };
    let origin = match save(path, &settings) {
        Ok(()) => Origin::Migrated(ini.path().to_owned()),
        Err(e) => {
            let _ = std::fs::remove_file(path);
            Origin::MigrationPending {
                ini: ini.path().to_owned(),
                error: e.to_string(),
            }
        }
    };
    Loaded { settings, origin }
}

/// Opens the settings for this launch. Call after taking the single-instance
/// lock. Idempotent: once `settings.json` exists the INI is ignored.
pub fn open(path: &Path, legacy: Option<&dyn Legacy>, now_unix: u64) -> Loaded {
    match std::fs::read(path) {
        Ok(bytes) => match from_json(&bytes) {
            Ok(settings) => Loaded {
                settings,
                origin: Origin::Existing,
            },
            Err(ParseError::Newer(v)) => Loaded {
                settings: Settings::default(),
                origin: Origin::ReadOnly(format!("设置文件来自更新的版本（schema {v}）")),
            },
            Err(ParseError::Corrupt(_)) => {
                let backup = backup_path(path, now_unix);
                if let Err(e) = std::fs::rename(path, &backup) {
                    return Loaded {
                        settings: Settings::default(),
                        origin: Origin::ReadOnly(format!("设置文件已损坏且无法备份（{e}）")),
                    };
                }
                let mut loaded = import(path, legacy);
                if !matches!(loaded.origin, Origin::ReadOnly(_)) {
                    loaded.origin = Origin::Recovered { backup };
                }
                loaded
            }
        },
        // A path through a file cannot hold settings either: import, and let
        // the write report why it failed so the next launch retries.
        Err(e)
            if matches!(
                e.kind(),
                std::io::ErrorKind::NotFound | std::io::ErrorKind::NotADirectory
            ) =>
        {
            import(path, legacy)
        }
        Err(e) => Loaded {
            settings: Settings::default(),
            origin: Origin::ReadOnly(format!("无法读取设置（{e}），请重启重试")),
        },
    }
}

/// Opens settings when the JSON location may be unknown. Without a place to
/// save, the legacy settings are still shown (read-only) instead of defaults,
/// so the user never sees their schedule silently replaced.
pub fn open_any(path: Option<&Path>, legacy: Option<&dyn Legacy>, now_unix: u64) -> Loaded {
    if let Some(path) = path {
        return open(path, legacy, now_unix);
    }
    let settings = legacy
        .filter(|l| l.path().is_file())
        .and_then(|l| l.read().ok())
        .unwrap_or_default();
    Loaded {
        settings,
        origin: Origin::ReadOnly("无法确定设置目录".into()),
    }
}

/// Default locations. Windows keeps the old app directory and resolves it
/// with the same Win32 rule as the C++ app (the LocalAppData known folder,
/// not the LOCALAPPDATA variable a launcher may override).
///
/// `OFFWORK_PROFILE_DIR` replaces the whole folder; it exists for the CI
/// smoke tests, which must not touch the runner's real profile.
pub mod paths {
    use std::path::PathBuf;

    const OVERRIDE: &str = "OFFWORK_PROFILE_DIR";

    fn overridden(name: &str) -> Option<PathBuf> {
        std::env::var_os(OVERRIDE)
            .filter(|v| !v.is_empty())
            .map(|d| PathBuf::from(d).join(name))
    }

    #[cfg(windows)]
    mod win32 {
        use std::os::windows::ffi::OsStringExt;
        use std::path::PathBuf;
        use windows_sys::Win32::System::Com::CoTaskMemFree;
        use windows_sys::Win32::System::SystemInformation::GetWindowsDirectoryW;
        use windows_sys::Win32::UI::Shell::{
            FOLDERID_LocalAppData, SHGetKnownFolderPath, KF_FLAG_CREATE,
        };

        /// `SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE)`, as
        /// `SettingsFilePath()` in src/settings.cpp.
        pub fn local_app_data() -> Option<PathBuf> {
            let mut raw = std::ptr::null_mut();
            // SAFETY: on success the shell allocates a NUL-terminated wide
            // string that we copy and free with CoTaskMemFree exactly once.
            unsafe {
                let hr = SHGetKnownFolderPath(
                    &FOLDERID_LocalAppData,
                    KF_FLAG_CREATE as _,
                    std::ptr::null_mut(),
                    &mut raw,
                );
                if hr < 0 || raw.is_null() {
                    if !raw.is_null() {
                        CoTaskMemFree(raw as _);
                    }
                    return None;
                }
                let len = (0..).take_while(|&i| *raw.add(i) != 0).count();
                let path = std::ffi::OsString::from_wide(std::slice::from_raw_parts(raw, len));
                CoTaskMemFree(raw as _);
                Some(PathBuf::from(path))
            }
        }

        /// Where the profile API puts a relative file name.
        pub fn windows_dir() -> Option<PathBuf> {
            let mut buf = [0u16; 260];
            // SAFETY: the buffer length is passed and the result is bounded by it.
            let n = unsafe { GetWindowsDirectoryW(buf.as_mut_ptr(), buf.len() as u32) } as usize;
            (n > 0 && n < buf.len())
                .then(|| PathBuf::from(std::ffi::OsString::from_wide(&buf[..n])))
        }
    }

    #[cfg(windows)]
    pub fn local_app_data() -> Option<PathBuf> {
        win32::local_app_data()
    }

    #[cfg(windows)]
    pub fn settings_json() -> Option<PathBuf> {
        overridden("settings.json").or_else(|| {
            Some(
                win32::local_app_data()?
                    .join("OffWork")
                    .join("settings.json"),
            )
        })
    }
    /// `%LocalAppData%\OffWork\settings.ini`; when that known folder is
    /// unavailable the C++ app used the relative name `OffWork.ini`, which the
    /// profile API resolves in the Windows directory.
    #[cfg(windows)]
    pub fn legacy_ini() -> Option<PathBuf> {
        overridden("settings.ini").or_else(|| match win32::local_app_data() {
            Some(d) => Some(d.join("OffWork").join("settings.ini")),
            None => Some(win32::windows_dir()?.join("OffWork.ini")),
        })
    }
    #[cfg(target_os = "macos")]
    pub fn settings_json() -> Option<PathBuf> {
        overridden("settings.json").or_else(|| {
            std::env::var_os("HOME")
                .map(|h| PathBuf::from(h).join("Library/Application Support/OffWork/settings.json"))
        })
    }
    #[cfg(all(unix, not(target_os = "macos")))]
    pub fn settings_json() -> Option<PathBuf> {
        if let Some(path) = overridden("settings.json") {
            return Some(path);
        }
        let base = std::env::var_os("XDG_CONFIG_HOME")
            .filter(|v| !v.is_empty())
            .map(PathBuf::from)
            .filter(|p| p.is_absolute())
            .or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".config")))?;
        Some(base.join("offwork").join("settings.json"))
    }
    #[cfg(not(windows))]
    pub fn legacy_ini() -> Option<PathBuf> {
        None
    }
}
