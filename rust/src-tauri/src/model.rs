//! Shared state: the saved settings, where they live and what the UI shows.

use offwork_core::edit::Commit;
use offwork_core::store::{self, Loaded, Origin};
use offwork_core::Settings;
use serde::Serialize;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Mutex;

pub const VERSION: &str = env!("CARGO_PKG_VERSION");

pub struct Model {
    /// Held across read → compute → save so concurrent edits (an OCR result
    /// and a manual commit) never overwrite each other.
    writer: Mutex<()>,
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
            writer: Mutex::new(()),
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

    /// The only way settings change: `edit` sees the latest saved value and
    /// its result is saved before any other edit can read. `Ok(None)` means
    /// nothing changed; `Err` is a user-facing message.
    pub fn update<F>(&self, edit: F) -> Result<Option<Settings>, String>
    where
        F: FnOnce(&Settings) -> Commit,
    {
        let _writer = self.writer.lock().unwrap_or_else(|e| e.into_inner());
        match edit(&self.current()) {
            Commit::Unchanged => Ok(None),
            Commit::Rejected(message) => Err(message.to_owned()),
            Commit::Saved(next) => self.persist(next).map(Some),
        }
    }

    /// Applies a value in memory only (window prefs while dragging).
    pub fn preview<F: FnOnce(&Settings) -> Settings>(&self, edit: F) -> Settings {
        let _writer = self.writer.lock().unwrap_or_else(|e| e.into_inner());
        let next = edit(&self.current()).normalized();
        *self.settings.lock().unwrap() = next.clone();
        next
    }

    /// Saves atomically; callers hold `writer`. The new value is kept in
    /// memory even when the disk write fails, and the next save retries
    /// (this also completes a pending migration). A newer-schema file is
    /// never overwritten.
    fn persist(&self, next: Settings) -> Result<Settings, String> {
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

#[cfg(test)]
mod tests {
    use super::*;
    use offwork_core::edit;
    use std::sync::{mpsc, Arc};
    use std::time::Duration;

    fn model(dir: &std::path::Path) -> Model {
        let path = dir.join("settings.json");
        let loaded = store::open(&path, None, 0);
        Model::new(Some(path), loaded)
    }

    /// Review finding 5: OCR read the settings, a salary edit was saved,
    /// then OCR saved its stale copy and silently reverted the salary.
    #[test]
    fn concurrent_ocr_and_manual_edit_both_survive() {
        let dir = tempfile::tempdir().unwrap();
        let model = Arc::new(model(dir.path()));
        let (inside, wait_inside) = mpsc::channel();
        let ocr = {
            let model = Arc::clone(&model);
            std::thread::spawn(move || {
                model
                    .update(|s| {
                        inside.send(()).unwrap();
                        // A slow edit: the manual commit arrives meanwhile.
                        std::thread::sleep(Duration::from_millis(300));
                        match edit::apply_clock_in(s, 535) {
                            Ok(next) => Commit::Saved(next),
                            Err(m) => Commit::Rejected(m),
                        }
                    })
                    .unwrap();
            })
        };
        wait_inside.recv().unwrap();
        let fields = edit::fields(&model.current()).with(edit::Field::Salary, "888");
        model
            .update(|s| edit::commit(s, edit::Field::Salary, &fields))
            .unwrap();
        ocr.join().unwrap();
        let saved =
            store::from_json(&std::fs::read(dir.path().join("settings.json")).unwrap()).unwrap();
        for s in [model.current(), saved] {
            assert_eq!((s.start_minutes, s.daily_salary), (535, 888.0));
        }
    }

    #[test]
    fn rejected_and_unchanged_edits_do_not_write() {
        let dir = tempfile::tempdir().unwrap();
        let model = model(dir.path());
        let fields = edit::fields(&model.current());
        assert_eq!(
            model.update(|s| edit::commit(s, edit::Field::Salary, &fields)),
            Ok(None)
        );
        let bad = fields.with(edit::Field::Salary, "abc");
        assert!(model
            .update(|s| edit::commit(s, edit::Field::Salary, &bad))
            .is_err());
        assert!(!dir.path().join("settings.json").exists());
    }
}
