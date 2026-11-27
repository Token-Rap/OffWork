use std::path::Path;

#[cfg(target_os = "macos")]
fn read(path: &Path) -> Result<String, String> {
    use std::ffi::{c_char, c_void, CStr, CString};
    extern "C" {
        fn offwork_vision(path: *const c_char) -> *mut c_char;
        fn free(ptr: *mut c_void);
    }
    let path = CString::new(path.as_os_str().as_encoded_bytes()).map_err(|_| "无效路径")?;
    // Vision returns either null or a freshly allocated NUL-terminated buffer.
    unsafe {
        let raw = offwork_vision(path.as_ptr());
        if raw.is_null() {
            return Err("系统识别失败，请换用清晰截图".into());
        }
        let text = CStr::from_ptr(raw).to_string_lossy().into_owned();
        free(raw.cast());
        Ok(text)
    }
}
#[cfg(target_os = "linux")]
fn read(path: &Path) -> Result<String, String> {
    let output = std::process::Command::new("tesseract")
        .arg(path)
        .args(["stdout", "-l", "chi_sim+eng", "--psm", "6"])
        .output()
        .map_err(|_| "请安装 tesseract-ocr 和 tesseract-ocr-chi-sim")?;
    let stderr = String::from_utf8_lossy(&output.stderr);
    if stderr.contains("Failed loading language") {
        return Err("缺少 Tesseract 中文语言包，请安装 tesseract-ocr-chi-sim".into());
    }
    if !output.status.success() {
        return Err("图片无法识别，请换用清晰截图".into());
    }
    Ok(String::from_utf8_lossy(&output.stdout).into_owned())
}
#[cfg(windows)]
fn read(path: &Path) -> Result<String, String> {
    use windows::{
        core::HSTRING,
        Globalization::Language,
        Graphics::Imaging::{BitmapAlphaMode, BitmapDecoder, BitmapPixelFormat},
        Media::Ocr::OcrEngine,
        Storage::{FileAccessMode, StorageFile},
    };
    fn recognize(path: &Path) -> windows::core::Result<String> {
        let file = StorageFile::GetFileFromPathAsync(&HSTRING::from(path.as_os_str()))?.get()?;
        let stream = file.OpenAsync(FileAccessMode::Read)?.get()?;
        let decoder = BitmapDecoder::CreateAsync(&stream)?.get()?;
        let limit = OcrEngine::MaxImageDimension()?;
        if decoder.PixelWidth()? > limit || decoder.PixelHeight()? > limit {
            return Err(windows::core::Error::new(
                windows::core::HRESULT(0x80070057u32 as i32),
                "请裁剪截图后重试",
            ));
        }
        let bitmap = decoder
            .GetSoftwareBitmapConvertedAsync(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Ignore)?
            .get()?;
        let engine = Language::CreateLanguage(&HSTRING::from("zh-Hans"))
            .and_then(|lang| OcrEngine::TryCreateFromLanguage(&lang))
            .or_else(|_| OcrEngine::TryCreateFromUserProfileLanguages())?;
        let result = engine.RecognizeAsync(&bitmap)?.get()?;
        let mut lines = Vec::new();
        for line in result.Lines()? {
            lines.push(line.Text()?.to_string());
        }
        Ok(lines.join("\n"))
    }
    use windows::Win32::System::WinRT::{RoInitialize, RoUninitialize, RO_INIT_MULTITHREADED};
    let absolute = dunce::canonicalize(path).map_err(|e| e.to_string())?;
    // This adapter runs on a dedicated worker thread. Balance every successful initialization.
    unsafe { RoInitialize(RO_INIT_MULTITHREADED) }.map_err(|e| e.to_string())?;
    let result = recognize(&absolute);
    unsafe { RoUninitialize() };
    result.map_err(|e| format!("系统识别失败，请检查 OCR 语言包（{e}）"))
}
pub const MAX_IMAGE_BYTES: u64 = 20 * 1024 * 1024;

/// Validates the drop before any engine runs: one supported image file of a
/// sane size. Every error is a readable sentence for the settings footer.
pub fn check(paths: &[std::path::PathBuf]) -> Result<&Path, String> {
    let [path] = paths else {
        return Err("请一次只拖入一张打卡截图".into());
    };
    if path.is_dir() {
        return Err("请拖入图片文件，而不是文件夹".into());
    }
    if !offwork_core::image_file::is_supported(&path.to_string_lossy()) {
        return Err("请拖入一张 PNG、JPEG 或 BMP 图片".into());
    }
    let meta = path
        .metadata()
        .map_err(|_| "无法读取图片，请确认文件仍然存在")?;
    if !meta.is_file() || meta.len() == 0 {
        return Err("图片文件为空或无效".into());
    }
    if meta.len() > MAX_IMAGE_BYTES {
        return Err("图片须小于 20 MiB，请裁剪后重试".into());
    }
    Ok(path)
}

pub fn recognize(path: &Path) -> Result<i32, String> {
    check(std::slice::from_ref(&path.to_path_buf()))?;
    offwork_core::ocr::clock_in(&read(path)?)
        .ok_or_else(|| "未识别到上班打卡时间，请手动填写".into())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    #[test]
    fn native_fixture() {
        let path =
            Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/data/dingtalk_clockin.png");
        assert_eq!(recognize(&path).unwrap(), 535);
    }

    #[test]
    fn rejected_drops_have_readable_errors() {
        let dir = tempfile::tempdir().unwrap();
        let png = dir.path().join("a.png");
        std::fs::write(&png, b"x").unwrap();
        let two = [png.clone(), png.clone()];
        assert!(check(&two).unwrap_err().contains("一张"));
        assert!(check(&[]).unwrap_err().contains("一张"));
        let folder = dir.path().join("shots.png");
        std::fs::create_dir(&folder).unwrap();
        assert!(check(&[folder]).unwrap_err().contains("文件夹"));
        let gif = dir.path().join("a.gif");
        std::fs::write(&gif, b"GIF89a").unwrap();
        assert!(check(&[gif]).unwrap_err().contains("PNG"));
        assert!(check(&[dir.path().join("missing.png")])
            .unwrap_err()
            .contains("无法读取"));
        let empty = dir.path().join("empty.jpg");
        std::fs::write(&empty, b"").unwrap();
        assert!(check(&[empty]).unwrap_err().contains("为空"));
        let big = dir.path().join("big.bmp");
        std::fs::File::create(&big)
            .unwrap()
            .set_len(MAX_IMAGE_BYTES + 1)
            .unwrap();
        assert!(check(&[big]).unwrap_err().contains("20 MiB"));
        assert_eq!(check(std::slice::from_ref(&png)).unwrap(), png.as_path());
    }

    #[test]
    fn corrupt_image_is_an_error_not_a_time() {
        let dir = tempfile::tempdir().unwrap();
        let bad: PathBuf = dir.path().join("corrupt.png");
        std::fs::write(&bad, b"\x89PNG\r\n\x1a\nthis is not an image").unwrap();
        let error = recognize(&bad).unwrap_err();
        assert!(!error.is_empty(), "{error}");
    }

    #[test]
    fn image_without_a_clock_in_time_asks_for_manual_entry() {
        // The app icon has no text at all.
        let icon = Path::new(env!("CARGO_MANIFEST_DIR")).join("icons/128x128.png");
        let error = recognize(&icon).unwrap_err();
        assert!(
            error.contains("手动填写") || error.contains("识别"),
            "{error}"
        );
    }
}
