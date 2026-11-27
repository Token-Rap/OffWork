//! AppImage self-update steps. Unix-only so the tests also run on macOS.

use std::io::{self, Write};
use std::os::unix::fs::PermissionsExt;
use std::path::Path;
use std::process::{Child, Command};

/// Replaces `image` with the verified `payload` atomically. The copy goes to
/// an exclusively created, randomly named file in the same directory
/// (`O_CREAT|O_EXCL`, never following a planted symlink or truncating an
/// existing file), is made executable through its own descriptor, flushed,
/// and only then renamed over the image.
pub fn stage(payload: &Path, image: &Path) -> io::Result<()> {
    let dir = image
        .parent()
        .ok_or_else(|| io::Error::other("AppImage has no parent directory"))?;
    let mut staged = tempfile::Builder::new()
        .prefix(".OffWork-update-")
        .tempfile_in(dir)?;
    let mut source = std::fs::File::open(payload)?;
    io::copy(&mut source, staged.as_file_mut())?;
    staged.as_file_mut().flush()?;
    staged
        .as_file()
        .set_permissions(std::fs::Permissions::from_mode(0o755))?;
    staged.as_file().sync_all()?;
    staged.persist(image).map_err(|e| e.error)?;
    Ok(())
}

/// Starts `image` only after process `pid` has exited, so the new instance
/// never meets the old one's single-instance lock and hands control back to
/// a process that is about to quit. Arguments are passed positionally to
/// `sh`, never interpolated into the script.
pub fn relaunch_after_exit(pid: u32, image: &Path) -> io::Result<Child> {
    Command::new("/bin/sh")
        .arg("-c")
        .arg(r#"while kill -0 "$0" 2>/dev/null; do sleep 0.2; done; exec "$1""#)
        .arg(pid.to_string())
        .arg(image)
        .stdin(std::process::Stdio::null())
        .spawn()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{Duration, Instant};

    /// Review finding 3: the old code copied to `<image>.new` with overwrite
    /// semantics, so a planted symlink or an existing file there was
    /// truncated and chmod-ed.
    #[test]
    fn staging_never_writes_through_a_planted_path() {
        let dir = tempfile::tempdir().unwrap();
        let image = dir.path().join("OffWork.AppImage");
        std::fs::write(&image, b"old").unwrap();
        let victim = dir.path().join("victim");
        std::fs::write(&victim, b"keep me").unwrap();
        std::fs::set_permissions(&victim, std::fs::Permissions::from_mode(0o600)).unwrap();
        std::os::unix::fs::symlink(&victim, image.with_extension("AppImage.new")).unwrap();
        std::fs::write(dir.path().join(".OffWork-update-"), b"unrelated").unwrap();
        let payload = dir.path().join("payload");
        std::fs::write(&payload, b"new image").unwrap();

        stage(&payload, &image).unwrap();

        assert_eq!(std::fs::read(&image).unwrap(), b"new image");
        assert_eq!(
            std::fs::metadata(&image).unwrap().permissions().mode() & 0o777,
            0o755
        );
        assert_eq!(std::fs::read(&victim).unwrap(), b"keep me");
        assert_eq!(
            std::fs::metadata(&victim).unwrap().permissions().mode() & 0o777,
            0o600
        );
        assert_eq!(
            std::fs::read(dir.path().join(".OffWork-update-")).unwrap(),
            b"unrelated"
        );
        // Only the image, payload, victim, symlink and unrelated file remain.
        assert_eq!(std::fs::read_dir(dir.path()).unwrap().count(), 5);
    }

    #[test]
    fn failed_staging_leaves_the_image_alone() {
        let dir = tempfile::tempdir().unwrap();
        let image = dir.path().join("OffWork.AppImage");
        std::fs::write(&image, b"old").unwrap();
        assert!(stage(&dir.path().join("missing"), &image).is_err());
        assert_eq!(std::fs::read(&image).unwrap(), b"old");
        assert_eq!(std::fs::read_dir(dir.path()).unwrap().count(), 1);
    }

    /// Review finding 7: the replacement used to start while the old process
    /// still held the single-instance lock.
    #[test]
    fn relaunch_waits_for_the_old_process_to_exit() {
        let dir = tempfile::tempdir().unwrap();
        let marker = dir.path().join("started");
        let image = dir.path().join("fake image's \"$(touch pwned)\".sh");
        std::fs::write(
            &image,
            format!("#!/bin/sh\ndate +%s%N > '{}'\n", marker.display()),
        )
        .unwrap();
        std::fs::set_permissions(&image, std::fs::Permissions::from_mode(0o755)).unwrap();
        let mut old = Command::new("sleep").arg("1.5").spawn().unwrap();
        let started = Instant::now();
        let mut waiter = relaunch_after_exit(old.id(), &image).unwrap();
        std::thread::sleep(Duration::from_millis(700));
        assert!(
            !marker.exists(),
            "must not start while the old process lives"
        );
        old.wait().unwrap();
        waiter.wait().unwrap();
        assert!(marker.exists(), "starts once the old process is gone");
        assert!(started.elapsed() >= Duration::from_millis(1400));
        assert!(!dir.path().join("pwned").exists() && !Path::new("pwned").exists());
    }
}
