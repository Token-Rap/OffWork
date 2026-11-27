//! Login item, read from the system every time (never cached in settings).

use crate::model::Autostart;
use offwork_core::autostart as core;
use std::path::PathBuf;

fn program() -> Option<PathBuf> {
    // An AppImage must be started through the image, not its mount point.
    if let Some(image) = std::env::var_os("APPIMAGE") {
        return Some(image.into());
    }
    std::env::current_exe().ok()
}

#[cfg(windows)]
fn read() -> Option<String> {
    core::registry::read(core::RUN_KEY, core::RUN_VALUE)
}
#[cfg(windows)]
fn write(enabled: bool) -> Result<(), String> {
    let exe = program().map(|p| dunce::simplified(&p).to_string_lossy().into_owned());
    core::registry::set(
        core::RUN_KEY,
        core::RUN_VALUE,
        enabled,
        &exe.unwrap_or_default(),
    )
}

#[cfg(not(windows))]
fn entry() -> Option<core::FileEntry> {
    let home = PathBuf::from(std::env::var_os("HOME")?);
    #[cfg(target_os = "macos")]
    return Some(core::FileEntry::launch_agent(&home));
    #[cfg(not(target_os = "macos"))]
    {
        let config = std::env::var_os("XDG_CONFIG_HOME")
            .map(PathBuf::from)
            .filter(|p| p.is_absolute())
            .unwrap_or_else(|| home.join(".config"));
        Some(core::FileEntry::xdg(&config))
    }
}
#[cfg(not(windows))]
fn read() -> Option<String> {
    let entry = entry()?;
    entry.enabled().then(|| entry.target().unwrap_or_default())
}
#[cfg(not(windows))]
fn write(enabled: bool) -> Result<(), String> {
    let entry = entry().ok_or("无法确定用户目录, 开机自启未改动")?;
    let exe = program().ok_or("无法读取程序路径, 开机自启未改动")?;
    entry.set(enabled, &exe)
}

pub fn state(error: Option<String>) -> Autostart {
    let value = read();
    let here = program().map(|p| p.to_string_lossy().into_owned());
    let other_target = value.as_deref().map(core::unquote).and_then(|target| {
        let same = here
            .as_deref()
            .is_some_and(|h| h.eq_ignore_ascii_case(target));
        (!same && !target.is_empty()).then(|| target.to_owned())
    });
    Autostart {
        enabled: value.is_some(),
        other_target,
        error,
    }
}

/// Writes, then reads back: the result always reflects the real state.
pub fn set(enabled: bool) -> Autostart {
    let error = match write(enabled) {
        Err(e) => Some(e),
        Ok(()) if read().is_some() != enabled => Some("开机自启未生效, 请检查系统设置".into()),
        Ok(()) => None,
    };
    state(error)
}
