fn main() {
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("macos") {
        cc::Build::new()
            .file("vision.m")
            .flag("-fobjc-arc")
            .compile("vision");
        for framework in ["Vision", "Foundation", "ImageIO"] {
            println!("cargo:rustc-link-lib=framework={framework}");
        }
        println!("cargo:rerun-if-changed=vision.m");
    }
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("windows") {
        cc::Build::new()
            .file("windows_tray.c")
            .file("legacy.c")
            .flag_if_supported("/utf-8")
            .compile("windows_shell");
        for library in ["shell32", "comctl32", "user32"] {
            println!("cargo:rustc-link-lib={library}");
        }
        println!("cargo:rerun-if-changed=windows_tray.c");
        println!("cargo:rerun-if-changed=legacy.c");
    }
    tauri_build::build()
}
