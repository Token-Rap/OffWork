//! Port of `src/image_file.h`: locale-independent extension check shared by
//! drag-and-drop and OCR. Accepts `/` and `\` separators on every OS.
pub fn is_supported(path: &str) -> bool {
    if path.is_empty() || path.contains('\0') {
        return false;
    }
    let name = path.rsplit(['/', '\\']).next().unwrap_or(path);
    match name.rfind('.') {
        None | Some(0) => false,
        Some(dot) => {
            let ext = name[dot..].to_ascii_lowercase();
            matches!(ext.as_str(), ".png" | ".jpg" | ".jpeg" | ".bmp")
        }
    }
}
