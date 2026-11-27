//! Shared state: the saved settings, where they live and what the UI shows.

use offwork_core::store::{self, Loaded, Origin};
use offwork_core::Settings;
use serde::Serialize;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Mutex;

pub const VERSION: &str = env!("CARGO_PKG_VERSION");

pub struct Model {
    pub settings: Mutex<Settings>,
    pub path: Option<PathBuf>,
    pub origin: Mutex<Origin>,
    pub notice: Mutex<String>,
    /// Bumped whenever the start or end time changes, so the reminder
    /// thread re-baselines instead of firing for an edit.
    pub schedule_generation: AtomicU64,
    pub ocr_busy: AtomicBool,
    pub tray: AtomicBool,
    pub pinned: AtomicBool,
    pub expanded: AtomicBool,
}

impl Model {
    pub fn new(path: Option<PathBuf>, loaded: Loaded) -> Self {
        let notice = if path.is_none() {
            "无法确定设置目录，修改不会保存".to_owned()
        } else {
            loaded.notice()
        };
        Self {
            settings: Mutex::new(loaded.settings),
            path,
            origin: Mutex::new(loaded.origin),
            notice: Mutex::new(notice),
            schedule_generation: AtomicU64::new(0),
            ocr_busy: AtomicBool::new(false),
            tray: AtomicBool::new(false),
            pinned: AtomicBool::new(true),
            expanded: AtomicBool::new(false),
        }
    }

    pub fn current(&self) -> Settings {
        self.settings.lock().unwrap().clone()
    }

    /// Saves atomically. The new value is kept in memory even when the disk
    /// write fails, and the next save retries (this also completes a pending
    /// migration). A newer-schema file is never overwritten.
    pub fn persist(&self, next: Settings) -> Result<Settings, String> {
        let next = next.normalized();
        let mut current = self.settings.lock().unwrap();
        if (current.start_minutes, current.end_minutes) != (next.start_minutes, next.end_minutes) {
            self.schedule_generation.fetch_add(1, Ordering::SeqCst);
        }
        *current = next.clone();
        drop(current);
        let mut origin = self.origin.lock().unwrap();
        if let Origin::ReadOnly(reason) = &*origin {
            return Err(format!("{reason}，本次修改不会保存"));
        }
        let path = self.path.as_ref().ok_or("无法确定设置目录，修改不会保存")?;
        store::save(path, &next).map_err(|e| format!("保存失败：{e}"))?;
        if matches!(*origin, Origin::MigrationPending { .. } | Origin::Fresh) {
            *origin = Origin::Existing;
            self.notice.lock().unwrap().clear();
        }
        Ok(next)
    }
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Autostart {
    pub enabled: bool,
    /// Another OffWork copy the entry points at (installed vs portable).
    pub other_target: Option<String>,
    pub error: Option<String>,
}
