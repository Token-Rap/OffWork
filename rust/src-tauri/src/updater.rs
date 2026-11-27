//! Background check of the latest stable GitHub release and a verified
//! download (official HTTPS hosts only, manual redirects, size limit, exact
//! length, SHA-256 from the release's SHA256SUMS).
//!
//! Windows keeps the Inno installer: `OffWork-Setup.exe /VERYSILENT` closes
//! this app through the legacy window and relaunches it. macOS opens the
//! DMG; an AppImage replaces itself and restarts; a .deb install only links
//! to the release page because the package manager owns it.

use crate::model::VERSION;
use offwork_core::update::{self, CheckSchedule, ReleaseInfo};
use serde::Serialize;
use sha2::Digest;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::time::{Duration, Instant};
use tauri::{AppHandle, Emitter, Manager};

#[derive(Clone, Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct UpdateView {
    pub available: Option<String>,
    pub downloading: bool,
    /// -1 unknown length, 0–99 downloading, 100 verified.
    pub percent: i32,
    pub error: Option<String>,
    /// True when the platform installs in place; false opens the release page.
    pub installs: bool,
}

#[derive(Default)]
pub struct Updater {
    started: Option<Instant>,
    schedule: CheckSchedule,
    checking: bool,
    release: Option<ReleaseInfo>,
    view: UpdateView,
}

pub struct UpdateState(pub Mutex<Updater>);

pub fn asset() -> Option<&'static str> {
    if cfg!(windows) {
        Some("OffWork-Setup.exe")
    } else if cfg!(all(target_os = "macos", target_arch = "aarch64")) {
        Some("OffWork-macOS-arm64.dmg")
    } else if cfg!(all(target_os = "linux", target_arch = "x86_64")) {
        std::env::var_os("APPIMAGE").map(|_| "OffWork-linux-x86_64.AppImage")
    } else {
        None
    }
}

/// The asset to look for when checking: a .deb still learns about updates.
fn check_asset() -> &'static str {
    asset().unwrap_or(if cfg!(target_os = "linux") {
        "OffWork-linux-amd64.deb"
    } else {
        "SHA256SUMS"
    })
}

fn agent() -> Result<ureq::Agent, String> {
    let tls = native_tls::TlsConnector::new().map_err(|e| e.to_string())?;
    Ok(ureq::AgentBuilder::new()
        .redirects(0)
        .timeout_connect(Duration::from_secs(5))
        .timeout_read(Duration::from_secs(15))
        .user_agent(&format!("OffWork/{VERSION}"))
        .tls_connector(std::sync::Arc::new(tls))
        .build())
}

/// GET with every redirect target re-checked against the allow list.
fn get(agent: &ureq::Agent, url: &str, accept: &str) -> Result<ureq::Response, String> {
    let mut url = url.to_owned();
    for _ in 0..=5 {
        if !update::allowed_url(&url) {
            return Err("更新地址不受信任".into());
        }
        let response = match agent.get(&url).set("Accept", accept).call() {
            Ok(r) => r,
            Err(ureq::Error::Status(_, r)) => r,
            Err(e) => return Err(e.to_string()),
        };
        match response.status() {
            200 => return Ok(response),
            301 | 302 | 303 | 307 | 308 => {
                let location = response.header("location").ok_or("重定向缺少地址")?;
                url = update::resolve_redirect(&url, location).ok_or("更新重定向不受信任")?;
            }
            status => return Err(format!("服务器返回 {status}")),
        }
    }
    Err("重定向次数过多".into())
}

fn read_limited(response: ureq::Response, limit: u64) -> Result<Vec<u8>, String> {
    let mut bytes = Vec::new();
    response
        .into_reader()
        .take(limit + 1)
        .read_to_end(&mut bytes)
        .map_err(|e| e.to_string())?;
    if bytes.len() as u64 > limit {
        return Err("响应过大".into());
    }
    Ok(bytes)
}

/// `Ok(None)`: a valid check that found nothing newer (still a success).
pub fn check() -> Result<Option<ReleaseInfo>, String> {
    let agent = agent()?;
    let asset = check_asset();
    // Read only the redirect of /releases/latest; never follow it to HTML.
    let mut info = match agent.get(update::LATEST_URL).call() {
        Ok(r) | Err(ureq::Error::Status(_, r)) => r
            .header("location")
            .and_then(|l| update::parse_release_location(l, asset)),
        Err(_) => None,
    };
    if info.is_none() {
        let response = get(&agent, update::LATEST_API, "application/vnd.github+json")?;
        let body = read_limited(response, update::MAX_JSON_BYTES as u64)?;
        info = update::parse_release_info(&String::from_utf8_lossy(&body), asset);
    }
    let info = info.ok_or("未找到有效的稳定版本")?;
    Ok(update::is_newer(&info.tag, VERSION).then_some(info))
}

/// Unique folder per download; incomplete files are removed with it.
fn download(
    info: &ReleaseInfo,
    name: &str,
    progress: impl Fn(i32),
) -> Result<(tempfile::TempDir, PathBuf), String> {
    let agent = agent()?;
    let manifest = read_limited(get(&agent, &info.checksums_url, "*/*")?, 64 * 1024)?;
    let expected = update::checksum_for(&String::from_utf8_lossy(&manifest), name)
        .ok_or("新版本缺少校验信息，已取消下载")?;
    let response = get(&agent, &info.asset_url, "*/*")?;
    let total = response
        .header("content-length")
        .and_then(|v| v.parse::<u64>().ok());
    if total.is_some_and(|t| t > update::MAX_DOWNLOAD_BYTES) {
        return Err("安装包过大".into());
    }
    let dir = tempfile::Builder::new()
        .prefix("OffWork-update-")
        .tempdir()
        .map_err(|e| e.to_string())?;
    let path = dir.path().join(name);
    let mut file = std::fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&path)
        .map_err(|e| e.to_string())?;
    let mut hasher = sha2::Sha256::new();
    let mut reader = response.into_reader();
    let mut buf = vec![0u8; 64 * 1024];
    let (mut received, mut last, deadline) = (0u64, -2, Instant::now() + Duration::from_secs(300));
    progress(update::download_percent(0, total));
    loop {
        if Instant::now() > deadline {
            return Err("下载超时".into());
        }
        let n = reader.read(&mut buf).map_err(|e| e.to_string())?;
        if n == 0 {
            break;
        }
        received += n as u64;
        if received > update::MAX_DOWNLOAD_BYTES {
            return Err("安装包过大".into());
        }
        hasher.update(&buf[..n]);
        file.write_all(&buf[..n]).map_err(|e| e.to_string())?;
        let percent = update::download_percent(received, total);
        if percent != last || percent == -1 {
            progress(percent);
            last = percent;
        }
    }
    file.sync_all().map_err(|e| e.to_string())?;
    if total.is_some_and(|t| t != received) || received == 0 {
        return Err("下载不完整".into());
    }
    if <[u8; 32]>::from(hasher.finalize()) != expected {
        return Err("安装包校验失败，已删除".into());
    }
    progress(100);
    Ok((dir, path))
}

#[cfg(windows)]
fn install(dir: tempfile::TempDir, path: &Path) -> Result<(), String> {
    let mut magic = [0u8; 2];
    std::fs::File::open(path)
        .and_then(|mut f| f.read_exact(&mut magic))
        .map_err(|e| e.to_string())?;
    if &magic != b"MZ" {
        return Err("安装包格式无效".into());
    }
    std::process::Command::new(path)
        .args(["/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"])
        .current_dir(dir.path())
        .spawn()
        .map_err(|e| format!("无法启动安装程序：{e}"))?;
    // The installer is running from this folder; keep it.
    let _ = dir.keep();
    Ok(())
}

#[cfg(target_os = "macos")]
fn install(dir: tempfile::TempDir, path: &Path) -> Result<(), String> {
    std::process::Command::new("open")
        .arg(path)
        .spawn()
        .map_err(|e| format!("无法打开安装包：{e}"))?;
    let _ = dir.keep();
    Ok(())
}

#[cfg(all(unix, not(target_os = "macos")))]
fn install(_dir: tempfile::TempDir, path: &Path) -> Result<(), String> {
    use std::os::unix::fs::PermissionsExt;
    let image = PathBuf::from(std::env::var_os("APPIMAGE").ok_or("仅 AppImage 支持自动更新")?);
    let staged = image.with_extension("AppImage.new");
    std::fs::copy(path, &staged).map_err(|e| e.to_string())?;
    std::fs::set_permissions(&staged, std::fs::Permissions::from_mode(0o755))
        .map_err(|e| e.to_string())?;
    std::fs::rename(&staged, &image).map_err(|e| e.to_string())?;
    std::process::Command::new(&image)
        .spawn()
        .map_err(|e| format!("无法重新启动：{e}"))?;
    Ok(())
}

fn publish(app: &AppHandle) {
    let view = app.state::<UpdateState>().0.lock().unwrap().view.clone();
    let _ = app.emit_to("main", "update", view);
}

pub fn view(app: &AppHandle) -> UpdateView {
    app.state::<UpdateState>().0.lock().unwrap().view.clone()
}

/// Called every clock tick; starts a background check when one is due.
pub fn poll(app: &AppHandle) {
    let state = app.state::<UpdateState>();
    let mut u = state.0.lock().unwrap();
    let started = *u.started.get_or_insert_with(Instant::now);
    let now = started.elapsed().as_millis() as u64;
    if u.checking || u.view.downloading || !u.schedule.due(now) {
        return;
    }
    u.checking = true;
    drop(u);
    let app = app.clone();
    std::thread::spawn(move || {
        let result = check();
        let state = app.state::<UpdateState>();
        let mut u = state.0.lock().unwrap();
        let now = u.started.map_or(0, |s| s.elapsed().as_millis() as u64);
        u.schedule = u.schedule.complete(now, result.is_ok());
        u.checking = false;
        if let Ok(Some(info)) = result {
            u.view.available = Some(info.tag.clone());
            u.view.installs = asset().is_some();
            u.release = Some(info);
        }
        drop(u);
        publish(&app);
    });
}

/// User clicked the update icon.
#[tauri::command]
pub fn start_update(app: AppHandle) -> Result<(), String> {
    let state = app.state::<UpdateState>();
    let mut u = state.0.lock().unwrap();
    let info = u.release.clone().ok_or("没有可用的更新")?;
    let Some(name) = asset() else {
        drop(u);
        let page = format!(
            "https://github.com/{}/releases/tag/{}",
            update::REPO,
            info.tag
        );
        return open_url(&page);
    };
    if u.view.downloading {
        return Ok(());
    }
    u.view.downloading = true;
    u.view.percent = -1;
    u.view.error = None;
    drop(u);
    publish(&app);
    std::thread::spawn(move || {
        let progress = |p: i32| {
            app.state::<UpdateState>().0.lock().unwrap().view.percent = p;
            publish(&app);
        };
        let result = download(&info, name, progress).and_then(|(dir, path)| install(dir, &path));
        let state = app.state::<UpdateState>();
        let mut u = state.0.lock().unwrap();
        u.view.downloading = false;
        match result {
            Ok(()) => {
                drop(u);
                // Windows: the installer closes this process itself. macOS
                // keeps running until the user replaces the app. AppImage
                // restarted above.
                if cfg!(all(unix, not(target_os = "macos"))) {
                    app.exit(0);
                }
            }
            Err(e) => {
                u.view.error = Some(e);
                u.view.percent = -1;
                drop(u);
            }
        }
        publish(&app);
    });
    Ok(())
}

fn open_url(url: &str) -> Result<(), String> {
    #[cfg(target_os = "linux")]
    let program = "xdg-open";
    #[cfg(target_os = "macos")]
    let program = "open";
    #[cfg(windows)]
    let program = "explorer";
    std::process::Command::new(program)
        .arg(url)
        .spawn()
        .map(|_| ())
        .map_err(|e| e.to_string())
}
