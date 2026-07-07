// ============================================================
// Auto-update integration (auto_update.dll)
// ============================================================

use std::ffi::{c_char, c_int, CString};
use std::net::{TcpStream, ToSocketAddrs};
use std::time::{Duration, Instant};

// --- C struct mapping ---

const AU_MAX_VERSION_LEN:   usize = 64;
const AU_MAX_PRIORITY_LEN:  usize = 16;
const AU_MAX_CHANGELOG_LEN: usize = 8192;
const AU_MAX_URL_LEN:       usize = 2048;

#[repr(C)]
#[derive(Debug, Clone)]
pub struct UpdateInfo {
    pub has_update: c_int,
    pub version:    [u8; AU_MAX_VERSION_LEN],
    pub priority:   [u8; AU_MAX_PRIORITY_LEN],
    pub changelog:  [u8; AU_MAX_CHANGELOG_LEN],
    pub best_url:   [u8; AU_MAX_URL_LEN],
}

impl UpdateInfo {
    fn str_from_bytes(bytes: &[u8]) -> String {
        let end = bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len());
        String::from_utf8_lossy(&bytes[..end]).to_string()
    }

    pub fn version_str(&self)   -> String { Self::str_from_bytes(&self.version) }
    pub fn priority_str(&self)  -> String { Self::str_from_bytes(&self.priority) }
    pub fn changelog_str(&self) -> String { Self::str_from_bytes(&self.changelog) }
    pub fn best_url_str(&self)  -> String { Self::str_from_bytes(&self.best_url) }
}

// --- FFI ---

extern "C" {
    fn setUpdateListsUrl(url: *const c_char);
    fn setCurrentVersion(version: *const c_char);
    fn getUpdateInfo(info: *mut UpdateInfo) -> c_int;
    fn update(url: *const c_char) -> c_int;
}

// --- safe wrappers ---

/// Probe all candidate URLs, return the one with lowest TCP latency.
/// Falls back to the first URL if probing fails for all.
pub fn select_best_url(urls: &[&str]) -> String {
    if urls.len() <= 1 {
        return urls.first().copied().unwrap_or("").to_string();
    }

    let mut best: Option<(&str, Duration)> = None;

    for url in urls {
        if let Some(host_port) = extract_host_port(url) {
            if let Ok(latency) = probe_tcp(&host_port) {
                eprintln!("[update] probe {url}: {:.0}ms", latency.as_secs_f64() * 1000.0);
                if best.is_none() || latency < best.unwrap().1 {
                    best = Some((url, latency));
                }
            }
        }
    }

    if let Some((url, _)) = best {
        eprintln!("[update] selected: {url}");
        url.to_string()
    } else {
        eprintln!("[update] probing failed for all URLs, using first");
        urls[0].to_string()
    }
}

fn extract_host_port(url: &str) -> Option<String> {
    // strip scheme
    let rest = url.strip_prefix("https://")
        .or_else(|| url.strip_prefix("http://"))?;
    // strip path
    let host_port = rest.split('/').next()?;
    // add default port if missing
    if host_port.contains(':') {
        Some(host_port.to_string())
    } else if url.starts_with("https://") {
        Some(format!("{host_port}:443"))
    } else {
        Some(format!("{host_port}:80"))
    }
}

fn probe_tcp(host_port: &str) -> Result<Duration, String> {
    let start = Instant::now();
    let addrs: Vec<_> = host_port
        .to_socket_addrs()
        .map_err(|e| format!("resolve error: {e}"))?
        .collect();
    let addr = addrs.first().ok_or("no address")?;
    let _stream = TcpStream::connect_timeout(addr, Duration::from_secs(5))
        .map_err(|e| format!("connect error: {e}"))?;
    Ok(start.elapsed())
}

pub fn configure_update(urls: &[&str], current_version: &str) {
    let best = select_best_url(urls);
    let c_url = CString::new(best).unwrap_or_default();
    unsafe { setUpdateListsUrl(c_url.as_ptr()); }

    let c_ver = CString::new(current_version).unwrap_or_default();
    unsafe { setCurrentVersion(c_ver.as_ptr()); }
}

pub fn check_for_update() -> Result<UpdateInfo, String> {
    let mut info = UpdateInfo {
        has_update: 0,
        version:    [0u8; AU_MAX_VERSION_LEN],
        priority:   [0u8; AU_MAX_PRIORITY_LEN],
        changelog:  [0u8; AU_MAX_CHANGELOG_LEN],
        best_url:   [0u8; AU_MAX_URL_LEN],
    };

    let ret = unsafe { getUpdateInfo(&mut info) };
    match ret {
        1  => Ok(info),
        0  => Err("no_update".into()),
        -1 => Err("network or parse error".into()),
        _  => Err(format!("unknown return code: {ret}")),
    }
}

pub fn do_update(url: &str) -> Result<(), String> {
    let c_url = CString::new(url).unwrap_or_default();
    let ret = unsafe { update(c_url.as_ptr()) };
    if ret == 1 {
        Ok(())
    } else {
        Err("update failed".into())
    }
}
