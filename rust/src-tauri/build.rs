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
    tauri_build::build()
}
