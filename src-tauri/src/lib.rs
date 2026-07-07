use std::ffi::{c_char, c_void, CStr};
use std::sync::Mutex;
use tauri::{AppHandle, Emitter, Manager, WindowEvent};

mod config;
mod update;
mod update_conf;

// ============================================================
// FFI callback types
// ============================================================

type PeerFoundCb           = extern "C" fn(*const c_char, u16, *mut c_void);
type TransferAnnounceCb    = extern "C" fn(u32, *const c_char, u64, *const c_char, *mut c_void);
type TransferProgressCb    = extern "C" fn(u32, u64, u64, *mut c_void);
type TransferCompleteCb    = extern "C" fn(u32, *const c_char, *mut c_void);
type ErrorCb               = extern "C" fn(u32, i32, *const c_char, *mut c_void);

// ============================================================
// FFI declarations
// ============================================================

extern "C" {
    fn efly_init() -> i32;
    fn efly_shutdown();
    fn efly_start_discovery(port: u16) -> i32;
    fn efly_stop_discovery();
    fn efly_send_file(
        file_path: *const c_char,
        remote_ip: *const c_char,
        remote_port: u16,
        out_file_id: *mut u32,
    ) -> i32;
    fn efly_accept_transfer(file_id: u32, save_dir: *const c_char) -> i32;
    fn efly_reject_transfer(file_id: u32) -> i32;
    fn efly_cancel_transfer(file_id: u32) -> i32;
    fn efly_get_transfer_progress(
        file_id: u32,
        out_total: *mut u64,
        out_transferred: *mut u64,
    ) -> i32;
    fn efly_set_peer_found_callback(cb: PeerFoundCb, user_data: *mut c_void);
    fn efly_set_transfer_announce_callback(cb: TransferAnnounceCb, user_data: *mut c_void);
    fn efly_set_transfer_progress_callback(cb: TransferProgressCb, user_data: *mut c_void);
    fn efly_set_transfer_complete_callback(cb: TransferCompleteCb, user_data: *mut c_void);
    fn efly_set_error_callback(cb: ErrorCb, user_data: *mut c_void);
}

// ============================================================
// global AppHandle for callbacks
// ============================================================

static APP: Mutex<Option<AppHandle>> = Mutex::new(None);

fn emit<E: serde::Serialize + Clone + 'static>(event: &str, payload: E) {
    if let Some(ref handle) = *APP.lock().unwrap() {
        let _ = handle.emit(event, payload);
    }
}

// ============================================================
// callback trampolines
// ============================================================

extern "C" fn on_peer_found(ip: *const c_char, port: u16, _: *mut c_void) {
    let ip_str = unsafe { CStr::from_ptr(ip) }.to_string_lossy().to_string();
    emit("efly:peer-found", serde_json::json!({ "ip": ip_str, "port": port }));
}

extern "C" fn on_transfer_announce(
    file_id: u32, filename: *const c_char,
    file_size: u64, from_ip: *const c_char, _: *mut c_void,
) {
    let name = unsafe { CStr::from_ptr(filename) }.to_string_lossy();
    let ip   = unsafe { CStr::from_ptr(from_ip) }.to_string_lossy();
    emit("efly:transfer-announce", serde_json::json!({
        "fileId": file_id, "filename": name,
        "fileSize": file_size, "fromIp": ip
    }));
}

extern "C" fn on_transfer_progress(file_id: u32, total: u64, transferred: u64, _: *mut c_void) {
    emit("efly:transfer-progress", serde_json::json!({
        "fileId": file_id, "total": total, "transferred": transferred
    }));
}

extern "C" fn on_transfer_complete(file_id: u32, saved_path: *const c_char, _: *mut c_void) {
    let path = unsafe { CStr::from_ptr(saved_path) }.to_string_lossy();
    emit("efly:transfer-complete", serde_json::json!({
        "fileId": file_id, "savedPath": path
    }));
}

extern "C" fn on_error(file_id: u32, code: i32, msg: *const c_char, _: *mut c_void) {
    let msg_str = unsafe { CStr::from_ptr(msg) }.to_string_lossy();
    emit("efly:error", serde_json::json!({
        "fileId": file_id, "code": code, "message": msg_str
    }));
}

// ============================================================
// Tauri commands
// ============================================================

#[tauri::command]
fn init_library() -> Result<(), String> {
    let ret = unsafe { efly_init() };
    if ret == 0 { Ok(()) }
    else { Err(format!("efly_init failed: {}", ret)) }
}

#[tauri::command]
fn start_discovery(port: u16) -> Result<(), String> {
    let ret = unsafe { efly_start_discovery(port) };
    if ret == 0 { Ok(()) }
    else { Err(format!("start_discovery failed: {}", ret)) }
}

#[tauri::command]
fn stop_discovery() {
    unsafe { efly_stop_discovery(); }
}

#[tauri::command]
fn send_file(path: String, ip: String, port: u16) -> Result<u32, String> {
    // verify the file exists before calling DLL
    let p = std::path::Path::new(&path);
    if !p.exists() {
        return Err(format!("file not found: {path}"));
    }
    if !p.is_file() {
        return Err(format!("not a file: {path}"));
    }

    eprintln!("[efly] send_file called: path={path}, ip={ip}, port={port}");

    let c_path = std::ffi::CString::new(path.clone()).map_err(|e| format!("invalid path: {e}"))?;
    let c_ip   = std::ffi::CString::new(ip.clone()).map_err(|e| format!("invalid ip: {e}"))?;
    let mut file_id: u32 = 0;
    let ret = unsafe {
        efly_send_file(c_path.as_ptr(), c_ip.as_ptr(), port, &mut file_id)
    };
    eprintln!("[efly] send_file result: code={ret}, file_id={file_id}");
    match ret {
        0   if file_id > 0 => Ok(file_id),
        -8  => Err("not initialized — start discovery first".into()),
        -3  => Err(format!("DLL cannot open file: {path}")),
        -9  => Err(format!("invalid argument — path={path}, ip={ip}, port={port}")),
        _   => Err(format!("send_file failed: code={ret}, path={path}")),
    }
}

#[tauri::command]
fn accept_transfer(file_id: u32, save_dir: String) -> Result<(), String> {
    let dir = if save_dir == "." || save_dir.is_empty() {
        // packaged exe has no write permission in the working directory;
        // fall back to the user's Downloads folder
        if let Ok(home) = std::env::var("USERPROFILE") {
            format!("{home}\\Downloads")
        } else {
            save_dir
        }
    } else {
        save_dir
    };
    let _ = std::fs::create_dir_all(&dir);
    let c_dir = std::ffi::CString::new(dir.clone()).map_err(|e| format!("invalid dir: {e}"))?;
    let ret = unsafe { efly_accept_transfer(file_id, c_dir.as_ptr()) };
    if ret == 0 { Ok(()) }
    else { Err(format!("accept_transfer failed: {ret}")) }
}

#[tauri::command]
fn reject_transfer(file_id: u32) -> Result<(), String> {
    let ret = unsafe { efly_reject_transfer(file_id) };
    if ret == 0 { Ok(()) }
    else { Err(format!("reject_transfer failed: {ret}")) }
}

#[tauri::command]
fn cancel_transfer(file_id: u32) -> Result<(), String> {
    let ret = unsafe { efly_cancel_transfer(file_id) };
    if ret == 0 { Ok(()) }
    else { Err(format!("cancel_transfer failed: {ret}")) }
}

#[tauri::command]
fn transfer_progress(file_id: u32) -> Result<serde_json::Value, String> {
    let mut total: u64 = 0;
    let mut transferred: u64 = 0;
    let ret = unsafe { efly_get_transfer_progress(file_id, &mut total, &mut transferred) };
    if ret == 0 {
        Ok(serde_json::json!({ "total": total, "transferred": transferred }))
    } else {
        Err(format!("progress query failed: {ret}"))
    }
}

// ---- auto-update commands ----

#[tauri::command]
fn configure_update(urls: Vec<String>, version: String) {
    let refs: Vec<&str> = urls.iter().map(|s| s.as_str()).collect();
    update::configure_update(&refs, &version);
}

#[tauri::command]
fn check_update() -> Result<serde_json::Value, String> {
    match update::check_for_update() {
        Ok(info) => {
            eprintln!("[update] update available: version={} priority={}",
                      info.version_str(), info.priority_str());
            Ok(serde_json::json!({
                "hasUpdate": info.has_update != 0,
                "version":   info.version_str(),
                "priority":  info.priority_str(),
                "changelog": info.changelog_str(),
                "bestUrl":   info.best_url_str(),
            }))
        }
        Err(e) if e == "no_update" => {
            eprintln!("[update] no update available");
            Ok(serde_json::json!({ "hasUpdate": false }))
        }
        Err(e) => {
            eprintln!("[update] check failed: {e}");
            Err(e)
        }
    }
}

#[tauri::command]
fn do_update(url: String) -> Result<(), String> {
    update::do_update(&url)
}

// ---- config commands ----

#[tauri::command]
fn load_config(app: AppHandle) -> Result<config::AppConfig, String> {
    let app_data = app.path().app_data_dir().map_err(|e| format!("{e}"))?;
    let cfg = config::AppConfig::load(&app_data);
    eprintln!("[config] loaded: {cfg:?}");
    Ok(cfg)
}

#[tauri::command]
fn save_config(app: AppHandle, cfg: config::AppConfig) -> Result<(), String> {
    let app_data = app.path().app_data_dir().map_err(|e| format!("{e}"))?;
    cfg.save(&app_data);
    eprintln!("[config] saved: {cfg:?}");
    Ok(())
}

// ---- run ----

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_dialog::init())
        .setup(|app| {
            // store AppHandle for callbacks
            *APP.lock().unwrap() = Some(app.handle().clone());

            // clean up DLL when window is closed
            if let Some(window) = app.get_webview_window("main") {
                window.on_window_event(|event| {
                    if let WindowEvent::CloseRequested { .. } = event {
                        unsafe { efly_shutdown(); }
                    }
                });
            }

            // configure auto-update
            let lists_urls = update_conf::candidate_urls();
            let current_ver = update_conf::effective_version();
            eprintln!("[update] mirrors: {}  version: {current_ver}", lists_urls.len());
            update::configure_update(lists_urls, &current_ver);

            // init DLL + register callbacks
            let ret = unsafe { efly_init() };
            if ret != 0 {
                eprintln!("efly_init failed: {}", ret);
            }
            unsafe {
                efly_set_peer_found_callback(on_peer_found, std::ptr::null_mut());
                efly_set_transfer_announce_callback(on_transfer_announce, std::ptr::null_mut());
                efly_set_transfer_progress_callback(on_transfer_progress, std::ptr::null_mut());
                efly_set_transfer_complete_callback(on_transfer_complete, std::ptr::null_mut());
                efly_set_error_callback(on_error, std::ptr::null_mut());
            }

            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            init_library,
            start_discovery,
            stop_discovery,
            send_file,
            accept_transfer,
            reject_transfer,
            cancel_transfer,
            transfer_progress,
            load_config,
            save_config,
            configure_update,
            check_update,
            do_update,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
