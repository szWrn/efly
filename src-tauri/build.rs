use std::path::Path;

fn main() {
    let efly_lib_dir  = Path::new("../src-lib/build/Release");
    let update_lib_dir = Path::new("../library/auto-update/build/Release");
    let vcpkg_bin      = Path::new("../library/auto-update/vcpkg_installed/x64-windows/bin");

    let out_dir = std::env::var("OUT_DIR").unwrap();
    let target_dir = Path::new(&out_dir).ancestors().nth(3).unwrap().to_path_buf();

    // ---- efly_core.dll ----
    let dll = efly_lib_dir.join("efly_core.dll");
    println!("cargo:rerun-if-changed={}", dll.display());
    println!("cargo:rustc-link-search=native={}", efly_lib_dir.display());
    println!("cargo:rustc-link-lib=dylib=efly_core");
    copy(&dll, &target_dir);

    // ---- auto_update.dll ----
    println!("cargo:rerun-if-changed={}", update_lib_dir.join("auto_update.dll").display());
    println!("cargo:rustc-link-search=native={}", update_lib_dir.display());
    println!("cargo:rustc-link-lib=dylib=auto_update");
    copy(&update_lib_dir.join("auto_update.dll"), &target_dir);

    // ---- dependencies (vcpkg dynamic libs) ----
    copy(&vcpkg_bin.join("libcurl.dll"), &target_dir);
    copy(&vcpkg_bin.join("z.dll"),         &target_dir);

    tauri_build::build()
}

fn copy(src: &Path, dest_dir: &Path) {
    if src.exists() {
        let dest = dest_dir.join(src.file_name().unwrap());
        std::fs::copy(src, &dest).ok();
        println!("cargo:warning=DLL copy: {}", dest.display());
    } else {
        println!("cargo:warning=MISSING {}", src.display());
    }
}
