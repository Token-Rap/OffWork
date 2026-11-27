//! Start at login. The system is the single source of truth: nothing is
//! mirrored into settings, so the app, the tray and the installer agree.
//!
//! - Windows: `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` value
//!   `OffWork`, a quoted path (shared with `installer/offwork.iss`).
//! - macOS: a per-user LaunchAgent plist.
//! - Linux: an XDG autostart desktop entry.

use std::path::{Path, PathBuf};

pub const RUN_KEY: &str = r"Software\Microsoft\Windows\CurrentVersion\Run";
pub const RUN_VALUE: &str = "OffWork";
pub const LAUNCH_AGENT_LABEL: &str = "io.github.token-rap.offwork";
pub const DESKTOP_FILE: &str = "offwork.desktop";

/// The exact Run value: quoted so a path with spaces is one argument.
pub fn quote(path: &str) -> String {
    format!("\"{path}\"")
}

/// The path a quoted Run value points at; unquoted input is returned as is.
pub fn unquote(value: &str) -> &str {
    if value.len() >= 2 && value.starts_with('"') && value.ends_with('"') {
        &value[1..value.len() - 1]
    } else {
        value
    }
}

fn xml_escape(s: &str) -> String {
    s.replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
}

pub fn launch_agent(program: &Path) -> String {
    format!(
        r#"<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>{LAUNCH_AGENT_LABEL}</string>
  <key>ProgramArguments</key>
  <array>
    <string>{}</string>
  </array>
  <key>RunAtLoad</key>
  <true/>
  <key>ProcessType</key>
  <string>Interactive</string>
</dict>
</plist>
"#,
        xml_escape(&program.to_string_lossy())
    )
}

/// Desktop Entry `Exec` quoting: always quoted, with `"`, `` ` ``, `$` and
/// `\` escaped, and `%` doubled so it is not a field code.
pub fn desktop_exec(program: &Path) -> String {
    let mut out = String::from("\"");
    for c in program.to_string_lossy().chars() {
        match c {
            '"' | '`' | '$' | '\\' => {
                out.push('\\');
                out.push(c)
            }
            '%' => out.push_str("%%"),
            _ => out.push(c),
        }
    }
    out.push('"');
    out
}

pub fn desktop_entry(program: &Path) -> String {
    format!(
        "[Desktop Entry]\nType=Application\nName=OffWork\nComment=下班手账\nExec={}\nTerminal=false\nX-GNOME-Autostart-enabled=true\n",
        desktop_exec(program)
    )
}

/// A file-based autostart entry (macOS LaunchAgent, XDG desktop file).
pub struct FileEntry {
    pub path: PathBuf,
    pub content: fn(&Path) -> String,
}

impl FileEntry {
    pub fn launch_agent(home: &Path) -> Self {
        Self {
            path: home
                .join("Library/LaunchAgents")
                .join(format!("{LAUNCH_AGENT_LABEL}.plist")),
            content: launch_agent,
        }
    }
    pub fn xdg(config_home: &Path) -> Self {
        Self {
            path: config_home.join("autostart").join(DESKTOP_FILE),
            content: desktop_entry,
        }
    }
    /// Enabled when the entry exists and is not switched off by the desktop.
    pub fn enabled(&self) -> bool {
        std::fs::read_to_string(&self.path).is_ok_and(|text| {
            !text.lines().any(|l| {
                let l = l.trim().replace(' ', "");
                l.eq_ignore_ascii_case("Hidden=true")
                    || l.eq_ignore_ascii_case("X-GNOME-Autostart-enabled=false")
                    || l.eq_ignore_ascii_case("<key>Disabled</key><true/>")
            })
        })
    }
    pub fn set(&self, enabled: bool, program: &Path) -> Result<(), String> {
        if enabled {
            let dir = self.path.parent().ok_or("无效路径")?;
            std::fs::create_dir_all(dir).map_err(|e| format!("无法开启开机自启: {e}"))?;
            std::fs::write(&self.path, (self.content)(program))
                .map_err(|e| format!("无法开启开机自启: {e}"))
        } else {
            match std::fs::remove_file(&self.path) {
                Err(e) if e.kind() != std::io::ErrorKind::NotFound => {
                    Err(format!("无法关闭开机自启: {e}"))
                }
                _ => Ok(()),
            }
        }
    }
    pub fn target(&self) -> Option<String> {
        let text = std::fs::read_to_string(&self.path).ok()?;
        text.lines()
            .find_map(|l| l.strip_prefix("Exec="))
            .map(|v| v.trim_matches('"').replace("\\\\", "\\"))
            .or_else(|| {
                let start = text.find("<array>")?;
                let s = &text[start..];
                let a = s.find("<string>")? + 8;
                let b = s.find("</string>")?;
                Some(s[a..b].replace("&amp;", "&"))
            })
    }
}

#[cfg(windows)]
pub mod registry {
    //! `src/autostart.cpp` in Rust. `key` is a parameter so tests can use a
    //! scratch key instead of the real Run key.
    use winreg::enums::{HKEY_CURRENT_USER, KEY_QUERY_VALUE, KEY_SET_VALUE};
    use winreg::RegKey;

    pub fn read(key: &str, value: &str) -> Option<String> {
        RegKey::predef(HKEY_CURRENT_USER)
            .open_subkey_with_flags(key, KEY_QUERY_VALUE)
            .ok()?
            .get_value::<String, _>(value)
            .ok()
            .filter(|v| !v.is_empty())
    }

    pub fn set(key: &str, value: &str, enabled: bool, program: &str) -> Result<(), String> {
        let hkcu = RegKey::predef(HKEY_CURRENT_USER);
        if enabled {
            if program.is_empty() {
                return Err("无法读取程序路径, 开机自启未改动".into());
            }
            let (run, _) = hkcu
                .create_subkey(key)
                .map_err(|e| format!("无法打开启动项: {e}"))?;
            run.set_value(value, &super::quote(program))
                .map_err(|e| format!("无法开启开机自启: {e}"))
        } else {
            let run = hkcu
                .open_subkey_with_flags(key, KEY_SET_VALUE)
                .map_err(|e| format!("无法打开启动项: {e}"))?;
            match run.delete_value(value) {
                Err(e) if e.kind() != std::io::ErrorKind::NotFound => {
                    Err(format!("无法关闭开机自启: {e}"))
                }
                _ => Ok(()),
            }
        }
    }
}
